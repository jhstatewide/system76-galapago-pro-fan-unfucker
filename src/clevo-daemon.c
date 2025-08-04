/*
 ============================================================================
 Name        : clevo-daemon.c
 Author      : AqD <iiiaqd@gmail.com>
 Version     :
 Description : Headless fan control daemon for Clevo laptops

 Based on http://www.association-apml.fr/upload/fanctrl.c by Jonas Diemer
 (diemer@gmx.de)

 ============================================================================

 This is a headless version of the fan control utility that runs as a daemon
 without any X11 dependencies. It provides automatic fan control based on
 temperature monitoring.

 ============================================================================
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <ctype.h>
#include <getopt.h>
#include <ncurses.h>

#include "privilege_manager.h"
#include "clevo-daemon-socket.h"
#include "clevo-daemon-dbus.h"
#include "fan_constants.h"

#define NAME "clevo-daemon"

#define EC_SC 0x66
#define EC_DATA 0x62

#define IBF 1
#define OBF 0
#define EC_SC_READ_CMD 0x80

/* EC registers can be read by EC_SC_READ_CMD or /sys/kernel/debug/ec/ec0/io:
 *
 * 1. modprobe ec_sys
 * 2. od -Ax -t x1 /sys/kernel/debug/ec/ec0/io
 */

#define EC_REG_SIZE 0x100
#define EC_REG_CPU_TEMP 0x07
#define EC_REG_FAN_DUTY 0xCE
#define EC_REG_FAN_RPMS_HI 0xD0
#define EC_REG_FAN_RPMS_LO 0xD1

#define MAX_FAN_RPM FAN_MAX_RPM

// Define MAX macro if not defined
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

// Fan safety thresholds - now using project-wide constants from fan_constants.h
#define MIN_FAN_DUTY FAN_MIN_DUTY
#define MIN_FAN_RPM FAN_MIN_RPM
#define SAFE_FAN_RPM FAN_SAFE_RPM
#define RPM_DUTY_RATIO FAN_RPM_DUTY_RATIO
#define EMERGENCY_DUTY FAN_EMERGENCY_DUTY

// Global variables
static int debug_mode = 0;
static int log_level = LOG_INFO;
static double status_interval = 2.0;
static int target_temperature = 65;
static int daemon_mode = 0;
static int foreground_mode = 0;  // New flag for foreground operation
static volatile int running = 1;
int max_duty_change_rate = 15;  // Default max duty change per cycle (%)
int max_duty_increase_rate = 10;  // Default max increase per cycle (%)
int max_duty_decrease_rate = 30;  // Default max decrease per cycle (%)

// Live stats mode variables
static int live_stats_mode = 0;
static double live_stats_interval = 0.1;  // 100ms default
static WINDOW* live_stats_window = NULL;
static int live_stats_initialized = 0;
static int last_display_cpu_temp = -1;
static int last_display_fan_duty = -1;
static int last_display_fan_rpm = -1;
static double last_display_pid_error = -999.0;
static double last_display_pid_p = -999.0;
static double last_display_pid_i = -999.0;
static double last_display_pid_d = -999.0;

// Debug log buffer for live stats display
static char debug_log_buffer[10][256];  // Last 10 log messages
static int debug_log_index = 0;
static int debug_log_count = 0;

// PID Controller variables
static double pid_kp = 4.0;  // Increased proportional gain for more aggressive response
static double pid_ki = 0.3;  // Increased integral gain to eliminate steady-state error
static double pid_kd = 1.0;  // Increased derivative gain for better damping
static double pid_integral = 0.0;
static double pid_prev_error = 0.0;
static double pid_output_min = 0.0;
static double pid_output_max = 100.0;
static int pid_enabled = 1;  // Enable PID control by default

// Temperature trend tracking for stuck detection
static int temp_history[10];  // Last 10 temperature readings
static int temp_history_index = 0;
static int temp_history_size = 0;
static int stuck_detection_counter = 0;  // Counter for stuck temperature detection
static int stuck_threshold_cycles = 20;  // Cycles before considering temperature stuck
static double stuck_temp_threshold = 2.0;  // Temperature change threshold for stuck detection

// Temperature validation for sensor glitch detection
static int last_cpu_temp = 0;
static int temp_validation_enabled = 1;  // Enable temperature validation by default
static int max_temp_change_per_cycle = 10;  // Maximum °C change per cycle (configurable)

// Fan health monitoring variables
static int fan_health_check_interval = 30;  // Check fan health every 30 seconds
static int fan_recovery_attempts = 0;
static int max_fan_recovery_attempts = 3;

// Adaptive PID Controller variables
static int adaptive_pid_enabled = 0;  // Disable adaptive tuning by default for stability
static int adaptive_learning_cycles = 0;  // Number of learning cycles completed
static double adaptive_performance_score = 0.0;  // Current performance score
static double adaptive_prev_score = 0.0;  // Previous performance score
static int adaptive_cycle_count = 0;  // Cycles since last tuning
static double adaptive_temp_history[60];  // Temperature history for analysis
static int adaptive_temp_history_index = 0;  // Current index in history
static int adaptive_temp_history_size = 0;  // Number of samples in history

// Adaptive tuning parameters
static double adaptive_kp_step = 0.1;  // Step size for Kp adjustments
static double adaptive_ki_step = 0.01;  // Step size for Ki adjustments  
static double adaptive_kd_step = 0.05;  // Step size for Kd adjustments
static int adaptive_tuning_interval = 30;  // Tuning interval in seconds
static double adaptive_target_performance = 0.8;  // Target performance score

// Shared memory structure
struct {
    volatile int exit;
    volatile int cpu_temp;
    volatile int fan_duty;
    volatile int fan_rpms;
    volatile int auto_duty;
    volatile int auto_duty_val;
    volatile int manual_next_fan_duty;
    volatile int manual_prev_fan_duty;
} *share_info = NULL;

// Add global variable for quiet mode
static int quiet_mode = 0;

// Fan health monitoring state
static struct {
    int low_rpm_count;
    int last_check_duty;
    int last_check_rpm;
    time_t last_check_time;
} fan_health = {0};

// Function declarations
static void daemon_init_share(void);
static int daemon_ec_worker(void);
static void daemon_on_sigterm(int signum);
static int daemon_dump_fan(void);
static int daemon_test_fan(int duty_percentage);
static int ec_init(void);
static int ec_auto_duty_adjust(void);
static int ec_query_cpu_temp(void);

static int ec_query_fan_duty(void);
static int ec_query_fan_rpms(void);
static int ec_write_fan_duty(int duty_percentage);
static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value);
static uint8_t ec_io_read(const uint32_t port);
static int ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value);
static int calculate_fan_duty(int raw_duty);
static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low);
static int check_proc_instances(const char* proc_name);
static void get_time_string(char* buffer, size_t max, const char* format);
static void signal_term(__sighandler_t handler);
static void parse_command_line(int argc, char* argv[]);
static bool setup_privileges(void);
static void show_privilege_help(void);
static void daemon_log(int priority, const char* format, ...);
static void daemonize(void);
static void check_fan_health(void);
static int attempt_fan_recovery(void);

// Alternative temperature reading functions
static int read_temp_from_sysfs(const char* path);
static int read_temp_from_coretemp(void);
static int read_temp_from_hwmon(void);
static int read_temp_from_thermal_zone(void);
static int read_temp_from_acpi(void);
static int get_alternative_cpu_temp(void);
static bool validate_ec_temp_with_alternative(int ec_temp);
static int get_cpu_temperature(void);

// Adaptive PID Controller functions
static void adaptive_pid_add_temp_history(int temp);
static double adaptive_pid_calculate_oscillation(void);
static double adaptive_pid_calculate_performance_score(void);
static void adaptive_pid_tune_parameters(void);

// Temperature trend tracking functions
static void add_temp_to_history(int temp);
static bool is_temp_stuck(void);
static int get_aggressive_duty_for_error(int temp_error);

// Temperature validation functions
static bool validate_temperature_reading(int current_temp, int last_temp, const char* sensor_name);
static int sanitize_temperature_reading(int current_temp, int last_temp, const char* sensor_name);

// Live stats functions
static void live_stats_init(void);
static void live_stats_display(void);
static void live_stats_cleanup(void);
static void live_stats_handle_resize(void);
static void live_stats_handle_input(void);


