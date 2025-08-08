#include "ec_interface.h"
#include "logging.h"
#include "fan_constants.h"
#include "pid_controller.h"
#include "fan_health.h"
#include "temperature_monitor.h"
#include "live_stats.h"

// Define MAX and MIN macros if not defined
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>
#include <time.h>

// Internal helper functions
static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value);
static uint8_t ec_io_read(const uint32_t port);
static int ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value);
static int calculate_fan_duty(int raw_duty);
static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low);

// Auto duty adjustment configuration
static struct {
    int target_temperature;
    int max_duty_cycle;
    int max_duty_increase_rate;
    int max_duty_decrease_rate;
    int fan_stall_prevention_threshold;
    bool pid_enabled;
    bool adaptive_pid_enabled;
    int adaptive_tuning_interval;
    int adaptive_cycle_count;
    double pid_kp, pid_ki, pid_kd;
    double pid_integral;
    double pid_prev_error;
    double pid_output_max, pid_output_min;
    int last_cpu_temp;
    int temp_history[10];
    int temp_history_index;
    time_t last_temp_update;
} ec_auto_config = {
    .target_temperature = 65,
    .max_duty_cycle = 100,
    .max_duty_increase_rate = 10,
    .max_duty_decrease_rate = 5,
    .fan_stall_prevention_threshold = 20,
    .pid_enabled = true,
    .adaptive_pid_enabled = true,
    .adaptive_tuning_interval = 100,
    .adaptive_cycle_count = 0,
    .pid_kp = 2.0,
    .pid_ki = 0.1,
    .pid_kd = 0.5,
    .pid_integral = 0.0,
    .pid_prev_error = 0.0,
    .pid_output_max = 100.0,
    .pid_output_min = 0.0,
    .last_cpu_temp = 0,
    .temp_history = {0},
    .temp_history_index = 0,
    .last_temp_update = 0
};

// External references (will be provided by main daemon)
extern int debug_mode;
extern share_info_t *share_info;

