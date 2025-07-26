#ifndef TEMPERATURE_MONITOR_H
#define TEMPERATURE_MONITOR_H

#include <stdbool.h>

/**
 * Temperature monitor structure
 */
typedef struct {
    int last_cpu_temp;
    int temp_history[10];
    int temp_history_index;
    int temp_history_size;
    int stuck_detection_counter;
    int max_temp_change_per_cycle;
    bool temp_validation_enabled;
} temperature_monitor_t;

/**
 * Initialize temperature monitor
 * @param max_temp_change Maximum temperature change per cycle
 * @param validation_enabled Enable temperature validation
 * @return Pointer to temperature monitor, NULL on failure
 */
temperature_monitor_t* temperature_monitor_init(int max_temp_change, bool validation_enabled);

/**
 * Get CPU temperature from multiple sources
 * @param monitor Temperature monitor instance
 * @return Temperature in degrees Celsius, -1 on error
 */
int temperature_monitor_get_cpu_temp(temperature_monitor_t* monitor);

/**
 * Validate temperature reading
 * @param monitor Temperature monitor instance
 * @param current_temp Current temperature reading
 * @param last_temp Previous temperature reading
 * @param sensor_name Name of the sensor for logging
 * @return true if valid, false otherwise
 */
bool temperature_monitor_validate_reading(temperature_monitor_t* monitor, 
                                       int current_temp, int last_temp, 
                                       const char* sensor_name);

/**
 * Sanitize temperature reading (handle invalid readings)
 * @param monitor Temperature monitor instance
 * @param current_temp Current temperature reading
 * @param last_temp Previous temperature reading
 * @param sensor_name Name of the sensor for logging
 * @return Sanitized temperature reading
 */
int temperature_monitor_sanitize_reading(temperature_monitor_t* monitor,
                                       int current_temp, int last_temp,
                                       const char* sensor_name);

/**
 * Add temperature to history for trend analysis
 * @param monitor Temperature monitor instance
 * @param temp Temperature to add
 */
void temperature_monitor_add_to_history(temperature_monitor_t* monitor, int temp);

/**
 * Check if temperature appears stuck
 * @param monitor Temperature monitor instance
 * @return true if temperature appears stuck, false otherwise
 */
bool temperature_monitor_is_stuck(temperature_monitor_t* monitor);

/**
 * Get alternative CPU temperature from standard Linux sources
 * @return Temperature in degrees Celsius, -1 on error
 */
int temperature_monitor_get_alternative_cpu_temp(void);

/**
 * Clean up temperature monitor
 * @param monitor Temperature monitor instance
 */
void temperature_monitor_cleanup(temperature_monitor_t* monitor);

#endif // TEMPERATURE_MONITOR_H 