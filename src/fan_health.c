#include "fan_health.h"
#include "ec_interface.h"
#include "logging.h"
#include "fan_constants.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

// Enhanced fan health monitor with diagnostic tracking
typedef struct {
    // Original fan health monitor
    fan_health_monitor_t base;
    
    // Diagnostic tracking
    int rpm_history[20];
    int duty_history[20];
    int temp_history[20];
    int history_index;
    int history_count;
    
    // Timing tracking
    struct timeval last_duty_change_time;
    struct timeval last_rpm_check_time;
    
    // Stall prediction
    int near_stall_count;
    int duty_oscillation_count;
    bool rpm_flutter_detected;
    bool temp_spike_detected;
    
    // Recovery analytics
    int successful_recovery_steps[10];
    int recovery_step_success_count;
} enhanced_fan_health_monitor_t;

fan_health_monitor_t* fan_health_init(int min_fan_rpm, int safe_fan_rpm, int emergency_duty,
                                     int rpm_duty_ratio, int health_check_interval) {
    enhanced_fan_health_monitor_t* monitor = malloc(sizeof(enhanced_fan_health_monitor_t));
    if (!monitor) {
        return NULL;
    }
    
    // Initialize base health monitoring state
    monitor->base.low_rpm_count = 0;
    monitor->base.last_check_duty = 0;
    monitor->base.last_check_rpm = 0;
    monitor->base.last_check_time = 0;
    
    // Initialize recovery state
    monitor->base.recovery_attempts = 0;
    monitor->base.max_recovery_attempts = 3;
    
    // Initialize safety thresholds
    monitor->base.min_fan_rpm = min_fan_rpm;
    monitor->base.safe_fan_rpm = safe_fan_rpm;
    monitor->base.emergency_duty = emergency_duty;
    monitor->base.rpm_duty_ratio = rpm_duty_ratio;
    monitor->base.health_check_interval = health_check_interval;
    
    // Initialize diagnostic tracking
    memset(monitor->rpm_history, 0, sizeof(monitor->rpm_history));
    memset(monitor->duty_history, 0, sizeof(monitor->duty_history));
    memset(monitor->temp_history, 0, sizeof(monitor->temp_history));
    monitor->history_index = 0;
    monitor->history_count = 0;
    
    // Initialize timing
    gettimeofday(&monitor->last_duty_change_time, NULL);
    gettimeofday(&monitor->last_rpm_check_time, NULL);
    
    // Initialize stall prediction
    monitor->near_stall_count = 0;
    monitor->duty_oscillation_count = 0;
    monitor->rpm_flutter_detected = false;
    monitor->temp_spike_detected = false;
    
    // Initialize recovery analytics
    memset(monitor->successful_recovery_steps, 0, sizeof(monitor->successful_recovery_steps));
    monitor->recovery_step_success_count = 0;
    
    return (fan_health_monitor_t*)monitor;
}

