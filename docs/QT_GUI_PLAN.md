# Qt GUI Utility Plan for KDE Environment

## Overview
A compact, transparent X11 utility for displaying fan RPM and temperature information from the clevo-daemon, designed specifically for KDE Plasma integration.

## Technology Stack
- **Framework**: Qt 6 with QWidget (not QML for lightweight approach)
- **Language**: C++
- **Communication**: Unix domain socket with clevo-daemon
- **Rendering**: QPainter for custom compact display
- **Transparency**: Qt's native transparency support through KDE compositor

## Architecture

### Core Components
1. **Main Application**: Qt application with transparent, frameless window
2. **Socket Communication**: Non-blocking communication with clevo-daemon
3. **Custom Display**: QPainter-based compact information display
4. **Timer System**: Periodic status updates (configurable interval)
5. **User Interaction**: Mouse events for dragging, context menu

### Window Design
- **Size**: ~200x100 pixels (compact but readable)
- **Transparency**: 80-90% opacity with solid text
- **Always-on-top**: `Qt::WindowStaysOnTopHint`
- **Frameless**: `Qt::FramelessWindowHint` for clean look
- **Draggable**: Custom mouse event handling
- **Clickable**: Toggle transparency on click

### Display Layout
```
┌─────────────────────────┐
│ CPU: 65°C  [WARM]      │
│ Fan: 45%   RPM: 2800   │
│ Mode: Auto              │
└─────────────────────────┘
```

### Color Coding
- **Temperature**: 
  - Green (<60°C): Normal
  - Cyan (60-70°C): Warm
  - Yellow (70-80°C): High
  - Red (>80°C): Critical
- **Fan RPM**: 
  - Green: Normal operation
  - Yellow: Low RPM
  - Red: Very low RPM
- **Status**: Color-coded based on temperature

## Implementation Plan

### Phase 1: Basic Structure
1. Create Qt application skeleton
2. Implement socket communication with clevo-daemon
3. Basic status parsing and display
4. Timer-based updates

### Phase 2: Visual Design
1. Custom QPainter drawing for compact display
2. Transparency support implementation
3. Color coding implementation
4. Font and layout optimization

### Phase 3: User Experience
1. Always-on-top functionality
2. Draggable window implementation
3. Context menu and keyboard shortcuts
4. Click-to-toggle transparency

### Phase 4: Polish
1. Smooth animations for value changes
2. Error handling and reconnection logic
3. Configuration options
4. System tray integration (optional)

## File Structure
```
src/
├── clevo-gui.cpp         # Main application
├── clevo-gui.h           # Header file
├── gui_display.cpp       # QPainter drawing functions
├── gui_display.h         # Display header
├── gui_config.cpp        # Configuration handling
├── gui_config.h          # Config header
├── gui_socket.cpp        # Socket communication
└── gui_socket.h          # Socket header
```

## Dependencies
```bash
# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-widgets-dev

# Or for Qt5 (still supported)
sudo apt install qt5-default qtbase5-dev
```

## Build System
```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(clevo-gui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(clevo-gui
    src/clevo-gui.cpp
    src/gui_display.cpp
    src/gui_config.cpp
    src/gui_socket.cpp
)

target_link_libraries(clevo-gui Qt6::Widgets)
```

## Key Qt Features

### Window Management
```cpp
// Transparent, frameless, always-on-top window
setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
setAttribute(Qt::WA_TranslucentBackground);
setWindowOpacity(0.85);
```

### Custom Painting
```cpp
void ClevoMonitor::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background with transparency
    painter.fillRect(rect(), QColor(0, 0, 0, 180));
    
    // Draw status information with color coding
    drawTemperature(painter);
    drawFanInfo(painter);
    drawModeInfo(painter);
}
```

### Socket Communication
```cpp
// Non-blocking socket communication
QSocketNotifier *socketNotifier = new QSocketNotifier(daemonSocket, QSocketNotifier::Read);
connect(socketNotifier, &QSocketNotifier::activated, this, &ClevoMonitor::readDaemonData);
```

### Timer Updates
```cpp
QTimer *updateTimer = new QTimer(this);
connect(updateTimer, &QTimer::timeout, this, &ClevoMonitor::updateStatus);
updateTimer->start(1000); // 1 second updates
```

## Features

### Core Features
- Real-time fan RPM and temperature display
- Color-coded status indicators
- Transparent background with solid text
- Always-on-top window
- Draggable interface
- Click to toggle transparency

### User Interaction
- **Left-click**: Toggle transparency
- **Right-click**: Context menu (settings, quit)
- **Drag**: Move window around screen
- **Keyboard**: Q to quit, R to refresh

### Configuration
- Update interval (0.5s to 5s)
- Window opacity level
- Display size
- Color themes
- Auto-start option

## Integration with KDE
- Automatic theming through Qt's KDE integration
- Proper compositor transparency support
- Native KDE window management
- Consistent with KDE application behavior

## Performance Considerations
- Minimal memory footprint
- Efficient socket communication
- Optimized painting (only redraw on changes)
- Lightweight timer system
- No unnecessary dependencies

## Error Handling
- Graceful daemon disconnection handling
- Auto-reconnection logic
- Fallback display for communication errors
- User-friendly error messages

## Future Enhancements
- System tray integration
- Multiple display layouts
- Historical data graphs
- Fan control integration
- Configuration GUI
- Plugin system for additional sensors 