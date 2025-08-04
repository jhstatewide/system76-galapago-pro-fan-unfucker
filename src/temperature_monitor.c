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

// Internal helper functions
static int read_temp_from_sysfs(const char* path);
static int read_temp_from_coretemp(void);
static int read_temp_from_hwmon(void);
static int read_temp_from_thermal_zone(void);
static int read_temp_from_acpi(void);
static bool validate_ec_temp_with_alternative(int ec_temp);

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
        logging_debug("Using standard Linux temperature sensor: %d°C", temp);
        return temp;
    }
    
    // Fall back to EC temperature
    int ec_temp = ec_query_cpu_temp();
    if (ec_temp > 0) {
        int sanitized_temp = temperature_monitor_sanitize_reading(monitor, ec_temp, monitor->last_cpu_temp, "CPU");
        if (sanitized_temp == ec_temp) {
            monitor->last_cpu_temp = ec_temp;
        }
        logging_debug("Using EC temperature sensor: %d°C", sanitized_temp);
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
    
    // Additional sanity checks
    if (current_temp < 0 || current_temp > 120) {
        logging_warning("Invalid %s temperature reading: %d°C (outside valid range 0-120°C)", 
                       sensor_name, current_temp);
        return false;
    }
    
    return true;
}

int temperature_monitor_sanitize_reading(temperature_monitor_t* monitor,
                                       int current_temp, int last_temp,
                                       const char* sensor_name) {
    if (!monitor || !monitor->temp_validation_enabled) {
        return current_temp;
    }
    
    static int invalid_reading_count = 0;
    static int last_valid_temp = 0;
    
    // First validate with alternative temperature sources
    if (!validate_ec_temp_with_alternative(current_temp)) {
        invalid_reading_count++;
        logging_warning("EC temperature failed validation with alternative sources");
    }
    
    if (temperature_monitor_validate_reading(monitor, current_temp, last_temp, sensor_name)) {
        invalid_reading_count = 0;
        last_valid_temp = current_temp;
        return current_temp;
    }
    
    invalid_reading_count++;
    
    // If we've had too many invalid readings, try to reset the EC
    if (invalid_reading_count >= 5) {
        logging_warning("Multiple invalid readings detected (%d in a row) - attempting EC reset", 
                      invalid_reading_count);
        
        // Try to "reset" the EC by cycling fan speeds
        ec_write_fan_duty(100); // Full speed
        usleep(500000);         // Wait 500ms
        
        // Use minimum duty that ensures minimum RPM
        int min_duty_for_min_rpm = (FAN_MIN_RPM + FAN_RPM_DUTY_RATIO - 1) / FAN_RPM_DUTY_RATIO; // Ceiling division
        ec_write_fan_duty(min_duty_for_min_rpm);  // Low speed but above minimum RPM
        usleep(500000);         // Wait 500ms
        
        // Re-read temperature after reset attempt
        int new_temp = ec_query_cpu_temp();
        if (new_temp != current_temp) {
            logging_info("EC reset successful - new temperature reading: %d°C", new_temp);
            invalid_reading_count = 0;
            return new_temp;
        }
        
        // If still stuck, log error and return last known good value
        logging_error("EC reset failed - temperature still stuck at %d°C", current_temp);
    }
    
    // Use the last valid temperature, but with a slight bias towards cooling
    // This ensures we don't get stuck in a dangerous high-temperature state
    if (last_valid_temp > 65) { // Assuming target temperature is around 65°C
        return last_valid_temp + 2; // Bias towards more cooling when temp was high
    }
    
    return last_valid_temp;
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
        return false; // Need at least 5 readings to detect stuck
    }
    
    // Calculate the average temperature over the last few readings
    int sum = 0;
    for (int i = 0; i < monitor->temp_history_size; i++) {
        sum += monitor->temp_history[i];
    }
    double avg_temp = (double)sum / monitor->temp_history_size;
    
    // Check if all recent temperatures are within the stuck threshold of the average
    int stuck_count = 0;
    int identical_count = 0;
    int last_temp = monitor->temp_history[0];
    
    for (int i = 0; i < monitor->temp_history_size; i++) {
        if (fabs(monitor->temp_history[i] - avg_temp) <= STUCK_TEMP_THRESHOLD) {
            stuck_count++;
        }
        if (monitor->temp_history[i] == last_temp) {
            identical_count++;
        }
        last_temp = monitor->temp_history[i];
    }
    
    // Consider temperature stuck if:
    // 1. Most readings are within threshold of average (standard check)
    // 2. OR we have several identical readings in a row (new check)
    // 3. OR temperature is high and not changing despite high fan speed
    bool stuck = (stuck_count >= monitor->temp_history_size * 0.6) || // Reduced from 0.8 to 0.6
                (identical_count >= monitor->temp_history_size * 0.8) ||
                (avg_temp > 70 && stuck_count >= monitor->temp_history_size * 0.5);
    
    if (stuck) {
        monitor->stuck_detection_counter++;
        if (monitor->stuck_detection_counter % 5 == 0) { // Increased frequency of debug logs
            logging_debug("Temperature stuck detection: avg=%.1f°C, stuck_count=%d/%d, identical=%d/%d, counter=%d", 
                      avg_temp, stuck_count, monitor->temp_history_size, identical_count, monitor->temp_history_size, monitor->stuck_detection_counter);
        }
    } else {
        monitor->stuck_detection_counter = 0;
    }
    
    // Consider stuck if we've detected it for multiple cycles
    // Reduced threshold for faster detection
    return (monitor->stuck_detection_counter >= (STUCK_THRESHOLD_CYCLES / 2));
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

