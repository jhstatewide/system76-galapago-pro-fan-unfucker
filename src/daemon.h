#ifndef DAEMON_H
#define DAEMON_H

#include <sys/types.h>

/**
 * Shared memory structure for inter-process communication
 */
typedef struct {
    volatile int exit;
    volatile int cpu_temp;
    volatile int fan_duty;
    volatile int fan_rpms;
    volatile int auto_duty;
    volatile int auto_duty_val;
    volatile int manual_next_fan_duty;
    volatile int manual_prev_fan_duty;
} daemon_shared_info_t;

/**
 * Daemon context structure
 */
typedef struct {
    daemon_shared_info_t* shared_info;
    volatile int running;
    void (*signal_handler)(int);
} daemon_context_t;

/**
 * Initialize daemon context
 * @param signal_handler Signal handler function
 * @return Pointer to daemon context, NULL on failure
 */
daemon_context_t* daemon_init(void (*signal_handler)(int));

/**
 * Initialize shared memory
 * @param context Daemon context
 * @return 0 on success, -1 on failure
 */
int daemon_init_shared_memory(daemon_context_t* context);

/**
 * Daemonize the process
 * @return 0 on success, -1 on failure
 */
int daemonize(void);

/**
 * Set up signal handlers
 * @param context Daemon context
 */
void daemon_setup_signals(daemon_context_t* context);

/**
 * Check if daemon should continue running
 * @param context Daemon context
 * @return true if should continue, false if should stop
 */
bool daemon_is_running(daemon_context_t* context);

/**
 * Stop the daemon
 * @param context Daemon context
 */
void daemon_stop(daemon_context_t* context);

/**
 * Update shared memory with current values
 * @param context Daemon context
 * @param cpu_temp Current CPU temperature
 * @param fan_duty Current fan duty cycle
 * @param fan_rpms Current fan RPM
 * @param auto_duty Auto duty mode flag
 * @param auto_duty_val Current auto duty value
 */
void daemon_update_shared_memory(daemon_context_t* context, int cpu_temp, int fan_duty, 
                               int fan_rpms, int auto_duty, int auto_duty_val);

/**
 * Get shared memory pointer
 * @param context Daemon context
 * @return Pointer to shared memory structure
 */
daemon_shared_info_t* daemon_get_shared_info(daemon_context_t* context);

/**
 * Clean up daemon context
 * @param context Daemon context
 */
void daemon_cleanup(daemon_context_t* context);

#endif // DAEMON_H 