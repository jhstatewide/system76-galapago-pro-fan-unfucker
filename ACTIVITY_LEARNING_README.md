# Activity-Based Learning for Adaptive PID Controller

## Overview

The adaptive PID controller now includes intelligent activity detection to prevent overfitting during idle periods. This ensures the controller only learns from meaningful thermal events rather than artificially high performance scores during stable idle conditions.

## The Problem: Overfitting During Idle Periods

### Traditional Adaptive PID Issues

The original adaptive PID controller had several problems when the system was idle:

1. **False Performance Scores**: During idle periods, temperatures stay constant and close to target, creating artificially high performance scores
2. **Meaningless Learning**: The controller would "learn" from idle conditions where no real thermal management was needed
3. **Parameter Drift**: Over time, parameters would drift toward values optimized for idle conditions rather than active thermal management
4. **Poor Response to Load**: When real thermal load occurred, the overfitted parameters would perform poorly

### Example Scenario

```
Idle Period (30 minutes):
- CPU: 45°C (target: 65°C) ✓
- GPU: 42°C (target: 65°C) ✓  
- Fan: 20% duty ✓
- Performance Score: 0.95 (artificially high)

Result: Controller "learns" that low fan speeds are optimal
```

```
Load Period (5 minutes):
- CPU: 85°C (target: 65°C) ✗
- GPU: 78°C (target: 65°C) ✗
- Fan: 20% duty (still using "learned" idle parameters) ✗
- Performance Score: 0.15 (poor)

Result: Controller fails to respond appropriately to real thermal load
```

## The Solution: Activity-Based Learning

### Core Principles

1. **Activity Detection**: Monitor both temperature changes and fan duty changes
2. **Learning Inhibition**: Prevent learning during extended idle periods
3. **Smart Resumption**: Resume learning when meaningful activity is detected
4. **Configurable Thresholds**: Allow tuning of activity sensitivity

### Activity Detection Logic

The system considers the system "active" when either:

- **Temperature Activity**: Temperature changes by ≥ threshold (default: 2°C)
- **Fan Activity**: Fan duty changes by ≥ threshold (default: 5%)

### Learning Inhibition Conditions

Learning is inhibited when:

1. **Extended Stability**: No significant activity for ≥ threshold (default: 300 seconds)
2. **Too Many Idle Cycles**: Consecutive idle learning cycles ≥ threshold (default: 5)
3. **Manual Inhibition**: Learning explicitly disabled during stable periods

### Learning Resumption

Learning automatically resumes when:

1. **New Activity Detected**: Significant temperature or fan changes occur
2. **Manual Reset**: PID controller is manually reset
3. **Mode Change**: Switching between auto/manual modes

## Configuration Parameters

### Activity Detection Thresholds

```bash
--adaptive-activity-threshold <°C>     # Temperature change threshold (1-10°C, default: 2)
--adaptive-fan-activity-threshold <%>  # Fan duty change threshold (1-20%, default: 5)
```

### Learning Inhibition Settings

```bash
--adaptive-stable-period <seconds>     # Stability period before inhibition (60-1800s, default: 300)
--adaptive-max-idle-cycles <number>    # Max idle cycles before inhibition (1-20, default: 5)
```

## Usage Examples

### Conservative Settings (Recommended for Most Users)

```bash
# Only learn during significant thermal events
sudo bin/clevo-indicator --adaptive-activity-threshold 3 \
    --adaptive-fan-activity-threshold 8 \
    --adaptive-stable-period 600 \
    --adaptive-max-idle-cycles 3
```

### Aggressive Settings (For Systems with Frequent Small Loads)

```bash
# Learn from smaller thermal changes
sudo bin/clevo-indicator --adaptive-activity-threshold 1 \
    --adaptive-fan-activity-threshold 2 \
    --adaptive-stable-period 120 \
    --adaptive-max-idle-cycles 2
```

### Traditional Behavior (No Activity Detection)

```bash
# Disable activity-based learning (not recommended)
sudo bin/clevo-indicator --adaptive-activity-threshold 100 \
    --adaptive-fan-activity-threshold 100 \
    --adaptive-stable-period 9999 \
    --adaptive-max-idle-cycles 9999
```

## Status Display Indicators

The status display shows the current learning state:

- **`[ACTIVE LEARNING]`**: Normal operation, learning enabled
- **`[LOW ACTIVITY]`**: Warning that learning may be inhibited soon
- **`[LEARNING INHIBITED]`**: Learning disabled due to idle period

## Debug Information

With `--debug` enabled, you'll see detailed activity detection:

```
[DEBUG] Activity detected: temp_change=3°C, fan_change=7%, temp_active=YES, fan_active=YES
[DEBUG] Learning inhibited: 350 seconds since last activity (threshold: 300)
[DEBUG] Skipping adaptive tuning due to inactivity (idle cycles: 3)
```

## Benefits

### 1. Prevents Overfitting
- No learning from idle conditions with artificially high performance scores
- Maintains parameters optimized for real thermal management scenarios

### 2. Improves Long-term Performance
- Parameters remain tuned for actual thermal load conditions
- Better response when real thermal events occur

### 3. Reduces Parameter Drift
- Prevents gradual parameter changes during extended idle periods
- Maintains learned parameters during stable conditions

### 4. Configurable Sensitivity
- Adjustable thresholds for different system characteristics
- Can be tuned for different usage patterns

## Testing

Use the provided test script to see activity-based learning in action:

```bash
./test_activity_learning.sh
```

This script demonstrates:
- Default activity detection settings
- Aggressive vs conservative thresholds
- Comparison with traditional adaptive PID behavior

## Best Practices

### 1. Start with Default Settings
The default settings work well for most systems. Only adjust if you notice specific issues.

### 2. Monitor Learning Behavior
Watch the status display to understand when learning is active vs inhibited.

### 3. Adjust Based on Usage Patterns
- **Gaming/Workstation**: Use conservative settings (higher thresholds)
- **Development/Light Use**: Use aggressive settings (lower thresholds)
- **Server/Always-on**: Use very conservative settings

### 4. Use Debug Mode for Tuning
Enable debug output to see detailed activity detection and learning decisions.

## Troubleshooting

### Learning Never Occurs
- Check if activity thresholds are too high
- Verify that temperature/fan changes are being detected
- Look for debug messages about activity detection

### Learning Inhibited Too Aggressively
- Increase `--adaptive-stable-period`
- Increase `--adaptive-max-idle-cycles`
- Decrease activity thresholds

### Learning Too Sensitive
- Increase activity thresholds
- Decrease `--adaptive-stable-period`
- Decrease `--adaptive-max-idle-cycles`

## Technical Details

### Activity Detection Algorithm

```c
bool activity_detected = false;
int temp_change = abs(current_temp - previous_temp);
int fan_change = abs(current_fan_duty - previous_fan_duty);

if (temp_change >= activity_threshold || fan_change >= fan_activity_threshold) {
    activity_detected = true;
    last_activity_time = current_time;
    consecutive_idle_cycles = 0;
}
```

### Learning Inhibition Logic

```c
bool should_learn = true;

if (learning_inhibited) {
    should_learn = false;
} else if (time_since_activity > stable_period_required) {
    should_learn = false;
} else if (consecutive_idle_cycles >= max_idle_cycles) {
    should_learn = false;
}
```

This system ensures that the adaptive PID controller only learns from meaningful thermal events, preventing overfitting and maintaining optimal performance across different usage scenarios. 