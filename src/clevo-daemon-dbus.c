/*
 ============================================================================
 Name        : clevo-daemon-dbus.c
 Author      : System76 Fan Control Daemon DBus Interface
 Version     : 1.0
 Description : DBus interface for clevo-daemon

 This module provides a DBus interface for clients to communicate
 with the clevo-daemon, allowing status queries and fan control commands.
 It intelligently broadcasts status updates only when clients are listening.

 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <stdarg.h>
#include <time.h>
#include <dbus/dbus.h>

#include "clevo-daemon-dbus.h"

// DBus interface definitions
#define DBUS_SERVICE_NAME "org.freedesktop.ClevoDaemon"
#define DBUS_OBJECT_PATH "/org/freedesktop/ClevoDaemon"
#define DBUS_INTERFACE "org.freedesktop.ClevoDaemon"

// External reference to shared memory structure
extern struct {
    volatile int exit;
    volatile int cpu_temp;
    volatile int fan_duty;
    volatile int fan_rpms;
    volatile int auto_duty;
    volatile int auto_duty_val;
    volatile int manual_next_fan_duty;
    volatile int manual_prev_fan_duty;
} *share_info;

// External function declarations
extern int ec_write_fan_duty(int duty_percentage);

// External variable declarations
extern int max_duty_change_rate;
extern int max_duty_increase_rate;
extern int max_duty_decrease_rate;

// Global DBus connection
static DBusConnection* dbus_conn = NULL;
static volatile int dbus_running = 1;
static int status_listeners_count = 0;

// Debug mode flag - set to 1 for enhanced debugging
static int dbus_debug_mode = 1;

// Enhanced logging function for DBus interface with debug mode
static void dbus_log(int priority, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    // In debug mode, also print to stderr for immediate visibility
    if (dbus_debug_mode) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[26];
        strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
        
        fprintf(stderr, "[%s] DBUS_DEBUG: ", time_str);
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
        fflush(stderr);
    }
    
    vsyslog(priority, format, args);
    va_end(args);
}

// Function to check DBus system bus availability
static int check_dbus_system_bus(void) {
    fprintf(stderr, "DBUS_DEBUG: Checking DBus system bus availability...\n");
    
    // Check if DBus daemon is running
    if (system("pgrep -x dbus-daemon > /dev/null 2>&1") != 0) {
        fprintf(stderr, "DBUS_DEBUG: ERROR - DBus daemon is not running!\n");
        return -1;
    }
    fprintf(stderr, "DBUS_DEBUG: DBus daemon is running\n");
    
    // Check if we can connect to system bus
    DBusError error;
    dbus_error_init(&error);
    
    DBusConnection* test_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (!test_conn) {
        fprintf(stderr, "DBUS_DEBUG: ERROR - Cannot connect to DBus system bus: %s\n", error.message);
        dbus_error_free(&error);
        return -1;
    }
    
    fprintf(stderr, "DBUS_DEBUG: Successfully connected to DBus system bus\n");
    dbus_connection_unref(test_conn);
    return 0;
}

// Function declarations
static DBusHandlerResult handle_method_call(DBusConnection* conn, DBusMessage* msg, void* user_data);
static int send_signal(const char* signal_name, int cpu_temp, int fan_duty, int fan_rpm, int auto_mode);

static void dbus_signal_handler(int sig);

int init_dbus_interface(void) {
    fprintf(stderr, "DBUS_DEBUG: ==========================================\n");
    fprintf(stderr, "DBUS_DEBUG: Starting DBus interface initialization\n");
    fprintf(stderr, "DBUS_DEBUG: ==========================================\n");
    
    // Check DBus system bus availability first
    if (check_dbus_system_bus() != 0) {
        fprintf(stderr, "DBUS_DEBUG: CRITICAL ERROR - DBus system bus is not available!\n");
        fprintf(stderr, "DBUS_DEBUG: This will cause the daemon to crash.\n");
        fprintf(stderr, "DBUS_DEBUG: Please ensure DBus is running: sudo systemctl start dbus\n");
        return -1;
    }
    
    DBusError error;
    dbus_error_init(&error);
    
    fprintf(stderr, "DBUS_DEBUG: Attempting to connect to DBus system bus...\n");
    
    // Connect directly to system bus since we run as root
    dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (!dbus_conn) {
        fprintf(stderr, "DBUS_DEBUG: CRITICAL ERROR - Failed to connect to DBus system bus: %s\n", error.message);
        dbus_error_free(&error);
        
        // Additional debugging information
        fprintf(stderr, "DBUS_DEBUG: Checking process privileges...\n");
        fprintf(stderr, "DBUS_DEBUG: UID: %d, EUID: %d\n", getuid(), geteuid());
        fprintf(stderr, "DBUS_DEBUG: GID: %d, EGID: %d\n", getgid(), getegid());
        
        // Check if we're running as root
        if (geteuid() != 0) {
            fprintf(stderr, "DBUS_DEBUG: WARNING - Not running as root, this may cause DBus connection issues\n");
        }
        
        return -1;
    }
    
    fprintf(stderr, "DBUS_DEBUG: Successfully connected to DBus system bus\n");
    dbus_log(LOG_INFO, "Successfully connected to DBus system bus");
    
    // Request the service name
    fprintf(stderr, "DBUS_DEBUG: Requesting DBus service name: %s\n", DBUS_SERVICE_NAME);
    
    int ret = dbus_bus_request_name(dbus_conn, DBUS_SERVICE_NAME, 
                                   DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    
    fprintf(stderr, "DBUS_DEBUG: Service name request result: %d\n", ret);
    
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "DBUS_DEBUG: CRITICAL ERROR - Failed to request DBus service name: %s\n", error.message);
        fprintf(stderr, "DBUS_DEBUG: Request result: %d (expected: %d)\n", ret, DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
        
        // Check if another instance is already running
        if (ret == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER) {
            fprintf(stderr, "DBUS_DEBUG: We already own this service name\n");
        } else if (ret == DBUS_REQUEST_NAME_REPLY_EXISTS) {
            fprintf(stderr, "DBUS_DEBUG: Service name already exists and cannot be replaced\n");
        }
        
        dbus_error_free(&error);
        dbus_connection_unref(dbus_conn);
        dbus_conn = NULL;
        return -1;
    }
    
    fprintf(stderr, "DBUS_DEBUG: Successfully registered DBus service name: %s\n", DBUS_SERVICE_NAME);
    dbus_log(LOG_INFO, "Successfully registered DBus service name: %s", DBUS_SERVICE_NAME);
    
    // Add message filter to handle incoming messages
    fprintf(stderr, "DBUS_DEBUG: Adding DBus message filter...\n");
    
    if (!dbus_connection_add_filter(dbus_conn, handle_method_call, NULL, NULL)) {
        fprintf(stderr, "DBUS_DEBUG: CRITICAL ERROR - Failed to add DBus message filter\n");
        dbus_log(LOG_ERR, "Failed to add DBus message filter");
        dbus_connection_unref(dbus_conn);
        dbus_conn = NULL;
        return -1;
    }
    
    fprintf(stderr, "DBUS_DEBUG: Successfully added DBus message filter\n");
    
    // Set up signal handling for cleanup
    signal(SIGTERM, dbus_signal_handler);
    signal(SIGINT, dbus_signal_handler);
    signal(SIGQUIT, dbus_signal_handler);
    
    fprintf(stderr, "DBUS_DEBUG: DBus interface initialization completed successfully\n");
    fprintf(stderr, "DBUS_DEBUG: ==========================================\n");
    
    dbus_log(LOG_INFO, "DBus interface initialized on %s", DBUS_SERVICE_NAME);
    return 0;
}

void stop_dbus_interface(void) {
    fprintf(stderr, "DBUS_DEBUG: Stopping DBus interface...\n");
    
    dbus_running = 0;
    
    if (dbus_conn) {
        fprintf(stderr, "DBUS_DEBUG: Removing message filter and closing connection...\n");
        dbus_connection_remove_filter(dbus_conn, handle_method_call, NULL);
        dbus_connection_unref(dbus_conn);
        dbus_conn = NULL;
    }
    
    fprintf(stderr, "DBUS_DEBUG: DBus interface stopped\n");
    dbus_log(LOG_INFO, "DBus interface stopped");
}

int has_status_listeners(void) {
    if (!dbus_conn) {
        return 0;
    }
    
    // Check if any clients are subscribed to our signals
    // This is a simple check - in a more sophisticated implementation,
    // you could track individual client subscriptions
    return status_listeners_count > 0;
}

int broadcast_status_update(int cpu_temp, int fan_duty, int fan_rpm, int auto_mode) {
    if (!dbus_conn || !has_status_listeners()) {
        // No clients listening, don't waste CPU on signal creation
        return 0;
    }
    
    return send_signal("StatusChanged", cpu_temp, fan_duty, fan_rpm, auto_mode);
}

int process_dbus_messages(void) {
    if (!dbus_conn) {
        return -1;
    }
    
    // Process pending messages with a timeout (same as working test program)
    dbus_connection_read_write_dispatch(dbus_conn, 1000); // 1 second timeout
    
    return 0;
}

static DBusHandlerResult handle_method_call(DBusConnection* conn, DBusMessage* msg, void* user_data) {
    (void)conn;
    (void)user_data;
    
    if (!dbus_running || !msg) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    const char* method_name = dbus_message_get_member(msg);
    const char* interface = dbus_message_get_interface(msg);
    const char* path = dbus_message_get_path(msg);
    
    // Add debug logging for all messages
    fprintf(stderr, "DBUS_DEBUG: Received DBus message - Type: %d, Interface: %s, Path: %s, Member: %s\n", 
            dbus_message_get_type(msg),
            interface ? interface : "NULL",
            path ? path : "NULL",
            method_name ? method_name : "NULL");
    
    // Handle signals (like NameAcquired) - just log and pass through
    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
        fprintf(stderr, "DBUS_DEBUG: DBus signal received: %s (interface: %s)\n", 
                method_name ? method_name : "NULL", 
                interface ? interface : "NULL");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    // Only handle method calls
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    fprintf(stderr, "DBUS_DEBUG: DBus method call received: %s (interface: %s)\n", 
            method_name ? method_name : "NULL", 
            interface ? interface : "NULL");
    
    if (!method_name || !interface) {
        fprintf(stderr, "DBUS_DEBUG: Invalid method call - missing method name or interface\n");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    // Check if this is for our interface
    if (strcmp(interface, DBUS_INTERFACE) != 0) {
        fprintf(stderr, "DBUS_DEBUG: Method call for different interface: %s (expected: %s)\n", 
                interface, DBUS_INTERFACE);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    // Check if share_info is available
    if (!share_info) {
        fprintf(stderr, "DBUS_DEBUG: Shared memory not available\n");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    // Handle different method calls
    if (strcmp(method_name, "GetStatus") == 0) {
        fprintf(stderr, "DBUS_DEBUG: Handling GetStatus method call\n");
        
        // Check if share_info is valid
        if (!share_info) {
            fprintf(stderr, "DBUS_DEBUG: ERROR - share_info is NULL!\n");
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
        
        fprintf(stderr, "DBUS_DEBUG: share_info is valid, creating reply...\n");
        
        // Create reply message (same approach as working test program)
        DBusMessage* reply = dbus_message_new_method_return(msg);
        if (!reply) {
            fprintf(stderr, "DBUS_DEBUG: Failed to create reply message\n");
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        
        fprintf(stderr, "DBUS_DEBUG: Reply message created, adding response...\n");
        
        // Add response string
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        
        // Create response string
        char response[256];
        snprintf(response, sizeof(response), 
                "CPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d",
                share_info->cpu_temp,
                share_info->fan_duty,
                share_info->fan_rpms,
                share_info->auto_duty);
        
        fprintf(stderr, "DBUS_DEBUG: Response string created: %s\n", response);
        
        // Use a const pointer to the string
        const char* response_ptr = response;
        
        fprintf(stderr, "DBUS_DEBUG: About to append string to message...\n");
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &response_ptr);
        fprintf(stderr, "DBUS_DEBUG: String appended successfully\n");
        
        fprintf(stderr, "DBUS_DEBUG: Response added to message, sending reply...\n");
        
        // Send reply
        if (!dbus_connection_send(dbus_conn, reply, NULL)) {
            fprintf(stderr, "DBUS_DEBUG: Failed to send reply\n");
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        
        dbus_message_unref(reply);
        fprintf(stderr, "DBUS_DEBUG: Successfully sent GetStatus reply\n");
        return DBUS_HANDLER_RESULT_HANDLED;
        
    } else if (strcmp(method_name, "SetFanDuty") == 0) {
        DBusError error;
        dbus_error_init(&error);
        
        int duty;
        if (dbus_message_get_args(msg, &error, DBUS_TYPE_INT32, &duty, DBUS_TYPE_INVALID)) {
            if (duty >= 1 && duty <= 100) {
                share_info->auto_duty = 0;
                share_info->manual_next_fan_duty = duty;
                fprintf(stderr, "DBUS_DEBUG: DBus client requested fan duty: %d%%\n", duty);
                
                // Create reply message
                DBusMessage* reply = dbus_message_new_method_return(msg);
                if (!reply) {
                    return DBUS_HANDLER_RESULT_NEED_MEMORY;
                }
                
                DBusMessageIter iter;
                dbus_message_iter_init_append(reply, &iter);
                const char* response = "OK: Fan set to requested duty";
                dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &response);
                
                if (!dbus_connection_send(dbus_conn, reply, NULL)) {
                    dbus_message_unref(reply);
                    return DBUS_HANDLER_RESULT_NEED_MEMORY;
                }
                
                dbus_message_unref(reply);
                return DBUS_HANDLER_RESULT_HANDLED;
            } else {
                // Create error reply
                DBusMessage* reply = dbus_message_new_error(msg, 
                    "org.freedesktop.ClevoDaemon.Error.InvalidValue",
                    "Duty cycle must be between 1 and 100");
                if (reply) {
                    dbus_connection_send(dbus_conn, reply, NULL);
                    dbus_message_unref(reply);
                }
                return DBUS_HANDLER_RESULT_HANDLED;
            }
        } else {
            // Create error reply
            DBusMessage* reply = dbus_message_new_error(msg, 
                "org.freedesktop.ClevoDaemon.Error.InvalidArgs",
                "Invalid arguments for SetFanDuty");
            if (reply) {
                dbus_connection_send(dbus_conn, reply, NULL);
                dbus_message_unref(reply);
            }
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        
    } else if (strcmp(method_name, "SetAutoMode") == 0) {
        DBusError error;
        dbus_error_init(&error);
        
        dbus_bool_t enabled;
        if (dbus_message_get_args(msg, &error, DBUS_TYPE_BOOLEAN, &enabled, DBUS_TYPE_INVALID)) {
            share_info->auto_duty = enabled ? 1 : 0;
            share_info->manual_next_fan_duty = 0;
            fprintf(stderr, "DBUS_DEBUG: DBus client %s auto mode\n", enabled ? "enabled" : "disabled");
            
            // Create reply message
            DBusMessage* reply = dbus_message_new_method_return(msg);
            if (!reply) {
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            }
            
            DBusMessageIter iter;
            dbus_message_iter_init_append(reply, &iter);
            const char* response = enabled ? "OK: Auto mode enabled" : "OK: Auto mode disabled";
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &response);
            
            if (!dbus_connection_send(dbus_conn, reply, NULL)) {
                dbus_message_unref(reply);
                return DBUS_HANDLER_RESULT_NEED_MEMORY;
            }
            
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_HANDLED;
        } else {
            // Create error reply
            DBusMessage* reply = dbus_message_new_error(msg, 
                "org.freedesktop.ClevoDaemon.Error.InvalidArgs",
                "Invalid arguments for SetAutoMode");
            if (reply) {
                dbus_connection_send(dbus_conn, reply, NULL);
                dbus_message_unref(reply);
            }
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        
    } else if (strcmp(method_name, "SubscribeStatus") == 0) {
        status_listeners_count++;
        fprintf(stderr, "DBUS_DEBUG: Client subscribed to status updates (total: %d)\n", status_listeners_count);
        
        // Create reply message
        DBusMessage* reply = dbus_message_new_method_return(msg);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        const char* response = "OK: Subscribed to status updates";
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &response);
        
        if (!dbus_connection_send(dbus_conn, reply, NULL)) {
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
        
    } else if (strcmp(method_name, "UnsubscribeStatus") == 0) {
        if (status_listeners_count > 0) {
            status_listeners_count--;
        }
        fprintf(stderr, "DBUS_DEBUG: Client unsubscribed from status updates (total: %d)\n", status_listeners_count);
        
        // Create reply message
        DBusMessage* reply = dbus_message_new_method_return(msg);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        const char* response = "OK: Unsubscribed from status updates";
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &response);
        
        if (!dbus_connection_send(dbus_conn, reply, NULL)) {
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
        
    } else if (strcmp(method_name, "SetMaxDutyChange") == 0) {
        DBusError error;
        dbus_error_init(&error);
        
        int rate;
        if (dbus_message_get_args(msg, &error, DBUS_TYPE_INT32, &rate, DBUS_TYPE_INVALID)) {
            if (rate >= 1 && rate <= 100) {
                max_duty_change_rate = rate;
                fprintf(stderr, "DBUS_DEBUG: DBus client set max duty change rate: %d%%\n", rate);
                
                // Create reply message
                DBusMessage* reply = dbus_message_new_method_return(msg);
                if (!reply) {
                    return DBUS_HANDLER_RESULT_NEED_MEMORY;
                }
                
                DBusMessageIter iter;
                dbus_message_iter_init_append(reply, &iter);
                const char* response = "OK: Max duty change rate updated";
                dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &response);
                
                if (!dbus_connection_send(dbus_conn, reply, NULL)) {
                    dbus_message_unref(reply);
                    return DBUS_HANDLER_RESULT_NEED_MEMORY;
                }
                
                dbus_message_unref(reply);
                return DBUS_HANDLER_RESULT_HANDLED;
            } else {
                // Create error reply
                DBusMessage* reply = dbus_message_new_error(msg, 
                    "org.freedesktop.ClevoDaemon.Error.InvalidValue",
                    "Rate must be between 1 and 100");
                if (reply) {
                    dbus_connection_send(dbus_conn, reply, NULL);
                    dbus_message_unref(reply);
                }
                return DBUS_HANDLER_RESULT_HANDLED;
            }
        } else {
            // Create error reply
            DBusMessage* reply = dbus_message_new_error(msg, 
                "org.freedesktop.ClevoDaemon.Error.InvalidArgs",
                "Invalid arguments for SetMaxDutyChange");
            if (reply) {
                dbus_connection_send(dbus_conn, reply, NULL);
                dbus_message_unref(reply);
            }
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        
    } else if (strcmp(method_name, "GetMaxDutyChange") == 0) {
        char response[64];
        snprintf(response, sizeof(response), "MAX_DUTY_CHANGE:%d", max_duty_change_rate);
        
        // Create reply message
        DBusMessage* reply = dbus_message_new_method_return(msg);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &response);
        
        if (!dbus_connection_send(dbus_conn, reply, NULL)) {
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
        
    } else {
        // Create error reply for unknown method
        DBusMessage* reply = dbus_message_new_error(msg, 
            "org.freedesktop.ClevoDaemon.Error.UnknownMethod",
            "Unknown method");
        if (reply) {
            dbus_connection_send(dbus_conn, reply, NULL);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}

static int send_signal(const char* signal_name, int cpu_temp, int fan_duty, int fan_rpm, int auto_mode) {
    if (!dbus_conn) {
        return -1;
    }
    
    DBusMessage* msg = dbus_message_new_signal(DBUS_OBJECT_PATH, DBUS_INTERFACE, signal_name);
    if (!msg) {
        dbus_log(LOG_ERR, "Failed to create DBus signal message");
        return -1;
    }
    
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    
    // Add signal parameters
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &cpu_temp);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &fan_duty);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &fan_rpm);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &auto_mode);
    
    // Send the signal
    dbus_bool_t sent = dbus_connection_send(dbus_conn, msg, NULL);
    dbus_message_unref(msg);
    
    if (!sent) {
        dbus_log(LOG_ERR, "Failed to send DBus signal");
        return -1;
    }
    
    return 0;
}



static void dbus_signal_handler(int sig) {
    (void)sig;
    dbus_log(LOG_INFO, "DBus interface received signal %s, shutting down", strsignal(sig));
    dbus_running = 0;
} 