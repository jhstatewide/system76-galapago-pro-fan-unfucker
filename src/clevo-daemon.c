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
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <ctype.h>

#include "privilege_manager.h"

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
static int status_interval = 2; // Default 2 seconds
static int target_temperature = 65; // Default target temperature
static int log_level = LOG_INFO;
static volatile int running = 1;

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
    
    // Find the first non-option argument
    int fan_duty_arg = -1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            fan_duty_arg = i;
            break;
        }
    }
    
    if (fan_duty_arg == -1) {
        // No fan duty argument provided - run daemon mode
        signal_term(&daemon_on_sigterm);
        daemon_init_share();
        
        daemon_log(LOG_INFO, "Starting fan control daemon with target temperature %d°C", target_temperature);
        
        // Run the main daemon loop
        while (running) {
            daemon_ec_worker();
            usleep(status_interval * 1000000); // Convert to microseconds
        }
        
        daemon_log(LOG_INFO, "Daemon stopped");
    } else {
        // Fan duty argument provided - run in CLI mode
        int val = atoi(argv[fan_duty_arg]);
        if (val < 1 || val > 100) {
            printf("invalid fan duty %d! (must be 1-100)\n", val);
            return EXIT_FAILURE;
        }
        return daemon_test_fan(val);
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
    int temp = MAX(share_info->cpu_temp, share_info->gpu_temp);
    int duty = share_info->fan_duty;
    int new_duty = duty;

    if (temp >= target_temperature) {
        // Gradually increase fan duty cycle to find steady state
        new_duty = MAX(duty + 2, 10);
    }
    else {
        // Decrease fan duty cycle if temperature is below target
        new_duty = MAX(duty - 2, 0);
    }

    if (new_duty > 100) {
        new_duty = 100;
    } else if (new_duty < 0) {
        new_duty = 0;
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
            char path[256];
            char comm[256];
            snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
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
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            log_level = LOG_DEBUG;
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            status_interval = atoi(argv[++i]);
            if (status_interval < 1 || status_interval > 60) {
                printf("Invalid interval: %d (must be 1-60 seconds)\n", status_interval);
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--target-temp") == 0 && i + 1 < argc) {
            target_temperature = atoi(argv[++i]);
            if (target_temperature < 40 || target_temperature > 100) {
                printf("Invalid target temperature: %d (must be 40-100°C)\n", target_temperature);
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "--help") == 0) {
            printf(
                "\n"
                "Usage: clevo-daemon [OPTIONS] [fan-duty-percentage]\n"
                "\n"
                "Headless fan control daemon for Clevo laptops.\n"
                "\n"
                "Options:\n"
                "  --debug\t\tEnable debug output\n"
                "  --interval <sec>\tSet status update interval (1-60 seconds, default: 2)\n"
                "  --target-temp <°C>\tSet the target temperature for auto fan control (40-100°C, default: 65)\n"
                "  -?, --help\t\tDisplay this help and exit\n"
                "\n"
                "Arguments:\n"
                "  [fan-duty-percentage]\tTarget fan duty in percentage, from 1 to 100\n"
                "\n"
                "Daemon Mode:\n"
                "  When run without arguments, operates as a daemon with automatic\n"
                "  temperature-based fan control.\n"
                "\n"
                "CLI Mode:\n"
                "  When a fan duty percentage is provided, sets the fan to that\n"
                "  percentage and displays current status.\n"
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