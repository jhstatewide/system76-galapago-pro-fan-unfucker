/*
 ============================================================================
 Name        : clevo-client.c
 Author      : System76 Fan Control Client
 Version     : 1.0
 Description : Modern client for Clevo fan control daemon

 This client provides a command-line interface to interact with the clevo-daemon
 using Unix domain sockets for efficient local communication.

 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <ncurses.h>

#define SOCKET_PATH "/run/clevo-daemon.sock"
#define BUFFER_SIZE 1024
#define MAX_RETRIES 3

typedef enum {
    CMD_STATUS,
    CMD_MONITOR,
    CMD_SET_FAN,
    CMD_SET_AUTO,
    CMD_SET_TARGET_TEMP,
    CMD_GET_TEMP,
    CMD_GET_FAN,
    CMD_TEMP_MONITOR,
    CMD_SET_MAX_DUTY_CHANGE,
    CMD_GET_MAX_DUTY_CHANGE,
    CMD_SET_MAX_DUTY_INCREASE,
    CMD_GET_MAX_DUTY_INCREASE,
    CMD_SET_MAX_DUTY_DECREASE,
    CMD_GET_MAX_DUTY_DECREASE,
    CMD_LIVE_STATS,
    CMD_HELP
} CommandType;

typedef struct {
    CommandType type;
    int fan_duty;
    int target_temperature;
    int max_duty_change_rate;
    int max_duty_increase_rate;
    int max_duty_decrease_rate;
    double monitor_interval;
    int verbose;
    int json_output;
    int live_stats_mode;
} ClientConfig;

static ClientConfig config = {0};
static volatile int running = 1;

// Function declarations
static int connect_to_daemon(void);
static int send_command(int sock, const char* command);
static int receive_response(int sock, char* buffer, size_t size);
static void print_status(const char* response);
static void print_help(void);
static void signal_handler(int sig);
static void monitor_loop(int sock);
static void parse_arguments(int argc, char* argv[]);
static int format_json_status(const char* response, char* json_buffer, size_t size);

// Live stats variables
static WINDOW* live_stats_window = NULL;
static int live_stats_initialized = 0;
static int last_display_cpu_temp = -1;
static int last_display_gpu_temp = -1;
static int last_display_fan_duty = -1;
static int last_display_fan_rpm = -1;

// Live stats function declarations
static void live_stats_init(void);
static int live_stats_display(int sock);
static void live_stats_cleanup(void);
static void live_stats_handle_resize(void);
static void live_stats_handle_input(void);

int main(int argc, char* argv[]) {
    printf("Clevo Fan Control Client v1.0\n");
    
    // Parse command line arguments
    parse_arguments(argc, argv);
    
    // Handle help command without connecting
    if (config.type == CMD_HELP) {
        print_help();
        return EXIT_SUCCESS;
    }
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Connect to daemon
    int sock = connect_to_daemon();
    if (sock < 0) {
        fprintf(stderr, "Failed to connect to daemon. Is clevo-daemon running?\n");
        return EXIT_FAILURE;
    }
    
    // Handle different commands
    switch (config.type) {
        case CMD_STATUS:
            {
                char command[64];
                snprintf(command, sizeof(command), "STATUS");
                if (send_command(sock, command) == 0) {
                    char response[BUFFER_SIZE];
                    if (receive_response(sock, response, sizeof(response)) == 0) {
                        if (config.json_output) {
                            char json_buffer[BUFFER_SIZE];
                            if (format_json_status(response, json_buffer, sizeof(json_buffer)) == 0) {
                                printf("%s\n", json_buffer);
                            }
                        } else {
                            print_status(response);
                        }
                    }
                }
            }
            break;
            
        case CMD_MONITOR:
            monitor_loop(sock);
            break;
            
        case CMD_SET_FAN:
            {
                char command[64];
                snprintf(command, sizeof(command), "SET_FAN %d", config.fan_duty);
                if (send_command(sock, command) == 0) {
                    char response[BUFFER_SIZE];
                    if (receive_response(sock, response, sizeof(response)) == 0) {
                        printf("Response: %s\n", response);
                    }
                }
            }
            break;
            
        case CMD_SET_AUTO:
            {
                char command[64];
                snprintf(command, sizeof(command), "SET_AUTO");
                if (send_command(sock, command) == 0) {
                    char response[BUFFER_SIZE];
                    if (receive_response(sock, response, sizeof(response)) == 0) {
                        printf("Response: %s\n", response);
                    }
                }
            }
            break;
            
        case CMD_SET_TARGET_TEMP:
            {
                char command[64];
                snprintf(command, sizeof(command), "SET_TARGET_TEMP %d", config.target_temperature);
                if (send_command(sock, command) == 0) {
                    char response[BUFFER_SIZE];
                    if (receive_response(sock, response, sizeof(response)) == 0) {
                        printf("Response: %s\n", response);
                    }
                }
            }
            break;
            
        case CMD_GET_TEMP:
            {
                char command[64];
                snprintf(command, sizeof(command), "GET_TEMP");
                if (send_command(sock, command) == 0) {
                    char response[BUFFER_SIZE];
                    if (receive_response(sock, response, sizeof(response)) == 0) {
                        int cpu_temp, gpu_temp;
                        if (sscanf(response, "CPU:%d GPU:%d", &cpu_temp, &gpu_temp) == 2) {
                            printf("Current Temperatures:\n");
                            printf("  CPU: %d°C\n", cpu_temp);
                            printf("  GPU: %d°C\n", gpu_temp);
                            
                            // Temperature status
                            int max_temp = (cpu_temp > gpu_temp) ? cpu_temp : gpu_temp;
                            if (max_temp >= 80) {
                                printf("  Status: \033[31mCRITICAL\033[0m (Consider reducing load)\n");
                            } else if (max_temp >= 70) {
                                printf("  Status: \033[33mHIGH\033[0m (Monitor closely)\n");
                            } else if (max_temp >= 60) {
                                printf("  Status: \033[36mWARM\033[0m (Normal under load)\n");
                            } else {
                                printf("  Status: \033[32mNORMAL\033[0m (Good)\n");
                            }
                        } else {
                            printf("Temperature: %s\n", response);
                        }
                    }
                }
            }
            break;
            
        case CMD_TEMP_MONITOR:
            {
                printf("Temperature Monitor - Press Ctrl+C to exit\n");
                printf("Time\t\tCPU\tGPU\tStatus\n");
                printf("----\t\t---\t---\t------\n");
                
                while (running) {
                    char command[64];
                    snprintf(command, sizeof(command), "GET_TEMP");
                    if (send_command(sock, command) == 0) {
                        char response[BUFFER_SIZE];
                        if (receive_response(sock, response, sizeof(response)) == 0) {
                            int cpu_temp, gpu_temp;
                            if (sscanf(response, "CPU:%d GPU:%d", &cpu_temp, &gpu_temp) == 2) {
                                time_t now = time(NULL);
                                struct tm *tm_info = localtime(&now);
                                char time_str[20];
                                strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
                                
                                // Determine status color and message
                                const char* status_color = "";
                                const char* status_msg = "";
                                int max_temp = (cpu_temp > gpu_temp) ? cpu_temp : gpu_temp;
                                
                                if (max_temp >= 80) {
                                    status_color = "\033[31m";  // Red
                                    status_msg = "CRITICAL";
                                } else if (max_temp >= 70) {
                                    status_color = "\033[33m";  // Yellow
                                    status_msg = "HIGH";
                                } else if (max_temp >= 60) {
                                    status_color = "\033[36m";  // Cyan
                                    status_msg = "WARM";
                                } else {
                                    status_color = "\033[32m";  // Green
                                    status_msg = "NORMAL";
                                }
                                
                                printf("%s\t%d°C\t%d°C\t%s%s\033[0m\n", 
                                       time_str, cpu_temp, gpu_temp, status_color, status_msg);
                            }
                        }
                    }
                    usleep((int)(config.monitor_interval * 1000000));
                }
            }
            break;
            
        case CMD_GET_FAN:
            {
                char command[64];
                snprintf(command, sizeof(command), "GET_FAN");
                if (send_command(sock, command) == 0) {
                    char response[BUFFER_SIZE];
                    if (receive_response(sock, response, sizeof(response)) == 0) {
                        printf("Fan: %s\n", response);
                    }
                }
            }
            break;
            
        case CMD_SET_MAX_DUTY_CHANGE:
            {
                char command[64];
                snprintf(command, sizeof(command), "SET_MAX_DUTY_CHANGE %d", config.max_duty_change_rate);
                if (send_command(sock, command) == 0) {
                    char response[BUFFER_SIZE];
                    if (receive_response(sock, response, sizeof(response)) == 0) {
                        printf("Response: %s\n", response);
                    }
                }
            }
            break;
            
        case CMD_GET_MAX_DUTY_CHANGE:
            {
                char command[64];
                snprintf(command, sizeof(command), "GET_MAX_DUTY_CHANGE");
                if (send_command(sock, command) == 0) {
                    char response[BUFFER_SIZE];
                    if (receive_response(sock, response, sizeof(response)) == 0) {
                        int rate;
                        if (sscanf(response, "MAX_DUTY_CHANGE:%d", &rate) == 1) {
                            printf("Current max duty change rate: %d%%\n", rate);
                        } else {
                            printf("Response: %s\n", response);
                        }
                    }
                }
            }
            break;
            
        case CMD_SET_MAX_DUTY_INCREASE: {
            char command[64];
            snprintf(command, sizeof(command), "SET_MAX_DUTY_INCREASE %d", config.max_duty_increase_rate);
            if (send_command(sock, command) == 0) {
                char response[BUFFER_SIZE];
                if (receive_response(sock, response, sizeof(response)) == 0) {
                    printf("Response: %s\n", response);
                }
            }
            break;
        }
        case CMD_GET_MAX_DUTY_INCREASE: {
            char command[64];
            snprintf(command, sizeof(command), "GET_MAX_DUTY_INCREASE");
            if (send_command(sock, command) == 0) {
                char response[BUFFER_SIZE];
                if (receive_response(sock, response, sizeof(response)) == 0) {
                    int rate;
                    if (sscanf(response, "MAX_DUTY_INCREASE:%d", &rate) == 1) {
                        printf("Current max duty increase rate: %d%%\n", rate);
                    } else {
                        printf("Response: %s\n", response);
                    }
                }
            }
            break;
        }
        case CMD_SET_MAX_DUTY_DECREASE: {
            char command[64];
            snprintf(command, sizeof(command), "SET_MAX_DUTY_DECREASE %d", config.max_duty_decrease_rate);
            if (send_command(sock, command) == 0) {
                char response[BUFFER_SIZE];
                if (receive_response(sock, response, sizeof(response)) == 0) {
                    printf("Response: %s\n", response);
                }
            }
            break;
        }
        case CMD_GET_MAX_DUTY_DECREASE: {
            char command[64];
            snprintf(command, sizeof(command), "GET_MAX_DUTY_DECREASE");
            if (send_command(sock, command) == 0) {
                char response[BUFFER_SIZE];
                if (receive_response(sock, response, sizeof(response)) == 0) {
                    int rate;
                    if (sscanf(response, "MAX_DUTY_DECREASE:%d", &rate) == 1) {
                        printf("Current max duty decrease rate: %d%%\n", rate);
                    } else {
                        printf("Response: %s\n", response);
                    }
                }
            }
            break;
        }
        case CMD_LIVE_STATS:
            live_stats_init();
            while (running) {
                int display_status = live_stats_display(sock);
                
                // Check if we need to reconnect
                if (display_status == -2) { // -2 indicates broken pipe
                    // Connection lost, try to reconnect
                    close(sock);
                    sock = connect_to_daemon();
                    if (sock < 0) {
                        // Failed to reconnect, show error and exit
                        live_stats_cleanup();
                        printf("Failed to reconnect to daemon. Exiting...\n");
                        return EXIT_FAILURE;
                    }
                }
                
                usleep((int)(config.monitor_interval * 1000000));
            }
            live_stats_cleanup();
            break;
            
        case CMD_HELP:
        default:
            print_help();
            break;
    }
    
    close(sock);
    return EXIT_SUCCESS;
}

static int connect_to_daemon(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    
    return sock;
}

static int send_command(int sock, const char* command) {
    ssize_t sent = send(sock, command, strlen(command), MSG_NOSIGNAL);
    if (sent < 0) {
        if (errno == EPIPE) {
            // Connection was closed by daemon
            return -2; // Special error code for broken pipe
        }
        perror("send");
        return -1;
    }
    return 0;
}

static int receive_response(int sock, char* buffer, size_t size) {
    ssize_t received = recv(sock, buffer, size - 1, 0);
    if (received < 0) {
        perror("recv");
        return -1;
    }
    buffer[received] = '\0';
    return 0;
}

static void print_status(const char* response) {
    // Parse response format: "CPU:XX GPU:XX FAN_DUTY:XX FAN_RPM:XX AUTO:XX"
    int cpu_temp, gpu_temp, fan_duty, fan_rpm, auto_mode;
    
    if (sscanf(response, "CPU:%d GPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d", 
                &cpu_temp, &gpu_temp, &fan_duty, &fan_rpm, &auto_mode) == 5) {
        
        printf("\n=== Clevo Fan Control Status ===\n");
        
        // Temperature section with color coding
        printf("Temperatures:\n");
        const char* cpu_color = (cpu_temp >= 80) ? "\033[31m" : 
                               (cpu_temp >= 70) ? "\033[33m" : 
                               (cpu_temp >= 60) ? "\033[36m" : "\033[32m";
        const char* gpu_color = (gpu_temp >= 80) ? "\033[31m" : 
                               (gpu_temp >= 70) ? "\033[33m" : 
                               (gpu_temp >= 60) ? "\033[36m" : "\033[32m";
        
        printf("  CPU: %s%d°C\033[0m\n", cpu_color, cpu_temp);
        printf("  GPU: %s%d°C\033[0m\n", gpu_color, gpu_temp);
        
        // Temperature status
        int max_temp = (cpu_temp > gpu_temp) ? cpu_temp : gpu_temp;
        const char* status_color = (max_temp >= 80) ? "\033[31m" : 
                                  (max_temp >= 70) ? "\033[33m" : 
                                  (max_temp >= 60) ? "\033[36m" : "\033[32m";
        const char* status_msg = (max_temp >= 80) ? "CRITICAL" : 
                                (max_temp >= 70) ? "HIGH" : 
                                (max_temp >= 60) ? "WARM" : "NORMAL";
        
        printf("  Status: %s%s\033[0m\n", status_color, status_msg);
        
        // Fan section
        printf("\nFan Control:\n");
        printf("  Duty Cycle: %d%%\n", fan_duty);
        printf("  RPM:        %d\n", fan_rpm);
        printf("  Auto Mode:  %s\n", auto_mode ? "ON" : "OFF");
        
        printf("===============================\n\n");
    } else {
        printf("Status: %s\n", response);
    }
}

static void monitor_loop(int sock) {
    printf("Monitoring fan control (Press Ctrl+C to stop)...\n\n");
    
    while (running) {
        char command[64];
        snprintf(command, sizeof(command), "STATUS");
        
        int send_result = send_command(sock, command);
        if (send_result == 0) {
            char response[BUFFER_SIZE];
            if (receive_response(sock, response, sizeof(response)) == 0) {
                // Clear screen and print status
                printf("\033[2J\033[H"); // Clear screen and move cursor to top
                print_status(response);
                
                if (config.verbose) {
                    time_t now = time(NULL);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&now));
                    printf("Last updated: %s\n", time_str);
                }
            }
        } else if (send_result == -2) {
            // Broken pipe - daemon connection lost
            printf("\nConnection to daemon lost. Attempting to reconnect...\n");
            close(sock);
            sock = connect_to_daemon();
            if (sock < 0) {
                printf("Failed to reconnect to daemon. Exiting...\n");
                break;
            }
            printf("Reconnected to daemon.\n");
        }
        
        usleep((int)(config.monitor_interval * 1000000));
    }
}

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nStopping monitor...\n");
}

static void print_help(void) {
    printf("Usage: clevo-client [OPTIONS] COMMAND\n\n");
    printf("Commands:\n");
    printf("  --status              Show current fan control status\n");
    printf("  --monitor [INTERVAL]  Continuously monitor status (default: 2.0s)\n");
    printf("  --live-stats [INTERVAL] Live statistics display (default: 0.1s)\n");
    printf("  --set-fan DUTY        Set fan duty cycle (1-100%%)\n");
    printf("  --set-auto            Enable automatic fan control\n");
    printf("  --set-target-temp TEMP Set target temperature for auto control (40-100°C)\n");
    printf("  --get-temp            Get current temperatures\n");
    printf("  --get-fan             Get current fan status\n");
    printf("  --set-max-duty-change RATE Set max duty change rate (1-100%%, default: 30)\n");
    printf("  --get-max-duty-change Get current max duty change rate\n");
    printf("  --set-max-increase-rate N   Set max fan duty increase per cycle (1-100)\n");
    printf("  --set-max-decrease-rate N   Set max fan duty decrease per cycle (1-100)\n");
    printf("  --get-max-increase-rate     Show current max fan duty increase per cycle\n");
    printf("  --get-max-decrease-rate     Show current max fan duty decrease per cycle\n");
    printf("  --temp-monitor [INTERVAL] Monitor temperatures continuously (default: 2.0s)\n");
    printf("  --help                Show this help message\n\n");
    printf("Options:\n");
    printf("  -v, --verbose         Enable verbose output\n");
    printf("  -j, --json            Output in JSON format\n");
    printf("  -h, --help            Show this help message\n\n");
    printf("Examples:\n");
    printf("  clevo-client --status\n");
    printf("  clevo-client --monitor 5\n");
    printf("  clevo-client --live-stats 0.1\n");
    printf("  clevo-client --set-fan 80\n");
    printf("  clevo-client --set-max-duty-change 10\n");
    printf("  clevo-client --get-max-duty-change\n");
    printf("  clevo-client --json --status\n");
}

static void parse_arguments(int argc, char* argv[]) {
    int opt;
    static struct option long_options[] = {
        {"status", no_argument, 0, 0x100},
        {"monitor", optional_argument, 0, 0x101},
        {"live-stats", optional_argument, 0, 0x102},
        {"set-fan", required_argument, 0, 0x103},
        {"set-auto", no_argument, 0, 0x104},
        {"set-target-temp", required_argument, 0, 0x105},
        {"get-temp", no_argument, 0, 0x106},
        {"get-fan", no_argument, 0, 0x107},
        {"temp-monitor", optional_argument, 0, 0x108},
        {"set-max-duty-change", required_argument, 0, 0x109},
        {"get-max-duty-change", no_argument, 0, 0x10A},
        {"set-max-increase-rate", required_argument, 0, 0x10B},
        {"get-max-increase-rate", no_argument, 0, 0x10C},
        {"set-max-decrease-rate", required_argument, 0, 0x10D},
        {"get-max-decrease-rate", no_argument, 0, 0x10E},
        {"verbose", no_argument, 0, 'v'},
        {"json", no_argument, 0, 'j'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "vjh", long_options, NULL)) != -1) {
        switch (opt) {
            case 0x100: // --status
                config.type = CMD_STATUS;
                break;
            case 0x101: // --monitor
                config.type = CMD_MONITOR;
                config.monitor_interval = 2.0; // Default 2 seconds
                if (optarg) {
                    config.monitor_interval = atof(optarg);
                    if (config.monitor_interval < 0.1) config.monitor_interval = 0.1;
                }
                break;
            case 0x102: // --live-stats
                config.type = CMD_LIVE_STATS;
                config.live_stats_mode = 1;
                config.monitor_interval = 0.1; // Default 100ms for live stats
                if (optarg) {
                    config.monitor_interval = atof(optarg);
                    if (config.monitor_interval < 0.05) config.monitor_interval = 0.05;
                }
                break;
            case 0x103: // --set-fan
                config.type = CMD_SET_FAN;
                config.fan_duty = atoi(optarg);
                if (config.fan_duty < 1 || config.fan_duty > 100) {
                    fprintf(stderr, "Error: Fan duty must be between 1 and 100\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 0x104: // --set-auto
                config.type = CMD_SET_AUTO;
                break;
            case 0x105: // --set-target-temp
                config.type = CMD_SET_TARGET_TEMP;
                config.target_temperature = atoi(optarg);
                if (config.target_temperature < 40 || config.target_temperature > 100) {
                    fprintf(stderr, "Error: Target temperature must be between 40 and 100°C\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 0x106: // --get-temp
                config.type = CMD_GET_TEMP;
                break;
            case 0x107: // --get-fan
                config.type = CMD_GET_FAN;
                break;
            case 0x108: // --temp-monitor
                config.type = CMD_TEMP_MONITOR;
                config.monitor_interval = 2.0; // Default 2 seconds
                if (optarg) {
                    config.monitor_interval = atof(optarg);
                    if (config.monitor_interval < 0.1) config.monitor_interval = 0.1;
                }
                break;
            case 0x109: // --set-max-duty-change
                config.type = CMD_SET_MAX_DUTY_CHANGE;
                config.max_duty_change_rate = atoi(optarg);
                if (config.max_duty_change_rate < 1 || config.max_duty_change_rate > 100) {
                    fprintf(stderr, "Error: Max duty change rate must be between 1 and 100\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 0x10A: // --get-max-duty-change
                config.type = CMD_GET_MAX_DUTY_CHANGE;
                break;
            case 0x10B: // --set-max-increase-rate
                config.type = CMD_SET_MAX_DUTY_INCREASE;
                config.max_duty_increase_rate = atoi(optarg);
                if (config.max_duty_increase_rate < 1 || config.max_duty_increase_rate > 100) {
                    fprintf(stderr, "Error: Max duty increase rate must be between 1 and 100\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 0x10C: // --get-max-increase-rate
                config.type = CMD_GET_MAX_DUTY_INCREASE;
                break;
            case 0x10D: // --set-max-decrease-rate
                config.type = CMD_SET_MAX_DUTY_DECREASE;
                config.max_duty_decrease_rate = atoi(optarg);
                if (config.max_duty_decrease_rate < 1 || config.max_duty_decrease_rate > 100) {
                    fprintf(stderr, "Error: Max duty decrease rate must be between 1 and 100\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 0x10E: // --get-max-decrease-rate
                config.type = CMD_GET_MAX_DUTY_DECREASE;
                break;
            case 'v':
                config.verbose = 1;
                break;
            case 'j':
                config.json_output = 1;
                break;
            case 'h':
                config.type = CMD_HELP;
                return;
            default:
                print_help();
                exit(EXIT_FAILURE);
        }
    }
    
    // If no command specified, default to status
    if (config.type == 0) {
        config.type = CMD_STATUS;
    }
}

static int format_json_status(const char* response, char* json_buffer, size_t size) {
    int cpu_temp, gpu_temp, fan_duty, fan_rpm, auto_mode;
    
    if (sscanf(response, "CPU:%d GPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d", 
                &cpu_temp, &gpu_temp, &fan_duty, &fan_rpm, &auto_mode) == 5) {
        
        snprintf(json_buffer, size,
                "{\n"
                "  \"cpu_temperature\": %d,\n"
                "  \"gpu_temperature\": %d,\n"
                "  \"fan_duty_cycle\": %d,\n"
                "  \"fan_rpm\": %d,\n"
                "  \"auto_mode\": %s\n"
                "}",
                cpu_temp, gpu_temp, fan_duty, fan_rpm, auto_mode ? "true" : "false");
        return 0;
    }
    
    return -1;
} 

// Live stats implementation
static void live_stats_init(void) {
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    // curs_set(0);  // Hide cursor - removed due to terminal compatibility issues
    // keypad(stdscr, TRUE);  // Removed due to terminal compatibility issues
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
    // refresh();  // Removed due to terminal compatibility issues
    
    // Handle window resize
    live_stats_handle_resize();
}

static void live_stats_handle_resize(void) {
    if (!live_stats_initialized) return;
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // Minimum window size check
    if (max_y < 8 || max_x < 40) {
        clear();
        mvprintw(max_y/2, (max_x-40)/2, "Window too small! Need 40x8 minimum");
        // refresh();  // Removed due to terminal compatibility issues
        return;
    }
    
    // Clear screen for redraw
    clear();
}

static int live_stats_display(int sock) {
    if (!live_stats_initialized) {
        return -1; // Indicate initialization failure
    }
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // Check window size
    // Temporarily disabled window size check
    /*
    if (max_y < 8 || max_x < 40) {
        printf("DEBUG: Window too small\n");
        live_stats_handle_resize();
        return -1; // Indicate window too small
    }
    */
    
    // Get current status from daemon
    char command[64];
    snprintf(command, sizeof(command), "STATUS");
    char response[BUFFER_SIZE];
    
    // Try to get status from daemon
    int comm_success = 0;
    int cpu_temp = 0, gpu_temp = 0, fan_duty = 0, fan_rpm = 0, auto_mode = 0;
    
    int send_result = send_command(sock, command);
    if (send_result == 0 && receive_response(sock, response, sizeof(response)) == 0) {
        // Parse response
        if (sscanf(response, "CPU:%d GPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d", 
                    &cpu_temp, &gpu_temp, &fan_duty, &fan_rpm, &auto_mode) == 5) {
            comm_success = 1;
        }
    } else if (send_result == -2) {
        // Broken pipe - daemon connection lost
        return -2; // Indicate broken pipe
    }
    
    if (comm_success) {
        int max_temp = (cpu_temp > gpu_temp) ? cpu_temp : gpu_temp;
        
        // Draw header
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(0, 0, "+--- Clevo Fan Control Client Live Stats ");
        for (int i = 37; i < max_x - 2; i++) mvprintw(0, i, "-");
        mvprintw(0, max_x - 2, "+");
        attroff(COLOR_PAIR(5) | A_BOLD);
        
        // Draw header info
        attron(COLOR_PAIR(4));
        mvprintw(1, 2, "Update: %5.0fms            ", config.monitor_interval * 1000);
        mvprintw(1, 30, "Mode: %-8s            ", auto_mode ? "Auto" : "Manual");
        attroff(COLOR_PAIR(4));
        
        // Draw separator
        mvprintw(2, 0, "+");
        for (int i = 1; i < max_x - 1; i++) mvprintw(2, i, "-");
        mvprintw(2, max_x - 1, "+");
        
        // Temperature section - only update if changed
        if (cpu_temp != last_display_cpu_temp || gpu_temp != last_display_gpu_temp) {
            mvprintw(3, 2, "Temperature:                                ");
            // CPU temperature with color coding
            if (cpu_temp >= 80) {
                attron(COLOR_PAIR(3));
            } else if (cpu_temp >= 70) {
                attron(COLOR_PAIR(2));
            } else {
                attron(COLOR_PAIR(1));
            }
            mvprintw(4, 4, "CPU: %3d°C            ", cpu_temp);
            attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3));
            // GPU temperature with color coding
            if (gpu_temp >= 80) {
                attron(COLOR_PAIR(3));
            } else if (gpu_temp >= 70) {
                attron(COLOR_PAIR(2));
            } else {
                attron(COLOR_PAIR(1));
            }
            mvprintw(4, 30, "GPU: %3d°C            ", gpu_temp);
            attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3));
            // Max temperature
            mvprintw(4, 56, "Max: %3d°C            ", max_temp);
            last_display_cpu_temp = cpu_temp;
            last_display_gpu_temp = gpu_temp;
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
            mvprintw(7, 56, "Health: %-5s            ", health_status);
            last_display_fan_duty = fan_duty;
            last_display_fan_rpm = fan_rpm;
        }
        
        // Draw separator
        mvprintw(8, 0, "+");
        for (int i = 1; i < max_x - 1; i++) mvprintw(8, i, "-");
        mvprintw(8, max_x - 1, "+");
        
        // Status section
        mvprintw(9, 2, "Status: %-10s            ", auto_mode ? "Auto Mode" : "Manual Mode");
        
        // Temperature status
        const char* temp_status = "NORMAL";
        if (max_temp >= 80) {
            temp_status = "CRITICAL";
            attron(COLOR_PAIR(3));
        } else if (max_temp >= 70) {
            temp_status = "HIGH";
            attron(COLOR_PAIR(2));
        } else if (max_temp >= 60) {
            temp_status = "WARM";
            attron(COLOR_PAIR(4));
        } else {
            attron(COLOR_PAIR(1));
        }
        mvprintw(9, 30, "Temp: %-8s            ", temp_status);
        attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3) | COLOR_PAIR(4));
        
        // Draw footer
        mvprintw(10, 0, "+");
        for (int i = 1; i < max_x - 1; i++) mvprintw(10, i, "-");
        mvprintw(10, max_x - 1, "+");
        
        // Instructions
        attron(COLOR_PAIR(4));
        mvprintw(11, 2, "Press 'q' to quit, 'r' to refresh display");
        attroff(COLOR_PAIR(4));
        return 0; // Indicate success
    } else {
        // Communication failed - show error message
        clear();
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(max_y/2 - 2, (max_x - 40)/2, "Connection Error");
        attroff(COLOR_PAIR(3) | A_BOLD);
        attron(COLOR_PAIR(4));
        mvprintw(max_y/2, (max_x - 50)/2, "Failed to communicate with daemon");
        mvprintw(max_y/2 + 1, (max_x - 40)/2, "Check if clevo-daemon is running");
        mvprintw(max_y/2 + 3, (max_x - 30)/2, "Press 'q' to quit");
        attroff(COLOR_PAIR(4));
        return -1; // Indicate communication failure
    }
    
    // Handle input
    live_stats_handle_input();
    
    // Refresh display
    refresh();
    return 0; // Indicate success
}

static void live_stats_cleanup(void) {
    if (live_stats_initialized) {
        // Restore terminal
        // curs_set(1);  // Show cursor - removed due to terminal compatibility issues
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
        } else if (ch == 'r' || ch == 'R') {
            // Force refresh by clearing display cache
            last_display_cpu_temp = -1;
            last_display_gpu_temp = -1;
            last_display_fan_duty = -1;
            last_display_fan_rpm = -1;
        }
    }
} 