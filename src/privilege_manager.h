#ifndef PRIVILEGE_MANAGER_H
#define PRIVILEGE_MANAGER_H

#include <stdbool.h>
#include <sys/types.h>

typedef enum {
    PRIV_METHOD_NONE = 0,
    PRIV_METHOD_SETUID,
    PRIV_METHOD_PKEXEC,
    PRIV_METHOD_SUDO,
    PRIV_METHOD_CAPABILITIES,
    PRIV_METHOD_SYSTEMD
} privilege_method_t;

typedef struct {
    privilege_method_t method;
    bool has_privileges;
    uid_t effective_uid;
    uid_t real_uid;
    bool can_elevate;
    char* error_message;
} privilege_status_t;

// Initialize privilege manager
void privilege_manager_init(void);

// Check current privilege status
privilege_status_t privilege_check_status(void);

// Attempt to elevate privileges using available methods
bool privilege_elevate(void);

// Drop privileges (for UI operations)
bool privilege_drop(void);

// Restore privileges (for EC operations)
bool privilege_restore(void);

// Get the best available privilege method
privilege_method_t privilege_get_best_method(void);

// Check if we can perform EC operations
bool privilege_can_access_ec(void);

// Clean up privilege manager
void privilege_manager_cleanup(void);

// Get human-readable method name
const char* privilege_method_name(privilege_method_t method);

#endif // PRIVILEGE_MANAGER_H 