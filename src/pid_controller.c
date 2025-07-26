#include "pid_controller.h"
#include "logging.h"
#include <math.h>
#include <stdlib.h>

// Internal helper functions
static double adaptive_pid_calculate_oscillation(pid_controller_t* pid);
static double adaptive_pid_calculate_performance_score(pid_controller_t* pid, int current_temp, int target_temp, int current_duty);
static void adaptive_pid_tune_parameters(pid_controller_t* pid, int current_temp, int target_temp, int current_duty);

pid_controller_t* pid_controller_init(double kp, double ki, double kd,
                                    int max_duty_change, int max_increase_rate, int max_decrease_rate,
                                    int min_fan_duty) {
    pid_controller_t* pid = malloc(sizeof(pid_controller_t));
    if (!pid) {
        return NULL;
    }
    
    // Initialize PID parameters
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    
    // Initialize PID state
    pid->integral = 0.0;
    pid->prev_error = 0.0;
    
    // Initialize output limits
    pid->output_min = 0.0;
    pid->output_max = 100.0;
    
    // Initialize adaptive tuning parameters
    pid->adaptive_enabled = false;
    pid->adaptive_tuning_interval = 30;
    pid->adaptive_target_performance = 0.8;
    pid->adaptive_cycle_count = 0;
    pid->adaptive_temp_history_index = 0;
    pid->adaptive_temp_history_size = 0;
    pid->adaptive_performance_score = 0.0;
    pid->adaptive_prev_score = 0.0;
    pid->adaptive_learning_cycles = 0;
    
    // Initialize adaptive tuning step sizes
    pid->adaptive_kp_step = 0.1;
    pid->adaptive_ki_step = 0.01;
    pid->adaptive_kd_step = 0.05;
    
    // Initialize temperature trend tracking
    pid->temp_history_index = 0;
    pid->temp_history_size = 0;
    pid->stuck_detection_counter = 0;
    pid->stuck_threshold_cycles = 20;
    pid->stuck_temp_threshold = 2.0;
    
    // Initialize rate limiting
    pid->max_duty_change_rate = max_duty_change;
    pid->max_duty_increase_rate = max_increase_rate;
    pid->max_duty_decrease_rate = max_decrease_rate;
    
    // Initialize safety thresholds
    pid->min_fan_duty = min_fan_duty;
    pid->min_fan_rpm = 500;
    pid->safe_fan_rpm = 1000;
    pid->emergency_duty = 60;
    pid->rpm_duty_ratio = 40;
    
    // Initialize temperature history
    for (int i = 0; i < 10; i++) {
        pid->temp_history[i] = 0;
    }
    
    // Initialize adaptive temperature history
    for (int i = 0; i < 60; i++) {
        pid->adaptive_temp_history[i] = 0.0;
    }
    
    return pid;
}

