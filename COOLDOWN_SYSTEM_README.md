# Enhanced Cooldown System for Stall Prevention

## Overview

The enhanced cooldown system is designed to **prevent fan stalls in the first place** rather than just recovering from them. It implements a multi-layered approach to maintain fan stability and prevent the recurring stall pattern we observed.

## Problem Analysis

### Root Cause of Recurring Stalls
The logs showed a clear pattern:
1. **Fan stalls** (0 RPM despite duty cycles)
2. **Recovery** (fan reaches 4889 RPM - very high!)
3. **Immediate re-stall** (normal control resumes, fan can't handle transition)
4. **Repeat cycle**

This indicated that the control system was driving the fan incorrectly, causing it to stall due to:
- **Aggressive recovery sequences** (4889 RPM is too high)
- **Immediate control resumption** after recovery
- **Lack of stabilization period** after recovery
- **Rapid duty cycle changes** that stress the fan

## Solution: Multi-Layer Cooldown System

### 1. **Post-Recovery Cooldown**
When a fan recovery is successful, the system enters a **45-second cooldown period**:
- **Duration**: 45 seconds
- **Stable Duty**: Maintains the recovery duty cycle
- **Purpose**: Prevents immediate re-stall by giving the fan time to stabilize
- **State**: `POST_RECOVERY` (state 1)

### 2. **Stall Prevention Cooldown**
When potential stall conditions are detected, the system enters a **preventive cooldown**:
- **Duration**: 15-20 seconds
- **Stable Duty**: Current duty cycle or emergency duty
- **Purpose**: Prevents stalls by maintaining stable conditions
- **State**: `STALL_PREVENTION` (state 2)

### 3. **Duty Change Limiting**
During any cooldown period, duty cycle changes are **limited to 5% per change**:
- **Prevents**: Rapid duty changes that could cause stalls
- **Allows**: Gradual adjustments for temperature control
- **Maintains**: Fan stability while still responding to thermal needs

## Implementation Details

### Cooldown State Management
```c
typedef struct {
    int cooldown_state;      // 0=normal, 1=post_recovery, 2=stall_prevention
    int cooldown_duration;   // seconds
    int stable_duty;         // duty cycle to maintain during cooldown
    time_t phase_start_time; // when cooldown started
} fan_health_t;
```

### Key Functions

#### `enter_cooldown_state(int state, int duration, int stable_duty)`
- Enters a cooldown state with specified parameters
- Logs the transition for debugging
- Sets up the cooldown timer

#### `update_cooldown_state(void)`
- Called every control cycle
- Checks if cooldown period has expired
- Maintains stable duty during cooldown
- Transitions back to normal control when complete

#### `get_cooldown_adjusted_duty(int requested_duty)`
- Limits duty changes to 5% during cooldown
- Returns adjusted duty cycle
- Logs adjustments for transparency

#### `is_in_cooldown(void)`
- Simple check for active cooldown state
- Used throughout the control system

## Integration Points

### 1. **Main Control Loop**
Both auto and manual control now use cooldown-adjusted duty cycles:
```c
int adjusted_duty = get_cooldown_adjusted_duty(next_duty);
ec_write_fan_duty(adjusted_duty);
```

### 2. **Recovery System**
All successful recoveries enter post-recovery cooldown:
```c
if (rpm > SAFE_FAN_RPM) {
    enter_cooldown_state(1, 45, recovery_duty);
    return EXIT_SUCCESS;
}
```

### 3. **Health Monitoring**
Stall prevention cooldown triggered by:
- **Critical RPM**: Below minimum threshold
- **Low RPM**: Below safe operating level
- **Rapid changes**: Multiple duty adjustments

## Cooldown Triggers

### Post-Recovery Cooldown (45 seconds)
- **Trigger**: Successful fan recovery
- **Duty**: Recovery duty cycle (25%, 60%, 80%, etc.)
- **Purpose**: Stabilize fan after recovery

### Stall Prevention Cooldown (15-20 seconds)
- **Trigger**: Low RPM detection (3+ consecutive readings)
- **Duty**: Current duty or emergency duty
- **Purpose**: Prevent stall before it happens

### Emergency Cooldown (20 seconds)
- **Trigger**: Critical RPM below minimum threshold
- **Duty**: Emergency duty (100%)
- **Purpose**: Immediate stabilization

## Benefits

### 1. **Prevents Recurring Stalls**
- Eliminates the "recover → immediate stall" cycle
- Gives fan time to stabilize after recovery
- Maintains consistent operating conditions

### 2. **Reduces Fan Stress**
- Limits rapid duty changes
- Prevents aggressive control sequences
- Maintains stable operating points

### 3. **Improves System Reliability**
- Fewer recovery attempts needed
- More predictable fan behavior
- Better thermal management

### 4. **Enhanced Debugging**
- Clear cooldown state logging
- Duty adjustment transparency
- Recovery sequence tracking

## Configuration

### Cooldown Durations
- **Post-Recovery**: 45 seconds (configurable)
- **Stall Prevention**: 15-20 seconds (configurable)
- **Emergency**: 20 seconds (configurable)

### Duty Change Limits
- **Normal Mode**: No limits
- **Cooldown Mode**: 5% maximum change per cycle

### Recovery Duty Cycles
- **Step 1**: 60% duty
- **Step 2**: 80% duty
- **Step 3**: 25% duty
- **Step 4**: Cycling through 25%, 40%, 25%, 60%, 25%

## Monitoring and Logging

### Cooldown State Logs
```
Entering cooldown state: POST_RECOVERY for 45 seconds, stable_duty=60%
Maintaining stable duty 60% during post-recovery cooldown (30 seconds remaining)
Post-recovery cooldown completed, returning to normal control
```

### Duty Adjustment Logs
```
Cooldown: limiting duty increase from 80% to 65%
Auto fan duty adjusted from 80% to 65% (cooldown)
```

### Recovery Sequence Logs
```
Fan responding at 4889 RPM - recovery successful
Entering cooldown state: POST_RECOVERY for 45 seconds, stable_duty=60%
```

## Testing and Validation

### Expected Behavior
1. **Fan stalls** → Recovery sequence
2. **Recovery successful** → 45-second cooldown at recovery duty
3. **Cooldown complete** → Gradual return to normal control
4. **No immediate re-stall** → Stable operation

### Verification Commands
```bash
# Monitor cooldown system
sudo journalctl -u clevo-daemon -f | grep -E "(cooldown|recovery|stall)"

# Check current state
sudo journalctl -u clevo-daemon --since "5 minutes ago" | grep -E "(Entering|Maintaining|completed)"
```

## Future Enhancements

### 1. **Adaptive Cooldown**
- Adjust cooldown duration based on fan behavior
- Learn from successful vs. failed recoveries
- Dynamic duty change limits

### 2. **Predictive Stall Detection**
- Monitor fan response patterns
- Detect stall precursors
- Proactive cooldown activation

### 3. **Temperature-Aware Cooldown**
- Adjust cooldown parameters based on CPU temperature
- Emergency bypass for critical thermal conditions
- Thermal-aware duty limits

### 4. **Fan Health Scoring**
- Track fan reliability over time
- Adjust cooldown parameters based on fan age/condition
- Predictive maintenance indicators

This comprehensive cooldown system should significantly reduce or eliminate the recurring stall pattern by providing the fan with stable operating conditions and preventing the aggressive control sequences that were causing the stalls. 