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

#include "privilege_manager.h"
#include "clevo-daemon-socket.h"

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
#define EC_REG_GPU_TEMP 0xCD
#define EC_REG_FAN_DUTY 0xCE
#define EC_REG_FAN_RPMS_HI 0xD0
#define EC_REG_FAN_RPMS_LO 0xD1

#define MAX_FAN_RPM 4400.0

// Define MAX macro if not defined
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

// Global variables
static int debug_mode = 0;
static int log_level = LOG_INFO;
static double status_interval = 2.0;
static int target_temperature = 65;
static int daemon_mode = 0;
static volatile int running = 1;

// PID Controller variables
static double pid_kp = 2.0;  // Proportional gain
static double pid_ki = 0.1;  // Integral gain  
static double pid_kd = 0.5;  // Derivative gain
static double pid_integral = 0.0;
static double pid_prev_error = 0.0;
static double pid_output_min = 0.0;
static double pid_output_max = 100.0;
static int pid_enabled = 1;  // Enable PID control by default

// Adaptive PID Controller variables
static int adaptive_pid_enabled = 1;  // Enable adaptive tuning
static int adaptive_learning_cycles = 0;  // Number of learning cycles completed
static double adaptive_performance_score = 0.0;  // Current performance score
static double adaptive_prev_score = 0.0;  // Previous performance score
static double adaptive_oscillation_penalty = 0.0;  // Penalty for oscillation
static double adaptive_overshoot_penalty = 0.0;  // Penalty for overshoot
static double adaptive_settling_time = 0.0;  // Time to reach target
static int adaptive_cycle_start_time = 0;  // Start time of current cycle
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
    volatile int gpu_temp;
    volatile int fan_duty;
    volatile int fan_rpms;
    volatile int auto_duty;
    volatile int auto_duty_val;
    volatile int manual_next_fan_duty;
    volatile int manual_prev_fan_duty;
} *share_info = NULL;

// Function declarations
static void daemon_init_share(void);
static int daemon_ec_worker(void);
static void daemon_on_sigterm(int signum);
static int daemon_dump_fan(void);
static int daemon_test_fan(int duty_percentage);
static int ec_init(void);
static int ec_auto_duty_adjust(void);
static int ec_query_cpu_temp(void);
static int ec_query_gpu_temp(void);
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

// Adaptive PID Controller functions
static void adaptive_pid_add_temp_history(int temp);
static double adaptive_pid_calculate_oscillation(void);
static double adaptive_pid_calculate_performance_score(void);
static void adaptive_pid_tune_parameters(void);
static void adaptive_pid_reset(void);
static void pid_reset(void);

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
    
    // Test EC access
    if (ec_init() != EXIT_SUCCESS) {
        printf("unable to control EC: %s\n", strerror(errno));
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
        
        // Daemonize if not in debug mode
        if (!debug_mode) {
            daemonize();
        }
        
        // Initialize socket server
        if (init_socket_server() != 0) {
            daemon_log(LOG_ERR, "Failed to initialize socket server");
            return EXIT_FAILURE;
        }
        
        daemon_log(LOG_INFO, "Starting fan control daemon with target temperature %d°C", target_temperature);
        
        // Run the main daemon loop
        while (running) {
            daemon_ec_worker();
            usleep((int)(status_interval * 1000000)); // Convert to microseconds
        }
        
        // Stop socket server
        stop_socket_server();
        
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
            
            // Daemonize if not in debug mode
            if (!debug_mode) {
                daemonize();
            }
            
            // Initialize socket server
            if (init_socket_server() != 0) {
                daemon_log(LOG_ERR, "Failed to initialize socket server");
                return EXIT_FAILURE;
            }
            
            daemon_log(LOG_INFO, "Starting fan control daemon with target temperature %d°C", target_temperature);
            
            // Run the main daemon loop
            while (running) {
                daemon_ec_worker();
                usleep((int)(status_interval * 1000000)); // Convert to microseconds
            }
            
            // Stop socket server
            stop_socket_server();
            
            daemon_log(LOG_INFO, "Daemon stopped");
        } else {
            printf("Invalid argument: %s\n", argv[fan_duty_arg]);
            printf("For fan duty (CLI mode): must be 1-100\n");
            printf("For target temperature (daemon mode): must be 40-100°C\n");
            printf("For daemon mode with default temperature: no arguments or --daemon\n");
            return EXIT_FAILURE;
        }
    }
    
    return EXIT_SUCCESS;
}

