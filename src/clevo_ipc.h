/*
 * Shared IPC client API using D-Bus for Clevo fan control
 */

#ifndef CLEVO_IPC_H
#define CLEVO_IPC_H

#include <stdbool.h>

typedef struct {
    int cpu_temp;
    int fan_duty;
    int fan_rpm;
    int auto_mode; /* 0 or 1 */
} ClevoStatus;

typedef struct ClevoIpc ClevoIpc;

/* Lifecycle */
int clevo_ipc_client_new(ClevoIpc **out_handle);
void clevo_ipc_free(ClevoIpc *handle);

/* RPCs */
int clevo_get_status(ClevoIpc *handle, ClevoStatus *out_status);
int clevo_set_fan_duty(ClevoIpc *handle, int duty);
int clevo_set_auto_mode(ClevoIpc *handle, bool enable);
int clevo_set_target_temp(ClevoIpc *handle, int celsius);

int clevo_get_max_duty_change(ClevoIpc *handle, int *out_rate);
int clevo_set_max_duty_change(ClevoIpc *handle, int rate);

int clevo_get_max_increase_rate(ClevoIpc *handle, int *out_rate);
int clevo_set_max_increase_rate(ClevoIpc *handle, int rate);

int clevo_get_max_decrease_rate(ClevoIpc *handle, int *out_rate);
int clevo_set_max_decrease_rate(ClevoIpc *handle, int rate);

int clevo_recover_temp(ClevoIpc *handle);

#endif /* CLEVO_IPC_H */


