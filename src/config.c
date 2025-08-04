#include "config.h"
#include "logging.h"
#include "fan_constants.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

clevo_config_t* config_init(void) {
    clevo_config_t* config = malloc(sizeof(clevo_config_t));
    if (!config) {
        return NULL;
    }
    
    // Set default values
    config->debug_mode = 0;
    config->quiet_mode = 0;
    config->log_level = LOG_INFO;
    
    config->daemon_mode = 0;
    config->status_interval = 2.0;
    config->target_temperature = 65;
    
    config->pid_enabled = 1;
    config->pid_kp = 4.0;
    config->pid_ki = 0.3;
    config->pid_kd = 1.0;
    
    config->adaptive_pid_enabled = 0;
    config->adaptive_tuning_interval = 30;
    config->adaptive_target_performance = 0.8;
    
    config->max_duty_change_rate = 25;  // Increased from 15 - softer overall change limit
    config->max_duty_increase_rate = 15;  // Increased from 10 - allow faster response to heat
    config->max_duty_decrease_rate = 20;  // Reduced from 30 - prevent sudden drops that cause stall
    config->fan_health_check_interval = 30;
    
    config->temp_validation_enabled = 1;
    config->max_temp_change_per_cycle = 10;
    
    config->live_stats_mode = 0;
    config->live_stats_interval = 0.1;
    
    config->min_fan_duty = FAN_MIN_DUTY;
    config->min_fan_rpm = FAN_MIN_RPM;
    config->safe_fan_rpm = FAN_SAFE_RPM;
    config->emergency_duty = FAN_EMERGENCY_DUTY;
    
    return config;
}

int config_load_from_file(clevo_config_t* config, const char* filename) {
    // TODO: Implement configuration file loading
    // For now, just return success
    return 0;
}

int config_parse_args(clevo_config_t* config, int argc, char* argv[]) {
    static struct option long_options[] = {
        {"debug",        no_argument,       0, 'd'},
        {"interval",     required_argument, 0, 'i'},
        {"target-temp",  required_argument, 0, 't'},
        {"daemon",       no_argument,       0, 'D'},
        {"pid-enabled",  required_argument, 0, 'p'},
        {"adaptive-pid", required_argument, 0, 'a'},
        {"adaptive-tuning-interval", required_argument, 0, 'A'},
        {"adaptive-target-performance", required_argument, 0, 'P'},
        {"fan-health-check", required_argument, 0, 'f'},
        {"max-duty-change", required_argument, 0, 'm'},
        {"temp-validation", required_argument, 0, 'v'},
        {"max-temp-change", required_argument, 0, 'T'},
        {"live-stats",   no_argument,       0, 'L'},
        {"help",         no_argument,       0, 'h'},
        {"max-increase-rate", required_argument, 0, 0x100},
        {"max-decrease-rate", required_argument, 0, 0x101},
        {"quiet", no_argument, 0, 'q'},
        {"log-level", required_argument, 0, 0x200},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "di:t:Dp:a:A:P:f:s:m:v:T:Lh?q", long_options, &option_index)) != -1) {
        switch (c) {
            case 'd':
                config->debug_mode = 1;
                config->log_level = LOG_DEBUG;
                break;
            case 'i':
                config->status_interval = atof(optarg);
                if (config->status_interval < 0.1 || config->status_interval > 60.0) {
                    printf("Invalid interval: %.1f (must be 0.1-60.0 seconds)\n", config->status_interval);
                    return -1;
                }
                break;
            case 't':
                config->target_temperature = atoi(optarg);
                if (config->target_temperature < 40 || config->target_temperature > 100) {
                    printf("Invalid target temperature: %d (must be 40-100°C)\n", config->target_temperature);
                    return -1;
                }
                break;
            case 'D':
                config->daemon_mode = 1;
                break;
            case 'p':
                config->pid_enabled = atoi(optarg);
                break;
            case 'a':
                config->adaptive_pid_enabled = atoi(optarg);
                break;
            case 'A':
                config->adaptive_tuning_interval = atoi(optarg);
                if (config->adaptive_tuning_interval < 10) config->adaptive_tuning_interval = 10;
                if (config->adaptive_tuning_interval > 300) config->adaptive_tuning_interval = 300;
                break;
            case 'P':
                config->adaptive_target_performance = atof(optarg);
                if (config->adaptive_target_performance < 0.1) config->adaptive_target_performance = 0.1;
                if (config->adaptive_target_performance > 1.0) config->adaptive_target_performance = 1.0;
                break;
            case 'f':
                config->fan_health_check_interval = atoi(optarg);
                if (config->fan_health_check_interval < 10) config->fan_health_check_interval = 10;
                if (config->fan_health_check_interval > 300) config->fan_health_check_interval = 300;
                break;
            case 's':
                printf("Warning: --fan-stuck-threshold option is deprecated and ignored\n");
                break;
            case 'm':
                config->max_duty_change_rate = atoi(optarg);
                if (config->max_duty_change_rate < 1 || config->max_duty_change_rate > 100) {
                    printf("Invalid max duty change rate: %d (must be 1-100%%)\n", config->max_duty_change_rate);
                    return -1;
                }
                break;
            case 'v':
                config->temp_validation_enabled = atoi(optarg);
                break;
            case 'T':
                config->max_temp_change_per_cycle = atoi(optarg);
                if (config->max_temp_change_per_cycle < 1 || config->max_temp_change_per_cycle > 50) {
                    printf("Invalid max temperature change: %d (must be 1-50°C)\n", config->max_temp_change_per_cycle);
                    return -1;
                }
                break;
            case 'L':
                config->live_stats_mode = 1;
                break;
            case 'h':
            case '?':
                config_print_help();
                return 1; // Special return code for help
            case 0x100: // --max-increase-rate
                config->max_duty_increase_rate = atoi(optarg);
                if (config->max_duty_increase_rate < 1 || config->max_duty_increase_rate > 100) {
                    printf("Invalid max increase rate: %d (must be 1-100%%)\n", config->max_duty_increase_rate);
                    return -1;
                }
                break;
            case 0x101: // --max-decrease-rate
                config->max_duty_decrease_rate = atoi(optarg);
                if (config->max_duty_decrease_rate < 1 || config->max_duty_decrease_rate > 100) {
                    printf("Invalid max decrease rate: %d (must be 1-100%%)\n", config->max_duty_decrease_rate);
                    return -1;
                }
                break;
            case 'q':
                config->quiet_mode = 1;
                config->log_level = LOG_ERR;
                break;
            case 0x200: // --log-level
                if (strcmp(optarg, "error") == 0) {
                    config->log_level = LOG_ERR;
                } else if (strcmp(optarg, "warning") == 0) {
                    config->log_level = LOG_WARNING;
                } else if (strcmp(optarg, "info") == 0) {
                    config->log_level = LOG_INFO;
                } else if (strcmp(optarg, "debug") == 0) {
                    config->log_level = LOG_DEBUG;
                } else {
                    printf("Invalid log level: %s (must be error, warning, info, or debug)\n", optarg);
                    return -1;
                }
                break;
            default:
                printf("Unknown option: %c\n", c);
                return -1;
        }
    }
    
    return 0;
}

