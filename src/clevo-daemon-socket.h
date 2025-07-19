/*
 ============================================================================
 Name        : clevo-daemon-socket.h
 Author      : System76 Fan Control Daemon Socket Server
 Version     : 1.0
 Description : Header file for Unix domain socket server

 ============================================================================
 */

#ifndef CLEVO_DAEMON_SOCKET_H
#define CLEVO_DAEMON_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the socket server
 * 
 * Creates and starts the Unix domain socket server that allows clients
 * to communicate with the daemon.
 * 
 * @return 0 on success, -1 on failure
 */
int init_socket_server(void);

/**
 * Stop the socket server
 * 
 * Gracefully shuts down the socket server and cleans up resources.
 */
void stop_socket_server(void);

#ifdef __cplusplus
}
#endif

#endif // CLEVO_DAEMON_SOCKET_H 