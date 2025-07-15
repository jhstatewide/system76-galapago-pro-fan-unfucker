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
static void status_display_update(void);
static void status_display_update_with_control(void);
static void status_display_cleanup(void);
static void status_display_show_help(void);
static char* status_get_temp_bar(int temp, int max_temp);
static char* status_get_fan_bar(int rpm, int max_rpm);
static char* status_get_color_code(int temp);
static void status_clear_screen(void);

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
static int status_interval = 2; // Default 2 seconds
static int target_temperature = 65; // Default target temperature

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
            usleep(status_interval * 1000000); // Convert to microseconds
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
    return G_SOURCE_CONTINUE;
}

static void ui_command_set_fan(long fan_duty) {
    int fan_duty_val = (int) fan_duty;
    if (fan_duty_val == 0) {
        if (debug_mode) printf("clicked on fan duty auto\n");
        share_info->auto_duty = 1;
        share_info->auto_duty_val = 0;
        share_info->manual_next_fan_duty = 0;
    } else {
        if (debug_mode) printf("clicked on fan duty: %d\n", fan_duty_val);
        share_info->auto_duty = 0;
        share_info->auto_duty_val = 0;
        share_info->manual_next_fan_duty = fan_duty_val;
    }
    ui_toggle_menuitems(fan_duty_val);
}

static void ui_command_quit(gchar* command) {
    if (debug_mode) printf("clicked on quit\n");
    gtk_main_quit();
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

    // printf("Auto duty adjust: %d -> %d\n", duty, new_duty);

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
                status_interval = atoi(argv[i + 1]);
                if (status_interval < 1) status_interval = 1;
                if (status_interval > 60) status_interval = 60;
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
  --interval <sec>\tSet status update interval (1-60 seconds, default: 2)\n\
  --target-temp <\u00b0C>\tSet the target temperature for auto fan control (40-100\u00b0C, default: 65)\n\
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

static void status_display_update(void) {
    // Get current values
    int cpu_temp = ec_query_cpu_temp();
    int gpu_temp = ec_query_gpu_temp();
    int fan_duty = ec_query_fan_duty();
    int fan_rpms = ec_query_fan_rpms();
    
    // Get current time
    char time_str[64];
    get_time_string(time_str, sizeof(time_str), "%H:%M:%S");
    
    // Clear screen and move to top
    status_clear_screen();
    
    // Header
    printf("\033[1;36m=== Clevo Fan Control - Live Status ===\033[0m\n");
    printf("Time: %s | Update Interval: %ds\n\n", time_str, status_interval);
    
    // Temperature section
    printf("\033[1mTemperatures:\033[0m\n");
    char* cpu_color = status_get_color_code(cpu_temp);
    char* gpu_color = status_get_color_code(gpu_temp);
    
    printf("CPU: %s[%s] %s%d°C\033[0m\n", 
           cpu_color, status_get_temp_bar(cpu_temp, 100), cpu_color, cpu_temp);
    printf("GPU: %s[%s] %s%d°C\033[0m\n", 
           gpu_color, status_get_temp_bar(gpu_temp, 100), gpu_color, gpu_temp);
    
    // Fan section
    printf("\n\033[1mFan Status:\033[0m\n");
    printf("Duty: %d%%\n", fan_duty);
    printf("RPM:  [%s] %d RPM\n", status_get_fan_bar(fan_rpms, 4400), fan_rpms);
    
    // Mode indicator
    printf("\n\033[1mControl Mode:\033[0m ");
    if (fan_duty == 0) {
        printf("\033[32m[AUTO]\033[0m - Automatic temperature-based control\n");
    } else {
        printf("\033[33m[MANUAL: %d%%]\033[0m - Manual fan control\n", fan_duty);
    }
    
    // Status indicators
    printf("\n\033[1mStatus:\033[0m\n");
    if (cpu_temp > 80 || gpu_temp > 80) {
        printf("  \033[31m⚠ CRITICAL TEMPERATURE\033[0m\n");
    } else if (cpu_temp > 70 || gpu_temp > 70) {
        printf("  \033[33m⚠ HIGH TEMPERATURE\033[0m\n");
    } else {
        printf("  \033[32m✓ Normal operation\033[0m\n");
    }
    
    // Footer
    printf("\n\033[2mPress Ctrl+C to exit\033[0m\n");
    fflush(stdout);
}

static void status_display_update_with_control(void) {
    // Update shared memory with current values
    share_info->cpu_temp = ec_query_cpu_temp();
    share_info->gpu_temp = ec_query_gpu_temp();
    share_info->fan_duty = ec_query_fan_duty();
    share_info->fan_rpms = ec_query_fan_rpms();
    
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
    printf("Time: %s | Update Interval: %ds\n\n", time_str, status_interval);
    
    // Temperature section
    printf("\033[1mTemperatures:\033[0m\n");
    char* cpu_color = status_get_color_code(share_info->cpu_temp);
    char* gpu_color = status_get_color_code(share_info->gpu_temp);
    
    printf("CPU: %s[%s] %s%d°C\033[0m\n", 
           cpu_color, status_get_temp_bar(share_info->cpu_temp, 100), cpu_color, share_info->cpu_temp);
    printf("GPU: %s[%s] %s%d°C\033[0m\n", 
           gpu_color, status_get_temp_bar(share_info->gpu_temp, 100), gpu_color, share_info->gpu_temp);
    
    // Fan section
    printf("\n\033[1mFan Status:\033[0m\n");
    printf("Duty: %d%%\n", share_info->fan_duty);
    printf("RPM:  [%s] %d RPM\n", status_get_fan_bar(share_info->fan_rpms, 4400), share_info->fan_rpms);
    
    // Mode indicator
    printf("\n\033[1mControl Mode:\033[0m ");
    if (share_info->auto_duty == 1) {
        printf("\033[32m[AUTO]\033[0m - Automatic temperature-based control\n");
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
    
    // Footer
    printf("\n\033[2mPress Ctrl+C to exit\033[0m\n");
    fflush(stdout);
}
