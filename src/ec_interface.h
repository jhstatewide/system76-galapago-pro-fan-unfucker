#ifndef EC_INTERFACE_H
#define EC_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "fan_constants.h"

// EC register definitions
#define EC_SC 0x66
#define EC_DATA 0x62
#define IBF 1
#define OBF 0
#define EC_SC_READ_CMD 0x80
#define EC_REG_SIZE 0x100
#define EC_REG_CPU_TEMP 0x07
#define EC_REG_FAN_DUTY 0xCE
#define EC_REG_FAN_RPMS_HI 0xD0
#define EC_REG_FAN_RPMS_LO 0xD1
#define MAX_FAN_RPM FAN_MAX_RPM

/**
 * Initialize EC interface
 * @return 0 on success, -1 on failure
 */
int ec_init(void);

/**
 * Query CPU temperature from EC
 * @return Temperature in degrees Celsius, -1 on error
 */
int ec_query_cpu_temp(void);

/**
 * Query current fan duty cycle
 * @return Fan duty cycle percentage (0-100), -1 on error
 */
int ec_query_fan_duty(void);

/**
 * Query current fan RPM
 * @return Fan RPM, -1 on error
 */
int ec_query_fan_rpms(void);

/**
 * Write fan duty cycle to EC
 * @param duty_percentage Fan duty cycle (1-100)
 * @return 0 on success, -1 on failure
 */
int ec_write_fan_duty(int duty_percentage);

/**
 * Write fan duty cycle to EC with retry logic and exponential backoff
 * @param duty_percentage Fan duty cycle (1-100)
 * @param max_retries Maximum number of retry attempts (default: 3)
 * @return 0 on success, -1 on failure
 */
int ec_write_fan_duty_with_retry(int duty_percentage, int max_retries);

/**
 * Test fan by setting duty cycle and reading back
 * @param duty_percentage Fan duty cycle to test
 * @return 0 on success, -1 on failure
 */
int ec_test_fan(int duty_percentage);

/**
 * Dump current fan information
 * @return 0 on success, -1 on failure
 */
int ec_dump_fan(void);

/**
 * Clean up EC interface
 */
void ec_cleanup(void);

// Auto duty adjustment configuration functions
/**
 * Set target temperature for auto duty adjustment
 * @param temp Target temperature in degrees Celsius
 */
void ec_set_target_temperature(int temp);

/**
 * Set maximum duty cycle for auto duty adjustment
 * @param duty Maximum duty cycle percentage (1-100)
 */
void ec_set_max_duty_cycle(int duty);

/**
 * Set PID controller parameters
 * @param kp Proportional gain
 * @param ki Integral gain
 * @param kd Derivative gain
 */
void ec_set_pid_parameters(double kp, double ki, double kd);

/**
 * Set rate limiting parameters for duty changes
 * @param max_increase Maximum duty increase per cycle
 * @param max_decrease Maximum duty decrease per cycle
 */
void ec_set_rate_limits(int max_increase, int max_decrease);

/**
 * Set fan stall prevention threshold
 * @param threshold Minimum duty cycle to prevent fan stalls
 */
void ec_set_stall_prevention_threshold(int threshold);

/**
 * Enable or disable PID controller
 * @param enabled true to enable PID, false for simple control
 */
void ec_enable_pid(bool enabled);

/**
 * Enable or disable adaptive PID tuning
 * @param enabled true to enable adaptive tuning
 */
void ec_enable_adaptive_pid(bool enabled);

/**
 * Enhanced auto duty adjustment with PID control, rate limiting, and stall prevention
 * This function implements the complete fan control logic including:
 * - PID controller with anti-windup
 * - Temperature history tracking for stuck detection
 * - Rate limiting with emergency bypass
 * - Fan stall prevention
 * - Critical temperature protection
 * - Aggressive recovery for stuck fans
 * 
 * @return Calculated duty cycle percentage (0-100)
 */
int ec_auto_duty_adjust(void);

#endif // EC_INTERFACE_H 