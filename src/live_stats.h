/*
 ============================================================================
 Name        : live_stats.h
 Description : Live statistics display module for Clevo fan control daemon

 This module provides a ncurses-based real-time display of fan control
 statistics including temperature, fan duty, RPM, and PID status.

 ============================================================================
 */

#ifndef LIVE_STATS_H
#define LIVE_STATS_H

#include <ncurses.h>
#include <stdbool.h>

// Share info structure (matches the one in clevo-daemon.c)
typedef struct {
    volatile int exit;
    volatile int cpu_temp;
    volatile int fan_duty;
    volatile int fan_rpms;
    volatile int auto_duty;
    volatile int auto_duty_val;
    volatile int manual_next_fan_duty;
    volatile int manual_prev_fan_duty;
} share_info_t;

// Configuration structure for live stats
typedef struct {
    bool enabled;
    double update_interval;
    bool debug_mode;
    int target_temperature;
    bool pid_enabled;
    const char* version;
} live_stats_config_t;

// Live stats state structure
typedef struct {
    WINDOW* window;
    bool initialized;
    int last_display_cpu_temp;
    int last_display_fan_duty;
    int last_display_fan_rpm;
    double last_display_pid_error;
    double last_display_pid_p;
    double last_display_pid_i;
    double last_display_pid_d;
} live_stats_state_t;

// Function declarations
void live_stats_init(live_stats_config_t* config);
void live_stats_display(share_info_t* share_info, live_stats_config_t* config);
void live_stats_cleanup(void);
void live_stats_handle_resize(void);
void live_stats_handle_input(volatile int* running, bool debug_mode);

// Configuration functions
void live_stats_set_enabled(bool enabled);
void live_stats_set_interval(double interval);
void live_stats_set_debug_mode(bool debug_mode);
void live_stats_set_target_temperature(int temp);
void live_stats_set_pid_enabled(bool enabled);
void live_stats_set_version(const char* version);

// Utility functions
bool live_stats_is_initialized(void);
bool live_stats_is_enabled(void);

#endif // LIVE_STATS_H 