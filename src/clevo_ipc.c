/*
 * Shared IPC client API using D-Bus for Clevo fan control
 */

#include "clevo_ipc.h"

#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DBUS_SERVICE_NAME "org.freedesktop.ClevoDaemon"
#define DBUS_OBJECT_PATH "/org/freedesktop/ClevoDaemon"
#define DBUS_INTERFACE    "org.freedesktop.ClevoDaemon"

struct ClevoIpc {
    DBusConnection *conn;
};

static DBusMessage* call_int(ClevoIpc *h, const char *method, int arg) {
    DBusMessage *msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, method);
    if (!msg) return NULL;
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &arg);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(h->conn, msg, -1, NULL);
    dbus_message_unref(msg);
    return reply;
}

static DBusMessage* call_bool(ClevoIpc *h, const char *method, dbus_bool_t arg) {
    DBusMessage *msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, method);
    if (!msg) return NULL;
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &arg);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(h->conn, msg, -1, NULL);
    dbus_message_unref(msg);
    return reply;
}

int clevo_ipc_client_new(ClevoIpc **out_handle) {
    if (!out_handle) return -1;
    *out_handle = NULL;
    DBusError err; dbus_error_init(&err);
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn) {
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        return -1;
    }
    ClevoIpc *h = (ClevoIpc*)calloc(1, sizeof(ClevoIpc));
    if (!h) return -1;
    h->conn = conn;
    *out_handle = h;
    return 0;
}

void clevo_ipc_free(ClevoIpc *handle) {
    if (!handle) return;
    if (handle->conn) dbus_connection_unref(handle->conn);
    free(handle);
}

int clevo_get_status(ClevoIpc *handle, ClevoStatus *out_status) {
    if (!handle || !out_status) return -1;
    // Try typed method first: GetStatus2 -> a{sv}
    {
        DBusMessage *msg2 = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, "GetStatus2");
        if (msg2) {
            DBusMessage *reply2 = dbus_connection_send_with_reply_and_block(handle->conn, msg2, 500, NULL);
            dbus_message_unref(msg2);
            if (reply2) {
                DBusMessageIter iter; dbus_message_iter_init(reply2, &iter);
                if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
                    DBusMessageIter dict; dbus_message_iter_recurse(&iter, &dict);
                    int cpu=0, duty=0, rpm=0; dbus_bool_t auto_b=FALSE;
                    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
                        DBusMessageIter entry; dbus_message_iter_recurse(&dict, &entry);
                        const char *key = NULL; dbus_message_iter_get_basic(&entry, &key);
                        dbus_message_iter_next(&entry);
                        DBusMessageIter var; dbus_message_iter_recurse(&entry, &var);
                        int at = dbus_message_iter_get_arg_type(&var);
                        if (key && at == DBUS_TYPE_INT32) {
                            int val; dbus_message_iter_get_basic(&var, &val);
                            if (strcmp(key, "cpu_temp") == 0) cpu = val;
                            else if (strcmp(key, "fan_duty") == 0) duty = val;
                            else if (strcmp(key, "fan_rpm") == 0) rpm = val;
                        } else if (key && at == DBUS_TYPE_BOOLEAN) {
                            dbus_message_iter_get_basic(&var, &auto_b);
                        }
                        dbus_message_iter_next(&dict);
                    }
                    out_status->cpu_temp = cpu;
                    out_status->fan_duty = duty;
                    out_status->fan_rpm = rpm;
                    out_status->auto_mode = auto_b ? 1 : 0;
                    dbus_message_unref(reply2);
                    return 0;
                }
                dbus_message_unref(reply2);
            }
        }
    }
    // Fallback to legacy string method
    DBusMessage *msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, "GetStatus");
    if (!msg) return -1;
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(handle->conn, msg, -1, NULL);
    dbus_message_unref(msg);
    if (!reply) return -1;
    DBusMessageIter iter; dbus_message_iter_init(reply, &iter);
    const char *resp = NULL;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&iter, &resp);
    }
    int ok = -1;
    if (resp) {
        int cpu, duty, rpm, auto_mode;
        if (sscanf(resp, "CPU:%d FAN_DUTY:%d FAN_RPM:%d AUTO:%d", &cpu, &duty, &rpm, &auto_mode) == 4) {
            out_status->cpu_temp = cpu;
            out_status->fan_duty = duty;
            out_status->fan_rpm = rpm;
            out_status->auto_mode = auto_mode;
            ok = 0;
        }
    }
    dbus_message_unref(reply);
    return ok;
}