int main(int argc, char* argv[]) {
    printf("Clevo Fan Control Daemon\n");
    
    // Parse command line arguments
    parse_command_line(argc, argv);
    
    // Check for multiple instances
    if (check_proc_instances(NAME) > 1) {
        printf("Multiple running instances!\n");
        return EXIT_FAILURE;
    }
    
    // Setup privileges using modern methods
    if (!setup_privileges()) {
        printf("Failed to setup privileges for EC access\n");
        return EXIT_FAILURE;
    }
    
    // Initialize live stats if enabled
    if (live_stats_mode) {
        live_stats_init();
    }
    
    // Test EC access
    if (ec_init() != EXIT_SUCCESS) {
        printf("unable to control EC: %s\n", strerror(errno));
        if (live_stats_mode) {
            live_stats_cleanup();
        }
        return EXIT_FAILURE;
    }
    
    // Check for remaining arguments after option processing
    int fan_duty_arg = -1;
    if (optind < argc) {
        fan_duty_arg = optind;
    }
    
    // If no non-option argument and daemon_mode is set (or no fan duty arg), run in daemon mode
    if (fan_duty_arg == -1 || daemon_mode) {
        // Run daemon mode
        signal_term(&daemon_on_sigterm);
        daemon_init_share();
        
        // Daemonize if not in debug mode AND not in live stats mode AND not in foreground mode
        if (!debug_mode && !live_stats_mode && !foreground_mode) {
            daemonize();
        }
        
        // Initialize socket server
        if (init_socket_server() != 0) {
            daemon_log(LOG_ERR, "Failed to initialize socket server");
            return EXIT_FAILURE;
        }
        
        // Initialize DBus interface with enhanced error handling
        daemon_log(LOG_INFO, "Attempting to initialize DBus interface...");
        
        if (foreground_mode) {
            fprintf(stderr, "\n=== DBUS INITIALIZATION DEBUG ===\n");
            fprintf(stderr, "Running in foreground mode with enhanced debugging\n");
            fprintf(stderr, "This will show detailed DBus connection information\n");
            fprintf(stderr, "==========================================\n\n");
        }
        
        if (init_dbus_interface() != 0) {
            if (foreground_mode) {
                fprintf(stderr, "\n=== DBUS INITIALIZATION FAILED ===\n");
                fprintf(stderr, "The daemon will now exit due to DBus initialization failure.\n");
                fprintf(stderr, "Please check the debug output above for details.\n");
                fprintf(stderr, "Common solutions:\n");
                fprintf(stderr, "1. Ensure DBus is running: sudo systemctl start dbus\n");
                fprintf(stderr, "2. Check if another instance is running: sudo pkill clevo-daemon\n");
                fprintf(stderr, "3. Verify system bus permissions\n");
                fprintf(stderr, "==========================================\n");
            }
            daemon_log(LOG_ERR, "Failed to initialize DBus interface - EXITING");
            return EXIT_FAILURE;
        } else {
            if (foreground_mode) {
                fprintf(stderr, "\n=== DBUS INITIALIZATION SUCCESS ===\n");
                fprintf(stderr, "DBus interface initialized successfully!\n");
                fprintf(stderr, "==========================================\n\n");
            }
            daemon_log(LOG_INFO, "DBus interface initialized successfully");
        }
        
        daemon_log(LOG_INFO, "Starting fan control daemon with target temperature %d°C", target_temperature);
        
        if (foreground_mode) {
            fprintf(stderr, "Daemon running in foreground mode. Press Ctrl+C to stop.\n");
            fprintf(stderr, "Target temperature: %d°C\n", target_temperature);
            fprintf(stderr, "Status interval: %.1f seconds\n", status_interval);
            fprintf(stderr, "Debug mode: %s\n", debug_mode ? "enabled" : "disabled");
            fprintf(stderr, "Live stats: %s\n", live_stats_mode ? "enabled" : "disabled");
            fprintf(stderr, "==========================================\n\n");
        }
        
        // Run the main daemon loop
        while (running) {
            daemon_ec_worker();
            
            // Process DBus messages
            process_dbus_messages();
            
            // Broadcast status update to DBus clients (only if listeners exist)
            broadcast_status_update(share_info->cpu_temp, share_info->fan_duty, 
                                 share_info->fan_rpms, share_info->auto_duty);
            
            usleep((int)(status_interval * 1000000)); // Convert to microseconds
        }
        
        // Stop socket server
        stop_socket_server();
        
        // Stop DBus interface
        stop_dbus_interface();
        
        daemon_log(LOG_INFO, "Daemon stopped");
    } else {
        // Check if the argument is a valid fan duty (1-100) or target temperature
        int val = atoi(argv[fan_duty_arg]);
        
        // If it's a valid fan duty (1-100), run in CLI mode
        if (val >= 1 && val <= 100) {
            return daemon_test_fan(val);
        }
        // If it's a valid target temperature (40-100), run in daemon mode with that temperature
        else if (val >= 40 && val <= 100) {
            target_temperature = val;
            signal_term(&daemon_on_sigterm);
            daemon_init_share();
            
            // Daemonize if not in debug mode AND not in live stats mode AND not in foreground mode
            if (!debug_mode && !live_stats_mode && !foreground_mode) {
                daemonize();
            }
            
            // Initialize socket server
            if (init_socket_server() != 0) {
                daemon_log(LOG_ERR, "Failed to initialize socket server");
                return EXIT_FAILURE;
            }
            
            // Initialize DBus interface with enhanced error handling
            daemon_log(LOG_INFO, "Attempting to initialize DBus interface...");
            
            if (foreground_mode) {
                fprintf(stderr, "\n=== DBUS INITIALIZATION DEBUG ===\n");
                fprintf(stderr, "Running in foreground mode with enhanced debugging\n");
                fprintf(stderr, "This will show detailed DBus connection information\n");
                fprintf(stderr, "==========================================\n\n");
            }
            
            if (init_dbus_interface() != 0) {
                if (foreground_mode) {
                    fprintf(stderr, "\n=== DBUS INITIALIZATION FAILED ===\n");
                    fprintf(stderr, "The daemon will now exit due to DBus initialization failure.\n");
                    fprintf(stderr, "Please check the debug output above for details.\n");
                    fprintf(stderr, "Common solutions:\n");
                    fprintf(stderr, "1. Ensure DBus is running: sudo systemctl start dbus\n");
                    fprintf(stderr, "2. Check if another instance is running: sudo pkill clevo-daemon\n");
                    fprintf(stderr, "3. Verify system bus permissions\n");
                    fprintf(stderr, "==========================================\n");
                }
                daemon_log(LOG_ERR, "Failed to initialize DBus interface - EXITING");
                return EXIT_FAILURE;
            } else {
                if (foreground_mode) {
                    fprintf(stderr, "\n=== DBUS INITIALIZATION SUCCESS ===\n");
                    fprintf(stderr, "DBus interface initialized successfully!\n");
                    fprintf(stderr, "==========================================\n\n");
                }
                daemon_log(LOG_INFO, "DBus interface initialized successfully");
            }
            
            daemon_log(LOG_INFO, "Starting fan control daemon with target temperature %d°C", target_temperature);
            
            if (foreground_mode) {
                fprintf(stderr, "Daemon running in foreground mode. Press Ctrl+C to stop.\n");
                fprintf(stderr, "Target temperature: %d°C\n", target_temperature);
                fprintf(stderr, "Status interval: %.1f seconds\n", status_interval);
                fprintf(stderr, "Debug mode: %s\n", debug_mode ? "enabled" : "disabled");
                fprintf(stderr, "Live stats: %s\n", live_stats_mode ? "enabled" : "disabled");
                fprintf(stderr, "==========================================\n\n");
            }
            
            // Run the main daemon loop
            while (running) {
                daemon_ec_worker();
                
                // Process DBus messages
                process_dbus_messages();
                
                // Broadcast status update to DBus clients (only if listeners exist)
                broadcast_status_update(share_info->cpu_temp, share_info->fan_duty, 
                                     share_info->fan_rpms, share_info->auto_duty);
                
                // Use shorter sleep intervals to check for signals more frequently
                int sleep_us = (int)(status_interval * 1000000);
                int check_interval = 100000; // Check every 0.1 seconds
                
                while (sleep_us > 0 && running) {
                    int sleep_chunk = (sleep_us > check_interval) ? check_interval : sleep_us;
                    usleep(sleep_chunk);
                    sleep_us -= sleep_chunk;
                }
            }
            
            // Stop socket server
            stop_socket_server();
            
            // Stop DBus interface
            stop_dbus_interface();
            
            daemon_log(LOG_INFO, "Daemon stopped");
        } else {
            printf("Invalid argument: %s\n", argv[fan_duty_arg]);
            printf("For fan duty (CLI mode): must be 1-100\n");
            printf("For target temperature (daemon mode): must be 40-100°C\n");
            printf("For daemon mode with default temperature: no arguments or --daemon\n");
            return EXIT_FAILURE;
        }
    }
    
    if (live_stats_mode) {
        live_stats_cleanup();
    }
    
    return EXIT_SUCCESS;
}

static void daemon_init_share(void) {
    void* shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    share_info = shm;
    share_info->exit = 0;
    share_info->cpu_temp = 0;
    share_info->fan_duty = 0;
    share_info->fan_rpms = 0;
    share_info->auto_duty = 1;
    share_info->auto_duty_val = 0;
    share_info->manual_next_fan_duty = 0;
    share_info->manual_prev_fan_duty = 0;
}