static void daemon_init_share(void) {
    void* shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    share_info = shm;
    share_info->exit = 0;
    share_info->cpu_temp = 0;
    share_info->gpu_temp = 0;
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
    
    // Read EC data
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
                share_info->cpu_temp = buf[EC_REG_CPU_TEMP];
                share_info->gpu_temp = buf[EC_REG_GPU_TEMP];
                share_info->fan_duty = calculate_fan_duty(buf[EC_REG_FAN_DUTY]);
                share_info->fan_rpms = calculate_fan_rpms(buf[EC_REG_FAN_RPMS_HI], buf[EC_REG_FAN_RPMS_LO]);
                if (debug_mode) daemon_log(LOG_DEBUG, "sysfs: cpu_temp=%d, gpu_temp=%d, fan_duty=%d, fan_rpms=%d", 
                    share_info->cpu_temp, share_info->gpu_temp, share_info->fan_duty, share_info->fan_rpms);
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
        share_info->cpu_temp = ec_query_cpu_temp();
        share_info->gpu_temp = ec_query_gpu_temp();
        share_info->fan_duty = ec_query_fan_duty();
        share_info->fan_rpms = ec_query_fan_rpms();
        if (debug_mode) daemon_log(LOG_DEBUG, "direct I/O: cpu_temp=%d, gpu_temp=%d, fan_duty=%d, fan_rpms=%d", 
            share_info->cpu_temp, share_info->gpu_temp, share_info->fan_duty, share_info->fan_rpms);
    }
    
    // auto EC control
    if (share_info->auto_duty == 1) {
        int next_duty = ec_auto_duty_adjust();
        if (debug_mode) daemon_log(LOG_DEBUG, "auto_duty=1, next_duty=%d, prev_auto_duty_val=%d", next_duty, share_info->auto_duty_val);
        if (next_duty != 0 && next_duty != share_info->auto_duty_val) {
            char s_time[256];
            get_time_string(s_time, 256, "%m/%d %H:%M:%S");
            daemon_log(LOG_INFO, "%s CPU=%d°C, GPU=%d°C, auto fan duty to %d%%", s_time, share_info->cpu_temp, share_info->gpu_temp, next_duty);
            int write_result = ec_write_fan_duty(next_duty);
            if (debug_mode) daemon_log(LOG_DEBUG, "ec_write_fan_duty (auto) returned: %d", write_result);
            share_info->auto_duty_val = next_duty;
        }
    }
    
    return EXIT_SUCCESS;
}

static int daemon_dump_fan(void) {
    printf("Dump fan information\n");
    printf("  FAN Duty: %d%%\n", ec_query_fan_duty());
    printf("  FAN RPMs: %d RPM\n", ec_query_fan_rpms());
    printf("  CPU Temp: %d°C\n", ec_query_cpu_temp());
    printf("  GPU Temp: %d°C\n", ec_query_gpu_temp());
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
    daemon_log(LOG_INFO, "Received signal %s, shutting down", strsignal(signum));
    running = 0;
    if (share_info != NULL) {
        share_info->exit = 1;
    }
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
        int temp = MAX(share_info->cpu_temp, share_info->gpu_temp);
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

    // PID Controller implementation
    int temp = MAX(share_info->cpu_temp, share_info->gpu_temp);
    double setpoint = (double)target_temperature;
    double process_variable = (double)temp;
    double error = process_variable - setpoint;
    
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
    int new_duty = (int)(output + 0.5); // Round to nearest integer
    
    // Ensure duty cycle is within valid range
    if (new_duty > 100) new_duty = 100;
    if (new_duty < 0) new_duty = 0;
    
    if (debug_mode) {
        daemon_log(LOG_DEBUG, "PID: temp=%d, setpoint=%.1f, error=%.1f, p=%.1f, i=%.1f, d=%.1f, output=%.1f, duty=%d",
               temp, setpoint, error, proportional, integral, derivative, output, new_duty);
    }
    
    return new_duty;
}

static int ec_query_cpu_temp(void) {
    return ec_io_read(EC_REG_CPU_TEMP);
}