int config_validate(const clevo_config_t* config) {
    if (!config) return -1;
    
    // Validate temperature range
    if (config->target_temperature < 40 || config->target_temperature > 100) {
        return -1;
    }
    
    // Validate interval range
    if (config->status_interval < 0.1 || config->status_interval > 60.0) {
        return -1;
    }
    
    // Validate PID parameters
    if (config->pid_kp < 0 || config->pid_ki < 0 || config->pid_kd < 0) {
        return -1;
    }
    
    return 0;
}

void config_print_help(void) {
    printf(
        "\n"
        "Usage: clevo-daemon [OPTIONS] [fan-duty-percentage|target-temperature]\n"
        "\n"
        "Headless fan control daemon for Clevo laptops.\n"
        "\n"
        "Options:\n"
        "  -d, --debug\t\tEnable debug output (prevents daemonization)\n"
        "  -i, --interval <sec>\tSet status update interval (0.1-60.0 seconds, default: 2.0)\n"
        "  -t, --target-temp <°C>\tSet the target temperature for auto fan control (40-100°C, default: 65)\n"
        "  -D, --daemon\t\tExplicitly run in daemon mode (default behavior)\n"
        "  -p, --pid-enabled <0|1>\tEnable/Disable PID control (default: 1)\n"
        "  -a, --adaptive-pid <0|1>\tEnable/Disable adaptive PID tuning (default: 0)\n"
        "  -A, --adaptive-tuning-interval <sec>\tSet adaptive tuning interval (10-300s, default: 30)\n"
        "  -P, --adaptive-target-performance <value>\tSet target performance score (0.1-1.0, default: 0.8)\n"
        "  -f, --fan-health-check <sec>\tSet fan health check interval (10-300s, default: 30)\n"
        "  -m, --max-duty-change <%%>\tSet the maximum duty change per cycle (1-100, default: 15)\n"
        "  -v, --temp-validation <0|1>\tEnable/Disable temperature validation (default: 1)\n"
        "  -T, --max-temp-change <°C>\tSet max temperature change per cycle (1-50°C, default: 10)\n"
        "  -L, --live-stats\tEnable live statistics display (prevents daemonization)\n"
        "  -h, -?, --help\tDisplay this help and exit\n"
        "  --max-increase-rate <%%>   Set max fan duty increase per cycle (1-100, default: 10)\n"
        "  --max-decrease-rate <%%>   Set max fan duty decrease per cycle (1-100, default: 30)\n"
        "  -q, --quiet                Suppress all logging except errors\n"
        "  --log-level LEVEL          Set log level: error, warning, info, debug\n"
        "\n"
        "Modes:\n"
        "  Daemon Mode (default):\n"
        "    - No arguments: Run daemon with default target temperature (65°C)\n"
        "    - --target-temp N: Run daemon with target temperature N°C\n"
        "    - --daemon: Explicitly run in daemon mode\n"
        "    - Temperature argument (40-100): Run daemon with that target temperature\n"
        "\n"
        "  CLI Mode:\n"
        "    - Fan duty argument (1-100): Set fan to that percentage and exit\n"
        "\n"
        "Examples:\n"
        "  ./clevo-daemon                    # Daemon mode, target 65°C\n"
        "  ./clevo-daemon --target-temp 55   # Daemon mode, target 55°C\n"
        "  ./clevo-daemon 55                 # Daemon mode, target 55°C\n"
        "  ./clevo-daemon 50                 # CLI mode, set fan to 50%%\n"
        "  ./clevo-daemon --debug            # Daemon mode with debug output\n"
        "  ./clevo-daemon --live-stats       # Live statistics display\n"
        "\n"
    );
}

void config_cleanup(clevo_config_t* config) {
    if (config) {
        free(config);
    }
} 