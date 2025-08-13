/*
 * Clevo Fan Control Daemon - Modular Version
 * 
 * This is the main entry point that orchestrates all the modular components:
 * - Configuration management
 * - Logging system
 * - Hardware interface (EC)
 * - Temperature monitoring
 * - PID control
 * - Fan health monitoring
 * - Live statistics display
 * - Daemon functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// Module includes
#include "config.h"
#include "logging.h"
#include "utils.h"
#include "ec_interface.h"
#include "temperature_monitor.h"
#include "pid_controller.h"
#include "fan_health.h"
#include "daemon.h"
#include "live_stats.h"
// UDS removed
#include "privilege_manager.h"
#include "fan_constants.h"

// Global module instances
static clevo_config_t* config = NULL;
static live_stats_t* live_stats = NULL;
static ec_interface_t* ec = NULL;
static temperature_monitor_t* temp_monitor = NULL;
static pid_controller_t* pid_controller = NULL;
static fan_health_monitor_t* fan_health = NULL;
static daemon_context_t* daemon_ctx = NULL;

// Signal handler
static void signal_handler(int signum) {
    logging_info("Received signal %s, shutting down", strsignal(signum));
    
    if (daemon_ctx) {
        daemon_stop(daemon_ctx);
    }
    
    // Clean up live stats if enabled
    if (live_stats) {
        live_stats_cleanup(live_stats);
    }
    
    // UDS removed
    
    // Force immediate exit to avoid waiting for sleep
    exit(EXIT_SUCCESS);
}

// Initialize all modules
static int initialize_modules(void) {
    // Initialize utilities
    if (utils_init() != 0) {
        printf("Failed to initialize utilities\n");
        return -1;
    }
    
    // Initialize configuration
    config = config_init();
    if (!config) {
        printf("Failed to initialize configuration\n");
        return -1;
    }
    
    // Initialize logging
    if (logging_init("clevo-daemon", config->log_level, config->quiet_mode, config->debug_mode) != 0) {
        printf("Failed to initialize logging\n");
        return -1;
    }
    
    // Initialize daemon context
    daemon_ctx = daemon_init(signal_handler);
    if (!daemon_ctx) {
        logging_error("Failed to initialize daemon context");
        return -1;
    }
    
    // Initialize shared memory
    if (daemon_init_shared_memory(daemon_ctx) != 0) {
        logging_error("Failed to initialize shared memory");
        return -1;
    }
    
    // Initialize EC interface
    if (ec_init() != 0) {
        logging_error("Failed to initialize EC interface");
        return -1;
    }
    
    // Initialize temperature monitor
    temp_monitor = temperature_monitor_init(config->max_temp_change_per_cycle, 
                                         config->temp_validation_enabled);
    if (!temp_monitor) {
        logging_error("Failed to initialize temperature monitor");
        return -1;
    }
    
    // Initialize PID controller
    pid_controller = pid_controller_init(config->pid_kp, config->pid_ki, config->pid_kd,
                                       config->max_duty_change_rate, config->max_duty_increase_rate,
                                       config->max_duty_decrease_rate, config->min_fan_duty);
    if (!pid_controller) {
        logging_error("Failed to initialize PID controller");
        return -1;
    }
    
    // Set up adaptive PID tuning if enabled
    if (config->adaptive_pid_enabled) {
        pid_controller_set_adaptive_tuning(pid_controller, true,
                                         config->adaptive_tuning_interval,
                                         config->adaptive_target_performance);
    }
    
    // Initialize fan health monitor
    fan_health = fan_health_init(config->min_fan_rpm, config->safe_fan_rpm,
                                config->emergency_duty, 40, config->fan_health_check_interval);
    if (!fan_health) {
        logging_error("Failed to initialize fan health monitor");
        return -1;
    }
    
    // Initialize live stats if enabled
    if (config->live_stats_mode) {
        live_stats = live_stats_init(config->live_stats_interval);
        if (!live_stats) {
            logging_error("Failed to initialize live stats");
            return -1;
        }
    }
    
    // UDS removed
    
    logging_info("All modules initialized successfully");
    return 0;
}

// Clean up all modules
static void cleanup_modules(void) {
    logging_info("Cleaning up modules...");
    
    // UDS removed
    
    // Clean up modules in reverse order
    if (live_stats) {
        live_stats_cleanup(live_stats);
        live_stats = NULL;
    }
    
    if (fan_health) {
        fan_health_cleanup(fan_health);
        fan_health = NULL;
    }
    
    if (pid_controller) {
        pid_controller_cleanup(pid_controller);
        pid_controller = NULL;
    }
    
    if (temp_monitor) {
        temperature_monitor_cleanup(temp_monitor);
        temp_monitor = NULL;
    }
    
    ec_cleanup();
    
    if (daemon_ctx) {
        daemon_cleanup(daemon_ctx);
        daemon_ctx = NULL;
    }
    
    logging_cleanup();
    
    if (config) {
        config_cleanup(config);
        config = NULL;
    }
    
    utils_cleanup();
}

// Main worker function
static int worker_loop(void) {
    int cpu_temp = 0;
    int fan_duty = 0;
    int fan_rpm = 0;
    int auto_duty = 1;
    int auto_duty_val = 0;
    
    logging_info("Starting fan control daemon with target temperature %d°C", config->target_temperature);
    
    while (daemon_is_running(daemon_ctx)) {
        // Read current fan data
        fan_duty = ec_query_fan_duty();
        fan_rpm = ec_query_fan_rpms();
        
        // Get CPU temperature
        cpu_temp = temperature_monitor_get_cpu_temp(temp_monitor);
        if (cpu_temp > 0) {
            temperature_monitor_add_to_history(temp_monitor, cpu_temp);
        }
        
        // Check fan health
        fan_health_check(fan_health, fan_duty, fan_rpm);
        
        // Auto fan control
        if (auto_duty && cpu_temp > 0) {
            int next_duty = pid_controller_calculate_duty(pid_controller, cpu_temp, 
                                                        config->target_temperature, 
                                                        fan_duty, fan_rpm);
            
            // Emergency bypass: Always write if we're in emergency mode, even if duty hasn't changed
            int temp_error = cpu_temp - config->target_temperature;
            bool emergency_mode = (temp_error >= 10) || (temp_error >= 5 && next_duty >= 80);
            
            if (next_duty != 0 && (next_duty != auto_duty_val || emergency_mode)) {
                char time_str[256];
                get_time_string(time_str, 256, "%m/%d %H:%M:%S");
                logging_telemetry("%s CPU=%d°C, auto fan duty to %d%%", time_str, cpu_temp, next_duty);
                
                if (ec_write_fan_duty(next_duty) == 0) {
                    auto_duty_val = next_duty;
                }
            }
        }
        
        // Update shared memory
        daemon_update_shared_memory(daemon_ctx, cpu_temp, fan_duty, fan_rpm, auto_duty, auto_duty_val);
        
        // Update live stats display if enabled
        if (live_stats && live_stats_is_initialized(live_stats)) {
            // Calculate PID values for display
            double pid_error = 0.0, pid_p = 0.0, pid_i = 0.0, pid_d = 0.0;
            if (config->pid_enabled && cpu_temp > 0) {
                pid_error = (double)cpu_temp - (double)config->target_temperature;
                // Note: PID terms would need to be exposed from pid_controller module
                // For now, we'll use placeholder values
                pid_p = pid_error * config->pid_kp;
                pid_i = 0.0; // Would need to be exposed from pid_controller
                pid_d = 0.0; // Would need to be exposed from pid_controller
            }
            
            live_stats_display(live_stats, cpu_temp, fan_duty, fan_rpm,
                             config->target_temperature, config->pid_enabled,
                             pid_error, pid_p, pid_i, pid_d, auto_duty,
                             temperature_monitor_is_stuck(temp_monitor),
                             0, 3, config->debug_mode); // Placeholder recovery values
        }
        
        // Sleep for the configured interval
        usleep((int)(config->status_interval * 1000000));
    }
    
    return 0;
}

// CLI mode for testing fan
static int cli_mode(int fan_duty) {
    printf("Testing fan duty: %d%%\n", fan_duty);
    
    if (ec_test_fan(fan_duty) != 0) {
        printf("Failed to test fan\n");
        return -1;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    printf("Clevo Fan Control Daemon (Modular Version)\n");
    
    // Initialize modules
    if (initialize_modules() != 0) {
        printf("Failed to initialize modules\n");
        return EXIT_FAILURE;
    }
    
    // Parse command line arguments
    int parse_result = config_parse_args(config, argc, argv);
    if (parse_result == 1) {
        // Help requested
        cleanup_modules();
        return EXIT_SUCCESS;
    } else if (parse_result != 0) {
        printf("Failed to parse command line arguments\n");
        cleanup_modules();
        return EXIT_FAILURE;
    }
    
    // Check for multiple instances
    if (check_proc_instances("clevo-daemon") > 1) {
        printf("Multiple running instances!\n");
        cleanup_modules();
        return EXIT_FAILURE;
    }
    
    // Setup privileges
    if (!setup_privileges()) {
        printf("Failed to setup privileges for EC access\n");
        cleanup_modules();
        return EXIT_FAILURE;
    }
    
    // Check for remaining arguments after option processing
    int fan_duty_arg = -1;
    if (optind < argc) {
        fan_duty_arg = optind;
    }
    
    // Set up signal handlers
    daemon_setup_signals(daemon_ctx);
    
    // Determine mode and run
    if (fan_duty_arg == -1 || config->daemon_mode) {
        // Daemon mode
        if (!config->debug_mode && !config->live_stats_mode) {
            if (daemonize() != 0) {
                logging_error("Failed to daemonize");
                cleanup_modules();
                return EXIT_FAILURE;
            }
        }
        
        // Run the main worker loop
        int result = worker_loop();
        
        logging_info("Daemon stopped");
        cleanup_modules();
        return result;
    } else {
        // CLI mode
        int val = atoi(argv[fan_duty_arg]);
        
        if (val >= 1 && val <= 100) {
            // Fan duty test mode
            int result = cli_mode(val);
            cleanup_modules();
            return result;
        } else if (val >= 40 && val <= 100) {
            // Target temperature mode
            config->target_temperature = val;
            
            if (!config->debug_mode && !config->live_stats_mode) {
                if (daemonize() != 0) {
                    logging_error("Failed to daemonize");
                    cleanup_modules();
                    return EXIT_FAILURE;
                }
            }
            
            int result = worker_loop();
            cleanup_modules();
            return result;
        } else {
            printf("Invalid argument: %s\n", argv[fan_duty_arg]);
            printf("For fan duty (CLI mode): must be 1-100\n");
            printf("For target temperature (daemon mode): must be 40-100°C\n");
            cleanup_modules();
            return EXIT_FAILURE;
        }
    }
} 