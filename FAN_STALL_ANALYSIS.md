# Fan Stall Analysis: Software Failure Modes and Prevention

## Executive Summary

Analysis of the clevo-daemon logs revealed critical fan stall conditions including RPM=0 (complete fan stop) and RPM values significantly below expected thresholds. This document identifies software failure modes that could cause these stalls and provides prioritized recommendations for prevention.

## Log Analysis Findings

### Critical Anomalies Detected
- **22:37:19**: RPM=0 (expected >2720) at duty=68% - **Fan completely stopped**
- **22:36:42**: RPM=2796 (expected >4000) at duty=100% - Fan not reaching expected speed
- **22:33:48**: RPM=197 (expected >1120) at duty=28% - Fan severely underperforming
- **Service timeout**: Daemon killed with SIGKILL due to shutdown timeout

## Software Failure Modes Analysis

### 1. EC I/O Timing Failures ⚠️ **CRITICAL**

**Problem**: The EC interface has insufficient timeout handling and retry logic.

**Code Location**: `src/ec_interface.c:75-85`
```c
static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);  // Only 1ms delay
        data = inb(port);
    }
    if (i >= 100) {
        logging_error("EC I/O wait error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x",
                port, data, flag, value);
        return -1;
    }
    return 0;
}
```

**Failure Mode**: 
- EC operations timeout after only 100ms
- Failed duty cycle writes result in fan not receiving commands
- Invalid RPM readings (returning 0) when EC is busy
- Fan control commands being ignored by hardware

**Impact**: This is the most likely cause of the RPM=0 condition observed in logs.

**Solution**: Implement exponential backoff and retry logic for EC operations.

### 2. Rate Limiting Aggressiveness ⚠️ **HIGH PRIORITY**

**Problem**: Rate limiting prevents necessary rapid fan response to temperature spikes.

**Code Location**: `src/clevo-daemon.c:968-980`
```c
if (new_duty > current_duty + max_increase) {
    int original_duty = new_duty;
    new_duty = current_duty + max_increase;  // Limits increase to 5-10%
    if (debug_mode) {
        daemon_log(LOG_DEBUG, "Normal rate limiting: limiting duty increase from %d to %d (max_increase=%d)", 
                  original_duty, new_duty, max_increase);
    }
}
```

**Failure Mode**:
- Temperature spikes rapidly but fan cannot respond quickly enough
- Delayed cooling response leads to temperature overshoot
- Fan gets stuck at low duty cycles during high load

**Solution**: Implement emergency bypass for rapid temperature changes.

### 3. Stall Prevention Logic Conflicts ⚠️ **MEDIUM PRIORITY**

**Problem**: Multiple stall prevention mechanisms can conflict and prevent necessary fan speeds.

**Code Location**: `src/clevo-daemon.c:945-955`
```c
// High duty stall prevention
if (new_duty > 80 && (current_time - fan_health.last_stall_detection) < 300) {
    logging_telemetry("Limiting duty to 80%% to avoid high duty stall (recent stall detected)");
    new_duty = 80;  // Limits to 80% even when 100% needed
}

// Stall prevention threshold
if (new_duty < fan_stall_prevention_threshold && temp > target_temperature) {
    logging_telemetry("Duty cycle %d%% below stall prevention threshold, adjusting to %d%%", 
              new_duty, fan_stall_prevention_threshold);
    new_duty = fan_stall_prevention_threshold;  // Forces 25% minimum
}
```

**Failure Mode**: System prevents high duty cycles when they're actually needed for cooling.

**Solution**: Add temperature-based exceptions to stall prevention logic.

### 4. Temperature Sensor Glitches ⚠️ **MEDIUM PRIORITY**

**Problem**: Basic temperature validation allows invalid readings to affect control.

**Code Location**: `src/clevo-daemon.c:133-150`
```c
// Temperature validation for sensor glitch detection
static int last_cpu_temp = 0;
static int temp_validation_enabled = 1;
static int max_temp_change_per_cycle = 10;  // Maximum °C change per cycle
```

**Failure Mode**: Invalid temperature readings cause incorrect PID responses, leading to inappropriate fan speeds.

**Solution**: Implement comprehensive temperature validation with fallback mechanisms.

### 5. PID Controller Windup ⚠️ **MEDIUM PRIORITY**

**Problem**: PID integral term can wind up during temperature spikes.

**Code Location**: `src/pid_controller.c:655-665`
```c
// Integral term with anti-windup
pid_integral += error;
if (pid_integral > 100.0) pid_integral = 100.0;
if (pid_integral < -100.0) pid_integral = -100.0;
double integral = pid_ki * pid_integral;
```

**Failure Mode**: Integral windup can cause controller to overshoot or undershoot, leading to fan speed oscillations.

**Solution**: Implement proper anti-windup with conditional integration.

### 6. Shared Memory Race Conditions ⚠️ **LOW PRIORITY**

**Problem**: Multiple processes accessing shared fan state without proper synchronization.

**Failure Mode**: Inconsistent fan state between processes, leading to control conflicts.

**Solution**: Implement proper synchronization mechanisms.

## Recommended Solutions

### Priority #1: Enhanced EC I/O Reliability

**Implementation**: Add retry logic with exponential backoff for all EC operations.

