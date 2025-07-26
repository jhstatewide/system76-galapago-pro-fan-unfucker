#ifndef CONFIG_H
#define CONFIG_H

/**
 * Configuration structure containing all program settings
 */
typedef struct {
    // Debug and logging settings
    int debug_mode;
    int quiet_mode;
    int log_level;
    
    // Daemon settings
    int daemon_mode;
    double status_interval;
    int target_temperature;
    
    // PID controller settings
    int pid_enabled;
    double pid_kp;
    double pid_ki;
    double pid_kd;
    
    // Adaptive PID settings
    int adaptive_pid_enabled;
    int adaptive_tuning_interval;
    double adaptive_target_performance;
    
    // Fan control settings
    int max_duty_change_rate;
    int max_duty_increase_rate;
    int max_duty_decrease_rate;
    int fan_health_check_interval;
    
    // Temperature validation settings
    int temp_validation_enabled;
    int max_temp_change_per_cycle;
    
    // Live stats settings
    int live_stats_mode;
    double live_stats_interval;
    
    // Fan safety thresholds
    int min_fan_duty;
    int min_fan_rpm;
    int safe_fan_rpm;
    int emergency_duty;
} clevo_config_t;

/**
 * Initialize configuration with default values
 * @return Pointer to configuration structure
 */
clevo_config_t* config_init(void);

/**
 * Load configuration from file
 * @param config Configuration structure
 * @param filename Configuration file path
 * @return 0 on success, -1 on failure
 */
int config_load_from_file(clevo_config_t* config, const char* filename);

/**
 * Parse command line arguments and update configuration
 * @param config Configuration structure
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, -1 on failure
 */
int config_parse_args(clevo_config_t* config, int argc, char* argv[]);

/**
 * Validate configuration settings
 * @param config Configuration structure
 * @return 0 if valid, -1 if invalid
 */
int config_validate(const clevo_config_t* config);

/**
 * Print configuration help
 */
void config_print_help(void);

/**
 * Clean up configuration
 * @param config Configuration structure
 */
void config_cleanup(clevo_config_t* config);

#endif // CONFIG_H 