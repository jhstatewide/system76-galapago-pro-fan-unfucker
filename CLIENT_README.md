# Clevo Fan Control Client

This is a modern command-line client for the Clevo fan control daemon. It provides a clean interface to monitor and control your laptop's fan system.

## Features

- **Real-time monitoring** of CPU/GPU temperatures and fan status
- **Manual fan control** with precise duty cycle settings
- **Automatic mode** switching
- **JSON output** for scripting and automation
- **Continuous monitoring** with configurable intervals

## Installation

Build and install the client along with the daemon:

```bash
make
sudo make install
```

## Usage

### Basic Commands

```bash
# Show current status
clevo-client status

# Monitor continuously (updates every 2 seconds)
clevo-client monitor

# Monitor with custom interval (5 seconds)
clevo-client monitor 5

# Set fan to 80% duty cycle
clevo-client set-fan 80

# Enable automatic fan control
clevo-client set-auto

# Get temperature only
clevo-client get-temp

# Get fan status only
clevo-client get-fan
```

### Advanced Options

```bash
# JSON output for scripting
clevo-client --json status

# Verbose output
clevo-client --verbose monitor

# Show help
clevo-client help
```

## Examples

### Monitor fan control in real-time
```bash
clevo-client monitor
```
Output:
```
=== Clevo Fan Control Status ===
CPU Temperature: 45°C
GPU Temperature: 52°C
Fan Duty Cycle:  60%
Fan RPM:         2640
Auto Mode:       ON
===============================
```

### Set manual fan control
```bash
clevo-client set-fan 90
```
Output:
```
Response: OK: Fan set to 90%
```

### JSON output for automation
```bash
clevo-client --json status
```
Output:
```json
{
  "cpu_temperature": 45,
  "gpu_temperature": 52,
  "fan_duty_cycle": 60,
  "fan_rpm": 2640,
  "auto_mode": true
}
```

## Architecture

The client communicates with the `clevo-daemon` through Unix domain sockets (`/tmp/clevo-daemon.sock`). This provides:

- **Efficient local communication** - no network overhead
- **Security** - file-based permissions
- **Reliability** - kernel-managed sockets
- **Simplicity** - standard Unix IPC

## Modern Linux Daemon-Client Patterns

This implementation follows modern Linux best practices:

1. **Unix Domain Sockets** - Fast, secure local communication
2. **Systemd Integration** - Proper service management
3. **Capability-based Security** - Minimal privilege escalation
4. **JSON Support** - Machine-readable output for automation
5. **Signal Handling** - Graceful shutdown and cleanup

## Troubleshooting

### Daemon not running
```bash
# Check if daemon is running
sudo systemctl status clevo-daemon

# Start the daemon
sudo systemctl start clevo-daemon

# Enable auto-start
sudo systemctl enable clevo-daemon
```

### Permission issues
```bash
# Check socket permissions
ls -la /tmp/clevo-daemon.sock

# Restart daemon if needed
sudo systemctl restart clevo-daemon
```

### Build issues
```bash
# Install dependencies
sudo apt-get install build-essential libcap-dev

# Clean and rebuild
make clean
make
```

## Integration with Other Tools

### System monitoring
```bash
# Add to your system monitor
watch -n 2 'clevo-client status'

# Log temperatures
clevo-client --json status >> /var/log/temperatures.log
```

### Scripting
```bash
#!/bin/bash
# Fan control script
TEMP=$(clevo-client --json status | jq -r '.cpu_temperature')
if [ "$TEMP" -gt 80 ]; then
    clevo-client set-fan 100
elif [ "$TEMP" -gt 70 ]; then
    clevo-client set-fan 80
else
    clevo-client set-auto
fi
```

## Security Notes

- The client requires no special privileges
- Socket communication is local-only
- Daemon runs with minimal capabilities (`CAP_SYS_RAWIO`)
- File permissions control access to the socket

## Contributing

The client is designed to be extensible. Common additions might include:

- **D-Bus integration** for desktop environments
- **Web interface** for remote monitoring
- **SNMP support** for enterprise monitoring
- **Logging integration** with systemd journal 