static int daemon_ec_worker(void) {
    if (debug_mode) daemon_log(LOG_DEBUG, "Worker loop iteration");
    
    // read EC - try sysfs first, fall back to direct I/O
    int sysfs_available = 0;
    int io_fd = open("/sys/kernel/debug/ec/ec0/io", O_RDONLY, 0);
    if (io_fd >= 0) {
        sysfs_available = 1;
        close(io_fd);
        if (debug_mode) daemon_log(LOG_DEBUG, "sysfs method available");
    } else {
        if (debug_mode) daemon_log(LOG_DEBUG, "sysfs method not available, falling back to direct I/O");
    }
    
    // Read fan data from EC (we still need this for fan control)
    if (sysfs_available) {
        int io_fd = open("/sys/kernel/debug/ec/ec0/io", O_RDONLY, 0);
        if (io_fd < 0) {
            if (debug_mode) daemon_log(LOG_DEBUG, "sysfs method failed, switching to direct I/O");
            sysfs_available = 0;
        } else {
            unsigned char buf[EC_REG_SIZE];
            ssize_t len = read(io_fd, buf, EC_REG_SIZE);
            close(io_fd);
            if (debug_mode) daemon_log(LOG_DEBUG, "sysfs read returned len=%ld", len);
            switch (len) {
            case -1:
                if (debug_mode) daemon_log(LOG_DEBUG, "unable to read EC from sysfs: %s", strerror(errno));
                sysfs_available = 0;
                break;
            case 0x100:
                // Use standard Linux temperature reading instead of EC
                int cpu_temp = get_cpu_temperature();
                if (cpu_temp > 0) {
                    share_info->cpu_temp = cpu_temp;
                } else {
                    // Fall back to EC temperature if standard method fails
                    int raw_cpu_temp = buf[EC_REG_CPU_TEMP];
                    share_info->cpu_temp = sanitize_temperature_reading(raw_cpu_temp, last_cpu_temp, "CPU");
                    if (share_info->cpu_temp == raw_cpu_temp) {
                        last_cpu_temp = raw_cpu_temp;
                    }
                }
                
                share_info->fan_duty = calculate_fan_duty(buf[EC_REG_FAN_DUTY]);
                share_info->fan_rpms = calculate_fan_rpms(buf[EC_REG_FAN_RPMS_HI], buf[EC_REG_FAN_RPMS_LO]);
                if (debug_mode) daemon_log(LOG_DEBUG, "sysfs: cpu_temp=%d, fan_duty=%d, fan_rpms=%d", 
                    share_info->cpu_temp, share_info->fan_duty, share_info->fan_rpms);
                break;
            default:
                if (debug_mode) daemon_log(LOG_DEBUG, "wrong EC size from sysfs: %ld", len);
                sysfs_available = 0;
            }
        }
    }
    
    // Fall back to direct I/O if sysfs is not available
    if (!sysfs_available) {
        if (debug_mode) daemon_log(LOG_DEBUG, "Using direct I/O for EC access");
        
        // Use standard Linux temperature reading instead of EC
        int cpu_temp = get_cpu_temperature();
        if (cpu_temp > 0) {
            share_info->cpu_temp = cpu_temp;
        } else {
            // Fall back to EC temperature if standard method fails
            int raw_cpu_temp = ec_query_cpu_temp();
            share_info->cpu_temp = sanitize_temperature_reading(raw_cpu_temp, last_cpu_temp, "CPU");
            if (share_info->cpu_temp == raw_cpu_temp) {
                last_cpu_temp = raw_cpu_temp;
            }
        }
        
        share_info->fan_duty = ec_query_fan_duty();
        share_info->fan_rpms = ec_query_fan_rpms();
        
        // Check fan health
        check_fan_health();
        
        if (debug_mode) daemon_log(LOG_DEBUG, "direct I/O: cpu_temp=%d, fan_duty=%d, fan_rpms=%d", 
            share_info->cpu_temp, share_info->fan_duty, share_info->fan_rpms);
    }
    
    // Check fan health
    check_fan_health();
    
    // auto EC control
    if (share_info->auto_duty == 1) {
        int next_duty = ec_auto_duty_adjust();
        if (debug_mode) daemon_log(LOG_DEBUG, "auto_duty=1, next_duty=%d, prev_auto_duty_val=%d", next_duty, share_info->auto_duty_val);
        
        // Emergency bypass: Always write if we're in emergency mode, even if duty hasn't changed
        int temp = share_info->cpu_temp;  // Only use CPU temperature
        bool emergency_mode = (temp >= target_temperature + 10) || (temp >= target_temperature + 5 && next_duty >= 80);
        
        if (next_duty != 0 && (next_duty != share_info->auto_duty_val || emergency_mode)) {
            char s_time[256];
            get_time_string(s_time, 256, "%m/%d %H:%M:%S");
            daemon_log(LOG_INFO, "%s CPU=%d°C, auto fan duty to %d%%", s_time, share_info->cpu_temp, next_duty);
            int write_result = ec_write_fan_duty(next_duty);
            if (debug_mode) daemon_log(LOG_DEBUG, "ec_write_fan_duty (auto) returned: %d", write_result);
            share_info->auto_duty_val = next_duty;
        }
    }
    
    // Update live stats display if enabled
    if (live_stats_mode && live_stats_initialized) {
        live_stats_display();
    }
    
    return EXIT_SUCCESS;
}

static int daemon_dump_fan(void) {
    printf("Dump fan information\n");
    printf("  FAN Duty: %d%%\n", ec_query_fan_duty());
    printf("  FAN RPMs: %d RPM\n", ec_query_fan_rpms());
    printf("  CPU Temp: %d°C\n", ec_query_cpu_temp());
    return EXIT_SUCCESS;
}

static int daemon_test_fan(int duty_percentage) {
    printf("Change fan duty to %d%%\n", duty_percentage);
    ec_write_fan_duty(duty_percentage);
    printf("\n");
    daemon_dump_fan();
    return EXIT_SUCCESS;
}

static void daemon_on_sigterm(int signum) {
    daemon_log(LOG_INFO, "Received signal %s, shutting down immediately", strsignal(signum));
    running = 0;
    if (share_info != NULL) {
        share_info->exit = 1;
    }
    
    // Clean up live stats if enabled
    if (live_stats_mode) {
        live_stats_cleanup();
    }
    
    // Stop socket server immediately
    stop_socket_server();
    
    // Force immediate exit to avoid waiting for sleep
    exit(EXIT_SUCCESS);
}



