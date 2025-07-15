#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "privilege_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <sys/io.h>

#ifdef HAVE_LIBCAP
#include <sys/capability.h>
#include <sys/prctl.h>
#endif

static privilege_status_t current_status = {0};
static uid_t original_uid = 0;
static gid_t original_gid = 0;
static bool initialized = false;

void privilege_manager_init(void) {
    if (initialized) return;
    
    original_uid = getuid();
    original_gid = getgid();
    current_status.real_uid = original_uid;
    current_status.effective_uid = geteuid();
    current_status.has_privileges = (geteuid() == 0);
    current_status.can_elevate = false;
    current_status.method = PRIV_METHOD_NONE;
    current_status.error_message = NULL;
    
    initialized = true;
}

privilege_status_t privilege_check_status(void) {
    if (!initialized) {
        privilege_manager_init();
    }
    
    current_status.effective_uid = geteuid();
    current_status.real_uid = getuid();
    current_status.has_privileges = (geteuid() == 0);
    
    return current_status;
}

static bool check_pkexec_available(void) {
    return access("/usr/bin/pkexec", X_OK) == 0;
}

static bool check_sudo_available(void) {
    return access("/usr/bin/sudo", X_OK) == 0;
}

static bool check_capabilities_supported(void) {
#ifdef HAVE_LIBCAP
    cap_t caps = cap_get_proc();
    if (caps == NULL) {
        return false;
    }
    cap_free(caps);
    return true;
#else
    return false;
#endif
}

privilege_method_t privilege_get_best_method(void) {
    // Priority order: capabilities > pkexec > sudo > setuid
    if (check_capabilities_supported()) {
        return PRIV_METHOD_CAPABILITIES;
    }
    if (check_pkexec_available()) {
        return PRIV_METHOD_PKEXEC;
    }
    if (check_sudo_available()) {
        return PRIV_METHOD_SUDO;
    }
    if (geteuid() == 0) {
        return PRIV_METHOD_SETUID;
    }
    return PRIV_METHOD_NONE;
}

bool privilege_can_access_ec(void) {
    // Try to access EC ports to see if we have permission
    if (ioperm(0x66, 1, 1) == 0) {
        ioperm(0x66, 1, 0); // Release the permission
        return true;
    }
    return false;
}

bool privilege_elevate(void) {
    privilege_status_t status = privilege_check_status();
    fprintf(stderr, "[DEBUG] privilege_elevate: euid=%d, uid=%d, has_privileges=%d\n", (int)geteuid(), (int)getuid(), status.has_privileges);
    if (status.has_privileges) {
        fprintf(stderr, "[DEBUG] Already have privileges\n");
        return true; // Already have privileges
    }
    
    privilege_method_t method = privilege_get_best_method();
    fprintf(stderr, "[DEBUG] privilege_get_best_method returned: %d (%s)\n", (int)method, privilege_method_name(method));
    
    switch (method) {
        case PRIV_METHOD_CAPABILITIES:
#ifdef HAVE_LIBCAP
            fprintf(stderr, "[DEBUG] Trying to add SYS_RAWIO capability\n");
            cap_t caps = cap_get_proc();
            if (caps != NULL) {
                cap_value_t cap_list[] = {CAP_SYS_RAWIO};
                if (cap_set_flag(caps, CAP_EFFECTIVE, 1, cap_list, CAP_SET) == 0) {
                    if (cap_set_proc(caps) == 0) {
                        cap_free(caps);
                        current_status.method = PRIV_METHOD_CAPABILITIES;
                        current_status.has_privileges = privilege_can_access_ec();
                        fprintf(stderr, "[DEBUG] cap_set_proc succeeded, can_access_ec=%d\n", current_status.has_privileges);
                        return current_status.has_privileges;
                    } else {
                        fprintf(stderr, "[DEBUG] cap_set_proc failed: %s\n", strerror(errno));
                    }
                } else {
                    fprintf(stderr, "[DEBUG] cap_set_flag failed: %s\n", strerror(errno));
                }
                cap_free(caps);
            } else {
                fprintf(stderr, "[DEBUG] cap_get_proc failed: %s\n", strerror(errno));
            }
#else
            fprintf(stderr, "[DEBUG] libcap not available at build time\n");
            current_status.error_message = "libcap not available at build time";
#endif
            break;
            
        case PRIV_METHOD_PKEXEC:
            fprintf(stderr, "[DEBUG] pkexec requires policy configuration\n");
            current_status.error_message = "pkexec requires policy configuration";
            break;
            
        case PRIV_METHOD_SUDO:
            fprintf(stderr, "[DEBUG] sudo requires sudoers configuration\n");
            current_status.error_message = "sudo requires sudoers configuration";
            break;
            
        case PRIV_METHOD_SETUID:
            fprintf(stderr, "[DEBUG] setuid method, geteuid()=%d\n", (int)geteuid());
            if (geteuid() == 0) {
                current_status.method = PRIV_METHOD_SETUID;
                current_status.has_privileges = true;
                return true;
            }
            current_status.error_message = "setuid requires binary to be owned by root";
            break;
            
        default:
            fprintf(stderr, "[DEBUG] no privilege elevation method available\n");
            current_status.error_message = "no privilege elevation method available";
            break;
    }
    fprintf(stderr, "[DEBUG] privilege_elevate failed: %s\n", current_status.error_message ? current_status.error_message : "unknown error");
    return false;
}

bool privilege_drop(void) {
    if (geteuid() == 0) {
        if (setuid(original_uid) == 0) {
            current_status.has_privileges = false;
            current_status.effective_uid = geteuid();
            return true;
        }
    }
    return true; // Already running as user
}

bool privilege_restore(void) {
    if (current_status.method == PRIV_METHOD_SETUID && geteuid() == 0) {
        // For setuid, we can switch back to root
        if (setuid(0) == 0) {
            current_status.has_privileges = true;
            current_status.effective_uid = 0;
            return true;
        }
    }
    
    // For other methods, we need to re-elevate
    return privilege_elevate();
}

void privilege_manager_cleanup(void) {
    if (current_status.error_message) {
        free(current_status.error_message);
        current_status.error_message = NULL;
    }
    initialized = false;
}

const char* privilege_method_name(privilege_method_t method) {
    switch (method) {
        case PRIV_METHOD_NONE: return "None";
        case PRIV_METHOD_SETUID: return "setuid";
        case PRIV_METHOD_PKEXEC: return "pkexec";
        case PRIV_METHOD_SUDO: return "sudo";
        case PRIV_METHOD_CAPABILITIES: return "capabilities";
        case PRIV_METHOD_SYSTEMD: return "systemd";
        default: return "Unknown";
    }
} 