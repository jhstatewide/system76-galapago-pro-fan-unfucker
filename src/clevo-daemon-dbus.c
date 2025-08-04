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

// Simple logging function for DBus interface
static void dbus_log(int priority, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsyslog(priority, format, args);
    va_end(args);
}

// Function declarations
static DBusHandlerResult handle_method_call(DBusConnection* conn, DBusMessage* msg, void* user_data);
static int send_signal(const char* signal_name, int cpu_temp, int fan_duty, int fan_rpm, int auto_mode);
static int send_method_reply(DBusMessage* msg, const char* response);
static int send_error_reply(DBusMessage* msg, const char* error_name, const char* error_message);
static void dbus_signal_handler(int sig);

int init_dbus_interface(void) {
    DBusError error;
    dbus_error_init(&error);
    
    // Connect to session bus (better for user applications)
    dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!dbus_conn) {
        // Fall back to system bus if session bus fails
        dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
        if (!dbus_conn) {
            dbus_log(LOG_ERR, "Failed to connect to DBus: %s", error.message);
            dbus_error_free(&error);
            return -1;
        }
    }
    
    // Request the service name
    int ret = dbus_bus_request_name(dbus_conn, DBUS_SERVICE_NAME, 
                                   DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        dbus_log(LOG_ERR, "Failed to request DBus service name: %s", error.message);
        dbus_error_free(&error);
        dbus_connection_unref(dbus_conn);
        dbus_conn = NULL;
        return -1;
    }
    
    // Add message filter to handle incoming messages
    if (!dbus_connection_add_filter(dbus_conn, handle_method_call, NULL, NULL)) {
        dbus_log(LOG_ERR, "Failed to add DBus message filter");
        dbus_connection_unref(dbus_conn);
        dbus_conn = NULL;
        return -1;
    }
    
    // Set up signal handling for cleanup
    signal(SIGTERM, dbus_signal_handler);
    signal(SIGINT, dbus_signal_handler);
    signal(SIGQUIT, dbus_signal_handler);
    
    dbus_log(LOG_INFO, "DBus interface initialized on %s", DBUS_SERVICE_NAME);
    return 0;
}

void stop_dbus_interface(void) {
    dbus_running = 0;
    
    if (dbus_conn) {
        dbus_connection_remove_filter(dbus_conn, handle_method_call, NULL);
        dbus_connection_unref(dbus_conn);
        dbus_conn = NULL;
    }
    
    dbus_log(LOG_INFO, "DBus interface stopped");
}

int has_status_listeners(void) {
    if (!dbus_conn) {
        return 0;
    }
    
    // Check if any clients are subscribed to our signals
    // This is a simple check - in a more sophisticated implementation,
    // you could track individual client subscriptions
    DBusError error;
    dbus_error_init(&error);
    
    // Try to get the list of unique names connected to the bus
    // This is a simplified approach - in practice, you might want to
    // track subscriptions more explicitly
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
    
    // Process pending messages with a short timeout
    dbus_connection_read_write_dispatch(dbus_conn, 10); // 10ms timeout
    
    return 0;
}

