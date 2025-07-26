#ifndef FAN_HEALTH_H
#define FAN_HEALTH_H

#include <stdbool.h>
#include <time.h>

/**
 * Fan health monitor structure
 */
typedef struct {
    // Health monitoring state
    int low_rpm_count;
    int last_check_duty;
    int last_check_rpm;
    time_t last_check_time;
    
    // Recovery state
    int recovery_attempts;
    int max_recovery_attempts;
    
    // Safety thresholds
    int min_fan_rpm;
    int safe_fan_rpm;
    int emergency_duty;
    int rpm_duty_ratio;
    int health_check_interval;
} fan_health_monitor_t;

/**
 * Initialize fan health monitor
 * @param min_fan_rpm Minimum fan RPM threshold
 * @param safe_fan_rpm Safe fan RPM threshold
 * @param emergency_duty Emergency duty cycle
 * @param rpm_duty_ratio Expected RPM per duty cycle percentage
 * @param health_check_interval Health check interval in seconds
 * @return Pointer to fan health monitor, NULL on failure
 */
fan_health_monitor_t* fan_health_init(int min_fan_rpm, int safe_fan_rpm, int emergency_duty,
                                     int rpm_duty_ratio, int health_check_interval);

/**
 * Check fan health status
 * @param monitor Fan health monitor instance
 * @param current_duty Current fan duty cycle
 * @param current_rpm Current fan RPM
 * @return 0 if healthy, -1 if unhealthy
 */
int fan_health_check(fan_health_monitor_t* monitor, int current_duty, int current_rpm);

/**
 * Attempt fan recovery
 * @param monitor Fan health monitor instance
 * @return 0 on success, -1 on failure
 */
int fan_health_attempt_recovery(fan_health_monitor_t* monitor);

/**
 * Get fan health status string
 * @param monitor Fan health monitor instance
 * @param current_duty Current fan duty cycle
 * @param current_rpm Current fan RPM
 * @return Health status string
 */
const char* fan_health_get_status_string(fan_health_monitor_t* monitor, int current_duty, int current_rpm);

/**
 * Reset fan health monitor
 * @param monitor Fan health monitor instance
 */
void fan_health_reset(fan_health_monitor_t* monitor);

/**
 * Clean up fan health monitor
 * @param monitor Fan health monitor instance
 */
void fan_health_cleanup(fan_health_monitor_t* monitor);

#endif // FAN_HEALTH_H 