void temperature_monitor_cleanup(temperature_monitor_t* monitor) {
    if (monitor) {
        free(monitor);
    }
}

// Internal helper functions
static int read_temp_from_sysfs(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    
    int temp = -1;
    if (fscanf(f, "%d", &temp) == 1) {
        // Convert from millidegrees to degrees
        temp /= 1000;
    }
    fclose(f);
    return temp;
}

static int read_temp_from_coretemp(void) {
    // Read from coretemp sensors (most reliable for CPU temperature)
    const char* coretemp_paths[] = {
        "/sys/class/hwmon/hwmon3/temp1_input",  // Package
        "/sys/class/hwmon/hwmon3/temp2_input",  // Core 0
        "/sys/class/hwmon/hwmon3/temp3_input",  // Core 1
        "/sys/class/hwmon/hwmon3/temp4_input",  // Core 2
        "/sys/class/hwmon/hwmon3/temp5_input",  // Core 3
        NULL
    };
    
    int max_temp = -1;
    for (int i = 0; coretemp_paths[i] != NULL; i++) {
        int temp = read_temp_from_sysfs(coretemp_paths[i]);
        if (temp > 0 && temp < 120) {
            if (temp > max_temp) {
                max_temp = temp;
            }
        }
    }
    
    return max_temp;
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

static bool validate_ec_temp_with_alternative(int ec_temp) {
    static int validation_failures = 0;
    
    int alt_temp = temperature_monitor_get_alternative_cpu_temp();
    if (alt_temp == -1) {
        // No alternative source available, assume EC temp is valid
        return true;
    }
    
    int temp_diff = abs(ec_temp - alt_temp);
    
    // Allow some difference due to different sensor locations
    if (temp_diff <= 15) {
        validation_failures = 0;
        if (temp_diff > 5) {
            logging_debug("Temperature validation: EC=%d°C, Alt=%d°C, diff=%d°C", 
                      ec_temp, alt_temp, temp_diff);
        }
        return true;
    }
    
    validation_failures++;
    logging_warning("Temperature validation failed: EC=%d°C, Alt=%d°C, diff=%d°C (failures=%d)", 
                   ec_temp, alt_temp, temp_diff, validation_failures);
    
    // If we've had multiple validation failures, the EC sensor might be stuck
    if (validation_failures >= 3) {
        logging_error("Multiple temperature validation failures - EC sensor may be stuck");
        return false;
    }
    
    return true;
} 