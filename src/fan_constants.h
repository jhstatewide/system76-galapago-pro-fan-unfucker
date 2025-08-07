#ifndef FAN_CONSTANTS_H
#define FAN_CONSTANTS_H

// Project-wide fan constants
#define FAN_MIN_RPM 500        // Absolute minimum fan RPM - fan should NEVER go below this
#define FAN_SAFE_RPM 1000      // Safe minimum fan RPM for normal operation
#define FAN_MIN_DUTY 15        // Minimum fan duty cycle (%) to ensure minimum RPM
#define FAN_EMERGENCY_DUTY 60  // Emergency duty cycle when RPM drops too low

// Hardware-specific RPM calibration
// Based on log analysis: Galago Pro delivers ~7-10 RPM per 1% duty cycle
// Original FAN_RPM_DUTY_RATIO 40 was 571% overestimate for this hardware
#define FAN_RPM_DUTY_RATIO 10  // Realistic RPM per 1% duty cycle for Galago Pro
#define GALAGO_PRO_RPM_DUTY_RATIO 10  // Specific to Galago Pro hardware
#define GENERIC_RPM_DUTY_RATIO 40  // Fallback for other hardware

// Maximum fan RPM for this laptop model
#define FAN_MAX_RPM 4400

// Fan control best practices constants
#define FAN_STALL_PREVENTION_DUTY 25    // Minimum duty to prevent fan stall
#define FAN_MAX_DUTY 85                 // Maximum duty cycle to prevent high-speed stalls
#define FAN_RESPONSE_TIMEOUT_MS 200     // Time to wait for fan response (ms)
#define FAN_CYCLING_COOLDOWN_MS 1000   // Minimum time between duty changes (ms)
#define FAN_CRITICAL_TEMP_THRESHOLD 95  // Critical temperature threshold (°C)
#define FAN_EMERGENCY_SHUTDOWN_TEMP 105 // Emergency shutdown temperature (°C)

// Hardware detection and calibration
#define HARDWARE_DETECTION_ENABLED 1    // Enable hardware-specific calibration
#define RPM_CALIBRATION_SAMPLES 10      // Number of samples for calibration
#define RPM_CALIBRATION_DUTY 50         // Duty cycle to use during calibration

#endif // FAN_CONSTANTS_H 