int pid_controller_calculate_duty(pid_controller_t* pid, int current_temp, int target_temp,
                                int current_duty, int current_rpm) {
    if (!pid) return current_duty;
    
    // Add temperature to history for trend analysis
    pid_controller_add_temp_to_history(pid, current_temp);
    
    // Add temperature to adaptive history if enabled
    if (pid->adaptive_enabled) {
        pid->adaptive_temp_history[pid->adaptive_temp_history_index] = (double)current_temp;
        pid->adaptive_temp_history_index = (pid->adaptive_temp_history_index + 1) % 60;
        if (pid->adaptive_temp_history_size < 60) {
            pid->adaptive_temp_history_size++;
        }
        pid->adaptive_cycle_count++;
        
        // Perform adaptive tuning at intervals
        if (pid->adaptive_cycle_count >= pid->adaptive_tuning_interval) {
            adaptive_pid_tune_parameters(pid, current_temp, target_temp, current_duty);
            pid->adaptive_cycle_count = 0;
        }
    }
    
    // Check if temperature is stuck
    bool temp_stuck = pid_controller_is_temp_stuck(pid);
    int temp_error = current_temp - target_temp;
    
    // Enhanced PID Controller with aggressive temperature control and learning
    // Strategy:
    // 1. Emergency response: 100% duty when temp is 8°C+ above target
    // 2. High temp response: 90% duty when temp is 5°C+ above target  
    // 3. Moderate temp response: 70% duty when temp is 3°C+ above target
    // 4. Stuck detection: Escalate duty if temperature isn't moving toward target
    // 5. Normal PID control: Standard PID for temperatures closer to target
    // 6. Minimum duty: 30% when above target to ensure cooling
    
    int new_duty = 0;
    
    if (temp_error >= 8) {
        // Emergency response: 100% duty when temp is 8°C+ above target
        new_duty = 100;
        logging_debug("Emergency response: temp=%d, target=%d, error=%d°C, setting duty to 100%%", 
                     current_temp, target_temp, temp_error);
    } else if (temp_error >= 5) {
        // High temp response: 90% duty when temp is 5°C+ above target
        new_duty = 90;
        logging_debug("High temperature response: temp=%d, target=%d, error=%d°C, setting duty to 90%%", 
                     current_temp, target_temp, temp_error);
    } else if (temp_error >= 3) {
        // Moderate temp response: 70% duty when temp is 3°C+ above target
        new_duty = 70;
        logging_debug("Moderate temperature response: temp=%d, target=%d, error=%d°C, setting duty to 70%%", 
                     current_temp, target_temp, temp_error);
    } else if (temp_error > 0) {
        // Normal PID control for temperatures closer to target
        double setpoint = (double)target_temp;
        double process_variable = (double)current_temp;
        double error = process_variable - setpoint;
        
        // Calculate PID terms
        double proportional = pid->kp * error;
        
        // Integral term with anti-windup
        pid->integral += error;
        if (pid->integral > 100.0) pid->integral = 100.0;
        if (pid->integral < -100.0) pid->integral = -100.0;
        double integral = pid->ki * pid->integral;
        
        // Derivative term
        double derivative = pid->kd * (error - pid->prev_error);
        
        // Calculate PID output
        double output = proportional + integral + derivative;
        
        // Clamp output to valid range
        if (output > pid->output_max) output = pid->output_max;
        if (output < pid->output_min) output = pid->output_min;
        
        // Store error for next iteration
        pid->prev_error = error;
        
        // Convert to integer duty cycle
        new_duty = (int)(output + 0.5); // Round to nearest integer
        
        // Ensure duty cycle is within valid range
        if (new_duty > 100) new_duty = 100;
        if (new_duty < 0) new_duty = 0;
        
        // Ensure minimum duty when temperature is above target
        if (current_temp > target_temp && new_duty < 30) {
            new_duty = 30; // Minimum 30% duty when above target
            logging_debug("Enforcing minimum duty: temp=%d, target=%d, setting minimum duty to 30%%", 
                         current_temp, target_temp);
        }
        
        logging_debug("PID calculation: temp=%d, setpoint=%.1f, error=%.1f, p=%.1f, i=%.1f, d=%.1f, output=%.1f, duty=%d",
                   current_temp, setpoint, error, proportional, integral, derivative, output, new_duty);
    } else {
        // Temperature is at or below target, use PID for fine control
        double setpoint = (double)target_temp;
        double process_variable = (double)current_temp;
        double error = process_variable - setpoint;
        
        double proportional = pid->kp * error;
        pid->integral += error;
        if (pid->integral > 100.0) pid->integral = 100.0;
        if (pid->integral < -100.0) pid->integral = -100.0;
        double integral = pid->ki * pid->integral;
        double derivative = pid->kd * (error - pid->prev_error);
        
        double output = proportional + integral + derivative;
        
        // For temperatures below target, we want to reduce fan speed
        // But we need to handle the negative output properly
        if (output < 0) {
            // Negative output means we want to reduce fan speed
            // Calculate how much to reduce from current duty
            int reduction = (int)(-output + 0.5); // Convert negative to positive reduction
            
            // Limit reduction to current duty (can't go below 0)
            if (reduction > current_duty) {
                reduction = current_duty;
            }
            
            new_duty = current_duty - reduction;
            
            logging_debug("Below target PID: temp=%d, setpoint=%.1f, error=%.1f, output=%.1f, reduction=%d, duty=%d",
                       current_temp, setpoint, error, output, reduction, new_duty);
        } else {
            // Positive output (shouldn't happen when below target, but handle it)
            if (output > pid->output_max) output = pid->output_max;
            if (output < pid->output_min) output = pid->output_min;
            new_duty = (int)(output + 0.5);
            
            logging_debug("Below target PID (positive output): temp=%d, setpoint=%.1f, error=%.1f, duty=%d",
                       current_temp, setpoint, error, new_duty);
        }
        
        // Ensure duty cycle is within valid range
        if (new_duty > 100) new_duty = 100;
        if (new_duty < 0) new_duty = 0;
        
        pid->prev_error = error;
    }
    
    // Stuck temperature escalation: If temperature is stuck and above target, escalate duty
    if (temp_stuck && temp_error > 0) {
        int escalated_duty = pid_controller_get_aggressive_duty_for_error(pid, temp_error);
        if (escalated_duty > new_duty) {
            new_duty = escalated_duty;
            logging_debug("Stuck temperature escalation: temp=%d, target=%d, escalating duty to %d%%", 
                         current_temp, target_temp, new_duty);
        }
    }
    
    // Rate limiting with emergency bypass
    int max_increase = pid->max_duty_increase_rate;
    int max_decrease = pid->max_duty_decrease_rate;
    
    // Emergency bypass: Allow faster rate limiting for critical temperature situations
    bool emergency_bypass = (temp_error >= 8) || (temp_error >= 5 && new_duty >= 80) || temp_stuck;
    
    // Critical bypass: For very high temperatures, bypass rate limiting entirely
    bool critical_bypass = (temp_error >= 12) || (current_temp >= target_temp + 15);
    
    // Cool-down bypass: For temperatures significantly below target, allow faster fan reduction
    bool cooldown_bypass = (temp_error <= -5) || (current_temp <= target_temp - 8);
    
    logging_debug("Rate limiting check: current_duty=%d, new_duty=%d, temp_error=%d, emergency_bypass=%s, critical_bypass=%s, cooldown_bypass=%s", 
                  current_duty, new_duty, temp_error, emergency_bypass ? "true" : "false", 
                  critical_bypass ? "true" : "false", cooldown_bypass ? "true" : "false");
    
    if (!emergency_bypass) {
        // Normal rate limiting
        if (new_duty > current_duty + max_increase) {
            int original_duty = new_duty;
            new_duty = current_duty + max_increase;
            logging_debug("Normal rate limiting: limiting duty increase from %d to %d (max_increase=%d)", 
                         original_duty, new_duty, max_increase);
        } else if (new_duty < current_duty - max_decrease) {
            int original_duty = new_duty;
            new_duty = current_duty - max_decrease;
            logging_debug("Normal rate limiting: limiting duty decrease from %d to %d (max_decrease=%d)", 
                         original_duty, new_duty, max_decrease);
        } else {
            logging_debug("Normal rate limiting: allowing duty change from %d to %d", current_duty, new_duty);
        }
    } else if (critical_bypass) {
        // Critical bypass: No rate limiting for extreme temperatures
        logging_debug("Critical bypass: allowing full duty change from %d to %d (temp=%d, target=%d, error=%d°C)", 
                      current_duty, new_duty, current_temp, target_temp, temp_error);
    } else if (cooldown_bypass) {
        // Cool-down bypass: Allow faster fan reduction when temperature is well below target
        int cooldown_max_change = max_decrease * 2; // Allow 2x normal decrease rate for cooldown
        if (new_duty < current_duty - cooldown_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty - cooldown_max_change;
            logging_debug("Cool-down bypass: limiting duty decrease from %d to %d (cooldown rate: %d)", 
                          original_duty, new_duty, cooldown_max_change);
        } else {
            logging_debug("Cool-down bypass: allowing duty change from %d to %d (temp=%d, target=%d, error=%d°C)", 
                          current_duty, new_duty, current_temp, target_temp, temp_error);
        }
    } else {
        // Emergency rate limiting: Allow faster response for critical situations (reduced values)
        int emergency_max_change;
        
        if (temp_error >= 10) {
            // Critical emergency: Allow up to 25% change per cycle (reduced from 50%)
            emergency_max_change = 25;
        } else if (temp_error >= 8) {
            // High emergency: Allow up to 15% change per cycle (reduced from 30%)
            emergency_max_change = 15;
        } else {
            // Moderate emergency: Allow up to 10% change per cycle (reduced from 20%)
            emergency_max_change = 10;
        }
        
        if (new_duty > current_duty + emergency_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty + emergency_max_change;
            logging_debug("Emergency rate limiting: limiting duty increase from %d to %d (emergency rate: %d)", 
                          original_duty, new_duty, emergency_max_change);
        } else if (new_duty < current_duty - emergency_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty - emergency_max_change;
            logging_debug("Emergency rate limiting: limiting duty decrease from %d to %d (emergency rate: %d)", 
                          original_duty, new_duty, emergency_max_change);
        } else {
            logging_debug("Emergency bypass: allowing duty change from %d to %d (temp=%d, target=%d, stuck=%s, emergency rate: %d)", 
                          current_duty, new_duty, current_temp, target_temp, temp_stuck ? "true" : "false", emergency_max_change);
        }
    }
    
    // Ensure duty cycle is within valid range
    if (new_duty > 100) new_duty = 100;
    if (new_duty < pid->min_fan_duty) new_duty = pid->min_fan_duty;  // Never go below minimum duty
    
    // Check if fan is potentially stuck
    int expected_min_rpm = new_duty * pid->rpm_duty_ratio;
    
    if (current_rpm < pid->min_fan_rpm || (current_rpm < expected_min_rpm * 0.7)) {  // Allow 30% tolerance
        logging_warning("Fan may be stuck: RPM=%d (expected >%d) at duty=%d%%", 
                      current_rpm, expected_min_rpm, new_duty);
        
        // Attempt recovery by temporarily boosting fan speed
        new_duty = (new_duty + 20 > 100) ? 100 : new_duty + 20;  // Boost by 20% or to at least 60%
        if (new_duty < 60) new_duty = 60;
        logging_info("Attempting fan recovery by setting duty to %d%%", new_duty);
    }
    
    logging_debug("Final duty calculation: temp=%d, target=%d, error=%d, duty=%d, stuck=%s",
               current_temp, target_temp, temp_error, new_duty, temp_stuck ? "true" : "false");
    
    return new_duty;
}

