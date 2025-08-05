/*
 ============================================================================
 Name        : live_stats.c
 Description : Live statistics display module for Clevo fan control daemon

 This module provides a ncurses-based real-time display of fan control
 statistics including temperature, fan duty, RPM, and PID status.

 ============================================================================
 */

#include "live_stats.h"
#include "logging.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

// Global state for live stats
static live_stats_state_t live_stats_state = {0};
static live_stats_config_t live_stats_config = {0};

// Debug log buffer (if needed)
static char debug_log_buffer[10][256];
static int debug_log_index = 0;
static int debug_log_count = 0;

// These functions are provided by the daemon - we'll use placeholder values for now
// In a full implementation, these would be passed as parameters or callbacks
static bool is_temp_stuck_placeholder(void) {
    return false;  // Placeholder - would be provided by daemon
}

static int fan_recovery_attempts_placeholder = 0;
static int max_fan_recovery_attempts_placeholder = 3;

void live_stats_init(live_stats_config_t* config) {
    if (config) {
        live_stats_config = *config;
    }
    
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    curs_set(0);  // Hide cursor
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);  // Non-blocking input
    
    // Enable colors if available
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_GREEN, COLOR_BLACK);   // Normal
        init_pair(2, COLOR_YELLOW, COLOR_BLACK);  // Warning
        init_pair(3, COLOR_RED, COLOR_BLACK);     // Error
        init_pair(4, COLOR_CYAN, COLOR_BLACK);    // Info
        init_pair(5, COLOR_WHITE, COLOR_BLACK);   // Header
    }
    
    // Create main window
    live_stats_state.window = stdscr;
    live_stats_state.initialized = true;
    
    // Clear screen and draw initial layout
    clear();
    refresh();
    
    // Handle window resize
    live_stats_handle_resize();
}

void live_stats_handle_resize(void) {
    if (!live_stats_state.initialized) return;
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // Minimum window size check
    if (max_y < 12 || max_x < 60) {
        clear();
        mvprintw(max_y/2, (max_x-40)/2, "Window too small! Need 60x12 minimum");
        refresh();
        return;
    }
    
    // Clear screen for redraw
    clear();
}