static int ec_init(void) {
    if (ioperm(EC_DATA, 1, 1) != 0)
        return EXIT_FAILURE;
    if (ioperm(EC_SC, 1, 1) != 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static int ec_auto_duty_adjust(void) {
    if (!pid_enabled) {
        // Fall back to simple control if PID is disabled
        int temp = share_info->cpu_temp;  // Only use CPU temperature
        int duty = share_info->fan_duty;
        int new_duty = duty;

        if (temp >= target_temperature) {
            new_duty = MAX(duty + 2, 10);
        } else {
            new_duty = MAX(duty - 2, 0);
        }

        if (new_duty > 100) {
            new_duty = 100;
        } else if (new_duty < 0) {
            new_duty = 0;
        }

        return new_duty;
    }

    // Enhanced PID Controller with aggressive temperature control and learning
    // Strategy:
    // 1. Emergency response: 100% duty when temp is 8°C+ above target
    // 2. High temp response: 90% duty when temp is 5°C+ above target  
    // 3. Moderate temp response: 70% duty when temp is 3°C+ above target
    // 4. Stuck detection: Escalate duty if temperature isn't moving toward target
    // 5. Normal PID control: Standard PID for temperatures closer to target
    // 6. Minimum duty: 30% when above target to ensure cooling

    // PID Controller implementation
    int temp = share_info->cpu_temp;  // Only use CPU temperature
    double setpoint = (double)target_temperature;
    double process_variable = (double)temp;
    double error = process_variable - setpoint;
    
    // Track temperature history for stuck detection
    add_temp_to_history(temp);
    
    // Add temperature to history for adaptive tuning
    if (adaptive_pid_enabled) {
        adaptive_pid_add_temp_history(temp);
        adaptive_cycle_count++;
        
        // Perform adaptive tuning at intervals
        if (adaptive_cycle_count >= adaptive_tuning_interval) {
            adaptive_pid_tune_parameters();
            adaptive_cycle_count = 0;
        }
    }
    
    // Determine if temperature is stuck at a suboptimal level
    bool temp_stuck = is_temp_stuck();
    int temp_error = temp - target_temperature;
    
    // Aggressive response for high temperatures with progressive escalation
    int new_duty = 0;
    
    if (temp_error >= 8) {
        // Emergency response: 100% duty when temp is 8°C+ above target
        new_duty = 100;
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "Emergency response: temp=%d, target=%d, error=%d°C, setting duty to 100%%", temp, target_temperature, temp_error);
        }
    } else if (temp_error >= 5) {
        // High temp response: 90% duty when temp is 5°C+ above target
        new_duty = 90;
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "High temperature response: temp=%d, target=%d, error=%d°C, setting duty to 90%%", temp, target_temperature, temp_error);
        }
    } else if (temp_error >= 3) {
        // Moderate temp response: 70% duty when temp is 3°C+ above target
        new_duty = 70;
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "Moderate temperature response: temp=%d, target=%d, error=%d°C, setting duty to 70%%", temp, target_temperature, temp_error);
        }
    } else if (temp_error > 0) {
        // Normal PID control for temperatures closer to target
        // Calculate PID terms
        double proportional = pid_kp * error;
        
        // Integral term with anti-windup
        pid_integral += error;
        if (pid_integral > 100.0) pid_integral = 100.0;
        if (pid_integral < -100.0) pid_integral = -100.0;
        double integral = pid_ki * pid_integral;
        
        // Derivative term
        double derivative = pid_kd * (error - pid_prev_error);
        
        // Calculate PID output
        double output = proportional + integral + derivative;
        
        // Clamp output to valid range
        if (output > pid_output_max) output = pid_output_max;
        if (output < pid_output_min) output = pid_output_min;
        
        // Store error for next iteration
        pid_prev_error = error;
        
        // Convert to integer duty cycle
        new_duty = (int)(output + 0.5); // Round to nearest integer
        
        // Ensure duty cycle is within valid range
        if (new_duty > 100) new_duty = 100;
        if (new_duty < 0) new_duty = 0;
        
        // Ensure minimum duty when temperature is above target
        if (temp > target_temperature && new_duty < 30) {
            new_duty = 30; // Minimum 30% duty when above target
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Enforcing minimum duty: temp=%d, target=%d, setting minimum duty to 30%%", temp, target_temperature);
            }
        }
        
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "PID calculation: temp=%d, setpoint=%.1f, error=%.1f, p=%.1f, i=%.1f, d=%.1f, output=%.1f, duty=%d",
                   temp, setpoint, error, proportional, integral, derivative, output, new_duty);
        }
    } else {
        // Temperature is at or below target, use PID for fine control
        double proportional = pid_kp * error;
        pid_integral += error;
        if (pid_integral > 100.0) pid_integral = 100.0;
        if (pid_integral < -100.0) pid_integral = -100.0;
        double integral = pid_ki * pid_integral;
        double derivative = pid_kd * (error - pid_prev_error);
        
        double output = proportional + integral + derivative;
        
        // For temperatures below target, we want to reduce fan speed
        // But we need to handle the negative output properly
        if (output < 0) {
            // Negative output means we want to reduce fan speed
            // Calculate how much to reduce from current duty
            int current_duty = share_info->fan_duty;
            int reduction = (int)(-output + 0.5); // Convert negative to positive reduction
            
            // Limit reduction to current duty (can't go below 0)
            if (reduction > current_duty) {
                reduction = current_duty;
            }
            
            new_duty = current_duty - reduction;
            
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Below target PID: temp=%d, setpoint=%.1f, error=%.1f, output=%.1f, reduction=%d, duty=%d",
                       temp, setpoint, error, output, reduction, new_duty);
            }
        } else {
            // Positive output (shouldn't happen when below target, but handle it)
            if (output > pid_output_max) output = pid_output_max;
            if (output < pid_output_min) output = pid_output_min;
            new_duty = (int)(output + 0.5);
            
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Below target PID (positive output): temp=%d, setpoint=%.1f, error=%.1f, duty=%d",
                       temp, setpoint, error, new_duty);
            }
        }
        
        // Ensure duty cycle is within valid range
        if (new_duty > 100) new_duty = 100;
        if (new_duty < 0) new_duty = 0;
        
        pid_prev_error = error;
    }
    
    // Stuck temperature escalation: If temperature is stuck and above target, escalate duty
    if (temp_stuck && temp_error > 0) {
        int escalated_duty = get_aggressive_duty_for_error(temp_error);
        if (escalated_duty > new_duty) {
            new_duty = escalated_duty;
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Stuck temperature escalation: temp=%d, target=%d, escalating duty to %d%%", temp, target_temperature, new_duty);
            }
        }
    }
    
    // Rate limiting with emergency bypass
    int current_duty = share_info->fan_duty;
    int max_increase = max_duty_increase_rate;
    int max_decrease = max_duty_decrease_rate;
    
    // Emergency bypass: Allow faster rate limiting for critical temperature situations
    bool emergency_bypass = (temp_error >= 8) || (temp_error >= 5 && new_duty >= 80) || temp_stuck;
    
    // Critical bypass: For very high temperatures, bypass rate limiting entirely
    bool critical_bypass = (temp_error >= 12) || (temp >= target_temperature + 15);
    
    // Cool-down bypass: For temperatures significantly below target, allow faster fan reduction
    bool cooldown_bypass = (temp_error <= -5) || (temp <= target_temperature - 8);
    
    if (debug_mode) {
        daemon_log(LOG_DEBUG, "Rate limiting check: current_duty=%d, new_duty=%d, temp_error=%d, emergency_bypass=%s, critical_bypass=%s, cooldown_bypass=%s", 
                  current_duty, new_duty, temp_error, emergency_bypass ? "true" : "false", 
                  critical_bypass ? "true" : "false", cooldown_bypass ? "true" : "false");
    }
    
    if (!emergency_bypass) {
        // Normal rate limiting
        if (new_duty > current_duty + max_increase) {
            int original_duty = new_duty;
            new_duty = current_duty + max_increase;
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Normal rate limiting: limiting duty increase from %d to %d (max_increase=%d)", original_duty, new_duty, max_increase);
            }
        } else if (new_duty < current_duty - max_decrease) {
            int original_duty = new_duty;
            new_duty = current_duty - max_decrease;
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Normal rate limiting: limiting duty decrease from %d to %d (max_decrease=%d)", original_duty, new_duty, max_decrease);
            }
        } else {
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Normal rate limiting: allowing duty change from %d to %d", current_duty, new_duty);
            }
        }
    } else if (critical_bypass) {
        // Critical bypass: No rate limiting for extreme temperatures
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "Critical bypass: allowing full duty change from %d to %d (temp=%d, target=%d, error=%d°C)", 
                      current_duty, new_duty, temp, target_temperature, temp_error);
        }
    } else if (cooldown_bypass) {
        // Cool-down bypass: Allow faster fan reduction when temperature is well below target
        int cooldown_max_change = max_decrease * 2; // Allow 2x normal decrease rate for cooldown
        if (new_duty < current_duty - cooldown_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty - cooldown_max_change;
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Cool-down bypass: limiting duty decrease from %d to %d (cooldown rate: %d)", 
                          original_duty, new_duty, cooldown_max_change);
            }
        } else {
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Cool-down bypass: allowing duty change from %d to %d (temp=%d, target=%d, error=%d°C)", 
                          current_duty, new_duty, temp, target_temperature, temp_error);
            }
        }
    } else {
        // Emergency rate limiting: Allow faster response for critical situations (reduced values)
        int emergency_max_change;
        
        if (temp_error >= 10) {
            // Critical emergency: Allow up to 25% change per cycle (reduced from 50%)
            emergency_max_change = 25;
        } else if (temp_error >= 8) {
            // High emergency: Allow up to 15% change per cycle (reduced from 30%)
            emergency_max_change = 15;
        } else {
            // Moderate emergency: Allow up to 10% change per cycle (reduced from 20%)
            emergency_max_change = 10;
        }
        
        if (new_duty > current_duty + emergency_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty + emergency_max_change;
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Emergency rate limiting: limiting duty increase from %d to %d (emergency rate: %d)", 
                          original_duty, new_duty, emergency_max_change);
            }
        } else if (new_duty < current_duty - emergency_max_change) {
            int original_duty = new_duty;
            new_duty = current_duty - emergency_max_change;
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Emergency rate limiting: limiting duty decrease from %d to %d (emergency rate: %d)", 
                          original_duty, new_duty, emergency_max_change);
            }
        } else {
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Emergency bypass: allowing duty change from %d to %d (temp=%d, target=%d, stuck=%s, emergency rate: %d)", 
                          current_duty, new_duty, temp, target_temperature, temp_stuck ? "true" : "false", emergency_max_change);
            }
        }
    }
    
    if (debug_mode) {
        daemon_log(LOG_DEBUG, "Final duty calculation: temp=%d, setpoint=%.1f, error=%.1f, duty=%d, stuck=%s",
               temp, setpoint, error, new_duty, temp_stuck ? "true" : "false");
    }
    
    // Ensure duty cycle is within valid range
    if (new_duty > 100) new_duty = 100;
    if (new_duty < MIN_FAN_DUTY) new_duty = MIN_FAN_DUTY;  // Never go below minimum duty
    
    // CRITICAL: Ensure duty cycle will result in RPM above minimum threshold
    // Calculate the minimum duty needed to achieve FAN_MIN_RPM
    int min_duty_for_min_rpm = (FAN_MIN_RPM + FAN_RPM_DUTY_RATIO - 1) / FAN_RPM_DUTY_RATIO; // Ceiling division
    if (new_duty < min_duty_for_min_rpm) {
        daemon_log(LOG_WARNING, "Preventing fan stall: duty=%d%% would result in RPM below minimum (%d), setting to %d%%", 
                  new_duty, FAN_MIN_RPM, min_duty_for_min_rpm);
        new_duty = min_duty_for_min_rpm;
    }
    
    // Check if fan is potentially stuck
    int current_rpms = share_info->fan_rpms;
    int expected_min_rpm = new_duty * RPM_DUTY_RATIO;
    
    if (current_rpms < MIN_FAN_RPM || (current_rpms < expected_min_rpm * 0.7)) {  // Allow 30% tolerance
        daemon_log(LOG_WARNING, "Fan may be stuck: RPM=%d (expected >%d) at duty=%d%%", 
                  current_rpms, expected_min_rpm, new_duty);
        
        // Attempt recovery by temporarily boosting fan speed
        new_duty = MAX(new_duty + 20, 60);  // Boost by 20% or to at least 60%
        daemon_log(LOG_INFO, "Attempting fan recovery by setting duty to %d%%", new_duty);
    }
    
    if (debug_mode) {
        daemon_log(LOG_DEBUG, "Final duty calculation: temp=%d, setpoint=%.1f, error=%.1f, duty=%d, stuck=%s",
               temp, setpoint, error, new_duty, temp_stuck ? "true" : "false");
    }
    
    return new_duty;
}