int ec_init(void) {
    if (ioperm(EC_DATA, 1, 1) != 0) {
        logging_error("Failed to get I/O permissions for EC_DATA: %s", strerror(errno));
        return -1;
    }
    if (ioperm(EC_SC, 1, 1) != 0) {
        logging_error("Failed to get I/O permissions for EC_SC: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int ec_query_cpu_temp(void) {
    return ec_io_read(EC_REG_CPU_TEMP);
}

int ec_query_fan_duty(void) {
    int raw_duty = ec_io_read(EC_REG_FAN_DUTY);
    return calculate_fan_duty(raw_duty);
}

int ec_query_fan_rpms(void) {
    int raw_rpm_hi = ec_io_read(EC_REG_FAN_RPMS_HI);
    int raw_rpm_lo = ec_io_read(EC_REG_FAN_RPMS_LO);
    return calculate_fan_rpms(raw_rpm_hi, raw_rpm_lo);
}

int ec_write_fan_duty(int duty_percentage) {
    if (duty_percentage < 1 || duty_percentage > 100) {
        logging_error("Invalid fan duty to write: %d", duty_percentage);
        return -1;
    }
    
    double v_d = ((double) duty_percentage) / 100.0 * 255.0;
    int v_i = (int) v_d;
    return ec_io_do(0x99, 0x01, v_i);
}

int ec_write_fan_duty_with_retry(int duty_percentage, int max_retries) {
    if (duty_percentage < 1 || duty_percentage > 100) {
        logging_error("Invalid fan duty to write: %d", duty_percentage);
        return -1;
    }
    
    double v_d = ((double) duty_percentage) / 100.0 * 255.0;
    int v_i = (int) v_d;
    
    // Use the standard ec_io_do function with multiple attempts
    for (int attempt = 0; attempt < max_retries; attempt++) {
        int result = ec_io_do(0x99, 0x01, v_i);
        if (result == 0) {
            if (attempt > 0) {
                logging_debug("Fan duty write succeeded on retry %d/%d: %d%% (raw: %d)", 
                             attempt + 1, max_retries, duty_percentage, v_i);
            }
            return 0;
        }
        
        logging_debug("Fan duty write attempt %d/%d failed: %d%% (raw: %d)", 
                     attempt + 1, max_retries, duty_percentage, v_i);
        
        if (attempt < max_retries - 1) {
            // Wait before next retry with exponential backoff
            int retry_delay_ms = (1 << attempt) * 5;  // 5ms, 10ms, 20ms...
            usleep(retry_delay_ms * 1000);
        }
    }
    
    logging_error("Fan duty write failed after %d retries: %d%% (raw: %d)", 
                 max_retries, duty_percentage, v_i);
    return -1;
}

int ec_test_fan(int duty_percentage) {
    logging_info("Testing fan duty: %d%%", duty_percentage);
    int result = ec_write_fan_duty(duty_percentage);
    if (result == 0) {
        ec_dump_fan();
    }
    return result;
}

int ec_dump_fan(void) {
    printf("Fan Information:\n");
    printf("  FAN Duty: %d%%\n", ec_query_fan_duty());
    printf("  FAN RPMs: %d RPM\n", ec_query_fan_rpms());
    printf("  CPU Temp: %d°C\n", ec_query_cpu_temp());
    return 0;
}

void ec_cleanup(void) {
    // Release I/O permissions
    ioperm(EC_DATA, 1, 0);
    ioperm(EC_SC, 1, 0);
}

// Configuration functions
void ec_set_target_temperature(int temp) {
    ec_auto_config.target_temperature = temp;
}

void ec_set_max_duty_cycle(int duty) {
    ec_auto_config.max_duty_cycle = duty;
}

void ec_set_pid_parameters(double kp, double ki, double kd) {
    ec_auto_config.pid_kp = kp;
    ec_auto_config.pid_ki = ki;
    ec_auto_config.pid_kd = kd;
}

void ec_set_rate_limits(int max_increase, int max_decrease) {
    ec_auto_config.max_duty_increase_rate = max_increase;
    ec_auto_config.max_duty_decrease_rate = max_decrease;
}

void ec_set_stall_prevention_threshold(int threshold) {
    ec_auto_config.fan_stall_prevention_threshold = threshold;
}

void ec_enable_pid(bool enabled) {
    ec_auto_config.pid_enabled = enabled;
}

void ec_enable_adaptive_pid(bool enabled) {
    ec_auto_config.adaptive_pid_enabled = enabled;
}

// Temperature history management
static void add_temp_to_history(int temp) {
    ec_auto_config.temp_history[ec_auto_config.temp_history_index] = temp;
    ec_auto_config.temp_history_index = (ec_auto_config.temp_history_index + 1) % 10;
    ec_auto_config.last_temp_update = time(NULL);
}

static bool is_temp_stuck(void) {
    if (ec_auto_config.temp_history_index < 5) {
        return false; // Not enough history
    }
    
    int recent_temps[5];
    int idx = ec_auto_config.temp_history_index;
    for (int i = 0; i < 5; i++) {
        idx = (idx - 1 + 10) % 10;
        recent_temps[i] = ec_auto_config.temp_history[idx];
    }
    
    // Check if temperature has been stable for 5 readings
    int avg_temp = 0;
    for (int i = 0; i < 5; i++) {
        avg_temp += recent_temps[i];
    }
    avg_temp /= 5;
    
    // If average is within 2°C of target and not changing, consider it stuck
    int temp_error = avg_temp - ec_auto_config.target_temperature;
    if (temp_error > 0 && temp_error < 5) {
        // Check if all recent readings are within 1°C of each other
        bool stable = true;
        for (int i = 1; i < 5; i++) {
            if (abs(recent_temps[i] - recent_temps[0]) > 1) {
                stable = false;
                break;
            }
        }
        return stable;
    }
    
    return false;
}

// Aggressive duty calculation for stuck temperatures
static int get_aggressive_duty_for_error(int temp_error) {
    if (temp_error >= 8) {
        return ec_auto_config.max_duty_cycle;
    } else if (temp_error >= 5) {
        return 90;
    } else if (temp_error >= 3) {
        return 70;
    } else if (temp_error >= 1) {
        return 50;
    }
    return 30; // Minimum aggressive duty
}

// Enhanced auto duty adjustment function
int ec_auto_duty_adjust(void) {
    if (!ec_auto_config.pid_enabled) {
        // Fall back to simple control if PID is disabled
        int temp = share_info->cpu_temp;
        int duty = share_info->fan_duty;
        int new_duty = duty;

        if (temp >= ec_auto_config.target_temperature) {
            new_duty = MAX(duty + 2, 10);
        } else {
            new_duty = MAX(duty - 2, 0);
        }

        if (new_duty > ec_auto_config.max_duty_cycle) {
            new_duty = ec_auto_config.max_duty_cycle;
        } else if (new_duty < 0) {
            new_duty = 0;
        }

        return new_duty;
    }

    // Enhanced PID Controller with aggressive temperature control and learning
    int temp = share_info->cpu_temp;
    double setpoint = (double)ec_auto_config.target_temperature;
    double process_variable = (double)temp;
    double error = process_variable - setpoint;
    
    // Track temperature history for stuck detection
    add_temp_to_history(temp);
    
    // Add temperature to history for adaptive tuning
    if (ec_auto_config.adaptive_pid_enabled) {
        // Note: This would call functions from pid_controller.c
        // adaptive_pid_add_temp_history(temp);
        ec_auto_config.adaptive_cycle_count++;
        
        // Perform adaptive tuning at intervals
        if (ec_auto_config.adaptive_cycle_count >= ec_auto_config.adaptive_tuning_interval) {
            // adaptive_pid_tune_parameters();
            ec_auto_config.adaptive_cycle_count = 0;
        }
    }
    
    // Determine if temperature is stuck at a suboptimal level
    bool temp_stuck = is_temp_stuck();
    int temp_error = temp - ec_auto_config.target_temperature;
    
    // Aggressive response for high temperatures with progressive escalation
    int new_duty = 0;
    
    if (temp_error >= 8) {
        // Emergency response: maximum duty when temp is 8°C+ above target
        new_duty = ec_auto_config.max_duty_cycle;
        if (debug_mode) {
            logging_debug("Emergency response: temp=%d, target=%d, error=%d°C, setting duty to %d%%", 
                         temp, ec_auto_config.target_temperature, temp_error, ec_auto_config.max_duty_cycle);
        }
    } else if (temp_error >= 5) {
        // High temp response: 90% duty when temp is 5°C+ above target
        new_duty = 90;
        if (debug_mode) {
            logging_debug("High temperature response: temp=%d, target=%d, error=%d°C, setting duty to 90%%", 
                         temp, ec_auto_config.target_temperature, temp_error);
        }
    } else if (temp_error >= 3) {
        // Moderate temp response: 70% duty when temp is 3°C+ above target
        new_duty = 70;
        if (debug_mode) {
            logging_debug("Moderate temperature response: temp=%d, target=%d, error=%d°C, setting duty to 70%%", 
                         temp, ec_auto_config.target_temperature, temp_error);
        }
    } else if (temp_error > 0) {
        // Normal PID control for temperatures closer to target
        // Calculate PID terms
        double proportional = ec_auto_config.pid_kp * error;
        
        // Integral term with anti-windup
        ec_auto_config.pid_integral += error;
        if (ec_auto_config.pid_integral > (double)ec_auto_config.max_duty_cycle) 
            ec_auto_config.pid_integral = (double)ec_auto_config.max_duty_cycle;
        if (ec_auto_config.pid_integral < -(double)ec_auto_config.max_duty_cycle) 
            ec_auto_config.pid_integral = -(double)ec_auto_config.max_duty_cycle;
        double integral = ec_auto_config.pid_ki * ec_auto_config.pid_integral;
        
        // Derivative term
        double derivative = ec_auto_config.pid_kd * (error - ec_auto_config.pid_prev_error);
        
        // Calculate PID output
        double output = proportional + integral + derivative;
        
        // Clamp output to valid range
        if (output > ec_auto_config.pid_output_max) output = ec_auto_config.pid_output_max;
        if (output < ec_auto_config.pid_output_min) output = ec_auto_config.pid_output_min;
        
        // Store error for next iteration
        ec_auto_config.pid_prev_error = error;
        
        // Convert to integer duty cycle
        new_duty = (int)(output + 0.5); // Round to nearest integer
        
        // Ensure duty cycle is within valid range
        if (new_duty > 100) new_duty = 100;
        if (new_duty < 0) new_duty = 0;
        
        // Ensure minimum duty when temperature is above target
        if (temp > ec_auto_config.target_temperature && new_duty < 30) {
            new_duty = 30; // Minimum 30% duty when above target
            if (debug_mode) {
                logging_debug("Enforcing minimum duty: temp=%d, target=%d, setting minimum duty to 30%%", 
                             temp, ec_auto_config.target_temperature);
            }
        }
        
        if (debug_mode) {
            logging_debug("PID calculation: temp=%d, setpoint=%.1f, error=%.1f, p=%.1f, i=%.1f, d=%.1f, output=%.1f, duty=%d",
                       temp, setpoint, error, proportional, integral, derivative, output, new_duty);
        }
    } else {
        // Temperature is at or below target, use PID for fine control
        double proportional = ec_auto_config.pid_kp * error;
        ec_auto_config.pid_integral += error;
        if (ec_auto_config.pid_integral > 100.0) ec_auto_config.pid_integral = 100.0;
        if (ec_auto_config.pid_integral < -100.0) ec_auto_config.pid_integral = -100.0;
        double integral = ec_auto_config.pid_ki * ec_auto_config.pid_integral;
        double derivative = ec_auto_config.pid_kd * (error - ec_auto_config.pid_prev_error);
        
        double output = proportional + integral + derivative;
        
        // For temperatures below target, we want to reduce fan speed
        // But we need to handle the negative output properly
        if (output < 0) {
            // Negative output means we want to reduce fan speed
            // Calculate how much to reduce from current duty
            int current_duty = share_info->fan_duty;
            int reduction = (int)(-output + 0.5); // Convert negative to positive reduction
            
            // Limit reduction to current duty (can't go below 0)
            if (reduction > current_duty) {
                reduction = current_duty;
            }
            
            new_duty = current_duty - reduction;
            
            if (debug_mode) {
                logging_debug("Below target PID: temp=%d, setpoint=%.1f, error=%.1f, output=%.1f, reduction=%d, duty=%d",
                           temp, setpoint, error, output, reduction, new_duty);
            }
        } else {
            // Positive output (shouldn't happen when below target, but handle it)
            if (output > ec_auto_config.pid_output_max) output = ec_auto_config.pid_output_max;
            if (output < ec_auto_config.pid_output_min) output = ec_auto_config.pid_output_min;
            new_duty = (int)(output + 0.5);
            
            if (debug_mode) {
                logging_debug("Below target PID (positive output): temp=%d, setpoint=%.1f, error=%.1f, duty=%d",
                           temp, setpoint, error, new_duty);
            }
        }
        
        // Ensure duty cycle is within valid range
        if (new_duty > 100) new_duty = 100;
        if (new_duty < 0) new_duty = 0;
        
        ec_auto_config.pid_prev_error = error;
    }
    
    // Stuck temperature escalation: If temperature is stuck and above target, escalate duty
    if (temp_stuck && temp_error > 0) {
        int escalated_duty = get_aggressive_duty_for_error(temp_error);
        if (escalated_duty > new_duty) {
            new_duty = escalated_duty;
            if (debug_mode) {
                logging_debug("Stuck temperature escalation: temp=%d, target=%d, escalating duty to %d%%", 
                             temp, ec_auto_config.target_temperature, new_duty);
            }
        }
    }
    
    // Rate limiting with emergency bypass and stall prevention
    int current_duty = share_info->fan_duty;
    int max_increase = ec_auto_config.max_duty_increase_rate;
    int max_decrease = ec_auto_config.max_duty_decrease_rate;
    
    // Temperature-context stall prevention: Only enforce minimum duty when not in critical temperature
    bool critical_temp_bypass = (temp_error >= 8) || (temp >= ec_auto_config.target_temperature + 10);
    
    if (new_duty < ec_auto_config.fan_stall_prevention_threshold && temp > ec_auto_config.target_temperature && !critical_temp_bypass) {
        logging_telemetry("Duty cycle %d%% below stall prevention threshold, adjusting to %d%% (temp=%d, target=%d)", 
                  new_duty, ec_auto_config.fan_stall_prevention_threshold, temp, ec_auto_config.target_temperature);
        new_duty = ec_auto_config.fan_stall_prevention_threshold;
    } else if (critical_temp_bypass && new_duty < ec_auto_config.fan_stall_prevention_threshold) {
        logging_telemetry("Critical temperature bypass: allowing duty %d%% below stall threshold (temp=%d, error=%d°C)", 
                  new_duty, temp, temp_error);
    }
    
    // Emergency bypass: Allow faster rate limiting for critical temperature situations
    bool emergency_bypass = (temp_error >= 8) || (temp_error >= 5 && new_duty >= 80) || temp_stuck;
    
    // High duty stall prevention: Avoid very high duty cycles if fan has been stuck recently (but allow for critical temps)
    bool high_duty_stall_prevention = (new_duty > 80) && (temp_error < 5) && (temp < ec_auto_config.target_temperature + 8);
    
    // Critical bypass: For very high temperatures, bypass rate limiting entirely
    bool critical_bypass = (temp_error >= 12) || (temp >= ec_auto_config.target_temperature + 15);
    
    // Cool-down bypass: For temperatures significantly below target, allow faster fan reduction
    bool cooldown_bypass = (temp_error <= -5) || (temp <= ec_auto_config.target_temperature - 8);
    
    // Stall prevention bypass: If fan is at risk of stalling, be more conservative (but allow critical temp bypass)
    bool stall_prevention_bypass = (current_duty <= ec_auto_config.fan_stall_prevention_threshold + 5) && 
                                  (new_duty < current_duty) && (temp > ec_auto_config.target_temperature) && !critical_temp_bypass;
    
    if (debug_mode) {
        logging_debug("Rate limiting check: current_duty=%d, new_duty=%d, temp_error=%d, emergency_bypass=%s, critical_bypass=%s, cooldown_bypass=%s, stall_prevention_bypass=%s", 
                  current_duty, new_duty, temp_error, emergency_bypass ? "true" : "false", 
                  critical_bypass ? "true" : "false", cooldown_bypass ? "true" : "false",
                  stall_prevention_bypass ? "true" : "false");
    }
    
    if (!emergency_bypass && !stall_prevention_bypass) {
        // High duty stall prevention: Limit duty to 80% unless temperature is critical
        if (high_duty_stall_prevention) {
            int original_duty = new_duty;
            new_duty = 80;
            if (debug_mode) {
                logging_debug("High duty stall prevention: limiting duty from %d%% to 80%% (temp=%d, error=%d°C)", 
                             original_duty, temp, temp_error);
            }
        }
        
        // Normal rate limiting with improved stall prevention
        if (new_duty > current_duty + max_increase) {
            int original_duty = new_duty;
            new_duty = current_duty + max_increase;
            if (debug_mode) {
                logging_debug("Normal rate limiting: limiting duty increase from %d to %d (max_increase=%d)", 
                             original_duty, new_duty, max_increase);
            }
        } else if (new_duty < current_duty - max_decrease) {
            int original_duty = new_duty;
            new_duty = current_duty - max_decrease;
            
            // Additional stall prevention: If we're reducing duty and near stall threshold, be more conservative (but allow critical temp bypass)
            if (new_duty < ec_auto_config.fan_stall_prevention_threshold + 10 && temp > ec_auto_config.target_temperature && !critical_temp_bypass) {
                new_duty = MAX(new_duty, ec_auto_config.fan_stall_prevention_threshold);
                if (debug_mode) {
                    logging_debug("Stall prevention: limiting duty decrease to %d%% (near stall threshold)", new_duty);
                }
            } else if (critical_temp_bypass && new_duty < ec_auto_config.fan_stall_prevention_threshold + 10) {
                if (debug_mode) {
                    logging_debug("Critical temp bypass: allowing duty decrease to %d%% (temp=%d, error=%d°C)", new_duty, temp, temp_error);
                }
            }
            
            if (debug_mode) {
                logging_debug("Normal rate limiting: limiting duty decrease from %d to %d (max_decrease=%d)", 
                             original_duty, new_duty, max_decrease);
            }
        } else {
            if (debug_mode) {
                logging_debug("Normal rate limiting: allowing duty change from %d to %d", current_duty, new_duty);
            }
        }
    } else if (critical_bypass) {
        // Critical bypass: No rate limiting for extreme temperatures
        if (debug_mode) {
            logging_debug("Critical bypass: allowing full duty change from %d to %d (temp=%d, target=%d, error=%d°C)", 
                          current_duty, new_duty, temp, ec_auto_config.target_temperature, temp_error);
        }
    } else if (stall_prevention_bypass) {
        // Stall prevention bypass: Be very conservative when near stall threshold
        int stall_max_decrease = max_decrease / 2; // Half the normal decrease rate
        if (new_duty < current_duty - stall_max_decrease) {
            int original_duty = new_duty;
            new_duty = current_duty - stall_max_decrease;
            if (debug_mode) {
                logging_debug("Stall prevention bypass: limiting duty decrease from %d to %d (stall rate: %d)", 
                              original_duty, new_duty, stall_max_decrease);
            }
        } else {
            if (debug_mode) {
                logging_debug("Stall prevention bypass: allowing duty change from %d to %d", current_duty, new_duty);
            }
        }
    } else if (cooldown_bypass) {
        // Cool-down bypass: Allow faster fan reduction when temperature is well below target
        int cooldown_max_change = max_decrease * 2; // Allow 2x normal decrease rate for cooldown
        if (new_duty < current_duty - cooldown_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty - cooldown_max_change;
            if (debug_mode) {
                logging_debug("Cool-down bypass: limiting duty decrease from %d to %d (cooldown rate: %d)", 
                              original_duty, new_duty, cooldown_max_change);
            }
        } else {
            if (debug_mode) {
                logging_debug("Cool-down bypass: allowing duty change from %d to %d (temp=%d, target=%d, error=%d°C)", 
                              current_duty, new_duty, temp, ec_auto_config.target_temperature, temp_error);
            }
        }
    } else {
        // Emergency rate limiting: Allow faster response for critical situations
        int emergency_max_change;
        
        if (temp_error >= 10) {
            // Critical emergency: Allow up to 35% change per cycle
            emergency_max_change = 35;
        } else if (temp_error >= 8) {
            // High emergency: Allow up to 25% change per cycle
            emergency_max_change = 25;
        } else {
            // Moderate emergency: Allow up to 20% change per cycle
            emergency_max_change = 20;
        }
        
        if (new_duty > current_duty + emergency_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty + emergency_max_change;
            if (debug_mode) {
                logging_debug("Emergency rate limiting: limiting duty increase from %d to %d (emergency rate: %d)", 
                              original_duty, new_duty, emergency_max_change);
            }
        } else if (new_duty < current_duty - emergency_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty - emergency_max_change;
            if (debug_mode) {
                logging_debug("Emergency rate limiting: limiting duty decrease from %d to %d (emergency rate: %d)", 
                              original_duty, new_duty, emergency_max_change);
            }
        } else {
            if (debug_mode) {
                logging_debug("Emergency bypass: allowing duty change from %d to %d (temp=%d, target=%d, stuck=%s, emergency rate: %d)", 
                              current_duty, new_duty, temp, ec_auto_config.target_temperature, temp_stuck ? "true" : "false", emergency_max_change);
            }
        }
    }
    
    if (debug_mode) {
        logging_debug("Final duty calculation: temp=%d, setpoint=%.1f, error=%.1f, duty=%d, stuck=%s",
                   temp, setpoint, error, new_duty, temp_stuck ? "true" : "false");
    }
    
    // Ensure duty cycle is within valid range
    if (new_duty > ec_auto_config.max_duty_cycle) new_duty = ec_auto_config.max_duty_cycle;
    if (new_duty < FAN_MIN_DUTY) new_duty = FAN_MIN_DUTY;  // Never go below minimum duty
    
    // CRITICAL: Ensure duty cycle will result in RPM above minimum threshold
    // Calculate the minimum duty needed to achieve FAN_MIN_RPM
    int min_duty_for_min_rpm = (FAN_MIN_RPM + FAN_RPM_DUTY_RATIO - 1) / FAN_RPM_DUTY_RATIO; // Ceiling division
    if (new_duty < min_duty_for_min_rpm) {
        logging_warning("Preventing fan stall: duty=%d%% would result in RPM below minimum (%d), setting to %d%%", 
                  new_duty, FAN_MIN_RPM, min_duty_for_min_rpm);
        new_duty = min_duty_for_min_rpm;
    }
    
    // Check if fan is potentially stuck
    int current_rpms = share_info->fan_rpms;
    int expected_min_rpm = new_duty * FAN_RPM_DUTY_RATIO;
    
    if (current_rpms < FAN_MIN_RPM || (current_rpms < expected_min_rpm * 0.5)) {  // Reduced tolerance to 50%
        logging_warning("Fan may be stuck: RPM=%d (expected >%d) at duty=%d%%", 
                  current_rpms, expected_min_rpm, new_duty);
        
        // More aggressive recovery: boost to emergency duty immediately
        int recovery_duty = MAX(new_duty + 30, FAN_EMERGENCY_DUTY);
        new_duty = MIN(recovery_duty, ec_auto_config.max_duty_cycle);  // Never exceed maximum duty
        logging_telemetry("Attempting aggressive fan recovery by setting duty to %d%%", new_duty);
        
        // Force immediate EC write with retry for recovery
        if (ec_write_fan_duty_with_retry(new_duty, 3) != 0) {
            logging_error("Emergency fan recovery failed - hardware issue suspected");
        }
    }
    
    // CRITICAL TEMPERATURE PROTECTION
    if (temp >= FAN_EMERGENCY_SHUTDOWN_TEMP) {
        logging_error("EMERGENCY: Temperature %d°C exceeds shutdown threshold %d°C - forcing %d%% duty", 
                  temp, FAN_EMERGENCY_SHUTDOWN_TEMP, ec_auto_config.max_duty_cycle);
        new_duty = ec_auto_config.max_duty_cycle; // Force maximum cooling (limited to prevent stalls)
    } else if (temp >= FAN_CRITICAL_TEMP_THRESHOLD) {
        logging_error("CRITICAL: Temperature %d°C exceeds critical threshold %d°C - ensuring adequate cooling", 
                  temp, FAN_CRITICAL_TEMP_THRESHOLD);
        new_duty = MAX(new_duty, 80); // Ensure at least 80% duty for critical temps
    }
    
    // FINAL VALIDATION: Ensure duty cycle is always within spec range
    if (new_duty < FAN_MIN_DUTY) {
        logging_warning("Duty cycle %d%% below minimum %d%%, adjusting", new_duty, FAN_MIN_DUTY);
        new_duty = FAN_MIN_DUTY;
    }
    if (new_duty > ec_auto_config.max_duty_cycle) {
        logging_warning("Duty cycle %d%% above maximum %d%%, capping", new_duty, ec_auto_config.max_duty_cycle);
        new_duty = ec_auto_config.max_duty_cycle;
    }
    
    if (debug_mode) {
        logging_debug("Final duty calculation: temp=%d, setpoint=%.1f, error=%.1f, duty=%d, stuck=%s",
                   temp, setpoint, error, new_duty, temp_stuck ? "true" : "false");
    }
    
    return new_duty;
}

static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 500)) {  // Increased from 100 to 500
        usleep(1000);
        data = inb(port);
    }
    if (i >= 500) {  // Updated timeout check
        logging_error("EC I/O wait error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x",
                port, data, flag, value);
        return -1;
    }
    return 0;
}

static uint8_t ec_io_read(const uint32_t port) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(EC_SC_READ_CMD, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, OBF, 1);
    uint8_t value = inb(EC_DATA);

    return value;
}

static int ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(cmd, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, IBF, 0);
    outb(value, EC_DATA);

    return ec_io_wait(EC_SC, IBF, 0);
}

static int calculate_fan_duty(int raw_duty) {
    return (int) ((double) raw_duty / 255.0 * 100.0);
}

static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    
    // Handle edge cases to prevent nonsensical RPM values
    if (raw_rpm <= 0) {
        return 0;
    }
    
    // Prevent division by very small numbers that could cause overflow
    if (raw_rpm < 10) {
        return 0;  // Fan is likely stopped or in error state
    }
    
    int calculated_rpm = 2156220 / raw_rpm;
    
    // Sanity check: RPM should be reasonable (0-10000 for laptop fans)
    if (calculated_rpm < 0 || calculated_rpm > 10000) {
        logging_debug("Invalid RPM calculation: raw_rpm=%d, calculated_rpm=%d", raw_rpm, calculated_rpm);
        return 0;  // Return 0 for invalid readings
    }
    
    return calculated_rpm;
} 