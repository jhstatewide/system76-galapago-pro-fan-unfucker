#include "temperature_monitor.h"
#include "ec_interface.h"
#include "logging.h"
#include "fan_constants.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

// Internal helper functions
static int read_temp_from_sysfs(const char* path);
static int read_temp_from_coretemp(void);
static int read_temp_from_hwmon(void);
static int read_temp_from_thermal_zone(void);
static int read_temp_from_acpi(void);
// validate_ec_temp_with_alternative function declaration removed (unused)

// External reference for debug mode
extern int debug_mode;

// Constants for stuck detection
#define STUCK_THRESHOLD_CYCLES 10
#define STUCK_TEMP_THRESHOLD 2.0

temperature_monitor_t* temperature_monitor_init(int max_temp_change, bool validation_enabled) {
    temperature_monitor_t* monitor = malloc(sizeof(temperature_monitor_t));
    if (!monitor) {
        return NULL;
    }
    
    monitor->last_cpu_temp = 0;
    monitor->temp_history_index = 0;
    monitor->temp_history_size = 0;
    monitor->stuck_detection_counter = 0;
    monitor->max_temp_change_per_cycle = max_temp_change;
    monitor->temp_validation_enabled = validation_enabled;
    
    // Initialize temperature history
    for (int i = 0; i < 10; i++) {
        monitor->temp_history[i] = 0;
    }
    
    return monitor;
}

int temperature_monitor_get_cpu_temp(temperature_monitor_t* monitor) {
    if (!monitor) return -1;
    
    // Try standard Linux temperature reading first
    int temp = temperature_monitor_get_alternative_cpu_temp();
    if (temp > 0) {
        if (debug_mode) {
            logging_debug("Using standard Linux temperature sensor: %d°C", temp);
        }
        return temp;
    }
    
    // Fall back to EC temperature
    int ec_temp = ec_query_cpu_temp();
    if (ec_temp > 0) {
        int sanitized_temp = temperature_monitor_sanitize_reading(monitor, ec_temp, monitor->last_cpu_temp, "CPU");
        if (sanitized_temp == ec_temp) {
            monitor->last_cpu_temp = ec_temp;
        }
        if (debug_mode) {
            logging_debug("Using EC temperature sensor: %d°C", sanitized_temp);
        }
        return sanitized_temp;
    }
    
    logging_warning("No temperature sensors found");
    return -1;
}

bool temperature_monitor_validate_reading(temperature_monitor_t* monitor, 
                                       int current_temp, int last_temp, 
                                       const char* sensor_name) {
    if (!monitor || !monitor->temp_validation_enabled) {
        return true;
    }
    
    if (last_temp == 0) {
        // First reading, always valid
        return true;
    }
    
    int temp_change = abs(current_temp - last_temp);
    
    // Check for stuck readings - if temp hasn't changed at all for multiple readings
    static int identical_reading_count = 0;
    if (current_temp == last_temp) {
        identical_reading_count++;
        if (identical_reading_count >= 10) { // 10 consecutive identical readings
            logging_warning("Temperature appears stuck: %d°C unchanged for %d readings", 
                          current_temp, identical_reading_count);
            return false;
        }
    } else {
        identical_reading_count = 0;
    }
    
    if (temp_change > monitor->max_temp_change_per_cycle) {
        logging_warning("Suspicious %s temperature change: %d°C -> %d°C (change: %d°C)", 
                       sensor_name, last_temp, current_temp, temp_change);
        return false;
    }
    
    // Check for impossible temperatures
    if (current_temp < 0 || current_temp > 120) {
        logging_warning("Impossible %s temperature: %d°C", sensor_name, current_temp);
        return false;
    }
    
    return true;
}

int temperature_monitor_sanitize_reading(temperature_monitor_t* monitor,
                                       int current_temp, int last_temp,
                                       const char* sensor_name) {
    if (!temperature_monitor_validate_reading(monitor, current_temp, last_temp, sensor_name)) {
        // Return last known good temperature if validation fails
        logging_warning("Temperature validation failed for %s, using last known good value: %d°C", 
                       sensor_name, last_temp);
        return last_temp;
    }
    
    return current_temp;
}

void temperature_monitor_add_to_history(temperature_monitor_t* monitor, int temp) {
    if (!monitor) return;
    
    monitor->temp_history[monitor->temp_history_index] = temp;
    monitor->temp_history_index = (monitor->temp_history_index + 1) % 10;
    if (monitor->temp_history_size < 10) {
        monitor->temp_history_size++;
    }
}

bool temperature_monitor_is_stuck(temperature_monitor_t* monitor) {
    if (!monitor || monitor->temp_history_size < 5) {
        return false;
    }
    
    int recent_temps[5];
    int idx = monitor->temp_history_index;
    for (int i = 0; i < 5; i++) {
        idx = (idx - 1 + 10) % 10;
        recent_temps[i] = monitor->temp_history[idx];
    }
    
    // Check if temperature has been stable for 5 readings
    int avg_temp = 0;
    for (int i = 0; i < 5; i++) {
        avg_temp += recent_temps[i];
    }
    avg_temp /= 5;
    
    // If average is within 2°C of target and not changing, consider it stuck
    int temp_error = avg_temp - 65; // Default target temperature
    if (temp_error > 0 && temp_error < 5) {
        // Check if all recent readings are within 1°C of each other
        bool stable = true;
        for (int i = 1; i < 5; i++) {
            if (abs(recent_temps[i] - recent_temps[0]) > 1) {
                stable = false;
                break;
            }
        }
        return stable;
    }
    
    return false;
}

