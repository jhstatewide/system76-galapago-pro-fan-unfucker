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


Build and Install
-----------------

```shell
sudo apt-get install libappindicator3-dev libgtk-3-dev
git clone https://github.com/SkyLandTW/clevo-indicator.git
cd clevo-indicator
make install
```


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

