/*
 ============================================================================
 Name        : clevo-dbus-client.c
 Author      : System76 Fan Control DBus Client
 Version     : 1.0
 Description : Example DBus client for clevo-daemon

 This client demonstrates how to connect to the clevo-daemon via DBus
 and receive status updates and send control commands.

 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dbus/dbus.h>

#define DBUS_SERVICE_NAME "org.freedesktop.ClevoDaemon"
#define DBUS_OBJECT_PATH "/org/freedesktop/ClevoDaemon"
#define DBUS_INTERFACE "org.freedesktop.ClevoDaemon"

static DBusConnection* dbus_conn = NULL;
static volatile int running = 1;

// Function declarations
static void signal_handler(int sig);
static int connect_to_dbus(void);
static int subscribe_to_status(void);
static int call_method(const char* method_name, int arg);
static int call_method_bool(const char* method_name, dbus_bool_t arg);
static void handle_status_signal(DBusMessage* msg);
static void process_dbus_messages(void);

int main(int argc, char* argv[]) {
    printf("Clevo Fan Control DBus Client v1.0\n");
    
    if (argc < 2) {
        printf("Usage: %s <command> [args]\n", argv[0]);
        printf("Commands:\n");
        printf("  status          - Get current status\n");
        printf("  monitor         - Monitor status updates\n");
        printf("  set-fan <duty>  - Set fan duty (1-100)\n");
        printf("  set-auto <0|1>  - Enable/disable auto mode\n");
        printf("  subscribe       - Subscribe to status updates\n");
        printf("  unsubscribe     - Unsubscribe from status updates\n");
        return EXIT_FAILURE;
    }
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Connect to DBus
    if (!connect_to_dbus()) {
        fprintf(stderr, "Failed to connect to DBus\n");
        return EXIT_FAILURE;
    }
    printf("Successfully connected to DBus system bus\n");
    
    const char* command = argv[1];
    
    if (strcmp(command, "status") == 0) {
        // Get current status
        DBusMessage* msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, 
                                                       DBUS_OBJECT_PATH, 
                                                       DBUS_INTERFACE, 
                                                       "GetStatus");
        if (!msg) {
            fprintf(stderr, "Failed to create method call\n");
            return EXIT_FAILURE;
        }
        
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(dbus_conn, msg, -1, NULL);
        dbus_message_unref(msg);
        
        if (!reply) {
            fprintf(stderr, "Failed to get status - no reply received\n");
            return EXIT_FAILURE;
        }
        
        DBusMessageIter iter;
        dbus_message_iter_init(reply, &iter);
        const char* response;
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&iter, &response);
            printf("Status: %s\n", response);
        }
        
        dbus_message_unref(reply);
        
    } else if (strcmp(command, "monitor") == 0) {
        // Subscribe and monitor status updates
        if (!subscribe_to_status()) {
            fprintf(stderr, "Failed to subscribe to status updates\n");
            return EXIT_FAILURE;
        }
        
        printf("Monitoring status updates (Press Ctrl+C to stop)...\n");
        
        while (running) {
            process_dbus_messages();
            usleep(100000); // 100ms
        }
        
        // Unsubscribe
        call_method("UnsubscribeStatus", 0);
        
    } else if (strcmp(command, "set-fan") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s set-fan <duty>\n", argv[0]);
            return EXIT_FAILURE;
        }
        
        int duty = atoi(argv[2]);
        if (duty < 1 || duty > 100) {
            fprintf(stderr, "Duty must be between 1 and 100\n");
            return EXIT_FAILURE;
        }
        
        if (call_method("SetFanDuty", duty) == 0) {
            printf("Fan duty set to %d%%\n", duty);
        } else {
            fprintf(stderr, "Failed to set fan duty\n");
            return EXIT_FAILURE;
        }
        
    } else if (strcmp(command, "set-auto") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s set-auto <0|1>\n", argv[0]);
            return EXIT_FAILURE;
        }
        
        dbus_bool_t enabled = atoi(argv[2]) != 0;
        if (call_method_bool("SetAutoMode", enabled) == 0) {
            printf("Auto mode %s\n", enabled ? "enabled" : "disabled");
        } else {
            fprintf(stderr, "Failed to set auto mode\n");
            return EXIT_FAILURE;
        }
        
    } else if (strcmp(command, "subscribe") == 0) {
        if (call_method("SubscribeStatus", 0) == 0) {
            printf("Subscribed to status updates\n");
        } else {
            fprintf(stderr, "Failed to subscribe\n");
            return EXIT_FAILURE;
        }
        
    } else if (strcmp(command, "unsubscribe") == 0) {
        if (call_method("UnsubscribeStatus", 0) == 0) {
            printf("Unsubscribed from status updates\n");
        } else {
            fprintf(stderr, "Failed to unsubscribe\n");
            return EXIT_FAILURE;
        }
        
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nSignal received, stopping...\n");
}

static int connect_to_dbus(void) {
    DBusError error;
    dbus_error_init(&error);
    
    // Connect to system bus (where the daemon runs)
    dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (!dbus_conn) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", error.message);
        dbus_error_free(&error);
        return 0;
    }
    
    return 1;
}

static int subscribe_to_status(void) {
    return call_method("SubscribeStatus", 0);
}

static int call_method(const char* method_name, int arg) {
    DBusMessage* msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, 
                                                   DBUS_OBJECT_PATH, 
                                                   DBUS_INTERFACE, 
                                                   method_name);
    if (!msg) {
        return -1;
    }
    
    if (arg != 0) {
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg, &iter);
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &arg);
    }
    
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(dbus_conn, msg, -1, NULL);
    dbus_message_unref(msg);
    
    if (!reply) {
        return -1;
    }
    
    dbus_message_unref(reply);
    return 0;
}

static int call_method_bool(const char* method_name, dbus_bool_t arg) {
    DBusMessage* msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, 
                                                   DBUS_OBJECT_PATH, 
                                                   DBUS_INTERFACE, 
                                                   method_name);
    if (!msg) {
        return -1;
    }
    
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &arg);
    
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(dbus_conn, msg, -1, NULL);
    dbus_message_unref(msg);
    
    if (!reply) {
        return -1;
    }
    
    dbus_message_unref(reply);
    return 0;
}

static void handle_status_signal(DBusMessage* msg) {
    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    
    int cpu_temp, fan_duty, fan_rpm;
    dbus_bool_t auto_mode;
    
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_basic(&iter, &cpu_temp);
        dbus_message_iter_next(&iter);
    }
    
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_basic(&iter, &fan_duty);
        dbus_message_iter_next(&iter);
    }
    
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_basic(&iter, &fan_rpm);
        dbus_message_iter_next(&iter);
    }
    
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_BOOLEAN) {
        dbus_message_iter_get_basic(&iter, &auto_mode);
    }
    
    printf("Status Update: CPU=%d°C, Fan=%d%% (%d RPM), Auto=%s\n", 
           cpu_temp, fan_duty, fan_rpm, auto_mode ? "ON" : "OFF");
}

static void process_dbus_messages(void) {
    if (!dbus_conn) {
        return;
    }
    
    // Process pending messages
    dbus_connection_read_write_dispatch(dbus_conn, 10); // 10ms timeout
    
    // Check for signals
    DBusMessage* msg;
    while ((msg = dbus_connection_pop_message(dbus_conn)) != NULL) {
        if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
            const char* interface = dbus_message_get_interface(msg);
            const char* member = dbus_message_get_member(msg);
            
            if (interface && member && 
                strcmp(interface, DBUS_INTERFACE) == 0 &&
                strcmp(member, "StatusChanged") == 0) {
                handle_status_signal(msg);
            }
        }
        
        dbus_message_unref(msg);
    }
} 