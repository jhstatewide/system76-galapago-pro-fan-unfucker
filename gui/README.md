# Clevo Fan Monitor GUI

A compact, transparent Qt6 GUI application for monitoring fan RPM and temperature information from the clevo-daemon.

## Features

- **Real-time monitoring**: Displays CPU temperature, fan duty cycle, and fan RPM
- **Transparent background**: Semi-transparent window that can be kept above other windows
- **Color-coded status**: Temperature and fan health indicators with visual colors
- **Draggable window**: Click and drag to reposition the monitor
- **Always-on-top**: Stays above other applications
- **Context menu**: Right-click for options (transparency, reconnect, quit)
- **Keyboard shortcuts**: Ctrl+Q to quit, R to refresh, T to toggle transparency

## Building

### Prerequisites
- Qt6 development packages
- CMake
- Build tools

```bash
# Install dependencies (if not already installed)
sudo apt install qt6-base-dev cmake build-essential

# Build the application
cd gui
mkdir build
cd build
cmake ..
make
```

### Running
```bash
# From the build directory
./bin/clevo-gui

# Or install and run from system
sudo make install
clevo-gui
```

## Usage

### Basic Operation
1. **Start the daemon**: Ensure `clevo-daemon` is running
2. **Launch the GUI**: Run `./bin/clevo-gui`
3. **Position the window**: Drag to desired location
4. **Monitor status**: Watch real-time updates

### Controls
- **Left-click + drag**: Move window
- **Ctrl + left-click**: Toggle transparency
- **Right-click**: Context menu
- **Ctrl+Q**: Quit application
- **R**: Refresh data
- **T**: Toggle transparency

### Display Information
- **CPU Temperature**: Color-coded by severity
  - Green (<60°C): Normal
  - Cyan (60-70°C): Warm
  - Yellow (70-80°C): High
  - Red (>80°C): Critical
- **Fan Duty Cycle**: Percentage of maximum fan speed
- **Fan RPM**: Actual fan speed with health indicators
- **Mode**: Auto or Manual fan control

## Configuration

The application stores settings in `~/.config/clevo-gui/clevo-gui.conf`:

- Update interval
- Window opacity
- Window position and size
- Display layout preferences
- Color coding settings

## Architecture

### Components
- **ClevoMonitor**: Main window widget
- **GuiSocket**: Daemon communication
- **GuiConfig**: Settings management
- **GuiDisplay**: Drawing utilities

### Communication
- Unix domain socket connection to `/run/clevo-daemon.sock`
- Non-blocking I/O with Qt event loop
- Automatic reconnection on connection loss
- Error handling and status reporting

## Development

### File Structure
```
gui/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
└── src/
    ├── clevo-gui.cpp       # Main application
    ├── clevo-gui.h         # Main header
    ├── gui_display.cpp     # Drawing functions
    ├── gui_display.h       # Display header
    ├── gui_config.cpp      # Configuration
    ├── gui_config.h        # Config header
    ├── gui_socket.cpp      # Socket communication
    └── gui_socket.h        # Socket header
```

### Adding Features
1. **New display layouts**: Add to `GuiDisplay` namespace
2. **Configuration options**: Extend `GuiConfig` class
3. **Socket commands**: Add to `GuiSocket` class
4. **UI elements**: Modify `ClevoMonitor` widget

## Troubleshooting

### Common Issues
- **"No Connection"**: Ensure `clevo-daemon` is running
- **Window not visible**: Check if compositor supports transparency
- **Build errors**: Verify Qt6 development packages are installed

### Debug Mode
Run with debug output:
```bash
QT_LOGGING_RULES="clevo-gui.debug=true" ./bin/clevo-gui
```

## Integration with KDE

The GUI is designed for KDE Plasma integration:
- Native Qt6 theming
- Proper compositor transparency support
- KDE window management compatibility
- Consistent with KDE application behavior

## License

Same license as the main project. 