static int ec_query_gpu_temp(void) {
    return ec_io_read(EC_REG_GPU_TEMP);
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
    return raw_rpm > 0 ? (2156220 / raw_rpm) : 0;
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
        {"debug",        no_argument,       0, 'd'},
        {"interval",     required_argument, 0, 'i'},
        {"target-temp",  required_argument, 0, 't'},
        {"daemon",       no_argument,       0, 'D'},
        {"pid-enabled",  required_argument, 0, 'p'},
        {"adaptive-pid", required_argument, 0, 'a'},
        {"adaptive-tuning-interval", required_argument, 0, 'A'},
        {"adaptive-target-performance", required_argument, 0, 'P'},
        {"help",         no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "di:t:Dp:a:A:P:h?", long_options, &option_index)) != -1) {
        switch (c) {
            case 'd':
                debug_mode = 1;
                log_level = LOG_DEBUG;
                break;
            case 'i':
                status_interval = atof(optarg);
                if (status_interval < 0.1 || status_interval > 60.0) {
                    printf("Invalid interval: %.1f (must be 0.1-60.0 seconds)\n", status_interval);
                    exit(EXIT_FAILURE);
                }
                break;
            case 't':
                target_temperature = atoi(optarg);
                if (target_temperature < 40 || target_temperature > 100) {
                    printf("Invalid target temperature: %d (must be 40-100°C)\n", target_temperature);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'D':
                daemon_mode = 1;
                break;
            case 'p':
                pid_enabled = atoi(optarg);
                break;
            case 'a':
                adaptive_pid_enabled = atoi(optarg);
                break;
            case 'A':
                adaptive_tuning_interval = atoi(optarg);
                if (adaptive_tuning_interval < 10) adaptive_tuning_interval = 10;
                if (adaptive_tuning_interval > 300) adaptive_tuning_interval = 300;
                break;
            case 'P':
                adaptive_target_performance = atof(optarg);
                if (adaptive_target_performance < 0.1) adaptive_target_performance = 0.1;
                if (adaptive_target_performance > 1.0) adaptive_target_performance = 1.0;
                break;
            case 'h':
            case '?':
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
                    "  -a, --adaptive-pid <0|1>\tEnable/Disable adaptive PID tuning (default: 1)\n"
                    "  -A, --adaptive-tuning-interval <sec>\tSet adaptive tuning interval (10-300s, default: 30)\n"
                    "  -P, --adaptive-target-performance <value>\tSet target performance score (0.1-1.0, default: 0.8)\n"
                    "  -h, -?, --help\tDisplay this help and exit\n"
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
                    "\n"
                    "Modern Privilege Management:\n"
                    "This program supports multiple privilege elevation methods:\n"
                    "\n"
                    "1. Capabilities (Recommended):\n"
                    "   sudo setcap cap_sys_rawio+ep bin/clevo-daemon\n"
                    "\n"
                    "2. Systemd Service (Background):\n"
                    "   sudo cp systemd/clevo-daemon.service /etc/systemd/system/\n"
                    "   sudo systemctl enable clevo-daemon.service\n"
                    "\n"
                    "3. Traditional setuid:\n"
                    "   sudo chown root bin/clevo-daemon\n"
                    "   sudo chmod u+s bin/clevo-daemon\n"
                    "\n"
                    "Note any fan duty change should take 1-2 seconds to come into effect.\n"
                    "\n"
                );
                exit(EXIT_SUCCESS);
            default:
                printf("Unknown option: %c\n", c);
                exit(EXIT_FAILURE);
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
    va_list args;
    va_start(args, format);
    
    if (priority <= log_level) {
        vsyslog(priority, format, args);
        if (debug_mode || priority <= LOG_WARNING) {
            vprintf(format, args);
            printf("\n");
            fflush(stdout);
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
    int temp = MAX(share_info->cpu_temp, share_info->gpu_temp);
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
    int temp = MAX(share_info->cpu_temp, share_info->gpu_temp);
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

static void adaptive_pid_reset(void) {
    adaptive_learning_cycles = 0;
    adaptive_performance_score = 0.0;
    adaptive_prev_score = 0.0;
    adaptive_oscillation_penalty = 0.0;
    adaptive_overshoot_penalty = 0.0;
    adaptive_settling_time = 0.0;
    adaptive_cycle_start_time = 0;
    adaptive_cycle_count = 0;
    adaptive_temp_history_index = 0;
    adaptive_temp_history_size = 0;
    
    // Reset step sizes to defaults
    adaptive_kp_step = 0.1;
    adaptive_ki_step = 0.01;
    adaptive_kd_step = 0.05;
    
    if (debug_mode) daemon_log(LOG_DEBUG, "Adaptive PID controller reset");
}

static void pid_reset(void) {
    pid_integral = 0.0;
    pid_prev_error = 0.0;
    // Reset adaptive PID if enabled
    if (adaptive_pid_enabled) {
        adaptive_pid_reset();
    }
    if (debug_mode) daemon_log(LOG_DEBUG, "PID controller and adaptive controller reset");
} 