void pid_controller_add_temp_to_history(pid_controller_t* pid, int temp) {
    if (!pid) return;
    
    pid->temp_history[pid->temp_history_index] = temp;
    pid->temp_history_index = (pid->temp_history_index + 1) % 10;
    if (pid->temp_history_size < 10) {
        pid->temp_history_size++;
    }
}

bool pid_controller_is_temp_stuck(pid_controller_t* pid) {
    if (!pid || pid->temp_history_size < 5) {
        return false; // Need at least 5 readings to detect stuck
    }
    
    // Calculate the average temperature over the last few readings
    int sum = 0;
    for (int i = 0; i < pid->temp_history_size; i++) {
        sum += pid->temp_history[i];
    }
    double avg_temp = (double)sum / pid->temp_history_size;
    
    // Check if all recent temperatures are within the stuck threshold of the average
    int stuck_count = 0;
    int identical_count = 0;
    int last_temp = pid->temp_history[0];
    
    for (int i = 0; i < pid->temp_history_size; i++) {
        if (fabs(pid->temp_history[i] - avg_temp) <= pid->stuck_temp_threshold) {
            stuck_count++;
        }
        if (pid->temp_history[i] == last_temp) {
            identical_count++;
        }
        last_temp = pid->temp_history[i];
    }
    
    // Consider temperature stuck if:
    // 1. Most readings are within threshold of average (standard check)
    // 2. OR we have several identical readings in a row (new check)
    // 3. OR temperature is high and not changing despite high fan speed
    bool stuck = (stuck_count >= pid->temp_history_size * 0.6) || // Reduced from 0.8 to 0.6
                (identical_count >= pid->temp_history_size * 0.8) ||
                (avg_temp > 70 && stuck_count >= pid->temp_history_size * 0.5);
    
    if (stuck) {
        pid->stuck_detection_counter++;
        if (pid->stuck_detection_counter % 5 == 0) { // Increased frequency of debug logs
            logging_debug("Temperature stuck detection: avg=%.1f°C, stuck_count=%d/%d, identical=%d/%d, counter=%d", 
                      avg_temp, stuck_count, pid->temp_history_size, identical_count, pid->temp_history_size, pid->stuck_detection_counter);
        }
    } else {
        pid->stuck_detection_counter = 0;
    }
    
    // Consider stuck if we've detected it for multiple cycles
    // Reduced threshold for faster detection
    return (pid->stuck_detection_counter >= (pid->stuck_threshold_cycles / 2));
}