int fan_health_check(fan_health_monitor_t* monitor, int current_duty, int current_rpm) {
    if (!monitor) return -1;
    
    enhanced_fan_health_monitor_t* enhanced_monitor = (enhanced_fan_health_monitor_t*)monitor;
    time_t current_time = time(NULL);
    struct timeval current_timeval;
    gettimeofday(&current_timeval, NULL);
    
    // Only check every interval seconds
    if (current_time - monitor->last_check_time < monitor->health_check_interval) {
        return 0;
    }
    
    // Update history for diagnostic analysis
    enhanced_monitor->rpm_history[enhanced_monitor->history_index] = current_rpm;
    enhanced_monitor->duty_history[enhanced_monitor->history_index] = current_duty;
    enhanced_monitor->history_index = (enhanced_monitor->history_index + 1) % 20;
    if (enhanced_monitor->history_count < 20) {
        enhanced_monitor->history_count++;
    }
    
    // Get CPU temperature for environmental context
    int cpu_temp = ec_query_cpu_temp();
    if (cpu_temp > 0) {
        enhanced_monitor->temp_history[enhanced_monitor->history_index] = cpu_temp;
    }
    
    // Calculate timing information
    long time_since_last_change = 0;
    long rpm_response_time = 0;
    if (current_duty != monitor->last_check_duty) {
        time_since_last_change = (current_timeval.tv_sec - enhanced_monitor->last_duty_change_time.tv_sec) * 1000000 +
                               (current_timeval.tv_usec - enhanced_monitor->last_duty_change_time.tv_usec);
        enhanced_monitor->last_duty_change_time = current_timeval;
        record_duty_change(); // Track duty changes for environmental analysis
    }
    
    // Calculate RPM response time
    rpm_response_time = (current_timeval.tv_sec - enhanced_monitor->last_rpm_check_time.tv_sec) * 1000000 +
                       (current_timeval.tv_usec - enhanced_monitor->last_rpm_check_time.tv_usec);
    enhanced_monitor->last_rpm_check_time = current_timeval;
    
    // Enhanced diagnostic logging
    int expected_min_rpm = current_duty * monitor->rpm_duty_ratio;
    float system_load = get_system_load();
    
    // Log hardware state
    logging_hardware_state(current_duty, current_rpm, expected_min_rpm, cpu_temp, system_load);
    
    // Log timing analysis if duty changed
    if (current_duty != monitor->last_check_duty) {
        logging_timing_analysis(current_duty, time_since_last_change, rpm_response_time, 
                              monitor->last_check_rpm, current_rpm);
    }
    
    // Stall prediction analysis
    bool rpm_flutter = detect_rpm_flutter(enhanced_monitor->rpm_history, enhanced_monitor->history_count);
    int duty_oscillations = count_duty_oscillations(enhanced_monitor->duty_history, enhanced_monitor->history_count);
    bool temp_spike = detect_temp_spike(enhanced_monitor->temp_history, enhanced_monitor->history_count, 5);
    
    // Update stall prediction state
    enhanced_monitor->rpm_flutter_detected = rpm_flutter;
    enhanced_monitor->duty_oscillation_count = duty_oscillations;
    enhanced_monitor->temp_spike_detected = temp_spike;
    
    // Log stall prediction
    logging_stall_prediction(rpm_flutter, duty_oscillations, temp_spike, enhanced_monitor->near_stall_count);
    
    // Log environmental context
    long system_uptime = get_system_uptime();
    int duty_changes_5min = get_duty_changes_last_minutes(5);
    float temp_gradient = calculate_temp_gradient(enhanced_monitor->temp_history, enhanced_monitor->history_count, 5.0);
    float ambient_temp = get_ambient_temperature();
    float humidity = get_humidity();
    
    logging_environmental_context(system_uptime, duty_changes_5min, temp_gradient, ambient_temp, humidity);
    
    // Reset counters if duty cycle has changed significantly
    if (abs(current_duty - monitor->last_check_duty) > 5) {
        monitor->low_rpm_count = 0;
    }
    
    // Check if RPMs are critically low
    if (current_rpm < monitor->min_fan_rpm) {
        // Emergency response for critically low RPM
        logging_error("CRITICAL: Fan RPM (%d) below minimum threshold (%d)", current_rpm, monitor->min_fan_rpm);
        
        // Log EC register dump for debugging
        uint8_t ec_registers[256];
        for (int i = 0; i < 256; i++) {
            ec_registers[i] = 0; // Placeholder - would need actual EC read function
        }
        logging_ec_register_dump(ec_registers, 256);
        
        ec_write_fan_duty_with_retry(monitor->emergency_duty, 3);  // Immediately boost fan with retry
        fan_health_attempt_recovery(monitor);  // Try recovery procedure
        monitor->low_rpm_count = 0;  // Reset counter after emergency response
        return -1;
    }
    // Check if RPMs are below safe operating level with more realistic thresholds
    else if (current_rpm < monitor->safe_fan_rpm || (current_rpm < expected_min_rpm * 0.5)) {
        monitor->low_rpm_count++;
        enhanced_monitor->near_stall_count++;
        
        if (monitor->low_rpm_count >= 3) {  // Increased threshold to reduce false alarms
            logging_warning("Low fan RPM detected: %d RPM at %d%% duty (expected >%d)", 
                          current_rpm, current_duty, expected_min_rpm);
            
            // Try immediate "kick" recovery for stuck fans
            if (current_rpm < 1000 && current_duty > 50) {
                logging_warning("Fan appears stuck at low RPM, attempting recovery kick");
                ec_write_fan_duty_with_retry(100, 3);  // Full speed kick
                usleep(2000000);  // Wait 2 seconds
                
                int rpm_after_kick = ec_query_fan_rpms();
                if (rpm_after_kick > current_rpm * 2) {
                    logging_info("Fan recovery kick successful: %d -> %d RPM", current_rpm, rpm_after_kick);
                    // Gradually step down to target duty
                    for (int step = 90; step >= current_duty; step -= 10) {
                        ec_write_fan_duty_with_retry(step, 3);
                        usleep(500000);
                    }
                    monitor->low_rpm_count = 0;
                    return 0;
                }
            }
            
            // Increase duty cycle by 15% for more aggressive response
            int new_duty = (current_duty + 15 > 100) ? 100 : current_duty + 15;
            
            // CRITICAL: Ensure duty cycle will result in RPM above minimum threshold
            int min_duty_for_min_rpm = (FAN_MIN_RPM + FAN_RPM_DUTY_RATIO - 1) / FAN_RPM_DUTY_RATIO; // Ceiling division
            if (new_duty < min_duty_for_min_rpm) {
                logging_warning("Preventing fan stall: duty=%d%% would result in RPM below minimum (%d), setting to %d%%", 
                              new_duty, FAN_MIN_RPM, min_duty_for_min_rpm);
                new_duty = min_duty_for_min_rpm;
            }
            ec_write_fan_duty_with_retry(new_duty, 3);
            logging_info("Increasing fan duty to %d%% to maintain safe RPM", new_duty);
            
            if (monitor->low_rpm_count >= 5) {  // Increased threshold for recovery
                fan_health_attempt_recovery(monitor);
            }
            return -1;
        }
    } else {
        monitor->low_rpm_count = 0;  // Reset counter when RPMs are normal
        enhanced_monitor->near_stall_count = 0; // Reset near stall count
    }
    
    // Update check state
    monitor->last_check_duty = current_duty;
    monitor->last_check_rpm = current_rpm;
    monitor->last_check_time = current_time;
    
    return 0;
}