int clevo_set_fan_duty(ClevoIpc *handle, int duty) {
    if (!handle) return -1;
    DBusMessage *reply = call_int(handle, "SetFanDuty", duty);
    if (!reply) return -1;
    dbus_message_unref(reply);
    return 0;
}

int clevo_set_auto_mode(ClevoIpc *handle, bool enable) {
    if (!handle) return -1;
    DBusMessage *reply = call_bool(handle, "SetAutoMode", enable ? TRUE : FALSE);
    if (!reply) return -1;
    dbus_message_unref(reply);
    return 0;
}

int clevo_set_target_temp(ClevoIpc *handle, int celsius) {
    if (!handle) return -1;
    DBusMessage *reply = call_int(handle, "SetTargetTemp", celsius);
    if (!reply) return -1;
    dbus_message_unref(reply);
    return 0;
}

int clevo_get_max_duty_change(ClevoIpc *handle, int *out_rate) {
    if (!handle || !out_rate) return -1;
    DBusMessage *msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, "GetMaxDutyChange");
    if (!msg) return -1;
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(handle->conn, msg, -1, NULL);
    dbus_message_unref(msg);
    if (!reply) return -1;
    DBusMessageIter iter; dbus_message_iter_init(reply, &iter);
    const char *resp = NULL;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) dbus_message_iter_get_basic(&iter, &resp);
    int ok = -1;
    if (resp) {
        int rate;
        if (sscanf(resp, "MAX_DUTY_CHANGE:%d", &rate) == 1) {
            *out_rate = rate; ok = 0;
        }
    }
    dbus_message_unref(reply);
    return ok;
}

int clevo_set_max_duty_change(ClevoIpc *handle, int rate) {
    if (!handle) return -1;
    DBusMessage *reply = call_int(handle, "SetMaxDutyChange", rate);
    if (!reply) return -1;
    dbus_message_unref(reply);
    return 0;
}

int clevo_get_max_increase_rate(ClevoIpc *handle, int *out_rate) {
    if (!handle || !out_rate) return -1;
    DBusMessage *msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, "GetMaxDutyIncrease");
    if (!msg) return -1;
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(handle->conn, msg, -1, NULL);
    dbus_message_unref(msg);
    if (!reply) return -1;
    DBusMessageIter iter; dbus_message_iter_init(reply, &iter);
    const char *resp = NULL;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) dbus_message_iter_get_basic(&iter, &resp);
    int ok = -1;
    if (resp) {
        int rate;
        if (sscanf(resp, "MAX_DUTY_INCREASE:%d", &rate) == 1) {
            *out_rate = rate; ok = 0;
        }
    }
    dbus_message_unref(reply);
    return ok;
}

int clevo_set_max_increase_rate(ClevoIpc *handle, int rate) {
    if (!handle) return -1;
    DBusMessage *reply = call_int(handle, "SetMaxDutyIncrease", rate);
    if (!reply) return -1;
    dbus_message_unref(reply);
    return 0;
}

int clevo_get_max_decrease_rate(ClevoIpc *handle, int *out_rate) {
    if (!handle || !out_rate) return -1;
    DBusMessage *msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, "GetMaxDutyDecrease");
    if (!msg) return -1;
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(handle->conn, msg, -1, NULL);
    dbus_message_unref(msg);
    if (!reply) return -1;
    DBusMessageIter iter; dbus_message_iter_init(reply, &iter);
    const char *resp = NULL;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) dbus_message_iter_get_basic(&iter, &resp);
    int ok = -1;
    if (resp) {
        int rate;
        if (sscanf(resp, "MAX_DUTY_DECREASE:%d", &rate) == 1) {
            *out_rate = rate; ok = 0;
        }
    }
    dbus_message_unref(reply);
    return ok;
}

int clevo_set_max_decrease_rate(ClevoIpc *handle, int rate) {
    if (!handle) return -1;
    DBusMessage *reply = call_int(handle, "SetMaxDutyDecrease", rate);
    if (!reply) return -1;
    dbus_message_unref(reply);
    return 0;
}

int clevo_recover_temp(ClevoIpc *handle) {
    if (!handle) return -1;
    DBusMessage *msg = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE, "RecoverTemp");
    if (!msg) return -1;
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(handle->conn, msg, -1, NULL);
    dbus_message_unref(msg);
    if (!reply) return -1;
    dbus_message_unref(reply);
    return 0;
}