int pid_controller_get_aggressive_duty_for_error(pid_controller_t* pid, int temp_error) {
    if (!pid) return 0;
    
    // Progressive escalation based on temperature error
    if (temp_error >= 12) {
        return 100; // Emergency: 100% duty for 12°C+ error
    } else if (temp_error >= 8) {
        return 95;  // Very high: 95% duty for 8-11°C error
    } else if (temp_error >= 5) {
        return 85;  // High: 85% duty for 5-7°C error
    } else if (temp_error >= 3) {
        return 75;  // Moderate: 75% duty for 3-4°C error
    } else if (temp_error >= 1) {
        return 60;  // Low: 60% duty for 1-2°C error
    } else {
        return 0;   // No escalation needed
    }
}

void pid_controller_set_adaptive_tuning(pid_controller_t* pid, bool enabled,
                                      int tuning_interval, double target_performance) {
    if (!pid) return;
    
    pid->adaptive_enabled = enabled;
    pid->adaptive_tuning_interval = tuning_interval;
    pid->adaptive_target_performance = target_performance;
}

void pid_controller_reset(pid_controller_t* pid) {
    if (!pid) return;
    
    pid->integral = 0.0;
    pid->prev_error = 0.0;
    pid->adaptive_cycle_count = 0;
    pid->stuck_detection_counter = 0;
}

