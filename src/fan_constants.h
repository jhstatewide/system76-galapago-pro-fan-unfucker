#ifndef FAN_CONSTANTS_H
#define FAN_CONSTANTS_H

// Project-wide fan constants
#define FAN_MIN_RPM 500        // Absolute minimum fan RPM - fan should NEVER go below this
#define FAN_SAFE_RPM 1000      // Safe minimum fan RPM for normal operation
#define FAN_MIN_DUTY 15        // Minimum fan duty cycle (%) to ensure minimum RPM
#define FAN_EMERGENCY_DUTY 60  // Emergency duty cycle when RPM drops too low
#define FAN_RPM_DUTY_RATIO 40  // Expected minimum RPM per 1% duty cycle

// Maximum fan RPM for this laptop model
#define FAN_MAX_RPM 4400

// Fan control best practices constants
#define FAN_STALL_PREVENTION_DUTY 25    // Minimum duty to prevent fan stall
#define FAN_RESPONSE_TIMEOUT_MS 200     // Time to wait for fan response (ms)
#define FAN_CYCLING_COOLDOWN_MS 1000   // Minimum time between duty changes (ms)
#define FAN_CRITICAL_TEMP_THRESHOLD 95  // Critical temperature threshold (°C)
#define FAN_EMERGENCY_SHUTDOWN_TEMP 105 // Emergency shutdown temperature (°C)

#endif // FAN_CONSTANTS_H 