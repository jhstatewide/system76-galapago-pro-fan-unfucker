GalapagoPro System76 fan unfucker ~~Clevo Fan Control Indicator for Ubuntu~~
======================================

Do you fucking hate your stupid fucking fan on your stupid fucking laptop because its fucking loud as fuck and goes crazy when it doesn't need to?

Do you also have a system76 galapago pro?  Or some other clevo laptop that this happens to work on (you better see the original author that this repo was forked from)?

All I did was add a new option to turn the fan to 1% so it can shut the fuck up for a second.  obviously this can let your cpu get hot.  so, use this wisely.  i am sure if you are her eyou hate your fan so much you hope the fucking cpu does melt.

everything below from the original repo, and still applies (installation/compilation,etc)
==========================================

This program is an Ubuntu indicator to control the fan of Clevo laptops, using reversed-engineering port information from ECView.

It shows the CPU temperature on the left and the GPU temperature on the right, and a menu for manual control.

![Clevo Indicator Screen](https://i.imgur.com/iAezQmN.png?1)

## Recent Improvements: Enhanced Fan Rate Limiting

**Problem Solved**: The fan was getting "stuck" at low RPM (100-200) after temperature surges, causing poor cooling performance.

**Solution**: Implemented softer, more gradual rate limiting that accounts for fan physics:

### New Rate Limiting Settings
- **Max duty increase**: 15% per cycle (was 10%) - 50% more responsive to heat
- **Max duty decrease**: 20% per cycle (was 30%) - prevents sudden drops that cause fan stall  
- **Max duty change**: 25% per cycle (was 15%) - overall softer limits
- **Emergency response**: Up to 35% for critical temperatures (was 25%)

### Why This Helps
Real fans have **inertia** - they can't instantly change speed. The previous aggressive rate limiting was causing:
- Fans to get stuck at low RPM after temperature surges
- Poor response to heat due to overly conservative limits
- Mechanical stress from rapid duty cycle changes

### Testing the Improvements
```bash
# Test with the new rate limiting
./test_rate_limiting.sh

# Run a heat test to verify fan behavior
stress-ng --cpu 4 --timeout 60
```

### Fine-tuning (if needed)
If the fan still gets stuck, you can make it even softer:
```bash
# More responsive settings
sudo ./clevo-daemon --max-duty-increase 20 --max-duty-decrease 15 --interval 1.0
```

For command-line, use *-h* to display help, or a number representing percentage of fan duty to control the fan (from 40% to 100%).

## Modern Daemon-Client Architecture

This project now includes a modern daemon-client architecture with the following components:

### Daemon (`clevo-daemon`)
- **Daemon Mode**: Runs in background with automatic temperature-based fan control
- **CLI Mode**: Sets fan to specific duty cycle and exits
- **Target Temperature**: Can set custom target temperature for automatic control

```bash
# Daemon mode with default temperature (65°C)
./bin/clevo-daemon

# Daemon mode with custom temperature (70°C)
./bin/clevo-daemon 70

# CLI mode - set fan to 80%
./bin/clevo-daemon 80
```

### Live Statistics Display

The daemon now includes a high-performance live statistics display mode using ncurses:

```bash
# Enable live stats display (updates every 100ms)
./bin/clevo-daemon --live-stats

# Live stats with custom target temperature
./bin/clevo-daemon --live-stats --target-temp 55

# Live stats with debug output
./bin/clevo-daemon --live-stats --debug
```

**Features:**
- **Real-time Updates**: Refreshes every 100ms with minimal CPU usage
- **Color-coded Display**: Green/Yellow/Red indicators for temperature and fan status
- **Efficient Rendering**: Only updates changed values using ncurses optimization
- **Interactive Controls**: Press 'q' to quit, 'r' to refresh display
- **Comprehensive Stats**: Shows temperatures, fan duty/RPM, PID values, health status
- **Terminal-friendly**: Works in any terminal emulator with proper cleanup

**Display Layout:**
```
┌─ Clevo Fan Control Live Stats ──────────────────────────────┐
│ Target: 60°C  │  Update: 100ms  │  PID: Enabled            │
├───────────────────────────────────────────────────────────────┤
│ Temperature:                                                 │
│   CPU: 72°C  │  GPU: 68°C  │  Max: 72°C                   │
├───────────────────────────────────────────────────────────────┤
│ Fan Control:                                                │
│   Duty: 45%  │  RPM: 2340  │  Health: OK                  │
├───────────────────────────────────────────────────────────────┤
│ PID Status:                                                 │
│   Error: +7°C  │  P: 28.0  │  I: 2.1  │  D: 7.0            │
├───────────────────────────────────────────────────────────────┤
│ Status: Auto Mode  │  Stuck: No  │  Recovery: 0/3          │
└───────────────────────────────────────────────────────────────┘
```

**Color Coding:**
- **Green**: Normal operation
- **Yellow**: Warning conditions (high temp, moderate fan usage)
- **Red**: Critical conditions (very high temp, fan problems)
- **Cyan**: Informational values
- **White**: Headers and labels

### Client (`clevo-client`)
Modern command-line client for monitoring and controlling the daemon:

```bash
# Show current status
./bin/clevo-client status

# Monitor continuously
./bin/clevo-client monitor

# Set fan to 90%
./bin/clevo-client set-fan 90

# Enable automatic control
./bin/clevo-client set-auto

# Set target temperature to 75°C
./bin/clevo-client set-target-temp 75

# JSON output for scripting
./bin/clevo-client --json status
```

### Features
- **Unix Domain Sockets**: Fast, secure local communication
- **Systemd Integration**: Proper service management
- **JSON Support**: Machine-readable output for automation
- **Real-time Monitoring**: Continuous status updates
- **Modern Security**: Capability-based privilege management

## System Installation (Recommended)

For a complete system installation with automatic startup at boot:

```bash
# Run the automated installer
./install.sh
```

This will:
- Install to `/opt/galago-pro-fan-control-daemon`
- Set up systemd service with low CPU priority
- Configure default target temperature of 45°C
- Enable automatic startup at boot
- Set proper capabilities for EC access

### Service Management

After installation:
```bash
# Start the service
sudo systemctl start galago-pro-fan-daemon

# Check status
sudo systemctl status galago-pro-fan-daemon

# View logs
sudo journalctl -u galago-pro-fan-daemon -f

# Stop the service
sudo systemctl stop galago-pro-fan-daemon
```

### Configuration

The daemon runs with these default settings:
- **Target Temperature**: 45°C (configurable)
- **CPU Priority**: Low (Nice=19)
- **Auto-restart**: On failure
- **Capabilities**: SYS_RAWIO for EC access

To modify settings, edit the configuration file:
```bash
sudo nano /opt/galago-pro-fan-control-daemon/etc/default.conf
```

### Uninstallation

To completely remove the system installation:
```bash
sudo /opt/galago-pro-fan-control-daemon/uninstall.sh
```

## Manual Build and Install

For manual installation to `/usr/local`:

```shell
# Install dependencies
sudo apt-get install libappindicator3-dev libgtk-3-dev libncurses5-dev

# Build and install
git clone https://github.com/SkyLandTW/clevo-indicator.git
cd clevo-indicator
make install
```

**Dependencies:**
- `libappindicator3-dev`: For the system tray indicator
- `libgtk-3-dev`: For GTK+ GUI components  
- `libncurses5-dev`: For the live statistics display mode


Notes
-----

The executable has setuid flag on, but must be run by the current desktop user,
because only the desktop user is allowed to display a desktop indicator in
Ubuntu, while a non-root user is not allowed to control Clevo EC by low-level
IO ports. The setuid=root creates a special situation in which this program can
fork itself and run under two users (one for desktop/indicator and the other
for EC control), so you could see two processes in ps, and killing either one
of them would immediately terminate the other.

Be careful not to use any other program accessing the EC by low-level IO
syscalls (inb/outb) at the same time - I don't know what might happen, since
every EC actions require multiple commands to be issued in correct sequence and
there is no kernel-level protection to ensure each action must be completed
before other actions can be performed... The program also attempts to prevent
abortion while issuing commands by catching all termination signals except
SIGKILL - don't kill the indicator by "kill -9" unless absolutely necessary.

