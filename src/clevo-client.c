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

#define SOCKET_PATH "/tmp/clevo-daemon.sock"
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
    CMD_HELP
} CommandType;

typedef struct {
    CommandType type;
    int fan_duty;
    int target_temperature;
    double monitor_interval;
    int verbose;
    int json_output;
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
    ssize_t sent = send(sock, command, strlen(command), 0);
    if (sent < 0) {
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
        
        if (send_command(sock, command) == 0) {
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
    printf("  status              Show current fan control status\n");
    printf("  monitor [INTERVAL]  Continuously monitor status (default: 2.0s)\n");
    printf("  set-fan DUTY        Set fan duty cycle (1-100%%)\n");
    printf("  set-auto            Enable automatic fan control\n");
    printf("  set-target-temp TEMP Set target temperature for auto control (40-100°C)\n");
    printf("  get-temp            Get current temperatures\n");
    printf("  get-fan             Get current fan status\n");
    printf("  temp-monitor [INTERVAL] Monitor temperatures continuously (default: 2.0s)\n");
    printf("  help                Show this help message\n\n");
    printf("Options:\n");
    printf("  -v, --verbose       Enable verbose output\n");
    printf("  -j, --json          Output in JSON format\n");
    printf("  -h, --help          Show this help message\n\n");
    printf("Examples:\n");
    printf("  clevo-client status\n");
    printf("  clevo-client monitor 5\n");
    printf("  clevo-client set-fan 80\n");
    printf("  clevo-client --json status\n");
}

static void parse_arguments(int argc, char* argv[]) {
    int opt;
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"json", no_argument, 0, 'j'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "vjh", long_options, NULL)) != -1) {
        switch (opt) {
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
    
    // Parse command
    if (optind >= argc) {
        config.type = CMD_STATUS; // Default to status
        return;
    }
    
    const char* command = argv[optind];
    
    if (strcmp(command, "status") == 0) {
        config.type = CMD_STATUS;
    } else if (strcmp(command, "monitor") == 0) {
        config.type = CMD_MONITOR;
        config.monitor_interval = 2.0; // Default 2 seconds
        if (optind + 1 < argc) {
            config.monitor_interval = atof(argv[optind + 1]);
            if (config.monitor_interval < 0.1) config.monitor_interval = 0.1;
        }
    } else if (strcmp(command, "set-fan") == 0) {
        config.type = CMD_SET_FAN;
        if (optind + 1 < argc) {
            config.fan_duty = atoi(argv[optind + 1]);
            if (config.fan_duty < 1 || config.fan_duty > 100) {
                fprintf(stderr, "Error: Fan duty must be between 1 and 100\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Error: Fan duty value required\n");
            exit(EXIT_FAILURE);
        }
    } else if (strcmp(command, "set-auto") == 0) {
        config.type = CMD_SET_AUTO;
    } else if (strcmp(command, "set-target-temp") == 0) {
        config.type = CMD_SET_TARGET_TEMP;
        if (optind + 1 < argc) {
            config.target_temperature = atoi(argv[optind + 1]);
            if (config.target_temperature < 40 || config.target_temperature > 100) {
                fprintf(stderr, "Error: Target temperature must be between 40 and 100°C\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Error: Target temperature value required\n");
            exit(EXIT_FAILURE);
        }
    } else if (strcmp(command, "get-temp") == 0) {
        config.type = CMD_GET_TEMP;
    } else if (strcmp(command, "get-fan") == 0) {
        config.type = CMD_GET_FAN;
    } else if (strcmp(command, "temp-monitor") == 0) {
        config.type = CMD_TEMP_MONITOR;
        config.monitor_interval = 2.0; // Default 2 seconds
        if (optind + 1 < argc) {
            config.monitor_interval = atof(argv[optind + 1]);
            if (config.monitor_interval < 0.1) config.monitor_interval = 0.1;
        }
    } else if (strcmp(command, "help") == 0) {
        config.type = CMD_HELP;
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_help();
        exit(EXIT_FAILURE);
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