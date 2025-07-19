/*
 ============================================================================
 Name        : clevo-indicator.c
 Author      : AqD <iiiaqd@gmail.com>
 Version     :
 Description : Ubuntu fan control indicator for Clevo laptops

 Based on http://www.association-apml.fr/upload/fanctrl.c by Jonas Diemer
 (diemer@gmx.de)

 ============================================================================

 TEST:
 gcc clevo-indicator.c -o clevo-indicator `pkg-config --cflags --libs appindicator3-0.1` -lm
 sudo chown root clevo-indicator
 sudo chmod u+s clevo-indicator

 Run as effective uid = root, but uid = desktop user (in order to use indicator).

 ============================================================================
 Auto fan control algorithm:

 The algorithm is to replace the builtin auto fan-control algorithm in Clevo
 laptops which is apparently broken in recent models such as W350SSQ, where the
 fan doesn't get kicked until both of GPU and CPU are really hot (and GPU
 cannot be hot anymore thanks to nVIDIA's Maxwell chips). It's far more
 aggressive than the builtin algorithm in order to keep the temperatures below
 60°C all the time, for maximized performance with Intel turbo boost enabled.

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

#include <libayatana-appindicator/app-indicator.h>
#include "privilege_manager.h"

#define NAME "clevo-indicator"

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

typedef enum {
    NA = 0, AUTO = 1, MANUAL = 2
} MenuItemType;

static void main_init_share(void);
static int main_ec_worker(void);
static void main_ui_worker(int argc, char** argv);
static void main_on_sigchld(int signum);
static void main_on_sigterm(int signum);
static int main_dump_fan(void);
static int main_test_fan(int duty_percentage);
static gboolean ui_update(gpointer user_data);
static void ui_command_set_fan(long fan_duty);
static void ui_command_quit(gchar* command);
static void ui_command_show_temp(gchar* command);
static void ui_toggle_menuitems(int fan_duty);
static void ec_on_sigterm(int signum);
static int ec_init(void);
static int ec_auto_duty_adjust(void);
static int ec_query_cpu_temp(void);
static int ec_query_gpu_temp(void);
static int ec_query_fan_duty(void);
static int ec_query_fan_rpms(void);
static int ec_write_fan_duty(int duty_percentage);
static int ec_io_wait(const uint32_t port, const uint32_t flag,
        const char value);
static uint8_t ec_io_read(const uint32_t port);
static int ec_io_do(const uint32_t cmd, const uint32_t port,
        const uint8_t value);
static int calculate_fan_duty(int raw_duty);
static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low);
static int check_proc_instances(const char* proc_name);
static void get_time_string(char* buffer, size_t max, const char* format);
static void signal_term(__sighandler_t handler);
static void parse_command_line(int argc, char* argv[]);
static bool setup_privileges(void);
static void show_privilege_help(void);
static void status_display_init(void);
static void status_display_update_with_control(void);
static void status_display_cleanup(void);
static void status_display_show_help(void);
static char* status_get_temp_bar(int temp, int max_temp);
static char* status_get_fan_bar(int rpm, int max_rpm);
static char* status_get_color_code(int temp);
static void status_clear_screen(void);
static void pid_reset(void);
static void calculate_temp_rate_of_change(void);
static char* get_temp_trend_symbol(double rate);
static char* get_temp_trend_color(double rate);
static void adaptive_pid_add_temp_history(int temp);
static double adaptive_pid_calculate_oscillation(void);
static double adaptive_pid_calculate_performance_score(void);
static void adaptive_pid_tune_parameters(void);
static void adaptive_pid_reset(void);

static AppIndicator* indicator = NULL;

struct {
    char label[256];
    GCallback callback;
    long option;
    MenuItemType type;
    GtkWidget* widget;

}static menuitems[] = {
        { "Set FAN to AUTO", G_CALLBACK(ui_command_set_fan), 0, AUTO, NULL },
        { "", NULL, 0L, NA, NULL },
        { "Set FAN to  60%", G_CALLBACK(ui_command_set_fan), 60, MANUAL, NULL },
        { "Set FAN to  70%", G_CALLBACK(ui_command_set_fan), 70, MANUAL, NULL },
        { "Set FAN to  80%", G_CALLBACK(ui_command_set_fan), 80, MANUAL, NULL },
        { "Set FAN to  90%", G_CALLBACK(ui_command_set_fan), 90, MANUAL, NULL },
        { "Set FAN to  1%", G_CALLBACK(ui_command_set_fan), 1, MANUAL, NULL },
        { "Set FAN to 100%", G_CALLBACK(ui_command_set_fan), 100, MANUAL, NULL },
        { "", NULL, 0L, NA, NULL },
        { "Show Temperatures", G_CALLBACK(ui_command_show_temp), 0L, NA, NULL },
        { "Quit", G_CALLBACK(ui_command_quit), 0L, NA, NULL }
};

static int menuitem_count = (sizeof(menuitems) / sizeof(menuitems[0]));

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
}static *share_info = NULL;

static pid_t parent_pid = 0;
static int debug_mode = 0;
static int status_mode = 0;
static double status_interval = 2.0; // Default 2 seconds
static int target_temperature = 65; // Default target temperature
static int temp_output_interval = 30; // Default 30 seconds for temperature output

// PID Controller variables
static double pid_kp = 2.0;  // Proportional gain
static double pid_ki = 0.1;  // Integral gain  
static double pid_kd = 0.5;  // Derivative gain
static double pid_integral = 0.0;
static double pid_prev_error = 0.0;
static double pid_output_min = 0.0;
static double pid_output_max = 100.0;
static int pid_enabled = 1;  // Enable PID control by default

// Temperature tracking for rate of change calculation
static int prev_cpu_temp = 0;
static int prev_gpu_temp = 0;
static double cpu_temp_rate = 0.0;  // °C per second
static double gpu_temp_rate = 0.0;  // °C per second
static time_t last_temp_update = 0;

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

// Rapid learning variables
static int adaptive_rapid_learning_cycles = 0;  // Cycles in rapid learning mode
static int adaptive_rapid_learning_max = 10;  // Maximum rapid learning cycles
static double adaptive_rapid_step_multiplier = 3.0;  // Multiplier for rapid learning steps
static double adaptive_steady_state_threshold = 0.05;  // Performance stability threshold
static int adaptive_consecutive_stable_cycles = 0;  // Consecutive cycles with stable performance
static int adaptive_steady_state_cycles_required = 5;  // Cycles required for steady state

int main(int argc, char* argv[]) {
    printf("Simple fan control utility for Clevo laptops\n");
    
    // Parse command line arguments
    parse_command_line(argc, argv);
    
    if (check_proc_instances(NAME) > 1) {
        printf("Multiple running instances!\n");
        char* display = getenv("DISPLAY");
        if (display != NULL && strlen(display) > 0) {
            int desktop_uid = getuid();
            setuid(desktop_uid);
            //
            gtk_init(&argc, &argv);
            GtkWidget* dialog = gtk_message_dialog_new(NULL, 0,
                    GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                    "Multiple running instances of %s!", NAME);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
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
    
    // Handle status mode
    if (status_mode) {
        signal_term(&main_on_sigterm);
        status_display_init();
        status_display_show_help();
        
        // Initialize shared memory for status mode
        main_init_share();
        
        // Run status display loop with auto fan control
        while (1) {
            status_display_update_with_control();
            usleep((int)(status_interval * 1000000)); // Convert to microseconds
        }
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
        // No fan duty argument provided - run indicator mode
        char* display = getenv("DISPLAY");
        if (display == NULL || strlen(display) == 0) {
            return main_dump_fan();
        } else {
            parent_pid = getpid();
            main_init_share();
            signal(SIGCHLD, &main_on_sigchld);
            signal_term(&main_on_sigterm);
            pid_t worker_pid = fork();
            if (worker_pid == 0) {
                signal(SIGCHLD, SIG_DFL);
                signal_term(&ec_on_sigterm);
                return main_ec_worker();
            } else if (worker_pid > 0) {
                main_ui_worker(argc, argv);
                share_info->exit = 1;
                waitpid(worker_pid, NULL, 0);
            } else {
                printf("unable to create worker: %s\n", strerror(errno));
                return EXIT_FAILURE;
            }
        }
    } else {
        // Fan duty argument provided
        int val = atoi(argv[fan_duty_arg]);
        if (val < 40 || val > 100) {
            printf("invalid fan duty %d!\n", val);
            return EXIT_FAILURE;
        }
        return main_test_fan(val);
    }
    return EXIT_SUCCESS;
}

static void main_init_share(void) {
    void* shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED,
            -1, 0);
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

static int main_ec_worker(void) {
    setuid(0);
    if (debug_mode) printf("[DEBUG] Worker started, attempting to modprobe ec_sys\n");
    system("modprobe ec_sys");
    
    // Try to determine if sysfs method is available
    int sysfs_available = 0;
    int io_fd = open("/sys/kernel/debug/ec/ec0/io", O_RDONLY, 0);
    if (io_fd >= 0) {
        sysfs_available = 1;
        close(io_fd);
        if (debug_mode) printf("[DEBUG] sysfs method available\n");
    } else {
        if (debug_mode) printf("[DEBUG] sysfs method not available, falling back to direct I/O\n");
    }
    
    int loop_count = 0;
    while (share_info->exit == 0) {
        if (debug_mode) printf("[DEBUG] Worker loop iteration %d\n", loop_count++);
        // check parent
        if (parent_pid != 0 && kill(parent_pid, 0) == -1) {
            if (debug_mode) printf("[DEBUG] worker on parent death\n");
            break;
        }
        // write EC
        int new_fan_duty = share_info->manual_next_fan_duty;
        if (new_fan_duty != 0 && new_fan_duty != share_info->manual_prev_fan_duty) {
            if (debug_mode) printf("[DEBUG] Writing new fan duty: %d\n", new_fan_duty);
            int write_result = ec_write_fan_duty(new_fan_duty);
            if (debug_mode) printf("[DEBUG] ec_write_fan_duty returned: %d\n", write_result);
            share_info->manual_prev_fan_duty = new_fan_duty;
        }
        
        // read EC - try sysfs first, fall back to direct I/O
        if (sysfs_available) {
            int io_fd = open("/sys/kernel/debug/ec/ec0/io", O_RDONLY, 0);
            if (io_fd < 0) {
                if (debug_mode) printf("[DEBUG] sysfs method failed, switching to direct I/O\n");
                sysfs_available = 0;
            } else {
                unsigned char buf[EC_REG_SIZE];
                ssize_t len = read(io_fd, buf, EC_REG_SIZE);
                close(io_fd);
                if (debug_mode) printf("[DEBUG] sysfs read returned len=%ld\n", len);
                switch (len) {
                case -1:
                    if (debug_mode) printf("[DEBUG] unable to read EC from sysfs: %s\n", strerror(errno));
                    sysfs_available = 0;
                    break;
                case 0x100:
                    share_info->cpu_temp = buf[EC_REG_CPU_TEMP];
                    share_info->gpu_temp = buf[EC_REG_GPU_TEMP];
                    share_info->fan_duty = calculate_fan_duty(buf[EC_REG_FAN_DUTY]);
                    share_info->fan_rpms = calculate_fan_rpms(buf[EC_REG_FAN_RPMS_HI], buf[EC_REG_FAN_RPMS_LO]);
                    if (debug_mode) printf("[DEBUG] sysfs: cpu_temp=%d, gpu_temp=%d, fan_duty=%d, fan_rpms=%d\n", share_info->cpu_temp, share_info->gpu_temp, share_info->fan_duty, share_info->fan_rpms);
                    break;
                default:
                    if (debug_mode) printf("[DEBUG] wrong EC size from sysfs: %ld\n", len);
                    sysfs_available = 0;
                }
            }
        }
        
        // Fall back to direct I/O if sysfs is not available
        if (!sysfs_available) {
            if (debug_mode) printf("[DEBUG] Using direct I/O for EC access\n");
            share_info->cpu_temp = ec_query_cpu_temp();
            share_info->gpu_temp = ec_query_gpu_temp();
            share_info->fan_duty = ec_query_fan_duty();
            share_info->fan_rpms = ec_query_fan_rpms();
            if (debug_mode) printf("[DEBUG] direct I/O: cpu_temp=%d, gpu_temp=%d, fan_duty=%d, fan_rpms=%d\n", share_info->cpu_temp, share_info->gpu_temp, share_info->fan_duty, share_info->fan_rpms);
        }
        
        // auto EC
        if (share_info->auto_duty == 1) {
            int next_duty = ec_auto_duty_adjust();
            if (debug_mode) printf("[DEBUG] auto_duty=1, next_duty=%d, prev_auto_duty_val=%d\n", next_duty, share_info->auto_duty_val);
            if (next_duty != 0 && next_duty != share_info->auto_duty_val) {
                char s_time[256];
                get_time_string(s_time, 256, "%m/%d %H:%M:%S");
                printf("%s CPU=%d°C, GPU=%d°C, auto fan duty to %d%%\n", s_time, share_info->cpu_temp, share_info->gpu_temp, next_duty);
                int write_result = ec_write_fan_duty(next_duty);
                if (debug_mode) printf("[DEBUG] ec_write_fan_duty (auto) returned: %d\n", write_result);
                share_info->auto_duty_val = next_duty;
            }
        }
        //
        usleep(200 * 1000);
    }
    if (debug_mode) printf("[DEBUG] Worker quit (share_info->exit=%d)\n", share_info->exit);
    return EXIT_SUCCESS;
}

static void main_ui_worker(int argc, char** argv) {
    if (debug_mode) printf("Indicator...\n");
    int desktop_uid = getuid();
    setuid(desktop_uid);
    //
    gtk_init(&argc, &argv);
    //
    GtkWidget* indicator_menu = gtk_menu_new();
    for (int i = 0; i < menuitem_count; i++) {
        GtkWidget* item;
        if (strlen(menuitems[i].label) == 0) {
            item = gtk_separator_menu_item_new();
        } else {
            item = gtk_menu_item_new_with_label(menuitems[i].label);
            g_signal_connect_swapped(item, "activate",
                    G_CALLBACK(menuitems[i].callback),
                    (void* ) menuitems[i].option);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(indicator_menu), item);
        menuitems[i].widget = item;
    }
    gtk_widget_show_all(indicator_menu);
    //
    indicator = app_indicator_new(NAME, "brasero",
            APP_INDICATOR_CATEGORY_HARDWARE);
    g_assert(IS_APP_INDICATOR(indicator));
    app_indicator_set_label(indicator, "Init..", "XX");
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ATTENTION);
    app_indicator_set_ordering_index(indicator, -2);
    app_indicator_set_title(indicator, "Clevo");
    app_indicator_set_menu(indicator, GTK_MENU(indicator_menu));
    g_timeout_add(500, &ui_update, NULL);
    ui_toggle_menuitems(share_info->fan_duty);
    
    // Print initial temperature status
    printf("Clevo Fan Control Indicator Started\n");
    printf("Current Status:\n");
    printf("  CPU: %d°C\n", share_info->cpu_temp);
    printf("  GPU: %d°C\n", share_info->gpu_temp);
    printf("  Fan: %d RPM (%d%% duty)\n", share_info->fan_rpms, share_info->fan_duty);
    printf("  Mode: %s\n", share_info->auto_duty ? "AUTO" : "MANUAL");
    printf("Press Ctrl+C to exit\n\n");
    
    gtk_main();
    if (debug_mode) printf("main on UI quit\n");
}

static void main_on_sigchld(int signum) {
    if (debug_mode) printf("main on worker quit signal\n");
    exit(EXIT_SUCCESS);
}

static void main_on_sigterm(int signum) {
    if (debug_mode) printf("main on signal: %s\n", strsignal(signum));
    if (status_mode) {
        status_display_cleanup();
    }
    if (share_info != NULL)
        share_info->exit = 1;
    exit(EXIT_SUCCESS);
}

static int main_dump_fan(void) {
    printf("Dump fan information\n");
    printf("  FAN Duty: %d%%\n", ec_query_fan_duty());
    printf("  FAN RPMs: %d RPM\n", ec_query_fan_rpms());
    printf("  CPU Temp: %d°C\n", ec_query_cpu_temp());
    printf("  GPU Temp: %d°C\n", ec_query_gpu_temp());
    return EXIT_SUCCESS;
}

static int main_test_fan(int duty_percentage) {
    printf("Change fan duty to %d%%\n", duty_percentage);
    ec_write_fan_duty(duty_percentage);
    printf("\n");
    main_dump_fan();
    return EXIT_SUCCESS;
}

static gboolean ui_update(gpointer user_data) {
    char label[256];
    sprintf(label, "%d℃ %d℃", share_info->cpu_temp, share_info->gpu_temp);
    app_indicator_set_label(indicator, label, "XXXXXX");
    char icon_name[256];
    double load = ((double) share_info->fan_rpms) / MAX_FAN_RPM * 100.0;
    double load_r = round(load / 5.0) * 5.0;
    sprintf(icon_name, "brasero-disc-%02d", (int) load_r);
    app_indicator_set_icon(indicator, icon_name);
    
    // Print temperature status at configurable intervals
    static int update_counter = 0;
    update_counter++;
    int output_ticks = temp_output_interval * 2; // Convert seconds to ticks (500ms per tick)
    if (update_counter >= output_ticks) {
        update_counter = 0;
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        printf("[%s] CPU: %d°C, GPU: %d°C, Fan: %d RPM (%d%% duty), Mode: %s\n",
               time_str, share_info->cpu_temp, share_info->gpu_temp, 
               share_info->fan_rpms, share_info->fan_duty,
               share_info->auto_duty ? "AUTO" : "MANUAL");
    }
    
    return G_SOURCE_CONTINUE;
}

static void ui_command_set_fan(long fan_duty) {
    int fan_duty_val = (int) fan_duty;
    if (fan_duty_val == 0) {
        if (debug_mode) printf("clicked on fan duty auto\n");
        share_info->auto_duty = 1;
        share_info->auto_duty_val = 0;
        share_info->manual_next_fan_duty = 0;
        // Reset PID controller when switching to auto mode
        pid_reset();
    } else {
        if (debug_mode) printf("clicked on fan duty: %d\n", fan_duty_val);
        share_info->auto_duty = 0;
        share_info->auto_duty_val = 0;
        share_info->manual_next_fan_duty = fan_duty_val;
        // Reset PID controller when switching to manual mode
        pid_reset();
    }
    ui_toggle_menuitems(fan_duty_val);
}

static void ui_command_quit(gchar* command) {
    if (debug_mode) printf("clicked on quit\n");
    gtk_main_quit();
}

static void ui_command_show_temp(gchar* command) {
    if (debug_mode) printf("clicked on show temperatures\n");
    // In indicator mode, we just print the current temperatures
    printf("Current Temperatures:\n");
    printf("  CPU: %d°C\n", ec_query_cpu_temp());
    printf("  GPU: %d°C\n", ec_query_gpu_temp());
    printf("  Fan: %d RPM\n", ec_query_fan_rpms());
    printf("  Duty: %d%%\n", ec_query_fan_duty());
}

static void ui_toggle_menuitems(int fan_duty) {
    for (int i = 0; i < menuitem_count; i++) {
        if (menuitems[i].widget == NULL)
            continue;
        if (fan_duty == 0)
            gtk_widget_set_sensitive(menuitems[i].widget,
                    menuitems[i].type != AUTO);
        else
            gtk_widget_set_sensitive(menuitems[i].widget,
                    menuitems[i].type != MANUAL
                            || (int) menuitems[i].option != fan_duty);
    }
}

static int ec_init(void) {
    if (ioperm(EC_DATA, 1, 1) != 0)
        return EXIT_FAILURE;
    if (ioperm(EC_SC, 1, 1) != 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static void ec_on_sigterm(int signum) {
    if (debug_mode) printf("ec on signal: %s\n", strsignal(signum));
    if (share_info != NULL)
        share_info->exit = 1;
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
        printf("[DEBUG] PID: temp=%d, setpoint=%.1f, error=%.1f, p=%.1f, i=%.1f, d=%.1f, output=%.1f, duty=%d\n",
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
        printf("Wrong fan duty to write: %d\n", duty_percentage);
        return EXIT_FAILURE;
    }
    double v_d = ((double) duty_percentage) / 100.0 * 255.0;
    int v_i = (int) v_d;
    return ec_io_do(0x99, 0x01, v_i);
}

static int ec_io_wait(const uint32_t port, const uint32_t flag,
        const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    if (i >= 100) {
        printf("wait_ec error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x\n",
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

    //wait_ec(EC_SC, EC_SC_IBF_FREE);
    ec_io_wait(EC_SC, OBF, 1);
    uint8_t value = inb(EC_DATA);

    return value;
}

static int ec_io_do(const uint32_t cmd, const uint32_t port,
        const uint8_t value) {
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
    int proc_name_len = strlen(proc_name);
    pid_t this_pid = getpid();
    DIR* dir;
    if (!(dir = opendir("/proc"))) {
        perror("can't open /proc");
        return -1;
    }
    int instance_count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        char* endptr;
        long lpid = strtol(ent->d_name, &endptr, 10);
        if (*endptr != '\0')
            continue;
        if (lpid == this_pid)
            continue;
        char buf[512];
        snprintf(buf, sizeof(buf), "/proc/%ld/comm", lpid);
        FILE* fp = fopen(buf, "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp) != NULL) {
                if ((buf[proc_name_len] == '\n' || buf[proc_name_len] == '\0')
                        && strncmp(buf, proc_name, proc_name_len) == 0) {
                    fprintf(stderr, "Process: %ld\n", lpid);
                    instance_count += 1;
                }
            }
            fclose(fp);
        }
    }
    closedir(dir);
    return instance_count;
}

static void get_time_string(char* buffer, size_t max, const char* format) {
    time_t timer;
    struct tm tm_info;
    time(&timer);
    localtime_r(&timer, &tm_info);
    strftime(buffer, max, format, &tm_info);
}

static void signal_term(__sighandler_t handler) {
    signal(SIGHUP, handler);
    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
    signal(SIGPIPE, handler);
    signal(SIGALRM, handler);
    signal(SIGTERM, handler);
    signal(SIGUSR1, handler);
    signal(SIGUSR2, handler);
}

static void parse_command_line(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        } else if (strcmp(argv[i], "--status") == 0) {
            status_mode = 1;
        } else if (strcmp(argv[i], "--interval") == 0) {
            if (i + 1 < argc) {
                status_interval = atof(argv[i + 1]);
                if (status_interval < 0.1) status_interval = 0.1;
                if (status_interval > 60.0) status_interval = 60.0;
                i++; // Skip the next argument
            } else {
                printf("Error: --interval requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--target-temp") == 0) {
            if (i + 1 < argc) {
                target_temperature = atoi(argv[i + 1]);
                if (target_temperature < 40) target_temperature = 40;
                if (target_temperature > 100) target_temperature = 100;
                i++; // Skip the next argument
            } else {
                printf("Error: --target-temp requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--temp-output-interval") == 0) {
            if (i + 1 < argc) {
                temp_output_interval = atoi(argv[i + 1]);
                if (temp_output_interval < 5) temp_output_interval = 5;
                if (temp_output_interval > 300) temp_output_interval = 300;
                i++; // Skip the next argument
            } else {
                printf("Error: --temp-output-interval requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--pid-kp") == 0) {
            if (i + 1 < argc) {
                pid_kp = atof(argv[i + 1]);
                i++; // Skip the next argument
            } else {
                printf("Error: --pid-kp requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--pid-ki") == 0) {
            if (i + 1 < argc) {
                pid_ki = atof(argv[i + 1]);
                i++; // Skip the next argument
            } else {
                printf("Error: --pid-ki requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--pid-kd") == 0) {
            if (i + 1 < argc) {
                pid_kd = atof(argv[i + 1]);
                i++; // Skip the next argument
            } else {
                printf("Error: --pid-kd requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--pid-output-min") == 0) {
            if (i + 1 < argc) {
                pid_output_min = atof(argv[i + 1]);
                i++; // Skip the next argument
            } else {
                printf("Error: --pid-output-min requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--pid-output-max") == 0) {
            if (i + 1 < argc) {
                pid_output_max = atof(argv[i + 1]);
                i++; // Skip the next argument
            } else {
                printf("Error: --pid-output-max requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--pid-enabled") == 0) {
            if (i + 1 < argc) {
                pid_enabled = atoi(argv[i + 1]);
                i++; // Skip the next argument
            } else {
                printf("Error: --pid-enabled requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--adaptive-pid") == 0) {
            if (i + 1 < argc) {
                adaptive_pid_enabled = atoi(argv[i + 1]);
                i++; // Skip the next argument
            } else {
                printf("Error: --adaptive-pid requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--adaptive-tuning-interval") == 0) {
            if (i + 1 < argc) {
                adaptive_tuning_interval = atoi(argv[i + 1]);
                if (adaptive_tuning_interval < 10) adaptive_tuning_interval = 10;
                if (adaptive_tuning_interval > 300) adaptive_tuning_interval = 300;
                i++; // Skip the next argument
            } else {
                printf("Error: --adaptive-tuning-interval requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--adaptive-target-performance") == 0) {
            if (i + 1 < argc) {
                adaptive_target_performance = atof(argv[i + 1]);
                if (adaptive_target_performance < 0.1) adaptive_target_performance = 0.1;
                if (adaptive_target_performance > 1.0) adaptive_target_performance = 1.0;
                i++; // Skip the next argument
            } else {
                printf("Error: --adaptive-target-performance requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--adaptive-rapid-cycles") == 0) {
            if (i + 1 < argc) {
                adaptive_rapid_learning_max = atoi(argv[i + 1]);
                if (adaptive_rapid_learning_max < 1) adaptive_rapid_learning_max = 1;
                if (adaptive_rapid_learning_max > 50) adaptive_rapid_learning_max = 50;
                i++; // Skip the next argument
            } else {
                printf("Error: --adaptive-rapid-cycles requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--adaptive-rapid-multiplier") == 0) {
            if (i + 1 < argc) {
                adaptive_rapid_step_multiplier = atof(argv[i + 1]);
                if (adaptive_rapid_step_multiplier < 1.0) adaptive_rapid_step_multiplier = 1.0;
                if (adaptive_rapid_step_multiplier > 10.0) adaptive_rapid_step_multiplier = 10.0;
                i++; // Skip the next argument
            } else {
                printf("Error: --adaptive-rapid-multiplier requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--adaptive-steady-threshold") == 0) {
            if (i + 1 < argc) {
                adaptive_steady_state_threshold = atof(argv[i + 1]);
                if (adaptive_steady_state_threshold < 0.01) adaptive_steady_state_threshold = 0.01;
                if (adaptive_steady_state_threshold > 0.2) adaptive_steady_state_threshold = 0.2;
                i++; // Skip the next argument
            } else {
                printf("Error: --adaptive-steady-threshold requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--adaptive-steady-cycles") == 0) {
            if (i + 1 < argc) {
                adaptive_steady_state_cycles_required = atoi(argv[i + 1]);
                if (adaptive_steady_state_cycles_required < 1) adaptive_steady_state_cycles_required = 1;
                if (adaptive_steady_state_cycles_required > 20) adaptive_steady_state_cycles_required = 20;
                i++; // Skip the next argument
            } else {
                printf("Error: --adaptive-steady-cycles requires a value\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "--pid-reset") == 0) {
            pid_reset();
            printf("PID controller state reset.\n");
            i++; // Skip the next argument
        } else if (strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "--help") == 0) {
            printf(
                    "\n\
Usage: clevo-indicator [OPTIONS] [fan-duty-percentage]\n\
\n\
Dump/Control fan duty on Clevo laptops. Display indicator by default.\n\
\n\
Options:\n\
  --debug\t\tEnable debug output\n\
  --status\t\tEnable live status display mode\n\
  --interval <sec>\tSet status update interval (0.1-60.0 seconds, default: 2.0)\n\
  --target-temp <\u00b0C>\tSet the target temperature for auto fan control (40-100\u00b0C, default: 65)\n\
  --temp-output-interval <sec>\tSet temperature output interval (5-300 seconds, default: 30)\n\
  --pid-kp <value>\tSet PID Proportional gain (default: 2.0)\n\
  --pid-ki <value>\tSet PID Integral gain (default: 0.1)\n\
  --pid-kd <value>\tSet PID Derivative gain (default: 0.5)\n\
  --pid-output-min <value>\tSet PID output minimum (default: 0.0)\n\
  --pid-output-max <value>\tSet PID output maximum (default: 100.0)\n\
  --pid-enabled <0|1>\tEnable/Disable PID control (default: 1)\n\
  --pid-reset\t\tReset PID controller state (integral, error, output)\n\
  --adaptive-pid <0|1>\tEnable/Disable adaptive PID tuning (default: 1)\n\
  --adaptive-tuning-interval <sec>\tSet adaptive tuning interval (10-300s, default: 30)\n\
  --adaptive-target-performance <value>\tSet target performance score (0.1-1.0, default: 0.8)\n\
  --adaptive-rapid-cycles <num>\tSet rapid learning cycles (1-50, default: 10)\n\
  --adaptive-rapid-multiplier <value>\tSet rapid learning step multiplier (1.0-10.0, default: 3.0)\n\
  --adaptive-steady-threshold <value>\tSet steady state threshold (0.01-0.2, default: 0.05)\n\
  --adaptive-steady-cycles <num>\tSet steady state cycles required (1-20, default: 5)\n\
  -?, --help\t\tDisplay this help and exit\n\
\n\
Arguments:\n\
  [fan-duty-percentage]\tTarget fan duty in percentage, from 40 to 100\n\
\n\
Status Display Mode:\n\
  When --status is used, displays a live updating console interface\n\
  showing temperatures, fan speeds, and control status with visual\n\
  indicators and color coding.\n\
\
Target Temperature Control:\n\
  Use --target-temp to set the desired temperature for auto fan control.\n\
  The system will attempt to keep temperatures at or below this value.\n\
  Example: --target-temp 60 will try to keep temps below 60\u00b0C.\n\
\n\
PID Controller:\n\
  The program now includes a sophisticated PID (Proportional-Integral-Derivative)\n\
  controller for smooth fan control that minimizes oscillation and provides\n\
  stable temperature regulation.\n\
\n\
  PID Parameters:\n\
    --pid-kp: Proportional gain (default: 2.0) - Controls response speed\n\
    --pid-ki: Integral gain (default: 0.1) - Eliminates steady-state error\n\
    --pid-kd: Derivative gain (default: 0.5) - Reduces overshoot and oscillation\n\
\n\
  Tuning Guidelines:\n\
    - Start with default values for most systems\n\
    - Increase Kp for faster response (but may cause oscillation)\n\
    - Increase Ki to eliminate temperature offset from target\n\
    - Increase Kd to reduce overshoot and oscillation\n\
    - Use --pid-reset to clear controller state if needed\n\
\n\
  Example tuning for aggressive cooling:\n\
    --pid-kp 3.0 --pid-ki 0.2 --pid-kd 0.8\n\
\n\
    Example tuning for quiet operation:\n\
    --pid-kp 1.5 --pid-ki 0.05 --pid-kd 0.3\n\
\n\
Adaptive PID Controller:\n\
  The system includes an adaptive PID controller that automatically tunes its\n\
  parameters based on performance metrics. It learns from temperature control\n\
  effectiveness and adjusts Kp, Ki, and Kd values to optimize performance.\n\
\n\
  Adaptive Features:\n\
    - Performance scoring based on error, oscillation, and fan efficiency\n\
    - Automatic parameter adjustment every 30 seconds (configurable)\n\
    - Learning cycles that track improvement over time\n\
    - Oscillation detection and damping\n\
    - Rapid learning phase for quick initial adaptation\n\
    - Steady state detection for conservative fine-tuning\n\
\n\
  Learning Phases:\n\
    1. Rapid Learning (first 10 cycles): Fast adaptation with 3x step sizes\n\
    2. Normal Tuning: Standard adaptation until steady state detected\n\
    3. Steady State: Conservative tuning when performance is stable\n\
\n\
  Adaptive Parameters:\n\
    --adaptive-pid: Enable/disable adaptive tuning\n\
    --adaptive-tuning-interval: How often to tune parameters (seconds)\n\
    --adaptive-target-performance: Target performance score (0.1-1.0)\n\
    --adaptive-rapid-cycles: Number of rapid learning cycles (1-50)\n\
    --adaptive-rapid-multiplier: Step size multiplier for rapid learning (1.0-10.0)\n\
    --adaptive-steady-threshold: Performance stability threshold (0.01-0.2)\n\
    --adaptive-steady-cycles: Cycles required for steady state (1-20)\n\
\n\
  Example adaptive configurations:\n\
    # Conservative adaptive tuning\n\
    --adaptive-tuning-interval 60 --adaptive-target-performance 0.7\n\
\n\
    # Aggressive adaptive tuning\n\
    --adaptive-tuning-interval 15 --adaptive-target-performance 0.9\n\
\n\
    # Rapid learning with extended initial phase\n\
    --adaptive-rapid-cycles 20 --adaptive-rapid-multiplier 5.0\n\
\n\
    # Conservative steady state detection\n\
    --adaptive-steady-threshold 0.03 --adaptive-steady-cycles 8\n\
\n\
  Modern Privilege Management:\n\
This program now supports multiple privilege elevation methods:\n\
\n\
1. Capabilities (Recommended):\n\
   sudo setcap cap_sys_rawio+ep bin/clevo-indicator\n\
\n\
2. Systemd Service (Background):\n\
   sudo cp systemd/clevo-indicator.service /etc/systemd/user/\n\
   systemctl --user enable clevo-indicator.service\n\
\n\
3. Traditional setuid:\n\
   sudo chown root bin/clevo-indicator\n\
   sudo chmod u+s bin/clevo-indicator\n\
\n\
Note any fan duty change should take 1-2 seconds to come into effect - you\n\
can verify by the fan speed displayed on indicator icon and also louder fan\n\
noise.\n\
\n\
In the indicator mode, this program would always attempt to load kernel\n\
module 'ec_sys', in order to query EC information from\n\
'/sys/kernel/debug/ec/ec0/io' instead of polling EC ports for readings,\n\
which may be more risky if interrupted or concurrently operated during the\n\
process.\n\
\n\
DO NOT MANIPULATE OR QUERY EC I/O PORTS WHILE THIS PROGRAM IS RUNNING.\n\
\n");
            exit(EXIT_SUCCESS);
        }
    }
}

static bool setup_privileges(void) {
    privilege_manager_init();
    
    privilege_status_t status = privilege_check_status();
    privilege_method_t best_method = privilege_get_best_method();
    
    if (debug_mode) {
        printf("[DEBUG] Current privilege status:\n");
        printf("[DEBUG]   Effective UID: %d\n", status.effective_uid);
        printf("[DEBUG]   Real UID: %d\n", status.real_uid);
        printf("[DEBUG]   Has privileges: %s\n", status.has_privileges ? "yes" : "no");
        printf("[DEBUG]   Best method: %s\n", privilege_method_name(best_method));
    }
    
    if (status.has_privileges) {
        if (debug_mode) printf("[DEBUG] Already have privileges\n");
        return true;
    }
    
    if (best_method == PRIV_METHOD_NONE) {
        printf("No privilege elevation method available.\n");
        show_privilege_help();
        return false;
    }
    
    if (debug_mode) printf("[DEBUG] Attempting to elevate privileges using %s\n", privilege_method_name(best_method));
    
    if (!privilege_elevate()) {
        printf("Failed to elevate privileges: %s\n", 
               status.error_message ? status.error_message : "unknown error");
        show_privilege_help();
        return false;
    }
    
    if (debug_mode) printf("[DEBUG] Successfully elevated privileges\n");
    return true;
}

static void show_privilege_help(void) {
    printf("\nPrivilege Setup Options:\n");
    printf("========================\n\n");
    
    printf("1. Capabilities (Recommended):\n");
    printf("   sudo setcap cap_sys_rawio+ep bin/clevo-indicator\n\n");
    
    printf("2. Systemd Service (Background):\n");
    printf("   sudo cp systemd/clevo-indicator.service /etc/systemd/user/\n");
    printf("   systemctl --user enable clevo-indicator.service\n");
    printf("   systemctl --user start clevo-indicator.service\n\n");
    
    printf("3. Setuid (Traditional):\n");
    printf("   sudo chown root bin/clevo-indicator\n");
    printf("   sudo chmod u+s bin/clevo-indicator\n\n");
    
    printf("4. Polkit Policy (Modern):\n");
    printf("   sudo cp polkit/org.freedesktop.policykit.clevo-indicator.policy /usr/share/polkit-1/actions/\n");
    printf("   sudo systemctl reload polkit\n\n");
    
    printf("5. Sudoers (Alternative):\n");
    printf("   echo '%%sudo ALL=(ALL) NOPASSWD: /usr/local/bin/clevo-indicator' | sudo tee /etc/sudoers.d/clevo-indicator\n\n");
}

// Status display functions
static void status_display_init(void) {
    // Set up terminal for better display
    printf("\033[?25l"); // Hide cursor
    status_clear_screen();
}

static void status_display_cleanup(void) {
    printf("\033[?25h"); // Show cursor
    printf("\033[0m");   // Reset colors
    printf("\n");
}

static void status_clear_screen(void) {
    printf("\033[2J");   // Clear screen
    printf("\033[H");    // Move cursor to top-left
}

static void pid_reset(void) {
    pid_integral = 0.0;
    pid_prev_error = 0.0;
    // Reset temperature tracking
    prev_cpu_temp = 0;
    prev_gpu_temp = 0;
    cpu_temp_rate = 0.0;
    gpu_temp_rate = 0.0;
    last_temp_update = 0;
    // Reset adaptive PID if enabled
    if (adaptive_pid_enabled) {
        adaptive_pid_reset();
    }
    if (debug_mode) printf("[DEBUG] PID controller, temperature tracking, and adaptive controller reset\n");
}

static void calculate_temp_rate_of_change(void) {
    time_t current_time = time(NULL);
    
    if (last_temp_update != 0) {
        double time_diff = difftime(current_time, last_temp_update);
        if (time_diff > 0) {
            cpu_temp_rate = (double)(share_info->cpu_temp - prev_cpu_temp) / time_diff;
            gpu_temp_rate = (double)(share_info->gpu_temp - prev_gpu_temp) / time_diff;
        }
    }
    
    prev_cpu_temp = share_info->cpu_temp;
    prev_gpu_temp = share_info->gpu_temp;
    last_temp_update = current_time;
}

static char* get_temp_trend_symbol(double rate) {
    if (rate > 2.0) return "↗↗";  // Rapidly increasing
    if (rate > 0.5) return "↗";   // Increasing
    if (rate < -2.0) return "↘↘"; // Rapidly decreasing
    if (rate < -0.5) return "↘";   // Decreasing
    return "→";                    // Stable
}

static char* get_temp_trend_color(double rate) {
    if (rate > 2.0) return "\033[31m";  // Red for rapidly increasing
    if (rate > 0.5) return "\033[33m";  // Yellow for increasing
    if (rate < -2.0) return "\033[32m"; // Green for rapidly decreasing
    if (rate < -0.5) return "\033[36m"; // Cyan for decreasing
    return "\033[37m";                   // White for stable
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
    
    // Determine if we're in rapid learning mode or steady state
    bool rapid_learning = (adaptive_rapid_learning_cycles < adaptive_rapid_learning_max);
    bool approaching_steady_state = (adaptive_consecutive_stable_cycles >= adaptive_steady_state_cycles_required);
    
    // Calculate dynamic step sizes based on learning phase
    double step_multiplier = 1.0;
    if (rapid_learning) {
        step_multiplier = adaptive_rapid_step_multiplier;
    } else if (approaching_steady_state) {
        step_multiplier = 0.3;  // Conservative tuning in steady state
    } else {
        step_multiplier = 1.0;  // Normal tuning
    }
    
    double current_kp_step = adaptive_kp_step * step_multiplier;
    double current_ki_step = adaptive_ki_step * step_multiplier;
    double current_kd_step = adaptive_kd_step * step_multiplier;
    
    if (debug_mode) {
        printf("[DEBUG] Adaptive PID: Score=%.3f, Change=%.3f, Kp=%.2f, Ki=%.3f, Kd=%.2f\n",
               current_score, score_change, pid_kp, pid_ki, pid_kd);
        printf("[DEBUG] Learning: Rapid=%s, Steady=%s, StepMult=%.1f\n",
               rapid_learning ? "YES" : "NO", approaching_steady_state ? "YES" : "NO", step_multiplier);
    }
    
    // Check for steady state (stable performance)
    if (fabs(score_change) < adaptive_steady_state_threshold) {
        adaptive_consecutive_stable_cycles++;
    } else {
        adaptive_consecutive_stable_cycles = 0;
    }
    
    // Adjust parameters based on performance
    if (score_change > 0.05) {
        // Performance improved, continue in same direction
        if (debug_mode) printf("[DEBUG] Adaptive PID: Performance improved, maintaining direction\n");
    } else if (score_change < -0.05) {
        // Performance degraded, reverse direction
        current_kp_step *= -0.8;
        current_ki_step *= -0.8;
        current_kd_step *= -0.8;
        if (debug_mode) printf("[DEBUG] Adaptive PID: Performance degraded, reversing direction\n");
    }
    
    // Adjust Kp (proportional gain)
    if (current_score < adaptive_target_performance) {
        pid_kp += current_kp_step;
        if (pid_kp < 0.5) pid_kp = 0.5;
        if (pid_kp > 5.0) pid_kp = 5.0;
    }
    
    // Adjust Ki (integral gain)
    double oscillation = adaptive_pid_calculate_oscillation();
    int temp = MAX(share_info->cpu_temp, share_info->gpu_temp);
    double error = fabs((double)temp - (double)target_temperature);
    
    if (oscillation > 3.0) {
        // High oscillation, reduce Ki and increase Kd
        pid_ki -= current_ki_step;
        pid_kd += current_kd_step;
    } else if (error > 5.0) {
        // High error, increase Ki
        pid_ki += current_ki_step;
    }
    
    // Clamp Ki and Kd values
    if (pid_ki < 0.01) pid_ki = 0.01;
    if (pid_ki > 0.5) pid_ki = 0.5;
    if (pid_kd < 0.1) pid_kd = 0.1;
    if (pid_kd > 2.0) pid_kd = 2.0;
    
    adaptive_prev_score = current_score;
    adaptive_performance_score = current_score;
    adaptive_learning_cycles++;
    
    // Update rapid learning cycle counter
    if (rapid_learning) {
        adaptive_rapid_learning_cycles++;
    }
    
    if (debug_mode) {
        printf("[DEBUG] Adaptive PID: New parameters - Kp=%.2f, Ki=%.3f, Kd=%.2f\n",
               pid_kp, pid_ki, pid_kd);
        printf("[DEBUG] Learning Progress: Rapid=%d/%d, Stable=%d/%d\n",
               adaptive_rapid_learning_cycles, adaptive_rapid_learning_max,
               adaptive_consecutive_stable_cycles, adaptive_steady_state_cycles_required);
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
    
    // Reset rapid learning variables
    adaptive_rapid_learning_cycles = 0;
    adaptive_consecutive_stable_cycles = 0;
    
    // Reset step sizes to defaults
    adaptive_kp_step = 0.1;
    adaptive_ki_step = 0.01;
    adaptive_kd_step = 0.05;
    
    if (debug_mode) printf("[DEBUG] Adaptive PID controller reset (including rapid learning state)\n");
}

static char* status_get_color_code(int temp) {
    if (temp < 50) return "\033[32m";      // Green for cool
    if (temp < 70) return "\033[33m";      // Yellow for warm
    if (temp < 85) return "\033[31m";      // Red for hot
    return "\033[35m";                      // Magenta for critical
}

static char* status_get_temp_bar(int temp, int max_temp) {
    static char bar[21];
    int filled = (temp * 20) / max_temp;
    if (filled > 20) filled = 20;
    if (filled < 0) filled = 0;
    
    for (int i = 0; i < 20; i++) {
        if (i < filled) {
            bar[i] = '#';
        } else {
            bar[i] = '-';
        }
    }
    bar[20] = '\0';
    return bar;
}

static char* status_get_fan_bar(int rpm, int max_rpm) {
    static char bar[21];
    int filled = (rpm * 20) / max_rpm;
    if (filled > 20) filled = 20;
    if (filled < 0) filled = 0;
    
    for (int i = 0; i < 20; i++) {
        if (i < filled) {
            bar[i] = '#';
        } else {
            bar[i] = '-';
        }
    }
    bar[20] = '\0';
    return bar;
}

static void status_display_show_help(void) {
    printf("\033[1;36m=== Clevo Fan Control - Live Status ===\033[0m\n");
    printf("Press Ctrl+C to exit\n\n");
}



static void status_display_update_with_control(void) {
    // Update shared memory with current values
    share_info->cpu_temp = ec_query_cpu_temp();
    share_info->gpu_temp = ec_query_gpu_temp();
    share_info->fan_duty = ec_query_fan_duty();
    share_info->fan_rpms = ec_query_fan_rpms();
    
    // Calculate temperature rate of change
    calculate_temp_rate_of_change();
    
    // Run auto fan control logic
    if (share_info->auto_duty == 1) {
        int next_duty = ec_auto_duty_adjust();
        if (next_duty != 0 && next_duty != share_info->auto_duty_val) {
            char s_time[256];
            get_time_string(s_time, 256, "%m/%d %H:%M:%S");
            printf("%s CPU=%d°C, GPU=%d°C, auto fan duty to %d%%\n", s_time, share_info->cpu_temp, share_info->gpu_temp, next_duty);
            printf("[DEBUG] Attempting to set fan duty to %d\n", next_duty);
            int write_result = ec_write_fan_duty(next_duty);
            printf("[DEBUG] ec_write_fan_duty returned: %d\n", write_result);
            int verify_duty = ec_query_fan_duty();
            printf("[DEBUG] Fan duty after write: %d\n", verify_duty);
            if (debug_mode) printf("[DEBUG] ec_write_fan_duty (auto) returned: %d\n", write_result);
            share_info->auto_duty_val = next_duty;
            share_info->fan_duty = next_duty; // Update the displayed value
        }
    }
    
    // Get current time
    char time_str[64];
    get_time_string(time_str, sizeof(time_str), "%H:%M:%S");
    
    // Clear screen and move to top
    status_clear_screen();
    
    // Header
    printf("\033[1;36m=== Clevo Fan Control - Live Status ===\033[0m\n");
    printf("Time: %s | Update Interval: %.1fs\n\n", time_str, status_interval);
    
    // Temperature section with trends
    printf("\033[1mTemperatures:\033[0m\n");
    char* cpu_color = status_get_color_code(share_info->cpu_temp);
    char* gpu_color = status_get_color_code(share_info->gpu_temp);
    char* cpu_trend_color = get_temp_trend_color(cpu_temp_rate);
    char* gpu_trend_color = get_temp_trend_color(gpu_temp_rate);
    char* cpu_trend_symbol = get_temp_trend_symbol(cpu_temp_rate);
    char* gpu_trend_symbol = get_temp_trend_symbol(gpu_temp_rate);
    
    printf("CPU: %s[%s] %s%d°C\033[0m %s%s%.1f°C/s\033[0m\n", 
           cpu_color, status_get_temp_bar(share_info->cpu_temp, 100), cpu_color, share_info->cpu_temp,
           cpu_trend_color, cpu_trend_symbol, cpu_temp_rate);
    printf("GPU: %s[%s] %s%d°C\033[0m %s%s%.1f°C/s\033[0m\n", 
           gpu_color, status_get_temp_bar(share_info->gpu_temp, 100), gpu_color, share_info->gpu_temp,
           gpu_trend_color, gpu_trend_symbol, gpu_temp_rate);
    
    // Fan section
    printf("\n\033[1mFan Status:\033[0m\n");
    printf("Duty: %d%%\n", share_info->fan_duty);
    printf("RPM:  [%s] %d RPM\n", status_get_fan_bar(share_info->fan_rpms, 4400), share_info->fan_rpms);
    
    // Mode indicator with enhanced PID info
    printf("\n\033[1mControl Mode:\033[0m ");
    if (share_info->auto_duty == 1) {
        if (pid_enabled) {
            if (adaptive_pid_enabled) {
                printf("\033[32m[AUTO ADAPTIVE PID]\033[0m - Self-tuning PID control\n");
                printf("  Target: %d°C | Kp: %.2f | Ki: %.3f | Kd: %.2f\n", 
                       target_temperature, pid_kp, pid_ki, pid_kd);
                printf("  Performance: %.3f | Learning Cycles: %d | Tuning Interval: %ds\n",
                       adaptive_performance_score, adaptive_learning_cycles, adaptive_tuning_interval);
                
                // Show rapid learning status
                bool rapid_learning = (adaptive_rapid_learning_cycles < adaptive_rapid_learning_max);
                bool approaching_steady_state = (adaptive_consecutive_stable_cycles >= adaptive_steady_state_cycles_required);
                
                if (rapid_learning) {
                    printf("  \033[33m[RAPID LEARNING] %d/%d cycles\033[0m - Fast adaptation phase\n",
                           adaptive_rapid_learning_cycles, adaptive_rapid_learning_max);
                } else if (approaching_steady_state) {
                    printf("  \033[32m[STEADY STATE] %d/%d stable cycles\033[0m - Conservative tuning\n",
                           adaptive_consecutive_stable_cycles, adaptive_steady_state_cycles_required);
                } else {
                    printf("  \033[36m[NORMAL TUNING] %d/%d stable cycles\033[0m - Standard adaptation\n",
                           adaptive_consecutive_stable_cycles, adaptive_steady_state_cycles_required);
                }
            } else {
                printf("\033[32m[AUTO PID]\033[0m - PID-based temperature control\n");
                printf("  Target: %d°C | Kp: %.1f | Ki: %.2f | Kd: %.1f\n", 
                       target_temperature, pid_kp, pid_ki, pid_kd);
            }
            
            // Show PID error and components if in debug mode
            if (debug_mode) {
                int temp = MAX(share_info->cpu_temp, share_info->gpu_temp);
                double error = (double)temp - (double)target_temperature;
                double proportional = pid_kp * error;
                double integral = pid_ki * pid_integral;
                double derivative = pid_kd * (error - pid_prev_error);
                
                printf("  Error: %.1f°C | P: %.1f | I: %.1f | D: %.1f\n",
                       error, proportional, integral, derivative);
                
                if (adaptive_pid_enabled) {
                    double oscillation = adaptive_pid_calculate_oscillation();
                    printf("  Oscillation: %.2f | Temp History: %d samples\n",
                           oscillation, adaptive_temp_history_size);
                }
            }
        } else {
            printf("\033[32m[AUTO SIMPLE]\033[0m - Simple temperature-based control\n");
        }
    } else {
        printf("\033[33m[MANUAL: %d%%]\033[0m - Manual fan control\n", share_info->fan_duty);
    }
    
    // Status indicators
    printf("\n\033[1mStatus:\033[0m\n");
    if (share_info->cpu_temp > 80 || share_info->gpu_temp > 80) {
        printf("  \033[31m⚠ CRITICAL TEMPERATURE\033[0m\n");
    } else if (share_info->cpu_temp > 70 || share_info->gpu_temp > 70) {
        printf("  \033[33m⚠ HIGH TEMPERATURE\033[0m\n");
    } else {
        printf("  \033[32m✓ Normal operation\033[0m\n");
    }
    
    // Temperature trend summary
    printf("\n\033[1mTemperature Trends:\033[0m\n");
    if (cpu_temp_rate > 2.0 || gpu_temp_rate > 2.0) {
        printf("  \033[31m⚠ Rapid temperature increase\033[0m\n");
    } else if (cpu_temp_rate > 0.5 || gpu_temp_rate > 0.5) {
        printf("  \033[33m⚠ Temperature increasing\033[0m\n");
    } else if (cpu_temp_rate < -2.0 || gpu_temp_rate < -2.0) {
        printf("  \033[32m✓ Rapid cooling\033[0m\n");
    } else if (cpu_temp_rate < -0.5 || gpu_temp_rate < -0.5) {
        printf("  \033[36m✓ Cooling\033[0m\n");
    } else {
        printf("  \033[37m→ Temperature stable\033[0m\n");
    }
    
    // Footer
    printf("\n\033[2mPress Ctrl+C to exit\033[0m\n");
    fflush(stdout);
}
