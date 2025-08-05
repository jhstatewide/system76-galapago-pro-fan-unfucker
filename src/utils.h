#ifndef UTILS_H
#define UTILS_H

#include <time.h>
#include <stdbool.h>

/**
 * Get current time as string
 * @param buffer Buffer to store time string
 * @param buffer_size Size of buffer
 * @param format Time format string
 */
void get_time_string(char* buffer, size_t buffer_size, const char* format);

/**
 * Get system uptime in seconds
 * @return System uptime in seconds, -1 on error
 */
long get_system_uptime(void);

/**
 * Get system load average (1-minute)
 * @return System load average, -1.0 on error
 */
float get_system_load(void);

/**
 * Get number of duty cycle changes in last N minutes
 * @param minutes Number of minutes to check
 * @return Number of duty changes, -1 on error
 */
int get_duty_changes_last_minutes(int minutes);

/**
 * Calculate temperature gradient (°C/min)
 * @param temp_history Array of temperature readings
 * @param num_readings Number of readings
 * @param time_span_minutes Time span in minutes
 * @return Temperature gradient in °C/min
 */
float calculate_temp_gradient(int* temp_history, int num_readings, float time_span_minutes);

/**
 * Get ambient temperature (if available)
 * @return Ambient temperature in °C, -999.0 if not available
 */
float get_ambient_temperature(void);

/**
 * Get humidity (if available)
 * @return Humidity percentage, -999.0 if not available
 */
float get_humidity(void);

/**
 * Check for RPM flutter (instability)
 * @param rpm_history Array of recent RPM readings
 * @param num_readings Number of readings
 * @return true if flutter detected, false otherwise
 */
bool detect_rpm_flutter(int* rpm_history, int num_readings);

/**
 * Count duty cycle oscillations
 * @param duty_history Array of recent duty readings
 * @param num_readings Number of readings
 * @return Number of oscillations detected
 */
int count_duty_oscillations(int* duty_history, int num_readings);

/**
 * Detect temperature spikes
 * @param temp_history Array of recent temperature readings
 * @param num_readings Number of readings
 * @param threshold Temperature spike threshold in °C
 * @return true if spike detected, false otherwise
 */
bool detect_temp_spike(int* temp_history, int num_readings, int threshold);

/**
 * Record a duty cycle change for tracking
 */
void record_duty_change(void);

#endif // UTILS_H 