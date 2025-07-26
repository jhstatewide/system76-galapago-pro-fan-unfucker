#ifndef UTILS_H
#define UTILS_H

#include <sys/types.h>

/**
 * Check how many instances of a process are running
 * @param proc_name Process name to check
 * @return Number of running instances
 */
int check_proc_instances(const char* proc_name);

/**
 * Format current time into a string
 * @param buffer Output buffer
 * @param max Maximum buffer size
 * @param format Time format string
 */
void get_time_string(char* buffer, size_t max, const char* format);

/**
 * Set up signal handlers
 * @param handler Signal handler function
 */
void signal_term(void (*handler)(int));

/**
 * Initialize utility functions
 * @return 0 on success, -1 on failure
 */
int utils_init(void);

/**
 * Clean up utility functions
 */
void utils_cleanup(void);

#endif // UTILS_H 