void live_stats_display(share_info_t* share_info, live_stats_config_t* config) {
    if (!live_stats_state.initialized || !share_info) return;
    
    // Update config if provided
    if (config) {
        live_stats_config = *config;
    }
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // Check window size - need more space if debug mode is enabled
    int min_height = live_stats_config.debug_mode ? 20 : 12;
    if (max_y < min_height || max_x < 60) {
        live_stats_handle_resize();
        return;
    }
    
    // Get current values
    int cpu_temp = share_info->cpu_temp;
    int fan_duty = share_info->fan_duty;
    int fan_rpm = share_info->fan_rpms;
    int max_temp = cpu_temp;  // Only use CPU temperature
    
    // Calculate PID values for display
    double pid_error = 0.0, pid_p = 0.0, pid_i = 0.0, pid_d = 0.0;
    if (live_stats_config.pid_enabled) {
        pid_error = (double)max_temp - (double)live_stats_config.target_temperature;
        // Note: PID terms would need to be passed from the daemon
        // For now, we'll just show the error
        pid_p = 0.0;  // Would be calculated in daemon
        pid_i = 0.0;  // Would be calculated in daemon
        pid_d = 0.0;  // Would be calculated in daemon
    }
    
    // Draw header
    attron(COLOR_PAIR(5) | A_BOLD);
    mvprintw(0, 0, "+--- Clevo Fan Control Live Stats v%s ", 
             live_stats_config.version ? live_stats_config.version : "unknown");
    for (int i = 32 + (live_stats_config.version ? strlen(live_stats_config.version) : 7); 
         i < max_x - 2; i++) mvprintw(0, i, "-");
    mvprintw(0, max_x - 2, "+");
    attroff(COLOR_PAIR(5) | A_BOLD);
    
    // Draw header info
    attron(COLOR_PAIR(4));
    mvprintw(1, 2, "Target: %3d°C            ", live_stats_config.target_temperature);
    mvprintw(1, 30, "Update: %5.0fms            ", live_stats_config.update_interval * 1000);
    mvprintw(1, 60, "PID: %-8s            ", live_stats_config.pid_enabled ? "Enabled" : "Disabled");
    if (live_stats_config.debug_mode) {
        mvprintw(1, 85, "DEBUG: ON            ");
    }
    attroff(COLOR_PAIR(4));
    
    // Draw separator
    mvprintw(2, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(2, i, "-");
    mvprintw(2, max_x - 1, "+");
    
    // Temperature section - only update if changed
    if (cpu_temp != live_stats_state.last_display_cpu_temp) {
        mvprintw(3, 2, "Temperature:                                ");
        // CPU temperature with color coding
        if (cpu_temp > live_stats_config.target_temperature + 10) {
            attron(COLOR_PAIR(3));
        } else if (cpu_temp > live_stats_config.target_temperature) {
            attron(COLOR_PAIR(2));
        } else {
            attron(COLOR_PAIR(1));
        }
        mvprintw(4, 4, "CPU: %3d°C            ", cpu_temp);
        attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3));
        live_stats_state.last_display_cpu_temp = cpu_temp;
    }
    
    // Draw separator
    mvprintw(5, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(5, i, "-");
    mvprintw(5, max_x - 1, "+");
    
    // Fan control section - only update if changed
    if (fan_duty != live_stats_state.last_display_fan_duty || 
        fan_rpm != live_stats_state.last_display_fan_rpm) {
        mvprintw(6, 2, "Fan Control:                                ");
        // Fan duty with color coding
        if (fan_duty >= 80) {
            attron(COLOR_PAIR(2));
        } else if (fan_duty >= 50) {
            attron(COLOR_PAIR(4));
        } else {
            attron(COLOR_PAIR(1));
        }
        mvprintw(7, 4, "Duty: %3d%%            ", fan_duty);
        attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(4));
        // Fan RPM with color coding
        if (fan_rpm < 1000 && fan_duty > 20) {
            attron(COLOR_PAIR(3));
        } else if (fan_rpm < 2000) {
            attron(COLOR_PAIR(2));
        } else {
            attron(COLOR_PAIR(1));
        }
        mvprintw(7, 30, "RPM: %5d            ", fan_rpm);
        attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3));
        // Fan health status
        const char* health_status = "OK";
        if (fan_rpm < 1000 && fan_duty > 20) {
            health_status = "LOW";
        } else if (fan_rpm < 2000 && fan_duty > 50) {
            health_status = "WARN";
        }
        mvprintw(7, 56, "Health: %-5s            ", health_status);
        live_stats_state.last_display_fan_duty = fan_duty;
        live_stats_state.last_display_fan_rpm = fan_rpm;
    }
    
    // Draw separator
    mvprintw(8, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(8, i, "-");
    mvprintw(8, max_x - 1, "+");
    
    // PID status section - only update if changed
    if (live_stats_config.pid_enabled && 
        (fabs(pid_error - live_stats_state.last_display_pid_error) > 0.1 || 
         fabs(pid_p - live_stats_state.last_display_pid_p) > 0.1 ||
         fabs(pid_i - live_stats_state.last_display_pid_i) > 0.1 ||
         fabs(pid_d - live_stats_state.last_display_pid_d) > 0.1)) {
        mvprintw(9, 2, "PID Status:                                ");
        // Error with color coding
        if (fabs(pid_error) > 10) {
            attron(COLOR_PAIR(3));
        } else if (fabs(pid_error) > 5) {
            attron(COLOR_PAIR(2));
        } else {
            attron(COLOR_PAIR(1));
        }
        mvprintw(10, 4, "Error: %+6.1f°C            ", pid_error);
        attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3));
        // PID terms
        mvprintw(10, 25, "P: %7.1f            ", pid_p);
        mvprintw(10, 45, "I: %7.1f            ", pid_i);
        mvprintw(10, 65, "D: %7.1f            ", pid_d);
        live_stats_state.last_display_pid_error = pid_error;
        live_stats_state.last_display_pid_p = pid_p;
        live_stats_state.last_display_pid_i = pid_i;
        live_stats_state.last_display_pid_d = pid_d;
    } else if (!live_stats_config.pid_enabled) {
        mvprintw(9, 2, "PID Status: Disabled                                ");
    }
    
    // Draw separator
    mvprintw(11, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(11, i, "-");
    mvprintw(11, max_x - 1, "+");
    
    // Status section
    mvprintw(12, 2, "Status: %-10s            ", share_info->auto_duty ? "Auto Mode" : "Manual Mode");
    
    // Stuck detection status
    const char* stuck_status = is_temp_stuck_placeholder() ? "Yes" : "No";
    if (is_temp_stuck_placeholder()) {
        attron(COLOR_PAIR(2));
    }
    mvprintw(12, 30, "Stuck: %-3s            ", stuck_status);
    if (is_temp_stuck_placeholder()) {
        attroff(COLOR_PAIR(2));
    }
    
    // Recovery attempts
    mvprintw(12, 56, "Recovery: %d/%d            ", fan_recovery_attempts_placeholder, max_fan_recovery_attempts_placeholder);
    
    // Draw footer
    mvprintw(13, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(13, i, "-");
    mvprintw(13, max_x - 1, "+");
    
    // Debug log area (only show if debug mode is enabled)
    if (live_stats_config.debug_mode) {
        // Draw debug separator
        mvprintw(14, 0, "+");
        for (int i = 1; i < max_x - 1; i++) mvprintw(14, i, "-");
        mvprintw(14, max_x - 1, "+");
        
        // Debug log header
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(15, 2, "Debug Log:");
        attroff(COLOR_PAIR(4) | A_BOLD);
        
        // Show last few debug messages
        int log_start = debug_log_count > 0 ? (debug_log_index - 1 + 10) % 10 : 0;
        for (int i = 0; i < debug_log_count && i < 4; i++) {
            int log_idx = (log_start - i + 10) % 10;
            if (log_idx >= 0 && log_idx < debug_log_count) {
                // Truncate long messages to fit screen
                char truncated[256];
                strncpy(truncated, debug_log_buffer[log_idx], max_x - 4);
                truncated[max_x - 4] = '\0';
                mvprintw(16 + i, 2, "%s", truncated);
            }
        }
        
        // Instructions
        attron(COLOR_PAIR(4));
        mvprintw(20, 2, "Press 'q' to quit, 'r' to refresh display");
        attroff(COLOR_PAIR(4));
    } else {
        // Instructions (when not in debug mode)
        attron(COLOR_PAIR(4));
        mvprintw(14, 2, "Press 'q' to quit, 'r' to refresh display");
        attroff(COLOR_PAIR(4));
    }
    
    // Handle input
    live_stats_handle_input(NULL, live_stats_config.debug_mode);
    
    // Refresh display
    refresh();
}

void live_stats_cleanup(void) {
    if (live_stats_state.initialized) {
        // Restore terminal
        curs_set(1);  // Show cursor
        endwin();
        live_stats_state.initialized = false;
        live_stats_state.window = NULL;
    }
}

void live_stats_handle_input(volatile int* running, bool debug_mode) {
    // Handle input from the user
    int ch = getch();
    if (ch != ERR) {
        if (ch == 'q' || ch == 'Q') {
            if (running) {
                *running = 0;
            }
            if (debug_mode) {
                syslog(LOG_DEBUG, "Received quit command from user input");
            }
        } else if (ch == 'r' || ch == 'R') {
            // Force refresh by clearing display cache
            live_stats_state.last_display_cpu_temp = -1;
            live_stats_state.last_display_fan_duty = -1;
            live_stats_state.last_display_fan_rpm = -1;
            live_stats_state.last_display_pid_error = -999.0;
            live_stats_state.last_display_pid_p = -999.0;
            live_stats_state.last_display_pid_i = -999.0;
            live_stats_state.last_display_pid_d = -999.0;
            if (debug_mode) {
                syslog(LOG_DEBUG, "Received refresh command from user input");
            }
        } else if (debug_mode) {
            syslog(LOG_DEBUG, "Received input: %c (0x%02x)", ch, ch);
        }
    }
}

// Configuration functions
void live_stats_set_enabled(bool enabled) {
    live_stats_config.enabled = enabled;
}

void live_stats_set_interval(double interval) {
    live_stats_config.update_interval = interval;
}

void live_stats_set_debug_mode(bool debug_mode) {
    live_stats_config.debug_mode = debug_mode;
}

void live_stats_set_target_temperature(int temp) {
    live_stats_config.target_temperature = temp;
}

void live_stats_set_pid_enabled(bool enabled) {
    live_stats_config.pid_enabled = enabled;
}

void live_stats_set_version(const char* version) {
    live_stats_config.version = version;
}

// Utility functions
bool live_stats_is_initialized(void) {
    return live_stats_state.initialized;
}

bool live_stats_is_enabled(void) {
    return live_stats_config.enabled;
} 