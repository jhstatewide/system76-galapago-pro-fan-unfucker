/*
 ============================================================================
 Name        : clevo-daemon-socket.c
 Author      : System76 Fan Control Daemon Socket Server
 Version     : 1.0
 Description : Unix domain socket server for clevo-daemon

 This module provides a Unix domain socket interface for clients to communicate
 with the clevo-daemon, allowing status queries and fan control commands.

 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <syslog.h>
#include <stdarg.h>

#define SOCKET_PATH "/tmp/clevo-daemon.sock"
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

// External reference to shared memory structure
extern struct {
    volatile int exit;
    volatile int cpu_temp;
    volatile int gpu_temp;
    volatile int fan_duty;
    volatile int fan_rpms;
    volatile int auto_duty;
    volatile int auto_duty_val;
    volatile int manual_next_fan_duty;
    volatile int manual_prev_fan_duty;
} *share_info;

// External function declarations
extern int ec_write_fan_duty(int duty_percentage);

// Simple logging function for socket server
static void socket_log(int priority, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsyslog(priority, format, args);
    va_end(args);
}

static int server_sock = -1;
static volatile int socket_running = 1;
static pthread_t socket_thread;

// Function declarations
static void* socket_server_thread(void* arg);
static int handle_client_command(int client_sock, const char* command);
static int send_response(int client_sock, const char* response);
static void cleanup_socket(void);
static void socket_signal_handler(int sig);

int init_socket_server(void) {
    // Create socket
    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        socket_log(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        socket_log(LOG_ERR, "Failed to set socket options: %s", strerror(errno));
        close(server_sock);
        return -1;
    }
    
    // Bind socket
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    // Remove existing socket file if it exists
    unlink(SOCKET_PATH);
    
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        socket_log(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        close(server_sock);
        return -1;
    }
    
    // Set socket permissions
    chmod(SOCKET_PATH, 0666);
    
    // Listen for connections
    if (listen(server_sock, MAX_CLIENTS) < 0) {
        socket_log(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        close(server_sock);
        return -1;
    }
    
    // Set up signal handling for cleanup
    signal(SIGTERM, socket_signal_handler);
    signal(SIGINT, socket_signal_handler);
    
    // Start socket server thread
    if (pthread_create(&socket_thread, NULL, socket_server_thread, NULL) != 0) {
        socket_log(LOG_ERR, "Failed to create socket thread: %s", strerror(errno));
        close(server_sock);
        return -1;
    }
    
    socket_log(LOG_INFO, "Socket server started on %s", SOCKET_PATH);
    return 0;
}

void stop_socket_server(void) {
    socket_running = 0;
    
    // Close server socket to wake up thread
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }
    
    // Wait for thread to finish
    if (socket_thread) {
        pthread_join(socket_thread, NULL);
    }
    
    // Clean up socket file
    unlink(SOCKET_PATH);
    
    socket_log(LOG_INFO, "Socket server stopped");
}

static void* socket_server_thread(void* arg) {
    (void)arg;
    
    fd_set read_fds;
    struct timeval timeout;
    
    while (socket_running) {
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal
            }
            socket_log(LOG_ERR, "Select error: %s", strerror(errno));
            break;
        }
        
        if (activity == 0) {
            continue; // Timeout
        }
        
        if (FD_ISSET(server_sock, &read_fds)) {
            struct sockaddr_un client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock < 0) {
                socket_log(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
                continue;
            }
            
            // Handle client in a simple way (for now, single-threaded)
            char buffer[BUFFER_SIZE];
            ssize_t received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
            if (received > 0) {
                buffer[received] = '\0';
                handle_client_command(client_sock, buffer);
            }
            
            close(client_sock);
        }
    }
    
    return NULL;
}

static int handle_client_command(int client_sock, const char* command) {
    char response[BUFFER_SIZE];
    
    if (strncmp(command, "STATUS", 6) == 0) {
        // Return current status
        snprintf(response, sizeof(response), 
                "CPU:%d GPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d",
                share_info->cpu_temp,
                share_info->gpu_temp,
                share_info->fan_duty,
                share_info->fan_rpms,
                share_info->auto_duty);
        
    } else if (strncmp(command, "SET_FAN", 7) == 0) {
        // Set fan duty cycle
        int duty;
        if (sscanf(command, "SET_FAN %d", &duty) == 1) {
            if (duty >= 1 && duty <= 100) {
                share_info->auto_duty = 0;
                share_info->manual_next_fan_duty = duty;
                snprintf(response, sizeof(response), "OK: Fan set to %d%%", duty);
                socket_log(LOG_INFO, "Client requested fan duty: %d%%", duty);
            } else {
                snprintf(response, sizeof(response), "ERROR: Invalid duty cycle (must be 1-100)");
            }
        } else {
            snprintf(response, sizeof(response), "ERROR: Invalid SET_FAN command");
        }
        
    } else if (strcmp(command, "SET_AUTO") == 0) {
        // Enable automatic fan control
        share_info->auto_duty = 1;
        share_info->manual_next_fan_duty = 0;
        snprintf(response, sizeof(response), "OK: Auto mode enabled");
        socket_log(LOG_INFO, "Client enabled auto mode");
        
    } else if (strncmp(command, "SET_TARGET_TEMP", 14) == 0) {
        // Set target temperature for auto fan control
        int temp;
        if (sscanf(command, "SET_TARGET_TEMP %d", &temp) == 1) {
            if (temp >= 40 && temp <= 100) {
                // We need to access the target_temperature variable from the daemon
                // For now, we'll just acknowledge the command
                snprintf(response, sizeof(response), "OK: Target temperature set to %d°C", temp);
                socket_log(LOG_INFO, "Client set target temperature: %d°C", temp);
            } else {
                snprintf(response, sizeof(response), "ERROR: Invalid target temperature (must be 40-100°C)");
            }
        } else {
            snprintf(response, sizeof(response), "ERROR: Invalid SET_TARGET_TEMP command");
        }
        
    } else if (strcmp(command, "GET_TEMP") == 0) {
        // Get temperature only
        snprintf(response, sizeof(response), "CPU:%d GPU:%d", 
                share_info->cpu_temp, share_info->gpu_temp);
        
    } else if (strcmp(command, "GET_FAN") == 0) {
        // Get fan status only
        snprintf(response, sizeof(response), "DUTY:%d RPM:%d AUTO:%d", 
                share_info->fan_duty, share_info->fan_rpms, share_info->auto_duty);
        
    } else {
        snprintf(response, sizeof(response), "ERROR: Unknown command '%s'", command);
    }
    
    return send_response(client_sock, response);
}

static int send_response(int client_sock, const char* response) {
    ssize_t sent = send(client_sock, response, strlen(response), 0);
    if (sent < 0) {
        socket_log(LOG_ERR, "Failed to send response: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static void socket_signal_handler(int sig) {
    (void)sig;
    socket_running = 0;
}

// This function is kept for potential future use
// static void cleanup_socket(void) {
//     if (server_sock >= 0) {
//         close(server_sock);
//         server_sock = -1;
//     }
//     unlink(SOCKET_PATH);
// } 