int fan_health_attempt_recovery(fan_health_monitor_t* monitor) {
    if (!monitor) return -1;
    
    if (monitor->recovery_attempts >= monitor->max_recovery_attempts) {
        logging_error("Maximum fan recovery attempts reached (%d/%d)", 
                     monitor->recovery_attempts, monitor->max_recovery_attempts);
        return -1;
    }
    
    monitor->recovery_attempts++;
    logging_info("Attempting fan recovery (attempt %d/%d)...", 
                monitor->recovery_attempts, monitor->max_recovery_attempts);
    
    // Enhanced recovery sequence with proven "kick" method
    logging_info("Starting fan recovery with kick method...");
    
    // Step 1: Full speed kick to overcome static friction
    int rpm_before = ec_query_fan_rpms();
    logging_info("Recovery step 1/3: Full speed kick (100%%)");
    ec_write_fan_duty_with_retry(100, 3);
    usleep(3000000);  // Wait 3 seconds for fan to respond
    
    int rpm_after_kick = ec_query_fan_rpms();
    logging_recovery_step(1, 3, 100, rpm_before, rpm_after_kick, 3000, rpm_after_kick > rpm_before);
    
    if (rpm_after_kick > rpm_before * 2) {
        logging_info("Fan kick successful: %d -> %d RPM", rpm_before, rpm_after_kick);
        
        // Step 2: Gradual step-down to target duty
        int target_duty = 60;  // Conservative target
        logging_info("Recovery step 2/3: Gradual step-down to %d%%", target_duty);
        
        for (int step = 90; step >= target_duty; step -= 10) {
            ec_write_fan_duty_with_retry(step, 3);
            usleep(1000000);  // Wait 1 second between steps
            
            int rpm_after_step = ec_query_fan_rpms();
            if (rpm_after_step < monitor->safe_fan_rpm) {
                logging_warning("RPM dropped too low during step-down, maintaining higher duty");
                ec_write_fan_duty_with_retry(step + 10, 3);
                return 0;
            }
        }
        
        // Step 3: Verify stability
        logging_info("Recovery step 3/3: Verifying stability");
        usleep(2000000);  // Wait 2 seconds
        int final_rpm = ec_query_fan_rpms();
        
        if (final_rpm > monitor->safe_fan_rpm) {
            logging_info("Fan recovery successful: stable at %d RPM", final_rpm);
            return 0;
        } else {
            logging_warning("Fan recovery incomplete: RPM still low at %d", final_rpm);
        }
    } else {
        logging_warning("Fan kick failed: RPM only increased from %d to %d", rpm_before, rpm_after_kick);
    }
    
    // If we get here, try one last emergency measure
            ec_write_fan_duty_with_retry(100, 3);
    logging_error("Fan recovery failed - setting to full speed for safety");
    return -1;
}

const char* fan_health_get_status_string(fan_health_monitor_t* monitor, int current_duty, int current_rpm) {
    if (!monitor) return "UNKNOWN";
    
    if (current_rpm < monitor->min_fan_rpm) {
        return "CRITICAL";
    } else if (current_rpm < monitor->safe_fan_rpm && current_duty > 20) {
        return "LOW";
    } else if (current_rpm < 2000 && current_duty > 50) {
        return "WARN";
    } else {
        return "OK";
    }
}

void fan_health_reset(fan_health_monitor_t* monitor) {
    if (!monitor) return;
    
    enhanced_fan_health_monitor_t* enhanced_monitor = (enhanced_fan_health_monitor_t*)monitor;
    
    monitor->low_rpm_count = 0;
    monitor->recovery_attempts = 0;
    monitor->last_check_time = 0;
    
    // Reset diagnostic tracking
    enhanced_monitor->near_stall_count = 0;
    enhanced_monitor->duty_oscillation_count = 0;
    enhanced_monitor->rpm_flutter_detected = false;
    enhanced_monitor->temp_spike_detected = false;
    enhanced_monitor->recovery_step_success_count = 0;
}

void fan_health_cleanup(fan_health_monitor_t* monitor) {
    if (monitor) {
        free(monitor);
    }
} 