static int ec_query_cpu_temp(void) {
    return ec_io_read(EC_REG_CPU_TEMP);
}

static int ec_query_fan_duty(void) {
    int raw_duty = ec_io_read(EC_REG_FAN_DUTY);
    return calculate_fan_duty(raw_duty);
}

static int ec_query_fan_rpms(void) {
    int raw_rpm_hi = ec_io_read(EC_REG_FAN_RPMS_HI);
    int raw_rpm_lo = ec_io_read(EC_REG_FAN_RPMS_LO);
    return calculate_fan_rpms(raw_rpm_hi, raw_rpm_lo);
}

static int ec_write_fan_duty(int duty_percentage) {
    // Enforce minimum duty cycle
    if (duty_percentage < MIN_FAN_DUTY) {
        daemon_log(LOG_INFO, "Adjusting fan duty to minimum: %d%% -> %d%%", duty_percentage, MIN_FAN_DUTY);
        duty_percentage = MIN_FAN_DUTY;
    }
    
    if (duty_percentage < 1 || duty_percentage > 100) {
        daemon_log(LOG_ERR, "Wrong fan duty to write: %d", duty_percentage);
        return EXIT_FAILURE;
    }
    double v_d = ((double) duty_percentage) / 100.0 * 255.0;
    int v_i = (int) v_d;
    return ec_io_do(0x99, 0x01, v_i);
}

static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    if (i >= 100) {
        daemon_log(LOG_ERR, "wait_ec error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x",
                port, data, flag, value);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static uint8_t ec_io_read(const uint32_t port) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(EC_SC_READ_CMD, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, OBF, 1);
    uint8_t value = inb(EC_DATA);

    return value;
}

static int ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(cmd, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, IBF, 0);
    outb(value, EC_DATA);

    return ec_io_wait(EC_SC, IBF, 0);
}

static int calculate_fan_duty(int raw_duty) {
    return (int) ((double) raw_duty / 255.0 * 100.0);
}

static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    
    // Handle edge cases to prevent nonsensical RPM values
    if (raw_rpm <= 0) {
        return 0;
    }
    
    // Prevent division by very small numbers that could cause overflow
    if (raw_rpm < 10) {
        return 0;  // Fan is likely stopped or in error state
    }
    
    int calculated_rpm = 2156220 / raw_rpm;
    
    // Sanity check: RPM should be reasonable (0-10000 for laptop fans)
    if (calculated_rpm < 0 || calculated_rpm > 10000) {
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "Invalid RPM calculation: raw_rpm=%d, calculated_rpm=%d", raw_rpm, calculated_rpm);
        }
        return 0;  // Return 0 for invalid readings
    }
    
    return calculated_rpm;
}

