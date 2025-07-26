#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <stdbool.h>

/**
 * PID controller structure
 */
typedef struct {
    // PID parameters
    double kp;
    double ki;
    double kd;
    
    // PID state
    double integral;
    double prev_error;
    
    // Output limits
    double output_min;
    double output_max;
    
    // Adaptive tuning parameters
    bool adaptive_enabled;
    int adaptive_tuning_interval;
    double adaptive_target_performance;
    int adaptive_cycle_count;
    double adaptive_temp_history[60];
    int adaptive_temp_history_index;
    int adaptive_temp_history_size;
    double adaptive_performance_score;
    double adaptive_prev_score;
    int adaptive_learning_cycles;
    
    // Adaptive tuning step sizes
    double adaptive_kp_step;
    double adaptive_ki_step;
    double adaptive_kd_step;
    
    // Temperature trend tracking
    int temp_history[10];
    int temp_history_index;
    int temp_history_size;
    int stuck_detection_counter;
    int stuck_threshold_cycles;
    double stuck_temp_threshold;
    
    // Rate limiting
    int max_duty_change_rate;
    int max_duty_increase_rate;
    int max_duty_decrease_rate;
    
    // Safety thresholds
    int min_fan_duty;
    int min_fan_rpm;
    int safe_fan_rpm;
    int emergency_duty;
    int rpm_duty_ratio;
} pid_controller_t;

/**
 * Initialize PID controller
 * @param kp Proportional gain
 * @param ki Integral gain
 * @param kd Derivative gain
 * @param max_duty_change Maximum duty change per cycle
 * @param max_increase_rate Maximum duty increase per cycle
 * @param max_decrease_rate Maximum duty decrease per cycle
 * @param min_fan_duty Minimum fan duty cycle
 * @return Pointer to PID controller, NULL on failure
 */
pid_controller_t* pid_controller_init(double kp, double ki, double kd,
                                    int max_duty_change, int max_increase_rate, int max_decrease_rate,
                                    int min_fan_duty);

/**
 * Calculate fan duty using PID control
 * @param pid PID controller instance
 * @param current_temp Current temperature
 * @param target_temp Target temperature
 * @param current_duty Current fan duty
 * @param current_rpm Current fan RPM
 * @return Calculated fan duty (0-100)
 */
int pid_controller_calculate_duty(pid_controller_t* pid, int current_temp, int target_temp,
                                int current_duty, int current_rpm);

/**
 * Add temperature to history for trend analysis
 * @param pid PID controller instance
 * @param temp Temperature to add
 */
void pid_controller_add_temp_to_history(pid_controller_t* pid, int temp);

/**
 * Check if temperature appears stuck
 * @param pid PID controller instance
 * @return true if temperature appears stuck, false otherwise
 */
bool pid_controller_is_temp_stuck(pid_controller_t* pid);

/**
 * Get aggressive duty for temperature error
 * @param pid PID controller instance
 * @param temp_error Temperature error (current - target)
 * @return Aggressive duty cycle
 */
int pid_controller_get_aggressive_duty_for_error(pid_controller_t* pid, int temp_error);

/**
 * Enable/disable adaptive PID tuning
 * @param pid PID controller instance
 * @param enabled Enable adaptive tuning
 * @param tuning_interval Tuning interval in seconds
 * @param target_performance Target performance score
 */
void pid_controller_set_adaptive_tuning(pid_controller_t* pid, bool enabled,
                                      int tuning_interval, double target_performance);

/**
 * Reset PID controller state
 * @param pid PID controller instance
 */
void pid_controller_reset(pid_controller_t* pid);

/**
 * Clean up PID controller
 * @param pid PID controller instance
 */
void pid_controller_cleanup(pid_controller_t* pid);

#endif // PID_CONTROLLER_H 