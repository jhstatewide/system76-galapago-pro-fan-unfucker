#include "logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

// Global logging state
static int log_level = LOG_INFO;
static int quiet_mode = 0;
static int debug_mode = 0;

int logging_init(const char* program_name, log_level_t initial_log_level, int initial_quiet_mode) {
    log_level = initial_log_level;
    quiet_mode = initial_quiet_mode;
    debug_mode = (initial_log_level == LOG_DEBUG);
    
    openlog(program_name, LOG_PID | LOG_CONS, LOG_DAEMON);
    return 0;
}

void logging_set_level(log_level_t level) {
    log_level = level;
    debug_mode = (level == LOG_DEBUG);
}

void logging_set_quiet(int quiet) {
    quiet_mode = quiet;
}

void logging_log(int priority, const char* format, ...) {
    if (quiet_mode && priority > LOG_ERR) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    if (priority <= log_level) {
        vsyslog(priority, format, args);
        
        // Only print to stdout if not in quiet mode and not in live stats mode
        if ((debug_mode || priority <= LOG_WARNING)) {
            vprintf(format, args);
            printf("\n");
            fflush(stdout);
        }
    }
    
    va_end(args);
}

void logging_debug(const char* format, ...) {
    if (log_level >= LOG_DEBUG) {
        va_list args;
        va_start(args, format);
        vsyslog(LOG_DEBUG, format, args);
        if (debug_mode) {
            vprintf(format, args);
            printf("\n");
            fflush(stdout);
        }
        va_end(args);
    }
}

void logging_info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logging_log(LOG_INFO, format, args);
    va_end(args);
}

void logging_warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logging_log(LOG_WARNING, format, args);
    va_end(args);
}

void logging_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logging_log(LOG_ERR, format, args);
    va_end(args);
}

void logging_cleanup(void) {
    closelog();
} 