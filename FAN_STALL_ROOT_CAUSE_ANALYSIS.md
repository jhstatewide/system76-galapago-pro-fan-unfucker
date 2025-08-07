# Fan Stall Root Cause Analysis: Galago Pro Hardware Mismatch

## Executive Summary

Analysis of journalctl logs and source code cross-reference reveals that the **fan stall prevention system is creating false positive stalls** due to unrealistic RPM expectations for the Galago Pro hardware. The system expects 40 RPM per 1% duty cycle but the actual hardware delivers approximately 7-10 RPM per 1% duty cycle.

## Log Evidence vs. Source Code Analysis

### Critical Findings from Logs

**From `sudo journalctl -u clevo-daemon --no-pager`:**
```
Aug 04 22:33:48 galp clevo-daemon[120229]: Fan may be stuck: RPM=197 (expected >1120) at duty=28%
Aug 04 22:36:42 galp clevo-daemon[120229]: Fan may be stuck: RPM=2796 (expected >4000) at duty=100%
Aug 04 22:37:19 galp clevo-daemon[120229]: Fan may be stuck: RPM=0 (expected >2720) at duty=68%
Aug 04 22:32:22 galp clevo-daemon[120229]: CRITICAL: Fan RPM (198) below minimum threshold (500)
```

### Source Code Root Cause

**From `fan_constants.h:9`:**
```c
#define FAN_RPM_DUTY_RATIO 40  // Expected minimum RPM per 1% duty cycle
```

**From `clevo-daemon.c:1087`:**
```c
int expected_min_rpm = new_duty * RPM_DUTY_RATIO;
```

**Mathematical Analysis:**
- **System Expectation**: 28% duty × 40 RPM/% = 1120 RPM expected
- **Actual Hardware**: 28% duty → 197 RPM delivered
- **Actual Ratio**: 197 ÷ 28 = **~7 RPM per 1% duty cycle**
- **Expectation vs Reality**: 40 RPM/% expected vs 7 RPM/% actual = **571% overestimate**

## Problem Classification

### Problem 1: Hardware-Specific RPM Calibration ⚠️ **CRITICAL**

**Issue**: Generic `FAN_RPM_DUTY_RATIO = 40` doesn't match Galago Pro fan characteristics.

**Evidence**:
- 100% duty expects 4000 RPM, gets 2796 RPM (70% of expected)
- 28% duty expects 1120 RPM, gets 197 RPM (18% of expected)
- 68% duty expects 2720 RPM, gets 0 RPM (sensor failure or stall)

**Impact**: Every RPM reading appears as a "stall" to the system.

### Problem 2: RPM Reading Validation Gaps ⚠️ **HIGH**

**Current Logic in `clevo-daemon.c:1785-1786`:**
```c
// If we have multiple consecutive zero RPM readings, this is a real stall
if (fan_health.consecutive_zero_rpm >= 2) {
```

**Missing Validation**:
- No filtering for obviously invalid readings (RPM=0 when duty>50%)
- No settling time after duty changes before trusting RPM readings
- No moving average or statistical filtering
- No confidence scoring for RPM readings

### Problem 3: Rate Limiting Implementation Gap ⚠️ **HIGH**

**From `test_rate_limiting.sh:89-90`:**
```bash
echo "The daemon now uses ec_write_fan_duty_with_retry() with 3 retries"
echo "and exponential backoff delays (5ms, 10ms, 20ms) for all fan duty writes."
```

**Current EC Interface in `FAN_STALL_ANALYSIS.md:26-27`:**
```c
usleep(1000);  // Only 1ms delay
```

**Gap**: The exponential backoff retry logic described in the test script is not yet implemented in the main daemon code.

### Problem 4: Stall Prevention Logic Conflicts ⚠️ **MEDIUM**

**From `clevo-daemon.c:940-943`:**
```c
// High duty stall prevention: Avoid very high duty cycles if fan has been stuck recently
if (new_duty > 80 && (current_time - fan_health.last_stall_detection) < 300) {
    logging_telemetry("Limiting duty to 80%% to avoid high duty stall (recent stall detected)");
    new_duty = 80;
}
```

**Combined with minimum threshold:**
```c
// Stall prevention: Never go below minimum safe duty
if (new_duty < fan_stall_prevention_threshold && temp > target_temperature) {
    new_duty = fan_stall_prevention_threshold;  // Forces 25% minimum
}
```

**Result**: System trapped between 25% minimum and 80% maximum during false stall conditions.

## Behavioral Analysis from Logs

### Stall Prevention Feedback Loop

