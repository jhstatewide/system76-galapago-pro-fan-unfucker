# DBus Interface for Clevo Fan Control Daemon

This document describes the new DBus interface that has been added to the clevo-daemon, providing intelligent status broadcasting that only sends signals when clients are listening.

## Overview

The DBus interface provides:
- **Intelligent broadcasting**: Status updates are only sent when clients are listening
- **Concurrent client support**: Multiple clients can connect simultaneously
- **System integration**: Better integration with desktop environments
- **Backward compatibility**: Socket interface is still available

## Features

### Smart Status Broadcasting
- The daemon checks for active listeners before sending status signals
- No CPU waste when no clients are connected
- Automatic subscription management

### Supported Methods
- `GetStatus()` - Get current fan control status
- `SetFanDuty(int duty)` - Set fan duty cycle (1-100%)
- `SetAutoMode(boolean enabled)` - Enable/disable automatic fan control
- `SubscribeStatus()` - Subscribe to status updates
- `UnsubscribeStatus()` - Unsubscribe from status updates
- `SetMaxDutyChange(int rate)` - Set maximum duty change rate
- `GetMaxDutyChange()` - Get current max duty change rate

### Signals
- `StatusChanged(int cpu_temp, int fan_duty, int fan_rpm, boolean auto_mode)` - Broadcast when status changes

## Building

The DBus interface requires the `libdbus-1-dev` package:

```bash
# Install dependencies
sudo apt-get install libdbus-1-dev

# Build the daemon with DBus support
make clean
make all
```

This will build:
- `bin/clevo-daemon` - Daemon with both socket and DBus interfaces
- `bin/clevo-client` - Original socket-based client
- `bin/clevo-dbus-client` - Example DBus client

## Usage

### Starting the Daemon
The daemon automatically starts both socket and DBus interfaces:

```bash
sudo ./bin/clevo-daemon
```

### Using the DBus Client

The example DBus client demonstrates how to interact with the daemon:

```bash
# Get current status
./bin/clevo-dbus-client status

# Monitor status updates in real-time
./bin/clevo-dbus-client monitor

# Set fan duty to 80%
./bin/clevo-dbus-client set-fan 80

# Enable auto mode
./bin/clevo-dbus-client set-auto 1

# Disable auto mode
./bin/clevo-dbus-client set-auto 0

# Subscribe to status updates
./bin/clevo-dbus-client subscribe

# Unsubscribe from status updates
./bin/clevo-dbus-client unsubscribe
```

### Programmatic DBus Access

You can also interact with the daemon using standard DBus tools:

```bash
# Get status using dbus-send
dbus-send --session --dest=org.freedesktop.ClevoDaemon \
    --type=method_call /org/freedesktop/ClevoDaemon \
    org.freedesktop.ClevoDaemon.GetStatus

# Set fan duty to 70%
dbus-send --session --dest=org.freedesktop.ClevoDaemon \
    --type=method_call /org/freedesktop/ClevoDaemon \
    org.freedesktop.ClevoDaemon.SetFanDuty int32:70

# Subscribe to status updates
dbus-send --session --dest=org.freedesktop.ClevoDaemon \
    --type=method_call /org/freedesktop/ClevoDaemon \
    org.freedesktop.ClevoDaemon.SubscribeStatus
```

### Monitoring Signals

To monitor status signals:

```bash
# Monitor all signals from the daemon
dbus-monitor --session "interface='org.freedesktop.ClevoDaemon'"
```

## Performance Benefits

### When No Clients Are Connected
- **Socket approach**: Minimal CPU usage (just accept() with timeout)
- **DBus approach**: No signal creation or transmission
- **Result**: Both approaches are efficient

### When Clients Are Connected
- **Socket approach**: Sequential client handling, clients wait
- **DBus approach**: Concurrent client handling, immediate responses
- **Result**: DBus provides better multi-client performance

### Data Volume
- Status updates: ~100 bytes every 2 seconds
- Live stats: ~100 bytes every 100ms
- DBus can easily handle this volume
- Multiple clients multiply the volume but remain manageable

## Implementation Details

### Smart Broadcasting
The daemon uses a subscription counter to track active listeners:

```c
static int status_listeners_count = 0;

int broadcast_status_update(int cpu_temp, int fan_duty, int fan_rpm, int auto_mode) {
    if (!dbus_conn || !has_status_listeners()) {
        // No clients listening, don't waste CPU on signal creation
        return 0;
    }
    
    return send_signal("StatusChanged", cpu_temp, fan_duty, fan_rpm, auto_mode);
}
```

### Client Subscription
Clients must explicitly subscribe to receive status updates:

```c
// Subscribe to status updates
call_method("SubscribeStatus", 0);

// Unsubscribe when done
call_method("UnsubscribeStatus", 0);
```

### Error Handling
The DBus interface provides detailed error messages:

- `org.freedesktop.ClevoDaemon.Error.InvalidValue` - Invalid parameter values
- `org.freedesktop.ClevoDaemon.Error.InvalidArgs` - Invalid method arguments
- `org.freedesktop.ClevoDaemon.Error.UnknownMethod` - Unknown method

## Migration Strategy

### Phase 1: Hybrid Approach (Current)
- Keep socket interface for backward compatibility
- Add DBus interface alongside socket
- Both interfaces work simultaneously

### Phase 2: Client Migration
- Update existing clients to use DBus
- Socket interface remains available
- Test and validate DBus functionality

### Phase 3: Socket Deprecation
- Mark socket interface as deprecated
- Encourage migration to DBus
- Maintain socket for legacy support

### Phase 4: Socket Removal
- Remove socket interface completely
- DBus becomes the primary interface

## Troubleshooting

### DBus Connection Issues
```bash
# Check if DBus is running
dbus-launch --version

# Check session bus
dbus-send --session --dest=org.freedesktop.DBus \
    --type=method_call /org/freedesktop/DBus \
    org.freedesktop.DBus.ListNames
```

### Permission Issues
The daemon needs appropriate permissions to access the EC registers:

```bash
# Set capabilities
sudo setcap cap_sys_rawio+ep bin/clevo-daemon

# Or run with sudo
sudo ./bin/clevo-daemon
```

### Debugging
Enable debug logging in the daemon:

```bash
sudo ./bin/clevo-daemon --debug
```

Check system logs for DBus-related messages:

```bash
journalctl -f | grep clevo-daemon
```

## Future Enhancements

### Planned Features
- **Signal filtering**: Allow clients to subscribe to specific status changes
- **Rate limiting**: Configurable update frequency per client
- **Authentication**: DBus-based authentication and authorization
- **Configuration**: DBus methods for runtime configuration

### Integration Opportunities
- **Desktop notifications**: Integrate with desktop notification systems
- **System monitoring**: Integration with system monitoring tools
- **Power management**: Integration with power management systems
- **Logging**: Integration with system logging frameworks 