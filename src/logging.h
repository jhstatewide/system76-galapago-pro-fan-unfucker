#ifndef LOGGING_H
#define LOGGING_H

#include <syslog.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Log levels
 */
typedef enum {
    LOG_LEVEL_ERROR = LOG_ERR,
    LOG_LEVEL_WARNING = LOG_WARNING,
    LOG_LEVEL_INFO = LOG_INFO,
    LOG_LEVEL_DEBUG = LOG_DEBUG
} log_level_t;

/**
 * Initialize logging system
 * @param program_name Name of the program for syslog
 * @param log_level Initial log level
 * @param quiet_mode Enable quiet mode (only errors)
 * @param debug_mode Enable debug mode (verbose telemetry logging)
 * @return 0 on success, -1 on failure
 */
int logging_init(const char* program_name, log_level_t log_level, int quiet_mode, int debug_mode);

/**
 * Set log level
 * @param level New log level
 */
void logging_set_level(log_level_t level);

/**
 * Set quiet mode
 * @param quiet Enable/disable quiet mode
 */
void logging_set_quiet(int quiet);

/**
 * Set debug mode
 * @param debug Enable/disable debug mode (controls telemetry logging verbosity)
 */
void logging_set_debug_mode(int debug);

/**
 * Check if debug mode is enabled
 * @return 1 if debug mode is enabled, 0 otherwise
 */
int logging_is_debug_mode(void);

/**
 * Log a message
 * @param priority Log priority level
 * @param format Format string
 * @param ... Variable arguments
 */
void logging_log(int priority, const char* format, ...);

/**
 * Log debug message (only if debug level enabled)
 * @param format Format string
 * @param ... Variable arguments
 */
void logging_debug(const char* format, ...);

/**
 * Log info message
 * @param format Format string
 * @param ... Variable arguments
 */
void logging_info(const char* format, ...);

/**
 * Log warning message
 * @param format Format string
 * @param ... Variable arguments
 */
void logging_warning(const char* format, ...);

/**
 * Log error message
 * @param format Format string
 * @param ... Variable arguments
 */
void logging_error(const char* format, ...);

/**
 * Log telemetry information (only logged in debug mode)
 * @param format Format string
 * @param ... Variable arguments
 */
void logging_telemetry(const char* format, ...);

/**
 * Enhanced diagnostic logging functions
 */

/**
 * Log hardware state during fan issues
 * @param duty Current duty cycle
 * @param rpm Current RPM
 * @param expected_rpm Expected RPM for current duty
 * @param cpu_temp CPU temperature
 * @param system_load System load (if available)
 */
void logging_hardware_state(int duty, int rpm, int expected_rpm, int cpu_temp, float system_load);

/**
 * Log timing analysis for fan response
 * @param duty_change New duty cycle
 * @param time_since_last_change Microseconds since last duty change
 * @param rpm_response_time Microseconds for RPM to respond
 * @param rpm_before Previous RPM reading
 * @param rpm_after Current RPM reading
 */
void logging_timing_analysis(int duty_change, long time_since_last_change, 
                           long rpm_response_time, int rpm_before, int rpm_after);

/**
 * Log recovery sequence details
 * @param step_number Current recovery step (1-based)
 * @param total_steps Total recovery steps
 * @param duty_step Duty cycle for this step
 * @param rpm_before RPM before step
 * @param rpm_after RPM after step
 * @param wait_time_ms Time waited for response
 * @param success Whether this step was successful
 */
void logging_recovery_step(int step_number, int total_steps, int duty_step,
                         int rpm_before, int rpm_after, int wait_time_ms, bool success);

/**
 * Log stall prediction indicators
 * @param rpm_flutter RPM instability detected
 * @param duty_oscillations Number of duty cycle oscillations
 * @param temp_spike_detected Temperature spike detected
 * @param near_stall_conditions Number of near-stall conditions
 */
void logging_stall_prediction(bool rpm_flutter, int duty_oscillations, 
                            bool temp_spike_detected, int near_stall_conditions);

/**
 * Log environmental context
 * @param system_uptime System uptime in seconds
 * @param duty_changes_last_5min Number of duty changes in last 5 minutes
 * @param temp_gradient Temperature change rate (°C/min)
 * @param ambient_temp Ambient temperature (if available)
 * @param humidity Humidity (if available)
 */
void logging_environmental_context(long system_uptime, int duty_changes_last_5min,
                                 float temp_gradient, float ambient_temp, float humidity);

/**
 * Log EC register dump for debugging
 * @param registers Array of register values (0-255)
 * @param num_registers Number of registers to log
 */
void logging_ec_register_dump(uint8_t* registers, int num_registers);

/**
 * Clean up logging system
 */
void logging_cleanup(void);

#endif // LOGGING_H 