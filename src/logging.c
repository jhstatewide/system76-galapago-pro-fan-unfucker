#include "logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Global logging state
static int log_level = LOG_INFO;
static int quiet_mode = 0;
static int debug_mode = 0;

// Timing tracking for diagnostic logging
static struct timeval last_duty_change_time = {0, 0};
static int last_duty_change = 0;
static int rpm_history[10] = {0}; // Track last 10 RPM readings
static int rpm_history_index = 0;

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

// Enhanced diagnostic logging functions

void logging_hardware_state(int duty, int rpm, int expected_rpm, int cpu_temp, float system_load) {
    logging_info("HARDWARE_STATE: duty=%d%%, rpm=%d, expected_rpm=%d, cpu_temp=%d°C, system_load=%.2f", 
                duty, rpm, expected_rpm, cpu_temp, system_load);
    
    // Calculate RPM deviation
    float rpm_deviation = 0.0;
    if (expected_rpm > 0) {
        rpm_deviation = ((float)(rpm - expected_rpm) / expected_rpm) * 100.0;
    }
    
    logging_debug("HARDWARE_ANALYSIS: rpm_deviation=%.1f%%, duty_efficiency=%.1f%%", 
                 rpm_deviation, (float)rpm / expected_rpm * 100.0);
}

void logging_timing_analysis(int duty_change, long time_since_last_change, 
                           long rpm_response_time, int rpm_before, int rpm_after) {
    logging_info("TIMING_ANALYSIS: duty_change=%d%%, time_since_last_change=%ldμs, rpm_response_time=%ldμs, rpm_before=%d, rpm_after=%d", 
                duty_change, time_since_last_change, rpm_response_time, rpm_before, rpm_after);
    
    // Track RPM history for flutter detection
    rpm_history[rpm_history_index] = rpm_after;
    rpm_history_index = (rpm_history_index + 1) % 10;
    
    // Calculate RPM stability
    int rpm_variance = 0;
    int rpm_sum = 0;
    for (int i = 0; i < 10; i++) {
        rpm_sum += rpm_history[i];
    }
    int rpm_avg = rpm_sum / 10;
    
    for (int i = 0; i < 10; i++) {
        rpm_variance += (rpm_history[i] - rpm_avg) * (rpm_history[i] - rpm_avg);
    }
    rpm_variance /= 10;
    
    logging_debug("TIMING_STABILITY: rpm_variance=%d, rpm_avg=%d, response_rate=%.2f rpm/μs", 
                 rpm_variance, rpm_avg, (float)(rpm_after - rpm_before) / rpm_response_time);
}

void logging_recovery_step(int step_number, int total_steps, int duty_step,
                         int rpm_before, int rpm_after, int wait_time_ms, bool success) {
    logging_info("RECOVERY_STEP: step=%d/%d, duty=%d%%, rpm_before=%d, rpm_after=%d, wait_time=%dms, success=%s", 
                step_number, total_steps, duty_step, rpm_before, rpm_after, wait_time_ms, success ? "true" : "false");
    
    if (success) {
        logging_info("RECOVERY_SUCCESS: step %d resolved stuck fan, rpm improvement: %d -> %d (+%d)", 
                    step_number, rpm_before, rpm_after, rpm_after - rpm_before);
    } else {
        logging_warning("RECOVERY_FAILURE: step %d did not resolve stuck fan, rpm: %d -> %d", 
                       step_number, rpm_before, rpm_after);
    }
}

void logging_stall_prediction(bool rpm_flutter, int duty_oscillations, 
                            bool temp_spike_detected, int near_stall_conditions) {
    logging_warning("STALL_PREDICTION: rpm_flutter=%s, duty_oscillations=%d, temp_spike=%s, near_stalls=%d", 
                   rpm_flutter ? "true" : "false", duty_oscillations, 
                   temp_spike_detected ? "true" : "false", near_stall_conditions);
    
    if (rpm_flutter || duty_oscillations > 3 || temp_spike_detected || near_stall_conditions > 0) {
        logging_warning("STALL_RISK: Multiple indicators suggest potential fan stall risk");
    }
}

void logging_environmental_context(long system_uptime, int duty_changes_last_5min,
                                 float temp_gradient, float ambient_temp, float humidity) {
    logging_info("ENVIRONMENTAL: uptime=%lds, duty_changes_5min=%d, temp_gradient=%.2f°C/min, ambient=%.1f°C, humidity=%.1f%%", 
                system_uptime, duty_changes_last_5min, temp_gradient, ambient_temp, humidity);
    
    // Analyze environmental risk factors
    bool high_risk = false;
    if (duty_changes_last_5min > 10) {
        logging_warning("ENVIRONMENTAL_RISK: High duty change frequency (%d in 5min) may stress fan controller", 
                       duty_changes_last_5min);
        high_risk = true;
    }
    
    if (fabs(temp_gradient) > 5.0) {
        logging_warning("ENVIRONMENTAL_RISK: Rapid temperature change (%.2f°C/min) may trigger protection modes", 
                       temp_gradient);
        high_risk = true;
    }
    
    if (high_risk) {
        logging_warning("ENVIRONMENTAL_ALERT: Multiple environmental risk factors detected");
    }
}

void logging_ec_register_dump(uint8_t* registers, int num_registers) {
    logging_info("EC_REGISTER_DUMP: Dumping %d registers", num_registers);
    
    // Log key registers with labels
    logging_info("EC_KEY_REGS: CPU_TEMP(0x07)=0x%02X, FAN_DUTY(0xCE)=0x%02X, FAN_RPM_HI(0xD0)=0x%02X, FAN_RPM_LO(0xD1)=0x%02X", 
                registers[0x07], registers[0xCE], registers[0xD0], registers[0xD1]);
    
    // Log full register dump in debug mode
    if (debug_mode) {
        logging_debug("EC_FULL_DUMP: Full register dump:");
        for (int i = 0; i < num_registers; i += 16) {
            char line[256] = "";
            char* ptr = line;
            ptr += sprintf(ptr, "0x%02X: ", i);
            
            for (int j = 0; j < 16 && (i + j) < num_registers; j++) {
                ptr += sprintf(ptr, "%02X ", registers[i + j]);
            }
            logging_debug("%s", line);
        }
    }
}

void logging_cleanup(void) {
    closelog();
} 