static int check_proc_instances(const char* proc_name) {
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

static void get_time_string(char* buffer, size_t max, const char* format) {
    time_t timer;
    struct tm tm_info;
    time(&timer);
    localtime_r(&timer, &tm_info);
    strftime(buffer, max, format, &tm_info);
}

static void signal_term(__sighandler_t handler) {
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
}

static void parse_command_line(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"debug", no_argument, 0, 'd'},
        {"daemon", no_argument, 0, 'D'},
        {"foreground", no_argument, 0, 'f'},  // New foreground option
        {"interval", required_argument, 0, 'i'},
        {"target", required_argument, 0, 't'},
        {"log-level", required_argument, 0, 'l'},
        {"live-stats", no_argument, 0, 'L'},
        {"live-stats-interval", required_argument, 0, 'I'},
        {"max-duty-change", required_argument, 0, 'm'},
        {"max-duty-increase", required_argument, 0, 'M'},
        {"max-duty-decrease", required_argument, 0, 'N'},
        {"privilege-help", no_argument, 0, 'p'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "hdDfi:t:l:LI:m:M:N:p", long_options, &option_index)) != -1) {
        switch (c) {
            case 'h':
                printf("Clevo Fan Control Daemon\n\n");
                printf("Usage: %s [OPTIONS] [FAN_DUTY|TARGET_TEMP]\n\n", NAME);
                printf("Options:\n");
                printf("  -h, --help                    Show this help message\n");
                printf("  -d, --debug                   Enable debug mode\n");
                printf("  -D, --daemon                  Run in daemon mode (default)\n");
                printf("  -f, --foreground              Run in foreground mode with enhanced debugging\n");
                printf("  -i, --interval SECONDS        Status update interval (default: 2.0)\n");
                printf("  -t, --target TEMP             Target temperature in Celsius (default: 65)\n");
                printf("  -l, --log-level LEVEL         Log level (0-7, default: 6)\n");
                printf("  -L, --live-stats              Enable live statistics display\n");
                printf("  -I, --live-stats-interval SECONDS  Live stats update interval (default: 0.1)\n");
                printf("  -m, --max-duty-change RATE    Max duty change per cycle %% (default: 15)\n");
                printf("  -M, --max-duty-increase RATE  Max duty increase per cycle %% (default: 10)\n");
                printf("  -N, --max-duty-decrease RATE  Max duty decrease per cycle %% (default: 30)\n");
                printf("  -p, --privilege-help          Show privilege setup help\n\n");
                printf("Arguments:\n");
                printf("  FAN_DUTY                      Set fan to specific duty cycle (1-100%%)\n");
                printf("  TARGET_TEMP                   Set target temperature (40-100°C)\n\n");
                printf("Examples:\n");
                printf("  %s --foreground               # Run in foreground with debug output\n", NAME);
                printf("  %s --debug --foreground       # Run in foreground with debug mode\n", NAME);
                printf("  %s 50                         # Set fan to 50%% duty\n", NAME);
                printf("  %s 70                         # Run daemon with 70°C target\n", NAME);
                printf("  %s --live-stats               # Run with live statistics\n", NAME);
                exit(EXIT_SUCCESS);
                break;
                
            case 'd':
                debug_mode = 1;
                break;
                
            case 'D':
                daemon_mode = 1;
                break;
                
            case 'f':
                foreground_mode = 1;
                debug_mode = 1;  // Enable debug mode when running in foreground
                break;
                
            case 'i':
                status_interval = atof(optarg);
                if (status_interval <= 0) {
                    fprintf(stderr, "Error: Invalid interval value: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
                
            case 't':
                target_temperature = atoi(optarg);
                if (target_temperature < 40 || target_temperature > 100) {
                    fprintf(stderr, "Error: Target temperature must be between 40 and 100°C\n");
                    exit(EXIT_FAILURE);
                }
                break;
                
            case 'l':
                log_level = atoi(optarg);
                if (log_level < 0 || log_level > 7) {
                    fprintf(stderr, "Error: Log level must be between 0 and 7\n");
                    exit(EXIT_FAILURE);
                }
                break;
                
            case 'L':
                live_stats_mode = 1;
                break;
                
            case 'I':
                live_stats_interval = atof(optarg);
                if (live_stats_interval <= 0) {
                    fprintf(stderr, "Error: Invalid live stats interval value: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
                
            case 'm':
                max_duty_change_rate = atoi(optarg);
                if (max_duty_change_rate < 1 || max_duty_change_rate > 100) {
                    fprintf(stderr, "Error: Max duty change rate must be between 1 and 100%%\n");
                    exit(EXIT_FAILURE);
                }
                break;
                
            case 'M':
                max_duty_increase_rate = atoi(optarg);
                if (max_duty_increase_rate < 1 || max_duty_increase_rate > 100) {
                    fprintf(stderr, "Error: Max duty increase rate must be between 1 and 100%%\n");
                    exit(EXIT_FAILURE);
                }
                break;
                
            case 'N':
                max_duty_decrease_rate = atoi(optarg);
                if (max_duty_decrease_rate < 1 || max_duty_decrease_rate > 100) {
                    fprintf(stderr, "Error: Max duty decrease rate must be between 1 and 100%%\n");
                    exit(EXIT_FAILURE);
                }
                break;
                
            case 'p':
                show_privilege_help();
                exit(EXIT_SUCCESS);
                break;
                
            case '?':
                exit(EXIT_FAILURE);
                break;
                
            default:
                abort();
        }
    }
}

static bool setup_privileges(void) {
    privilege_manager_init();
    privilege_status_t status = privilege_check_status();
    
    if (status.has_privileges) {
        return true;
    }
    
    if (privilege_elevate()) {
        return true;
    }
    
    printf("Failed to elevate privileges: %s\n", 
           status.error_message ? status.error_message : "unknown error");
    show_privilege_help();
    return false;
}

static void show_privilege_help(void) {
    printf("\nPrivilege elevation failed. Try one of these methods:\n\n");
    printf("1. Capabilities (Recommended):\n");
    printf("   sudo setcap cap_sys_rawio+ep bin/clevo-daemon\n\n");
    printf("2. Systemd Service:\n");
    printf("   sudo cp systemd/clevo-daemon.service /etc/systemd/system/\n");
    printf("   sudo systemctl enable clevo-daemon.service\n\n");
    printf("3. Traditional setuid:\n");
    printf("   sudo chown root bin/clevo-daemon\n");
    printf("   sudo chmod u+s bin/clevo-daemon\n\n");
}

static void daemon_log(int priority, const char* format, ...) {
    if (quiet_mode && priority > LOG_ERR) {
        return;
    }
    va_list args;
    va_start(args, format);
    if (priority <= log_level) {
        vsyslog(priority, format, args);
        // Only print to stdout if not in live stats mode (to avoid interfering with ncurses)
        if ((debug_mode || priority <= LOG_WARNING) && !live_stats_mode) {
            vprintf(format, args);
            printf("\n");
            fflush(stdout);
        }
        // Store log messages in debug buffer for live stats display
        if (live_stats_mode && (debug_mode || priority <= LOG_WARNING)) {
            char temp_buffer[128];  // Reduced buffer size to avoid truncation
            vsnprintf(temp_buffer, sizeof(temp_buffer), format, args);
            // Add timestamp
            char timestamp[20];  // Increased from default to ensure space for timestamp
            time_t now = time(NULL);
            strftime(timestamp, sizeof(timestamp), "%H:%M:%S", localtime(&now));
            // Store in circular buffer with safe string concatenation
            snprintf(debug_log_buffer[debug_log_index], sizeof(debug_log_buffer[0]),
                    "[%.8s] %.200s", timestamp, temp_buffer);  // Explicit length limits
            debug_log_index = (debug_log_index + 1) % 10;
            if (debug_log_count < 10) debug_log_count++;
        }
    }
    va_end(args);
} 

static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    umask(0);
    
    pid_t sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }
    
    if ((chdir("/")) < 0) {
        exit(EXIT_FAILURE);
    }
    
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

static void adaptive_pid_add_temp_history(int temp) {
    adaptive_temp_history[adaptive_temp_history_index] = (double)temp;
    adaptive_temp_history_index = (adaptive_temp_history_index + 1) % 60;
    if (adaptive_temp_history_size < 60) {
        adaptive_temp_history_size++;
    }
}

static double adaptive_pid_calculate_oscillation(void) {
    if (adaptive_temp_history_size < 10) return 0.0;
    
    double variance = 0.0;
    double mean = 0.0;
    
    // Calculate mean
    for (int i = 0; i < adaptive_temp_history_size; i++) {
        mean += adaptive_temp_history[i];
    }
    mean /= adaptive_temp_history_size;
    
    // Calculate variance
    for (int i = 0; i < adaptive_temp_history_size; i++) {
        double diff = adaptive_temp_history[i] - mean;
        variance += diff * diff;
    }
    variance /= adaptive_temp_history_size;
    
    return sqrt(variance);
}

static double adaptive_pid_calculate_performance_score(void) {
    int temp = share_info->cpu_temp;  // Only use CPU temperature
    double error = fabs((double)temp - (double)target_temperature);
    double oscillation = adaptive_pid_calculate_oscillation();
    
    // Base score based on error (closer to target = higher score)
    double error_score = 1.0 - (error / 50.0);  // Normalize error to 0-1
    if (error_score < 0.0) error_score = 0.0;
    if (error_score > 1.0) error_score = 1.0;
    
    // Oscillation penalty (less oscillation = higher score)
    double oscillation_penalty = oscillation / 10.0;  // Normalize oscillation
    if (oscillation_penalty > 1.0) oscillation_penalty = 1.0;
    
    // Fan efficiency penalty (lower fan usage = higher score, but only if temp is good)
    double fan_efficiency = 1.0 - ((double)share_info->fan_duty / 100.0);
    double fan_score = (error < 5.0) ? fan_efficiency : 0.0;  // Only consider fan efficiency if temp is close to target
    
    // Combine scores
    double final_score = (error_score * 0.6) + ((1.0 - oscillation_penalty) * 0.3) + (fan_score * 0.1);
    
    return final_score;
}

static void adaptive_pid_tune_parameters(void) {
    double current_score = adaptive_pid_calculate_performance_score();
    double score_change = current_score - adaptive_prev_score;
    
    if (debug_mode) {
        daemon_log(LOG_DEBUG, "Adaptive PID: Score=%.3f, Change=%.3f, Kp=%.2f, Ki=%.3f, Kd=%.2f",
               current_score, score_change, pid_kp, pid_ki, pid_kd);
    }
    
    // Adjust parameters based on performance
    if (score_change > 0.05) {
        // Performance improved, continue in same direction
        if (debug_mode) daemon_log(LOG_DEBUG, "Adaptive PID: Performance improved, maintaining direction");
    } else if (score_change < -0.05) {
        // Performance degraded, reverse direction
        adaptive_kp_step *= -0.8;
        adaptive_ki_step *= -0.8;
        adaptive_kd_step *= -0.8;
        if (debug_mode) daemon_log(LOG_DEBUG, "Adaptive PID: Performance degraded, reversing direction");
    }
    
    // Adjust Kp (proportional gain)
    if (current_score < adaptive_target_performance) {
        pid_kp += adaptive_kp_step;
        if (pid_kp < 0.5) pid_kp = 0.5;
        if (pid_kp > 5.0) pid_kp = 5.0;
    }
    
    // Adjust Ki (integral gain)
    double oscillation = adaptive_pid_calculate_oscillation();
    int temp = share_info->cpu_temp;  // Only use CPU temperature
    double error = fabs((double)temp - (double)target_temperature);
    
    if (oscillation > 3.0) {
        // High oscillation, reduce Ki and increase Kd
        pid_ki -= adaptive_ki_step;
        pid_kd += adaptive_kd_step;
    } else if (error > 5.0) {
        // High error, increase Ki
        pid_ki += adaptive_ki_step;
    }
    
    // Clamp Ki and Kd values
    if (pid_ki < 0.01) pid_ki = 0.01;
    if (pid_ki > 0.5) pid_ki = 0.5;
    if (pid_kd < 0.1) pid_kd = 0.1;
    if (pid_kd > 2.0) pid_kd = 2.0;
    
    adaptive_prev_score = current_score;
    adaptive_performance_score = current_score;
    adaptive_learning_cycles++;
    
    if (debug_mode) {
        daemon_log(LOG_DEBUG, "Adaptive PID: New parameters - Kp=%.2f, Ki=%.3f, Kd=%.2f",
               pid_kp, pid_ki, pid_kd);
    }
}





static void check_fan_health(void) {
    time_t current_time = time(NULL);
    int current_duty = share_info->fan_duty;
    int current_rpm = share_info->fan_rpms;
    
    // Only check every 30 seconds
    if (current_time - fan_health.last_check_time < 30) {
        return;
    }
    
    // Reset counters if duty cycle has changed significantly
    if (abs(current_duty - fan_health.last_check_duty) > 5) {
        fan_health.low_rpm_count = 0;
    }
    
    int expected_min_rpm = current_duty * RPM_DUTY_RATIO;
    
    // Check if RPMs are critically low
    if (current_rpm < MIN_FAN_RPM) {
        // Emergency response for critically low RPM
        daemon_log(LOG_ERR, "CRITICAL: Fan RPM (%d) below minimum threshold (%d)", current_rpm, MIN_FAN_RPM);
        ec_write_fan_duty(EMERGENCY_DUTY);  // Immediately boost fan
        attempt_fan_recovery();  // Try recovery procedure
        fan_health.low_rpm_count = 0;  // Reset counter after emergency response
    }
    // Check if RPMs are below safe operating level
    else if (current_rpm < SAFE_FAN_RPM || (current_rpm < expected_min_rpm * 0.7)) {
        fan_health.low_rpm_count++;
        
        if (fan_health.low_rpm_count >= 2) {  // Reduced threshold for faster response
            daemon_log(LOG_WARNING, "Low fan RPM detected: %d RPM at %d%% duty", 
                      current_rpm, current_duty);
            
            // Increase duty cycle by 10% or to minimum safe duty
            int new_duty = MAX(current_duty + 10, MIN_FAN_DUTY);
            ec_write_fan_duty(new_duty);
            daemon_log(LOG_INFO, "Increasing fan duty to %d%% to maintain safe RPM", new_duty);
            
            if (fan_health.low_rpm_count >= 4) {  // If problem persists, try recovery
                attempt_fan_recovery();
            }
        }
    } else {
        fan_health.low_rpm_count = 0;  // Reset counter when RPMs are normal
    }
    
    // Update check state
    fan_health.last_check_duty = current_duty;
    fan_health.last_check_rpm = current_rpm;
    fan_health.last_check_time = current_time;
}

static int attempt_fan_recovery(void) {
    daemon_log(LOG_INFO, "Attempting fan recovery...");
    
    // First try: Full speed to kick-start
    ec_write_fan_duty(100);
    usleep(1000000);  // Wait 1 second at full speed
    
    int rpm = ec_query_fan_rpms();
    if (rpm > SAFE_FAN_RPM) {
        daemon_log(LOG_INFO, "Fan kick-started successfully at %d RPM", rpm);
        return EXIT_SUCCESS;
    }
    
    // If kick-start didn't work, try aggressive cycling
    int recovery_duties[] = {100, 80, 100, 60, 100, 40, 100};  // Always return to 100%
    int num_duties = sizeof(recovery_duties) / sizeof(recovery_duties[0]);
    
    for (int i = 0; i < num_duties; i++) {
        int duty = recovery_duties[i];
        daemon_log(LOG_INFO, "Recovery step %d/%d: Setting fan to %d%%", 
                  i + 1, num_duties, duty);
        
        ec_write_fan_duty(duty);
        usleep(800000);  // Wait longer (800ms) between changes
        
        // Check if fan responded
        rpm = ec_query_fan_rpms();
        if (rpm > SAFE_FAN_RPM) {
            daemon_log(LOG_INFO, "Fan responding at %d RPM - recovery successful", rpm);
            
            // Gradually step down to ensure stability
            for (int step = 90; step >= MIN_FAN_DUTY; step -= 10) {
                ec_write_fan_duty(step);
                usleep(500000);
                rpm = ec_query_fan_rpms();
                if (rpm < SAFE_FAN_RPM) {
                    // If RPM drops too low during step-down, go back to higher duty
                    ec_write_fan_duty(step + 20);
                    daemon_log(LOG_INFO, "Maintaining higher duty (%d%%) for stability", step + 20);
                    return EXIT_SUCCESS;
                }
            }
            return EXIT_SUCCESS;
        }
    }
    
    // If we get here, try one last emergency measure
    ec_write_fan_duty(100);
    daemon_log(LOG_ERR, "Fan recovery failed - setting to full speed for safety");
    return EXIT_FAILURE;
}

// Temperature trend tracking functions
static void add_temp_to_history(int temp) {
    temp_history[temp_history_index] = temp;
    temp_history_index = (temp_history_index + 1) % 10;
    if (temp_history_size < 10) {
        temp_history_size++;
    }
}

static bool is_temp_stuck(void) {
    if (temp_history_size < 5) {
        return false; // Need at least 5 readings to detect stuck
    }
    
    // Calculate the average temperature over the last few readings
    int sum = 0;
    for (int i = 0; i < temp_history_size; i++) {
        sum += temp_history[i];
    }
    double avg_temp = (double)sum / temp_history_size;
    
    // Check if all recent temperatures are within the stuck threshold of the average
    int stuck_count = 0;
    int identical_count = 0;
    int last_temp = temp_history[0];
    
    for (int i = 0; i < temp_history_size; i++) {
        if (fabs(temp_history[i] - avg_temp) <= stuck_temp_threshold) {
            stuck_count++;
        }
        if (temp_history[i] == last_temp) {
            identical_count++;
        }
        last_temp = temp_history[i];
    }
    
    // Consider temperature stuck if:
    // 1. Most readings are within threshold of average (standard check)
    // 2. OR we have several identical readings in a row (new check)
    // 3. OR temperature is high and not changing despite high fan speed
    bool stuck = (stuck_count >= temp_history_size * 0.6) || // Reduced from 0.8 to 0.6
                (identical_count >= temp_history_size * 0.8) ||
                (avg_temp > target_temperature + 5 && 
                 share_info->fan_duty > 70 && 
                 stuck_count >= temp_history_size * 0.5);
    
    if (stuck) {
        stuck_detection_counter++;
        if (debug_mode && stuck_detection_counter % 5 == 0) { // Increased frequency of debug logs
            daemon_log(LOG_DEBUG, "Temperature stuck detection: avg=%.1f°C, stuck_count=%d/%d, identical=%d/%d, counter=%d", 
                      avg_temp, stuck_count, temp_history_size, identical_count, temp_history_size, stuck_detection_counter);
        }
    } else {
        stuck_detection_counter = 0;
    }
    
    // Consider stuck if we've detected it for multiple cycles
    // Reduced threshold for faster detection
    return (stuck_detection_counter >= (stuck_threshold_cycles / 2));
}

static int get_aggressive_duty_for_error(int temp_error) {
    // Progressive escalation based on temperature error
    if (temp_error >= 12) {
        return 100; // Emergency: 100% duty for 12°C+ error
    } else if (temp_error >= 8) {
        return 95;  // Very high: 95% duty for 8-11°C error
    } else if (temp_error >= 5) {
        return 85;  // High: 85% duty for 5-7°C error
    } else if (temp_error >= 3) {
        return 75;  // Moderate: 75% duty for 3-4°C error
    } else if (temp_error >= 1) {
        return FAN_EMERGENCY_DUTY;  // Low: emergency duty for 1-2°C error
    } else {
        return 0;   // No escalation needed
    }
}

static bool validate_temperature_reading(int current_temp, int last_temp, const char* sensor_name) {
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
            daemon_log(LOG_WARNING, "Temperature appears stuck: %d°C unchanged for %d readings", 
                      current_temp, identical_reading_count);
            return false;
        }
    } else {
        identical_reading_count = 0;
    }
    
    if (temp_change > max_temp_change_per_cycle) {
        daemon_log(LOG_WARNING, "Suspicious %s temperature change: %d°C -> %d°C (change: %d°C)", 
                   sensor_name, last_temp, current_temp, temp_change);
        return false;
    }
    
    // Additional sanity checks
    if (current_temp < 0 || current_temp > 120) {
        daemon_log(LOG_WARNING, "Invalid %s temperature reading: %d°C (outside valid range 0-120°C)", 
                   sensor_name, current_temp);
        return false;
    }
    
    return true;
}

