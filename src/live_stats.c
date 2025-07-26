#include "live_stats.h"
#include "logging.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

live_stats_t* live_stats_init(double update_interval) {
    live_stats_t* stats = malloc(sizeof(live_stats_t));
    if (!stats) {
        return NULL;
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
    
    // Initialize structure
    stats->window = stdscr;
    stats->initialized = 1;
    stats->update_interval = update_interval;
    
    // Initialize display cache
    stats->last_display_cpu_temp = -1;
    stats->last_display_fan_duty = -1;
    stats->last_display_fan_rpm = -1;
    stats->last_display_pid_error = -999.0;
    stats->last_display_pid_p = -999.0;
    stats->last_display_pid_i = -999.0;
    stats->last_display_pid_d = -999.0;
    
    // Initialize debug log buffer
    stats->debug_log_index = 0;
    stats->debug_log_count = 0;
    
    // Get initial window dimensions
    getmaxyx(stdscr, stats->max_y, stats->max_x);
    
    // Clear screen and draw initial layout
    clear();
    refresh();
    
    // Handle window resize
    live_stats_handle_resize(stats);
    
    return stats;
}

void live_stats_display(live_stats_t* stats, int cpu_temp, int fan_duty, int fan_rpm,
                       int target_temp, bool pid_enabled, double pid_error, double pid_p,
                       double pid_i, double pid_d, bool auto_duty, bool stuck_detected,
                       int recovery_attempts, int max_recovery_attempts, bool debug_mode) {
    if (!stats || !stats->initialized) return;
    
    // Update window dimensions
    getmaxyx(stdscr, stats->max_y, stats->max_x);
    
    // Check window size - need more space if debug mode is enabled
    int min_height = debug_mode ? 20 : 12;
    if (stats->max_y < min_height || stats->max_x < 60) {
        live_stats_handle_resize(stats);
        return;
    }
    
    // Draw header
    attron(COLOR_PAIR(5) | A_BOLD);
    mvprintw(0, 0, "+--- Clevo Fan Control Live Stats ");
    for (int i = 32; i < stats->max_x - 2; i++) mvprintw(0, i, "-");
    mvprintw(0, stats->max_x - 2, "+");
    attroff(COLOR_PAIR(5) | A_BOLD);
    
    // Draw header info
    attron(COLOR_PAIR(4));
    mvprintw(1, 2, "Target: %3d°C            ", target_temp);
    mvprintw(1, 30, "Update: %5.0fms            ", stats->update_interval * 1000);
    mvprintw(1, 60, "PID: %-8s            ", pid_enabled ? "Enabled" : "Disabled");
    if (debug_mode) {
        mvprintw(1, 85, "DEBUG: ON            ");
    }
    attroff(COLOR_PAIR(4));
    
    // Draw separator
    mvprintw(2, 0, "+");
    for (int i = 1; i < stats->max_x - 1; i++) mvprintw(2, i, "-");
    mvprintw(2, stats->max_x - 1, "+");
    
    // Temperature section - only update if changed
    if (cpu_temp != stats->last_display_cpu_temp) {
        mvprintw(3, 2, "Temperature:                                ");
        // CPU temperature with color coding
        if (cpu_temp > target_temp + 10) {
            attron(COLOR_PAIR(3));
        } else if (cpu_temp > target_temp) {
            attron(COLOR_PAIR(2));
        } else {
            attron(COLOR_PAIR(1));
        }
        mvprintw(4, 4, "CPU: %3d°C            ", cpu_temp);
        attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3));
        stats->last_display_cpu_temp = cpu_temp;
    }
    
    // Draw separator
    mvprintw(5, 0, "+");
    for (int i = 1; i < stats->max_x - 1; i++) mvprintw(5, i, "-");
    mvprintw(5, stats->max_x - 1, "+");
    
    // Fan control section - only update if changed
    if (fan_duty != stats->last_display_fan_duty || fan_rpm != stats->last_display_fan_rpm) {
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
        stats->last_display_fan_duty = fan_duty;
        stats->last_display_fan_rpm = fan_rpm;
    }
    
    // Draw separator
    mvprintw(8, 0, "+");
    for (int i = 1; i < stats->max_x - 1; i++) mvprintw(8, i, "-");
    mvprintw(8, stats->max_x - 1, "+");
    
    // PID status section - only update if changed
    if (pid_enabled && (fabs(pid_error - stats->last_display_pid_error) > 0.1 || 
                       fabs(pid_p - stats->last_display_pid_p) > 0.1 ||
                       fabs(pid_i - stats->last_display_pid_i) > 0.1 ||
                       fabs(pid_d - stats->last_display_pid_d) > 0.1)) {
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
        stats->last_display_pid_error = pid_error;
        stats->last_display_pid_p = pid_p;
        stats->last_display_pid_i = pid_i;
        stats->last_display_pid_d = pid_d;
    } else if (!pid_enabled) {
        mvprintw(9, 2, "PID Status: Disabled                                ");
    }
    
    // Draw separator
    mvprintw(11, 0, "+");
    for (int i = 1; i < stats->max_x - 1; i++) mvprintw(11, i, "-");
    mvprintw(11, stats->max_x - 1, "+");
    
    // Status section
    mvprintw(12, 2, "Status: %-10s            ", auto_duty ? "Auto Mode" : "Manual Mode");
    
    // Stuck detection status
    const char* stuck_status = stuck_detected ? "Yes" : "No";
    if (stuck_detected) {
        attron(COLOR_PAIR(2));
    }
    mvprintw(12, 30, "Stuck: %-3s            ", stuck_status);
    if (stuck_detected) {
        attroff(COLOR_PAIR(2));
    }
    
    // Recovery attempts
    mvprintw(12, 56, "Recovery: %d/%d            ", recovery_attempts, max_recovery_attempts);
    
    // Draw footer
    mvprintw(13, 0, "+");
    for (int i = 1; i < stats->max_x - 1; i++) mvprintw(13, i, "-");
    mvprintw(13, stats->max_x - 1, "+");
    
    // Debug log area (only show if debug mode is enabled)
    if (debug_mode) {
        // Draw debug separator
        mvprintw(14, 0, "+");
        for (int i = 1; i < stats->max_x - 1; i++) mvprintw(14, i, "-");
        mvprintw(14, stats->max_x - 1, "+");
        
        // Debug log header
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(15, 2, "Debug Log:");
        attroff(COLOR_PAIR(4) | A_BOLD);
        
        // Show last few debug messages
        int log_start = stats->debug_log_count > 0 ? (stats->debug_log_index - 1 + 10) % 10 : 0;
        for (int i = 0; i < stats->debug_log_count && i < 4; i++) {
            int log_idx = (log_start - i + 10) % 10;
            if (log_idx >= 0 && log_idx < stats->debug_log_count) {
                // Truncate long messages to fit screen
                char truncated[256];
                strncpy(truncated, stats->debug_log_buffer[log_idx], stats->max_x - 4);
                truncated[stats->max_x - 4] = '\0';
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
    if (live_stats_handle_input(stats)) {
        // User requested quit
        return;
    }
    
    // Refresh display
    refresh();
}

void live_stats_handle_resize(live_stats_t* stats) {
    if (!stats || !stats->initialized) return;
    
    getmaxyx(stdscr, stats->max_y, stats->max_x);
    
    // Minimum window size check
    if (stats->max_y < 12 || stats->max_x < 60) {
        clear();
        mvprintw(stats->max_y/2, (stats->max_x-40)/2, "Window too small! Need 60x12 minimum");
        refresh();
        return;
    }
    
    // Clear screen for redraw
    clear();
}

bool live_stats_handle_input(live_stats_t* stats) {
    if (!stats || !stats->initialized) return false;
    
    // Handle input from the user
    int ch = getch();
    if (ch != ERR) {
        if (ch == 'q' || ch == 'Q') {
            return true; // Request quit
        } else if (ch == 'r' || ch == 'R') {
            // Force refresh by clearing display cache
            stats->last_display_cpu_temp = -1;
            stats->last_display_fan_duty = -1;
            stats->last_display_fan_rpm = -1;
            stats->last_display_pid_error = -999.0;
            stats->last_display_pid_p = -999.0;
            stats->last_display_pid_i = -999.0;
            stats->last_display_pid_d = -999.0;
        }
    }
    
    return false;
}

void live_stats_add_debug_log(live_stats_t* stats, const char* message) {
    if (!stats || !stats->initialized) return;
    
    // Add timestamp
    char timestamp[20];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", localtime(&now));
    
    // Store in circular buffer with safe string concatenation
    snprintf(stats->debug_log_buffer[stats->debug_log_index], sizeof(stats->debug_log_buffer[0]),
            "[%.8s] %.200s", timestamp, message);
    stats->debug_log_index = (stats->debug_log_index + 1) % 10;
    if (stats->debug_log_count < 10) stats->debug_log_count++;
}

bool live_stats_is_initialized(live_stats_t* stats) {
    return stats && stats->initialized;
}

void live_stats_cleanup(live_stats_t* stats) {
    if (stats && stats->initialized) {
        // Restore terminal
        curs_set(1);  // Show cursor
        endwin();
        stats->initialized = 0;
        stats->window = NULL;
    }
    
    if (stats) {
        free(stats);
    }
} 