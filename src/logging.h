#ifndef LOGGING_H
#define LOGGING_H

#include <syslog.h>

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
 * @return 0 on success, -1 on failure
 */
int logging_init(const char* program_name, log_level_t log_level, int quiet_mode);

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
 * Clean up logging system
 */
void logging_cleanup(void);

#endif // LOGGING_H 