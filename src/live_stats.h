#ifndef LIVE_STATS_H
#define LIVE_STATS_H

#include <ncurses.h>

/**
 * Live stats display structure
 */
typedef struct {
    WINDOW* window;
    int initialized;
    double update_interval;
    
    // Display cache for optimization
    int last_display_cpu_temp;
    int last_display_fan_duty;
    int last_display_fan_rpm;
    double last_display_pid_error;
    double last_display_pid_p;
    double last_display_pid_i;
    double last_display_pid_d;
    
    // Debug log buffer
    char debug_log_buffer[10][256];
    int debug_log_index;
    int debug_log_count;
    
    // Display dimensions
    int max_y;
    int max_x;
} live_stats_t;

/**
 * Initialize live stats display
 * @param update_interval Update interval in seconds
 * @return Pointer to live stats structure, NULL on failure
 */
live_stats_t* live_stats_init(double update_interval);

/**
 * Display live statistics
 * @param stats Live stats structure
 * @param cpu_temp Current CPU temperature
 * @param fan_duty Current fan duty cycle
 * @param fan_rpm Current fan RPM
 * @param target_temp Target temperature
 * @param pid_enabled PID control enabled flag
 * @param pid_error PID error value
 * @param pid_p PID proportional term
 * @param pid_i PID integral term
 * @param pid_d PID derivative term
 * @param auto_duty Auto duty mode flag
 * @param stuck_detected Temperature stuck detection flag
 * @param recovery_attempts Fan recovery attempts
 * @param max_recovery_attempts Maximum recovery attempts
 * @param debug_mode Debug mode flag
 */
void live_stats_display(live_stats_t* stats, int cpu_temp, int fan_duty, int fan_rpm,
                       int target_temp, bool pid_enabled, double pid_error, double pid_p,
                       double pid_i, double pid_d, bool auto_duty, bool stuck_detected,
                       int recovery_attempts, int max_recovery_attempts, bool debug_mode);

/**
 * Handle window resize
 * @param stats Live stats structure
 */
void live_stats_handle_resize(live_stats_t* stats);

/**
 * Handle user input
 * @param stats Live stats structure
 * @return true if should quit, false to continue
 */
bool live_stats_handle_input(live_stats_t* stats);

/**
 * Add debug log message
 * @param stats Live stats structure
 * @param message Debug message
 */
void live_stats_add_debug_log(live_stats_t* stats, const char* message);

/**
 * Check if live stats is initialized
 * @param stats Live stats structure
 * @return true if initialized, false otherwise
 */
bool live_stats_is_initialized(live_stats_t* stats);

/**
 * Clean up live stats display
 * @param stats Live stats structure
 */
void live_stats_cleanup(live_stats_t* stats);

#endif // LIVE_STATS_H 