int temperature_monitor_get_alternative_cpu_temp(void) {
    // Try coretemp first (most reliable)
    int temp = read_temp_from_coretemp();
    if (temp > 0) return temp;
    
    // Fall back to other sources
    temp = read_temp_from_hwmon();
    if (temp > 0) return temp;
    
    temp = read_temp_from_thermal_zone();
    if (temp > 0) return temp;
    
    temp = read_temp_from_acpi();
    if (temp > 0) return temp;
    
    return -1;
}

// Enhanced function to get CPU temperature using standard Linux methods
int temperature_monitor_get_cpu_temperature(void) {
    // Try coretemp first (most reliable for CPU temperature)
    int temp = read_temp_from_coretemp();
    if (temp > 0) {
        if (debug_mode) {
            logging_debug("Using coretemp sensor: %d°C", temp);
        }
        return temp;
    }
    
    // Fall back to other hwmon sources
    temp = read_temp_from_hwmon();
    if (temp > 0) {
        if (debug_mode) {
            logging_debug("Using hwmon sensor: %d°C", temp);
        }
        return temp;
    }
    
    // Fall back to thermal zones
    temp = read_temp_from_thermal_zone();
    if (temp > 0) {
        if (debug_mode) {
            logging_debug("Using thermal zone sensor: %d°C", temp);
        }
        return temp;
    }
    
    // Last resort: ACPI
    temp = read_temp_from_acpi();
    if (temp > 0) {
        if (debug_mode) {
            logging_debug("Using ACPI sensor: %d°C", temp);
        }
        return temp;
    }
    
    // If all else fails, return -1
    logging_warning("No standard temperature sensors found");
    return -1;
}

void temperature_monitor_cleanup(temperature_monitor_t* monitor) {
    if (monitor) {
        free(monitor);
    }
}

static int read_temp_from_sysfs(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    
    int temp;
    if (fscanf(f, "%d", &temp) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    
    // Convert from millidegrees to degrees if needed
    if (temp > 1000) {
        temp /= 1000;
    }
    
    return temp;
}

static int read_temp_from_coretemp(void) {
    // Try coretemp paths
    const char* coretemp_paths[] = {
        "/sys/class/hwmon/hwmon0/temp1_input",
        "/sys/class/hwmon/hwmon1/temp1_input",
        "/sys/class/hwmon/hwmon2/temp1_input",
        NULL
    };
    
    for (int i = 0; coretemp_paths[i] != NULL; i++) {
        int temp = read_temp_from_sysfs(coretemp_paths[i]);
        if (temp > 0 && temp < 120) {
            return temp;
        }
    }
    return -1;
}

static int read_temp_from_hwmon(void) {
    // Try common hwmon paths
    const char* hwmon_paths[] = {
        "/sys/class/hwmon/hwmon0/temp1_input",
        "/sys/class/hwmon/hwmon1/temp1_input", 
        "/sys/class/hwmon/hwmon2/temp1_input",
        "/sys/class/hwmon/hwmon0/temp2_input",
        "/sys/class/hwmon/hwmon1/temp2_input",
        NULL
    };
    
    for (int i = 0; hwmon_paths[i] != NULL; i++) {
        int temp = read_temp_from_sysfs(hwmon_paths[i]);
        if (temp > 0 && temp < 120) {
            return temp;
        }
    }
    return -1;
}

static int read_temp_from_thermal_zone(void) {
    // Try thermal zone paths
    const char* thermal_paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        "/sys/class/thermal/thermal_zone2/temp",
        NULL
    };
    
    for (int i = 0; thermal_paths[i] != NULL; i++) {
        int temp = read_temp_from_sysfs(thermal_paths[i]);
        if (temp > 0 && temp < 120) {
            return temp;
        }
    }
    return -1;
}

static int read_temp_from_acpi(void) {
    // Try ACPI thermal paths
    const char* acpi_paths[] = {
        "/proc/acpi/thermal_zone/THM0/temperature",
        "/proc/acpi/thermal_zone/THM1/temperature",
        NULL
    };
    
    for (int i = 0; acpi_paths[i] != NULL; i++) {
        FILE* f = fopen(acpi_paths[i], "r");
        if (!f) continue;
        
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            // Parse ACPI format: "temperature:             45 C"
            char* temp_str = strstr(line, "temperature:");
            if (temp_str) {
                temp_str += 12; // Skip "temperature:"
                while (*temp_str == ' ' || *temp_str == '\t') temp_str++;
                int temp = atoi(temp_str);
                fclose(f);
                return temp;
            }
        }
        fclose(f);
    }
    return -1;
}

// validate_ec_temp_with_alternative function removed (unused) 