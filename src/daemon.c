#include "daemon.h"
#include "logging.h"
#include "utils.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

daemon_context_t* daemon_init(void (*signal_handler)(int)) {
    daemon_context_t* context = malloc(sizeof(daemon_context_t));
    if (!context) {
        return NULL;
    }
    
    context->shared_info = NULL;
    context->running = 1;
    context->signal_handler = signal_handler;
    
    return context;
}

int daemon_init_shared_memory(daemon_context_t* context) {
    if (!context) return -1;
    
    void* shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (shm == MAP_FAILED) {
        logging_error("Failed to create shared memory");
        return -1;
    }
    
    context->shared_info = (daemon_shared_info_t*)shm;
    context->shared_info->exit = 0;
    context->shared_info->cpu_temp = 0;
    context->shared_info->fan_duty = 0;
    context->shared_info->fan_rpms = 0;
    context->shared_info->auto_duty = 1;
    context->shared_info->auto_duty_val = 0;
    context->shared_info->manual_next_fan_duty = 0;
    context->shared_info->manual_prev_fan_duty = 0;
    
    return 0;
}

int daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        logging_error("Failed to fork daemon process");
        return -1;
    }
    if (pid > 0) {
        // Parent process exits
        exit(EXIT_SUCCESS);
    }
    
    // Create new session
    pid_t sid = setsid();
    if (sid < 0) {
        logging_error("Failed to create new session");
        return -1;
    }
    
    // Change to root directory
    if ((chdir("/")) < 0) {
        logging_error("Failed to change to root directory");
        return -1;
    }
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    logging_info("Daemon process started");
    return 0;
}

void daemon_setup_signals(daemon_context_t* context) {
    if (!context || !context->signal_handler) return;
    
    signal_term(context->signal_handler);
}

bool daemon_is_running(daemon_context_t* context) {
    if (!context) return false;
    return context->running != 0;
}

void daemon_stop(daemon_context_t* context) {
    if (!context) return;
    
    context->running = 0;
    if (context->shared_info) {
        context->shared_info->exit = 1;
    }
    
    logging_info("Daemon stopping");
}

void daemon_update_shared_memory(daemon_context_t* context, int cpu_temp, int fan_duty, 
                               int fan_rpms, int auto_duty, int auto_duty_val) {
    if (!context || !context->shared_info) return;
    
    context->shared_info->cpu_temp = cpu_temp;
    context->shared_info->fan_duty = fan_duty;
    context->shared_info->fan_rpms = fan_rpms;
    context->shared_info->auto_duty = auto_duty;
    context->shared_info->auto_duty_val = auto_duty_val;
}

daemon_shared_info_t* daemon_get_shared_info(daemon_context_t* context) {
    if (!context) return NULL;
    return context->shared_info;
}

void daemon_cleanup(daemon_context_t* context) {
    if (!context) return;
    
    if (context->shared_info) {
        munmap(context->shared_info, 4096);
        context->shared_info = NULL;
    }
    
    free(context);
} 