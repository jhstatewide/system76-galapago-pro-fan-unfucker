# PID Controller for Clevo Fan Control

## Overview

The fan control system now includes a sophisticated PID (Proportional-Integral-Derivative) controller that provides smooth, stable temperature regulation while minimizing fan oscillation and noise.

## How PID Control Works

### PID Components

1. **Proportional (P)**: Responds to the current error (difference between target and actual temperature)
   - Higher values = faster response but potential oscillation
   - Lower values = slower response but more stable

2. **Integral (I)**: Accumulates error over time to eliminate steady-state offset
   - Eliminates the difference between target and actual temperature
   - Too high can cause oscillation

3. **Derivative (D)**: Responds to the rate of change of error
   - Reduces overshoot and oscillation
   - Acts as a "damping" force

### Mathematical Formula

```
Output = Kp × Error + Ki × ∫Error dt + Kd × d(Error)/dt
```

Where:
- `Error = Current_Temperature - Target_Temperature`
- `Kp`, `Ki`, `Kd` are the tuning parameters

## Default Settings

- **Kp (Proportional)**: 2.0
- **Ki (Integral)**: 0.1  
- **Kd (Derivative)**: 0.5
- **Target Temperature**: 65°C
- **Output Range**: 0-100%

## Tuning Guidelines

### Starting Point
Begin with the default settings. These provide a good balance of performance and quietness for most systems.

### Tuning Parameters

#### Kp (Proportional Gain)
- **Too low**: Slow response, temperature may exceed target
- **Too high**: Oscillation, rapid fan speed changes
- **Good range**: 1.0 - 4.0

#### Ki (Integral Gain)  
- **Too low**: Temperature may not reach target (steady-state error)
- **Too high**: Oscillation, overshoot
- **Good range**: 0.05 - 0.3

#### Kd (Derivative Gain)
- **Too low**: Overshoot, oscillation
- **Too high**: Slow response, may amplify noise
- **Good range**: 0.1 - 1.0

### Preset Configurations

#### Conservative (Default)
```bash
--pid-kp 2.0 --pid-ki 0.1 --pid-kd 0.5
```
Good for most systems, balanced performance and quietness.

#### Aggressive Cooling
```bash
--pid-kp 3.0 --pid-ki 0.2 --pid-kd 0.8
```
Faster response, keeps temperatures lower, may be louder.

#### Quiet Operation
```bash
--pid-kp 1.5 --pid-ki 0.05 --pid-kd 0.3
```
Minimal fan changes, quieter operation, may run slightly warmer.

#### Minimal Oscillation
```bash
--pid-kp 1.0 --pid-ki 0.02 --pid-kd 0.8
```
Very stable fan speeds, good for noise-sensitive environments.

## Testing Your Configuration

Use the test script to compare different settings:

```bash
./test_pid_control.sh
```

This will run 4 different configurations for 10 seconds each, allowing you to observe the behavior.

## Command Line Options

### PID Parameters
- `--pid-kp <value>`: Set proportional gain
- `--pid-ki <value>`: Set integral gain  
- `--pid-kd <value>`: Set derivative gain
- `--pid-output-min <value>`: Minimum output (default: 0.0)
- `--pid-output-max <value>`: Maximum output (default: 100.0)
- `--pid-enabled <0|1>`: Enable/disable PID control
- `--pid-reset`: Reset PID controller state

### Examples

```bash
# Conservative settings
sudo bin/clevo-indicator --status --target-temp 65

# Aggressive cooling
sudo bin/clevo-indicator --status --target-temp 60 --pid-kp 3.0 --pid-ki 0.2 --pid-kd 0.8

# Quiet operation  
sudo bin/clevo-indicator --status --target-temp 70 --pid-kp 1.5 --pid-ki 0.05 --pid-kd 0.3

# Disable PID (use simple control)
sudo bin/clevo-indicator --status --pid-enabled 0
```

## Troubleshooting

### Fan Oscillating Rapidly
- Reduce Kp (try 1.5 or 1.0)
- Increase Kd (try 0.8 or 1.0)
- Check if target temperature is realistic

### Temperature Not Reaching Target
- Increase Ki (try 0.2 or 0.3)
- Increase Kp (try 2.5 or 3.0)
- Check if target temperature is achievable

### Slow Response to Load Changes
- Increase Kp (try 2.5 or 3.0)
- Reduce Kd (try 0.3 or 0.2)

### Fan Too Loud
- Reduce Kp (try 1.5 or 1.0)
- Increase target temperature
- Use quiet preset configuration

## Advanced Features

### Anti-Windup Protection
The PID controller includes anti-windup protection to prevent the integral term from accumulating excessive values that could cause instability.

### Smooth Transitions
The controller automatically resets its state when switching between manual and automatic modes to prevent sudden fan speed changes.

### Debug Output
Enable debug mode to see detailed PID calculations:
```bash
sudo bin/clevo-indicator --status --debug
```

This will show:
- Current temperature and target
- Error calculation
- P, I, D component values
- Final output and duty cycle

## Comparison with Simple Control

### Simple Control (Old Method)
- On/off behavior around target temperature
- Frequent fan speed changes
- Potential for oscillation
- Simple but less stable

### PID Control (New Method)
- Smooth, proportional response
- Minimal oscillation
- Better temperature stability
- More sophisticated but more stable

## Technical Details

### Sampling Rate
The PID controller operates at the same rate as the status display interval (default: 2 seconds). For more responsive control, use a shorter interval:

```bash
sudo bin/clevo-indicator --status --interval 1
```

### Temperature Input
The controller uses the maximum of CPU and GPU temperature as the process variable, ensuring the hottest component drives the control decision.

### Output Clamping
The controller output is clamped to the valid fan duty cycle range (0-100%) to prevent invalid commands to the fan controller. 