1. **Initial False Detection**: `RPM=197 (expected >1120)` → System thinks fan is stalled
2. **Stall Prevention Activation**: `"Duty cycle 15% below stall prevention threshold, adjusting to 25%"`
3. **Recovery Attempts**: `"Entering cooldown state: STALL_PREVENTION for 20 seconds, stable_duty=60%"`
4. **Continued False Readings**: `RPM=0` during recovery (likely sensor timing issues)
5. **Escalation**: `"Fan still stuck at low RPM (1299), will try full recovery"`
6. **System Timeout**: `"State 'stop-sigterm' timed out. Killing."`

### Service Stability Impact

**From logs:**
```
Aug 04 22:34:38 galp systemd[1]: clevo-daemon.service: Failed with result 'timeout'.
Aug 04 22:49:35 galp systemd[1]: clevo-daemon.service: Failed with result 'timeout'.
Aug 04 22:59:17 galp systemd[1]: clevo-daemon.service: Failed with result 'timeout'.
```

**Pattern**: Daemon gets stuck in recovery loops and fails to respond to shutdown signals.

## Recommended Solutions

### Immediate Fixes (Priority 1)

#### 1. Hardware-Specific RPM Calibration
```c
// In fan_constants.h
#define GALAGO_PRO_RPM_DUTY_RATIO 10  // Realistic: ~10 RPM per 1% duty cycle
// Or implement dynamic calibration during startup
```

#### 2. Implement RPM Reading Validation
```c
// Add settling time after duty changes
#define FAN_RPM_SETTLING_TIME_MS 2000

// In fan health check
if (time_since_duty_change < FAN_RPM_SETTLING_TIME_MS) {
    // Don't trust RPM readings immediately after duty change
    return;  // Skip this check cycle
}
```

#### 3. Add Moving Average RPM Filtering
```c
// Implement 5-point moving average for RPM readings
// Only trigger stall detection on filtered values
typedef struct {
    int rpm_history[5];
    int history_index;
    int history_count;
} rpm_filter_t;
```

### Medium-Term Improvements (Priority 2)

#### 4. Implement Exponential Backoff (From test_rate_limiting.sh)
```c
// Integrate the retry logic described in test script
int ec_write_fan_duty_with_retry(int duty, int max_attempts) {
    int delays[] = {5000, 10000, 20000};  // 5ms, 10ms, 20ms in microseconds
    // Implementation with exponential backoff
}
```

#### 5. Smarter Stall Detection Thresholds
```c
// Hardware-aware stall detection
if (consecutive_low_rpm >= 3 && 
    time_since_duty_change > 3000ms &&
    current_rpm < (expected_min_rpm * 0.3)) {  // 30% tolerance instead of 70%
    // Only then consider it a real stall
}
```

#### 6. Temperature-Context Stall Prevention
```c
// Don't limit high duty cycles when temperature is critical
if (temp < critical_threshold) {
    // Allow normal stall prevention
} else {
    // Override stall prevention for thermal protection
}
```

### Long-Term Architecture (Priority 3)

#### 7. Hardware Detection and Calibration
```c
// Implement hardware-specific fan profiles
typedef struct {
    char* model_name;
    int rpm_duty_ratio;
    int min_rpm_threshold;
    int stall_detection_delay;
} fan_profile_t;
```

#### 8. Adaptive Learning System
```c
// Learn actual fan characteristics during operation
// Adjust expectations based on observed performance
```

## Testing and Validation

### Validation Steps

1. **Deploy hardware-specific RPM ratio**: Test with `GALAGO_PRO_RPM_DUTY_RATIO = 10`
2. **Implement RPM settling time**: Add 2-second delay after duty changes
3. **Monitor for false stall reductions**: Check logs for decreased "Fan may be stuck" messages
4. **Verify thermal protection**: Ensure temperature control still works effectively
5. **Test service stability**: Confirm daemon no longer times out during shutdown

### Success Metrics

- **Reduce false stall detections by >90%**
- **Eliminate service timeout failures**
- **Maintain effective thermal control**
- **Improve system responsiveness during temperature spikes**

## Conclusion

The Galago Pro fan stall issues are primarily **software-induced false positives** rather than hardware failures. The daemon's expectations are calibrated for different hardware, causing it to fight against normal fan behavior. 

**Key insight**: The stall prevention system has become the primary cause of the stalls it's trying to prevent.

**Primary fix**: Adjust `FAN_RPM_DUTY_RATIO` from 40 to approximately 10 to match actual Galago Pro hardware characteristics.

**Secondary fixes**: Implement proper RPM validation, settling times, and the exponential backoff retry logic already described in `test_rate_limiting.sh`.