static int sanitize_temperature_reading(int current_temp, int last_temp, const char* sensor_name) {
    if (!temp_validation_enabled) {
        return current_temp;
    }
    
    static int invalid_reading_count = 0;
    static int last_valid_temp = 0;
    
    // First validate with alternative temperature sources
    if (!validate_ec_temp_with_alternative(current_temp)) {
        invalid_reading_count++;
        daemon_log(LOG_WARNING, "EC temperature failed validation with alternative sources");
    }
    
    if (validate_temperature_reading(current_temp, last_temp, sensor_name)) {
        invalid_reading_count = 0;
        last_valid_temp = current_temp;
        return current_temp;
    }
    
    invalid_reading_count++;
    
    // If we've had too many invalid readings, try to reset the EC
    if (invalid_reading_count >= 5) {
        daemon_log(LOG_WARNING, "Multiple invalid readings detected (%d in a row) - attempting EC reset", 
                  invalid_reading_count);
        
        // Try to "reset" the EC by cycling fan speeds
        ec_write_fan_duty(100); // Full speed
        usleep(500000);         // Wait 500ms
        ec_write_fan_duty(30);  // Low speed
        usleep(500000);         // Wait 500ms
        
        // Re-read temperature after reset attempt
        int new_temp = ec_query_cpu_temp();
        if (new_temp != current_temp) {
            daemon_log(LOG_INFO, "EC reset successful - new temperature reading: %d°C", new_temp);
            invalid_reading_count = 0;
            return new_temp;
        }
        
        // If still stuck, log error and return last known good value
        daemon_log(LOG_ERR, "EC reset failed - temperature still stuck at %d°C", current_temp);
    }
    
    // Use the last valid temperature, but with a slight bias towards cooling
    // This ensures we don't get stuck in a dangerous high-temperature state
    if (last_valid_temp > target_temperature) {
        return last_valid_temp + 2; // Bias towards more cooling when temp was high
    }
    
    return last_valid_temp;
}