static DBusHandlerResult handle_method_call(DBusConnection* conn, DBusMessage* msg, void* user_data) {
    (void)conn;
    (void)user_data;
    
    if (!dbus_running) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    const char* method_name = dbus_message_get_member(msg);
    const char* interface = dbus_message_get_interface(msg);
    
    if (!method_name || !interface) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    // Check if this is for our interface
    if (strcmp(interface, DBUS_INTERFACE) != 0) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    
    // Handle different method calls
    if (strcmp(method_name, "GetStatus") == 0) {
        char response[256];
        snprintf(response, sizeof(response), 
                "CPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d",
                share_info->cpu_temp,
                share_info->fan_duty,
                share_info->fan_rpms,
                share_info->auto_duty);
        return send_method_reply(msg, response);
        
    } else if (strcmp(method_name, "SetFanDuty") == 0) {
        DBusError error;
        dbus_error_init(&error);
        
        int duty;
        if (dbus_message_get_args(msg, &error, DBUS_TYPE_INT32, &duty, DBUS_TYPE_INVALID)) {
            if (duty >= 1 && duty <= 100) {
                share_info->auto_duty = 0;
                share_info->manual_next_fan_duty = duty;
                dbus_log(LOG_INFO, "DBus client requested fan duty: %d%%", duty);
                return send_method_reply(msg, "OK: Fan set to requested duty");
            } else {
                return send_error_reply(msg, "org.freedesktop.ClevoDaemon.Error.InvalidValue",
                                      "Duty cycle must be between 1 and 100");
            }
        } else {
            return send_error_reply(msg, "org.freedesktop.ClevoDaemon.Error.InvalidArgs",
                                  "Invalid arguments for SetFanDuty");
        }
        
    } else if (strcmp(method_name, "SetAutoMode") == 0) {
        DBusError error;
        dbus_error_init(&error);
        
        dbus_bool_t enabled;
        if (dbus_message_get_args(msg, &error, DBUS_TYPE_BOOLEAN, &enabled, DBUS_TYPE_INVALID)) {
            share_info->auto_duty = enabled ? 1 : 0;
            share_info->manual_next_fan_duty = 0;
            dbus_log(LOG_INFO, "DBus client %s auto mode", enabled ? "enabled" : "disabled");
            return send_method_reply(msg, enabled ? "OK: Auto mode enabled" : "OK: Auto mode disabled");
        } else {
            return send_error_reply(msg, "org.freedesktop.ClevoDaemon.Error.InvalidArgs",
                                  "Invalid arguments for SetAutoMode");
        }
        
    } else if (strcmp(method_name, "SubscribeStatus") == 0) {
        status_listeners_count++;
        dbus_log(LOG_INFO, "Client subscribed to status updates (total: %d)", status_listeners_count);
        return send_method_reply(msg, "OK: Subscribed to status updates");
        
    } else if (strcmp(method_name, "UnsubscribeStatus") == 0) {
        if (status_listeners_count > 0) {
            status_listeners_count--;
        }
        dbus_log(LOG_INFO, "Client unsubscribed from status updates (total: %d)", status_listeners_count);
        return send_method_reply(msg, "OK: Unsubscribed from status updates");
        
    } else if (strcmp(method_name, "SetMaxDutyChange") == 0) {
        DBusError error;
        dbus_error_init(&error);
        
        int rate;
        if (dbus_message_get_args(msg, &error, DBUS_TYPE_INT32, &rate, DBUS_TYPE_INVALID)) {
            if (rate >= 1 && rate <= 100) {
                max_duty_change_rate = rate;
                dbus_log(LOG_INFO, "DBus client set max duty change rate: %d%%", rate);
                return send_method_reply(msg, "OK: Max duty change rate updated");
            } else {
                return send_error_reply(msg, "org.freedesktop.ClevoDaemon.Error.InvalidValue",
                                      "Rate must be between 1 and 100");
            }
        } else {
            return send_error_reply(msg, "org.freedesktop.ClevoDaemon.Error.InvalidArgs",
                                  "Invalid arguments for SetMaxDutyChange");
        }
        
    } else if (strcmp(method_name, "GetMaxDutyChange") == 0) {
        char response[64];
        snprintf(response, sizeof(response), "MAX_DUTY_CHANGE:%d", max_duty_change_rate);
        return send_method_reply(msg, response);
        
    } else {
        return send_error_reply(msg, "org.freedesktop.ClevoDaemon.Error.UnknownMethod",
                              "Unknown method");
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

static int send_method_reply(DBusMessage* msg, const char* response) {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    if (!reply) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &response);
    
    dbus_bool_t sent = dbus_connection_send(dbus_conn, reply, NULL);
    dbus_message_unref(reply);
    
    if (!sent) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}

static int send_error_reply(DBusMessage* msg, const char* error_name, const char* error_message) {
    DBusMessage* reply = dbus_message_new_error(msg, error_name, error_message);
    if (!reply) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    
    dbus_bool_t sent = dbus_connection_send(dbus_conn, reply, NULL);
    dbus_message_unref(reply);
    
    if (!sent) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    
    return DBUS_HANDLER_RESULT_HANDLED;
}

static void dbus_signal_handler(int sig) {
    (void)sig;
    dbus_log(LOG_INFO, "DBus interface received signal %s, shutting down", strsignal(sig));
    dbus_running = 0;
} 