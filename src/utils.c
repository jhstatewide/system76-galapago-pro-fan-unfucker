#include "utils.h"
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <math.h>
#include <stdbool.h>

// Global state for tracking duty changes
static time_t duty_change_times[100] = {0};
static int duty_change_index = 0;
static int duty_change_count = 0;

int check_proc_instances(const char* proc_name) {
    DIR* dir = opendir("/proc");
    if (dir == NULL) return 0;
    
    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR && isdigit(ent->d_name[0])) {
            char path[512];
            char comm[256];
            if (snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name) >= (int)sizeof(path)) {
                continue; // Skip if path would be truncated
            }
            FILE* f = fopen(path, "r");
            if (f != NULL) {
                if (fgets(comm, sizeof(comm), f) != NULL) {
                    comm[strcspn(comm, "\n")] = 0;
                    if (strcmp(comm, proc_name) == 0) {
                        count++;
                    }
                }
                fclose(f);
            }
        }
    }
    closedir(dir);
    return count;
}

void get_time_string(char* buffer, size_t max, const char* format) {
    time_t timer;
    struct tm tm_info;
    time(&timer);
    localtime_r(&timer, &tm_info);
    strftime(buffer, max, format, &tm_info);
}

void signal_term(void (*handler)(int)) {
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
}

int utils_init(void) {
    // No initialization needed for basic utilities
    return 0;
}

void utils_cleanup(void) {
    // No cleanup needed for basic utilities
}

// Enhanced utility functions for diagnostic logging

long get_system_uptime(void) {
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        return -1;
    }
    return info.uptime;
}

float get_system_load(void) {
    double loadavg[3];
    if (getloadavg(loadavg, 3) == -1) {
        return -1.0;
    }
    return (float)loadavg[0]; // Return 1-minute load average
}

int get_duty_changes_last_minutes(int minutes) {
    time_t current_time = time(NULL);
    time_t cutoff_time = current_time - (minutes * 60);
    int count = 0;
    
    for (int i = 0; i < duty_change_count; i++) {
        if (duty_change_times[i] >= cutoff_time) {
            count++;
        }
    }
    return count;
}

float calculate_temp_gradient(int* temp_history, int num_readings, float time_span_minutes) {
    if (num_readings < 2) {
        return 0.0;
    }
    
    int temp_diff = temp_history[num_readings - 1] - temp_history[0];
    float gradient = (float)temp_diff / time_span_minutes;
    return gradient;
}

float get_ambient_temperature(void) {
    // Try to read from thermal zone 0 (usually ambient)
    FILE* f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f != NULL) {
        int temp;
        if (fscanf(f, "%d", &temp) == 1) {
            fclose(f);
            return (float)temp / 1000.0; // Convert from millidegrees
        }
        fclose(f);
    }
    
    // Try alternative ambient sensor paths
    const char* ambient_paths[] = {
        "/sys/class/hwmon/hwmon0/temp1_input",
        "/sys/class/hwmon/hwmon1/temp1_input",
        "/sys/class/hwmon/hwmon2/temp1_input"
    };
    
    for (int i = 0; i < 3; i++) {
        f = fopen(ambient_paths[i], "r");
        if (f != NULL) {
            int temp;
            if (fscanf(f, "%d", &temp) == 1) {
                fclose(f);
                return (float)temp / 1000.0;
            }
            fclose(f);
        }
    }
    
    return -999.0; // Not available
}

float get_humidity(void) {
    // Try to read humidity from hwmon (if available)
    const char* humidity_paths[] = {
        "/sys/class/hwmon/hwmon0/humidity1_input",
        "/sys/class/hwmon/hwmon1/humidity1_input",
        "/sys/class/hwmon/hwmon2/humidity1_input"
    };
    
    for (int i = 0; i < 3; i++) {
        FILE* f = fopen(humidity_paths[i], "r");
        if (f != NULL) {
            int humidity;
            if (fscanf(f, "%d", &humidity) == 1) {
                fclose(f);
                return (float)humidity / 1000.0; // Convert from millipercent
            }
            fclose(f);
        }
    }
    
    return -999.0; // Not available
}

bool detect_rpm_flutter(int* rpm_history, int num_readings) {
    if (num_readings < 5) {
        return false;
    }
    
    // Calculate variance of recent RPM readings
    int sum = 0;
    for (int i = 0; i < num_readings; i++) {
        sum += rpm_history[i];
    }
    float mean = (float)sum / num_readings;
    
    float variance = 0.0;
    for (int i = 0; i < num_readings; i++) {
        float diff = rpm_history[i] - mean;
        variance += diff * diff;
    }
    variance /= num_readings;
    
    // Consider flutter if variance is high relative to mean
    float coefficient_of_variation = sqrt(variance) / mean;
    return coefficient_of_variation > 0.1; // 10% threshold
}

int count_duty_oscillations(int* duty_history, int num_readings) {
    if (num_readings < 3) {
        return 0;
    }
    
    int oscillations = 0;
    for (int i = 1; i < num_readings - 1; i++) {
        // Check for peak or valley
        if ((duty_history[i] > duty_history[i-1] && duty_history[i] > duty_history[i+1]) ||
            (duty_history[i] < duty_history[i-1] && duty_history[i] < duty_history[i+1])) {
            oscillations++;
        }
    }
    return oscillations;
}

bool detect_temp_spike(int* temp_history, int num_readings, int threshold) {
    if (num_readings < 2) {
        return false;
    }
    
    // Check if temperature increased by threshold in last reading
    int temp_change = temp_history[num_readings - 1] - temp_history[num_readings - 2];
    return temp_change >= threshold;
}

// Function to record duty change for tracking
void record_duty_change(void) {
    time_t current_time = time(NULL);
    duty_change_times[duty_change_index] = current_time;
    duty_change_index = (duty_change_index + 1) % 100;
    if (duty_change_count < 100) {
        duty_change_count++;
    }
} 