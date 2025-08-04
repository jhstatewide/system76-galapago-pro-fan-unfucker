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

#endif // FAN_CONSTANTS_H 