void pid_controller_cleanup(pid_controller_t* pid) {
    if (pid) {
        free(pid);
    }
}

// Internal helper functions
static double adaptive_pid_calculate_oscillation(pid_controller_t* pid) {
    if (pid->adaptive_temp_history_size < 10) return 0.0;
    
    double variance = 0.0;
    double mean = 0.0;
    
    // Calculate mean
    for (int i = 0; i < pid->adaptive_temp_history_size; i++) {
        mean += pid->adaptive_temp_history[i];
    }
    mean /= pid->adaptive_temp_history_size;
    
    // Calculate variance
    for (int i = 0; i < pid->adaptive_temp_history_size; i++) {
        double diff = pid->adaptive_temp_history[i] - mean;
        variance += diff * diff;
    }
    variance /= pid->adaptive_temp_history_size;
    
    return sqrt(variance);
}

static double adaptive_pid_calculate_performance_score(pid_controller_t* pid, int current_temp, int target_temp, int current_duty) {
    double error = fabs((double)current_temp - (double)target_temp);
    double oscillation = adaptive_pid_calculate_oscillation(pid);
    
    // Base score based on error (closer to target = higher score)
    double error_score = 1.0 - (error / 50.0);  // Normalize error to 0-1
    if (error_score < 0.0) error_score = 0.0;
    if (error_score > 1.0) error_score = 1.0;
    
    // Oscillation penalty (less oscillation = higher score)
    double oscillation_penalty = oscillation / 10.0;  // Normalize oscillation
    if (oscillation_penalty > 1.0) oscillation_penalty = 1.0;
    
    // Fan efficiency penalty (lower fan usage = higher score, but only if temp is good)
    double fan_efficiency = 1.0 - ((double)current_duty / 100.0);
    double fan_score = (error < 5.0) ? fan_efficiency : 0.0;  // Only consider fan efficiency if temp is close to target
    
    // Combine scores
    double final_score = (error_score * 0.6) + ((1.0 - oscillation_penalty) * 0.3) + (fan_score * 0.1);
    
    return final_score;
}

static void adaptive_pid_tune_parameters(pid_controller_t* pid, int current_temp, int target_temp, int current_duty) {
    double current_score = adaptive_pid_calculate_performance_score(pid, current_temp, target_temp, current_duty);
    double score_change = current_score - pid->adaptive_prev_score;
    
    logging_debug("Adaptive PID: Score=%.3f, Change=%.3f, Kp=%.2f, Ki=%.3f, Kd=%.2f",
               current_score, score_change, pid->kp, pid->ki, pid->kd);
    
    // Adjust parameters based on performance
    if (score_change > 0.05) {
        // Performance improved, continue in same direction
        logging_debug("Adaptive PID: Performance improved, maintaining direction");
    } else if (score_change < -0.05) {
        // Performance degraded, reverse direction
        pid->adaptive_kp_step *= -0.8;
        pid->adaptive_ki_step *= -0.8;
        pid->adaptive_kd_step *= -0.8;
        logging_debug("Adaptive PID: Performance degraded, reversing direction");
    }
    
    // Adjust Kp (proportional gain)
    if (current_score < pid->adaptive_target_performance) {
        pid->kp += pid->adaptive_kp_step;
        if (pid->kp < 0.5) pid->kp = 0.5;
        if (pid->kp > 5.0) pid->kp = 5.0;
    }
    
    // Adjust Ki (integral gain)
    double oscillation = adaptive_pid_calculate_oscillation(pid);
    double error = fabs((double)current_temp - (double)target_temp);
    
    if (oscillation > 3.0) {
        // High oscillation, reduce Ki and increase Kd
        pid->ki -= pid->adaptive_ki_step;
        pid->kd += pid->adaptive_kd_step;
    } else if (error > 5.0) {
        // High error, increase Ki
        pid->ki += pid->adaptive_ki_step;
    }
    
    // Clamp Ki and Kd values
    if (pid->ki < 0.01) pid->ki = 0.01;
    if (pid->ki > 0.5) pid->ki = 0.5;
    if (pid->kd < 0.1) pid->kd = 0.1;
    if (pid->kd > 2.0) pid->kd = 2.0;
    
    pid->adaptive_prev_score = current_score;
    pid->adaptive_performance_score = current_score;
    pid->adaptive_learning_cycles++;
    
    logging_debug("Adaptive PID: New parameters - Kp=%.2f, Ki=%.3f, Kd=%.2f",
               pid->kp, pid->ki, pid->kd);
} 