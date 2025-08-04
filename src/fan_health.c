#include "fan_health.h"
#include "ec_interface.h"
#include "logging.h"
#include "fan_constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

fan_health_monitor_t* fan_health_init(int min_fan_rpm, int safe_fan_rpm, int emergency_duty,
                                     int rpm_duty_ratio, int health_check_interval) {
    fan_health_monitor_t* monitor = malloc(sizeof(fan_health_monitor_t));
    if (!monitor) {
        return NULL;
    }
    
    // Initialize health monitoring state
    monitor->low_rpm_count = 0;
    monitor->last_check_duty = 0;
    monitor->last_check_rpm = 0;
    monitor->last_check_time = 0;
    
    // Initialize recovery state
    monitor->recovery_attempts = 0;
    monitor->max_recovery_attempts = 3;
    
    // Initialize safety thresholds
    monitor->min_fan_rpm = min_fan_rpm;
    monitor->safe_fan_rpm = safe_fan_rpm;
    monitor->emergency_duty = emergency_duty;
    monitor->rpm_duty_ratio = rpm_duty_ratio;
    monitor->health_check_interval = health_check_interval;
    
    return monitor;
}

int fan_health_check(fan_health_monitor_t* monitor, int current_duty, int current_rpm) {
    if (!monitor) return -1;
    
    time_t current_time = time(NULL);
    
    // Only check every interval seconds
    if (current_time - monitor->last_check_time < monitor->health_check_interval) {
        return 0;
    }
    
    // Reset counters if duty cycle has changed significantly
    if (abs(current_duty - monitor->last_check_duty) > 5) {
        monitor->low_rpm_count = 0;
    }
    
    int expected_min_rpm = current_duty * monitor->rpm_duty_ratio;
    
    // Check if RPMs are critically low
    if (current_rpm < monitor->min_fan_rpm) {
        // Emergency response for critically low RPM
        logging_error("CRITICAL: Fan RPM (%d) below minimum threshold (%d)", current_rpm, monitor->min_fan_rpm);
        ec_write_fan_duty(monitor->emergency_duty);  // Immediately boost fan
        fan_health_attempt_recovery(monitor);  // Try recovery procedure
        monitor->low_rpm_count = 0;  // Reset counter after emergency response
        return -1;
    }
    // Check if RPMs are below safe operating level
    else if (current_rpm < monitor->safe_fan_rpm || (current_rpm < expected_min_rpm * 0.7)) {
        monitor->low_rpm_count++;
        
        if (monitor->low_rpm_count >= 2) {  // Reduced threshold for faster response
            logging_warning("Low fan RPM detected: %d RPM at %d%% duty", 
                          current_rpm, current_duty);
            
            // Increase duty cycle by 10% or to minimum safe duty
            int new_duty = (current_duty + 10 > 100) ? 100 : current_duty + 10;
            
            // CRITICAL: Ensure duty cycle will result in RPM above minimum threshold
            int min_duty_for_min_rpm = (FAN_MIN_RPM + FAN_RPM_DUTY_RATIO - 1) / FAN_RPM_DUTY_RATIO; // Ceiling division
            if (new_duty < min_duty_for_min_rpm) {
                logging_warning("Preventing fan stall: duty=%d%% would result in RPM below minimum (%d), setting to %d%%", 
                              new_duty, FAN_MIN_RPM, min_duty_for_min_rpm);
                new_duty = min_duty_for_min_rpm;
            }
            ec_write_fan_duty(new_duty);
            logging_info("Increasing fan duty to %d%% to maintain safe RPM", new_duty);
            
            if (monitor->low_rpm_count >= 4) {  // If problem persists, try recovery
                fan_health_attempt_recovery(monitor);
            }
            return -1;
        }
    } else {
        monitor->low_rpm_count = 0;  // Reset counter when RPMs are normal
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
    
    // First try: Full speed to kick-start
    ec_write_fan_duty(100);
    usleep(1000000);  // Wait 1 second at full speed
    
    int rpm = ec_query_fan_rpms();
    if (rpm > monitor->safe_fan_rpm) {
        logging_info("Fan kick-started successfully at %d RPM", rpm);
        return 0;
    }
    
    // If kick-start didn't work, try aggressive cycling
    int recovery_duties[] = {100, 80, 100, 60, 100, 40, 100};  // Always return to 100%
    int num_duties = sizeof(recovery_duties) / sizeof(recovery_duties[0]);
    
    for (int i = 0; i < num_duties; i++) {
        int duty = recovery_duties[i];
        logging_info("Recovery step %d/%d: Setting fan to %d%%", 
                  i + 1, num_duties, duty);
        
        ec_write_fan_duty(duty);
        usleep(800000);  // Wait longer (800ms) between changes
        
        // Check if fan responded
        rpm = ec_query_fan_rpms();
        if (rpm > monitor->safe_fan_rpm) {
            logging_info("Fan responding at %d RPM - recovery successful", rpm);
            
            // Gradually step down to ensure stability
            for (int step = 90; step >= 15; step -= 10) {
                ec_write_fan_duty(step);
                usleep(500000);
                rpm = ec_query_fan_rpms();
                if (rpm < monitor->safe_fan_rpm) {
                    // If RPM drops too low during step-down, go back to higher duty
                    ec_write_fan_duty(step + 20);
                    logging_info("Maintaining higher duty (%d%%) for stability", step + 20);
                    return 0;
                }
            }
            return 0;
        }
    }
    
    // If we get here, try one last emergency measure
    ec_write_fan_duty(100);
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
    
    monitor->low_rpm_count = 0;
    monitor->recovery_attempts = 0;
    monitor->last_check_time = 0;
}

void fan_health_cleanup(fan_health_monitor_t* monitor) {
    if (monitor) {
        free(monitor);
    }
} 