```c
static int ec_write_fan_duty_with_retry(int duty_percentage, int max_retries) {
    for (int attempt = 0; attempt < max_retries; attempt++) {
        int result = ec_write_fan_duty(duty_percentage);
        if (result == 0) return 0;
        
        // Exponential backoff: 1ms, 2ms, 4ms, 8ms...
        int delay_ms = (1 << attempt);
        usleep(delay_ms * 1000);
        
        logging_debug("EC write retry %d/%d for duty %d%%", attempt + 1, max_retries, duty_percentage);
    }
    
    logging_error("EC write failed after %d retries for duty %d%%", max_retries, duty_percentage);
    return -1;
}
```

### Priority #2: Emergency Temperature Response

**Implementation**: Bypass rate limiting for critical temperature changes.

```c
// Enhanced rate limiting with emergency bypass
bool emergency_bypass = (temp_error >= 8) || 
                       (temp_error >= 5 && new_duty >= 80) || 
                       temp_stuck ||
                       (temp_error >= 3 && temp_error > last_temp_error + 2);  // Rapid temp increase

if (emergency_bypass) {
    max_increase = 50;  // Allow 50% duty increase for emergencies
    logging_warning("Emergency bypass: temp_error=%d, allowing rapid duty increase", temp_error);
}
```

### Priority #3: Enhanced Stall Detection

**Implementation**: More aggressive stall detection with immediate recovery.

```c
// Enhanced stall detection
if (current_rpm < MIN_FAN_RPM || current_rpm < expected_rpm * 0.5) {
    // Fan is stalled - immediate recovery
    int recovery_duty = MAX(new_duty + 30, FAN_EMERGENCY_DUTY);
    new_duty = MIN(recovery_duty, 100);
    
    logging_warning("Fan stall detected: RPM=%d (expected >%d) - emergency recovery to %d%%", 
                   current_rpm, expected_rpm, new_duty);
    
    // Force immediate EC write with retry
    if (ec_write_fan_duty_with_retry(new_duty, 3) != 0) {
        logging_error("Emergency fan recovery failed - hardware issue suspected");
    }
}
```

### Priority #4: Temperature Validation

**Implementation**: Comprehensive temperature validation with fallback mechanisms.

```c
// Enhanced temperature validation
static int validate_temperature(int temp, int last_temp) {
    // Basic range check
    if (temp < 0 || temp > 120) {
        logging_warning("Temperature out of range: %d°C", temp);
        return last_temp;  // Use last valid reading
    }
    
    // Rate of change check
    int temp_change = abs(temp - last_temp);
    if (temp_change > max_temp_change_per_cycle) {
        logging_warning("Suspicious temperature change: %d°C -> %d°C (change: %d°C)", 
                       last_temp, temp, temp_change);
        return last_temp;  // Use last valid reading
    }
    
    // Stuck sensor detection
    if (temp == last_temp && temp_change == 0) {
        static int stuck_count = 0;
        stuck_count++;
        if (stuck_count > 10) {
            logging_warning("Temperature sensor appears stuck at %d°C", temp);
            return last_valid_temp;  // Use last known good reading
        }
    } else {
        stuck_count = 0;
    }
    
    return temp;  // Temperature is valid
}
```

## Implementation Strategy

### Phase 1: Immediate (Week 1)
1. Implement EC retry logic with exponential backoff
2. Add emergency temperature bypass for rate limiting
3. Enhance stall detection with immediate recovery
4. Add comprehensive temperature validation

### Phase 2: Short-term (Week 2-3)
1. Refactor PID controller with proper anti-windup
2. Implement shared memory synchronization
3. Add comprehensive logging for all failure modes
4. Create automated testing for stall conditions

### Phase 3: Medium-term (Month 2)
1. Implement adaptive PID tuning
2. Add predictive stall detection
3. Create fan health monitoring dashboard
4. Develop automated recovery procedures

### Phase 4: Long-term (Month 3+)
1. Machine learning for stall prediction
2. Advanced temperature modeling
3. Integration with system monitoring tools
4. Performance optimization

## Monitoring and Alerting

### Key Metrics to Monitor
- EC I/O failure rate
- Temperature validation failures
- Stall detection frequency
- Emergency bypass activations
- PID controller performance

### Alerting Thresholds
- EC failures > 5% of operations
- Temperature glitches > 2 per hour
- Stall detections > 1 per hour
- Emergency bypass > 3 per hour

## Conclusion

The primary cause of fan stalls appears to be EC I/O timing failures combined with overly aggressive rate limiting. The immediate priority should be implementing robust retry logic for EC operations and emergency bypass mechanisms for rapid temperature changes.

The RPM=0 condition observed in logs strongly suggests hardware communication failures rather than mechanical fan issues. Implementing the recommended solutions should significantly reduce stall occurrences and improve overall system reliability.

## Appendix: Code References

- **EC Interface**: `src/ec_interface.c:75-85`
- **Rate Limiting**: `src/clevo-daemon.c:968-980`
- **Stall Prevention**: `src/clevo-daemon.c:945-955`
- **PID Controller**: `src/pid_controller.c:655-665`
- **Temperature Monitoring**: `src/temperature_monitor.c:177-227`
- **Fan Health**: `src/fan_health.c:185-226` 