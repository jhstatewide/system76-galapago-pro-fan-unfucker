/*
 ============================================================================
 Name        : clevo-daemon-dbus.h
 Author      : System76 Fan Control Daemon DBus Interface
 Version     : 1.0
 Description : Header file for DBus interface

 ============================================================================
 */

#ifndef CLEVO_DAEMON_DBUS_H
#define CLEVO_DAEMON_DBUS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the DBus interface
 * 
 * Creates and starts the DBus service that allows clients
 * to communicate with the daemon via DBus.
 * 
 * @return 0 on success, -1 on failure
 */
int init_dbus_interface(void);

/**
 * Stop the DBus interface
 * 
 * Gracefully shuts down the DBus service and cleans up resources.
 */
void stop_dbus_interface(void);

/**
 * Check if any clients are listening for status updates
 * 
 * @return 1 if clients are listening, 0 otherwise
 */
int has_status_listeners(void);

/**
 * Broadcast status update to all listening clients
 * 
 * Only sends the signal if there are active listeners.
 * 
 * @param cpu_temp CPU temperature
 * @param fan_duty Fan duty cycle
 * @param fan_rpm Fan RPM
 * @param auto_mode Auto mode status
 * @return 0 on success, -1 on failure
 */
int broadcast_status_update(int cpu_temp, int fan_duty, int fan_rpm, int auto_mode);

/**
 * Handle DBus method calls from clients
 * 
 * This function should be called periodically to process incoming DBus messages.
 * 
 * @return 0 on success, -1 on failure
 */
int process_dbus_messages(void);

#ifdef __cplusplus
}
#endif

#endif // CLEVO_DAEMON_DBUS_H 