// Alternative temperature reading functions
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

static int get_alternative_cpu_temp(void) {
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

// New function to get CPU temperature using standard Linux methods
static int get_cpu_temperature(void) {
    // Try coretemp first (most reliable for CPU temperature)
    int temp = read_temp_from_coretemp();
    if (temp > 0) {
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "Using coretemp sensor: %d°C", temp);
        }
        return temp;
    }
    
    // Fall back to other hwmon sources
    temp = read_temp_from_hwmon();
    if (temp > 0) {
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "Using hwmon sensor: %d°C", temp);
        }
        return temp;
    }
    
    // Fall back to thermal zones
    temp = read_temp_from_thermal_zone();
    if (temp > 0) {
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "Using thermal zone sensor: %d°C", temp);
        }
        return temp;
    }
    
    // Last resort: ACPI
    temp = read_temp_from_acpi();
    if (temp > 0) {
        if (debug_mode) {
            daemon_log(LOG_DEBUG, "Using ACPI sensor: %d°C", temp);
        }
        return temp;
    }
    
    // If all else fails, return -1
    daemon_log(LOG_WARNING, "No standard temperature sensors found");
    return -1;
}

static bool validate_ec_temp_with_alternative(int ec_temp) {
    static int validation_failures = 0;
    
    int alt_temp = get_alternative_cpu_temp();
    if (alt_temp == -1) {
        // No alternative source available, assume EC temp is valid
        return true;
    }
    
    int temp_diff = abs(ec_temp - alt_temp);
    
    // Allow some difference due to different sensor locations
    if (temp_diff <= 15) {
        validation_failures = 0;
        if (debug_mode && temp_diff > 5) {
            daemon_log(LOG_DEBUG, "Temperature validation: EC=%d°C, Alt=%d°C, diff=%d°C", 
                      ec_temp, alt_temp, temp_diff);
        }
        return true;
    }
    
    validation_failures++;
    daemon_log(LOG_WARNING, "Temperature validation failed: EC=%d°C, Alt=%d°C, diff=%d°C (failures=%d)", 
               ec_temp, alt_temp, temp_diff, validation_failures);
    
    // If we've had multiple validation failures, the EC sensor might be stuck
    if (validation_failures >= 3) {
        daemon_log(LOG_ERR, "Multiple temperature validation failures - EC sensor may be stuck");
        return false;
    }
    
    return true;
}

// Live stats implementation
static void live_stats_init(void) {
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
    live_stats_window = stdscr;
    live_stats_initialized = 1;
    
    // Clear screen and draw initial layout
    clear();
    refresh();
    
    // Handle window resize
    live_stats_handle_resize();
}

static void live_stats_handle_resize(void) {
    if (!live_stats_initialized) return;
    
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

static void live_stats_display(void) {
    if (!live_stats_initialized) return;
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // Check window size - need more space if debug mode is enabled
    int min_height = debug_mode ? 20 : 12;
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
    if (pid_enabled) {
        pid_error = (double)max_temp - (double)target_temperature;
        pid_p = pid_kp * pid_error;
        pid_i = pid_ki * pid_integral;
        pid_d = pid_kd * (pid_error - pid_prev_error);
    }
    
    // Draw header
    attron(COLOR_PAIR(5) | A_BOLD);
    mvprintw(0, 0, "+--- Clevo Fan Control Live Stats ");
    for (int i = 32; i < max_x - 2; i++) mvprintw(0, i, "-");
    mvprintw(0, max_x - 2, "+");
    attroff(COLOR_PAIR(5) | A_BOLD);
    
    // Draw header info
    attron(COLOR_PAIR(4));
    mvprintw(1, 2, "Target: %3d°C            ", target_temperature); // more padding
    mvprintw(1, 30, "Update: %5.0fms            ", live_stats_interval * 1000); // more padding
    mvprintw(1, 60, "PID: %-8s            ", pid_enabled ? "Enabled" : "Disabled"); // more padding
    if (debug_mode) {
        mvprintw(1, 85, "DEBUG: ON            ");
    }
    attroff(COLOR_PAIR(4));
    
    // Draw separator
    mvprintw(2, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(2, i, "-");
    mvprintw(2, max_x - 1, "+");
    
    // Temperature section - only update if changed
    if (cpu_temp != last_display_cpu_temp) {
        mvprintw(3, 2, "Temperature:                                ");
        // CPU temperature with color coding
        if (cpu_temp > target_temperature + 10) {
            attron(COLOR_PAIR(3));
        } else if (cpu_temp > target_temperature) {
            attron(COLOR_PAIR(2));
        } else {
            attron(COLOR_PAIR(1));
        }
        mvprintw(4, 4, "CPU: %3d°C            ", cpu_temp);
        attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3));
        last_display_cpu_temp = cpu_temp;
    }
    
    // Draw separator
    mvprintw(5, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(5, i, "-");
    mvprintw(5, max_x - 1, "+");
    
    // Fan control section - only update if changed
    if (fan_duty != last_display_fan_duty || fan_rpm != last_display_fan_rpm) {
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
        mvprintw(7, 56, "Health: %-5s            ", health_status); // pad to 5 chars
        last_display_fan_duty = fan_duty;
        last_display_fan_rpm = fan_rpm;
    }
    
    // Draw separator
    mvprintw(8, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(8, i, "-");
    mvprintw(8, max_x - 1, "+");
    
    // PID status section - only update if changed
    if (pid_enabled && (fabs(pid_error - last_display_pid_error) > 0.1 || 
                       fabs(pid_p - last_display_pid_p) > 0.1 ||
                       fabs(pid_i - last_display_pid_i) > 0.1 ||
                       fabs(pid_d - last_display_pid_d) > 0.1)) {
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
        last_display_pid_error = pid_error;
        last_display_pid_p = pid_p;
        last_display_pid_i = pid_i;
        last_display_pid_d = pid_d;
    } else if (!pid_enabled) {
        mvprintw(9, 2, "PID Status: Disabled                                ");
    }
    
    // Draw separator
    mvprintw(11, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(11, i, "-");
    mvprintw(11, max_x - 1, "+");
    
    // Status section
    mvprintw(12, 2, "Status: %-10s            ", share_info->auto_duty ? "Auto Mode" : "Manual Mode");
    
    // Stuck detection status
    const char* stuck_status = is_temp_stuck() ? "Yes" : "No";
    if (is_temp_stuck()) {
        attron(COLOR_PAIR(2));
    }
    mvprintw(12, 30, "Stuck: %-3s            ", stuck_status); // pad to 3 chars
    if (is_temp_stuck()) {
        attroff(COLOR_PAIR(2));
    }
    
    // Recovery attempts
    mvprintw(12, 56, "Recovery: %d/%d            ", fan_recovery_attempts, max_fan_recovery_attempts);
    
    // Draw footer
    mvprintw(13, 0, "+");
    for (int i = 1; i < max_x - 1; i++) mvprintw(13, i, "-");
    mvprintw(13, max_x - 1, "+");
    
    // Debug log area (only show if debug mode is enabled)
    if (debug_mode) {
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
    live_stats_handle_input();
    
    // Refresh display
    refresh();
}

static void live_stats_cleanup(void) {
    if (live_stats_initialized) {
        // Restore terminal
        curs_set(1);  // Show cursor
        endwin();
        live_stats_initialized = 0;
        live_stats_window = NULL;
    }
}

static void live_stats_handle_input(void) {
    // Handle input from the user
    int ch = getch();
    if (ch != ERR) {
        if (ch == 'q' || ch == 'Q') {
            running = 0;
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Received quit command from user input");
            }
        } else if (ch == 'r' || ch == 'R') {
            // Force refresh by clearing display cache
            last_display_cpu_temp = -1;
            last_display_fan_duty = -1;
            last_display_fan_rpm = -1;
            last_display_pid_error = -999.0;
            last_display_pid_p = -999.0;
            last_display_pid_i = -999.0;
            last_display_pid_d = -999.0;
            if (debug_mode) {
                daemon_log(LOG_DEBUG, "Received refresh command from user input");
            }
        } else if (debug_mode) {
            daemon_log(LOG_DEBUG, "Received input: %c (0x%02x)", ch, ch);
        }
    }
} 