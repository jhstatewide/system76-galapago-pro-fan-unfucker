# Clevo Fan Control Daemon - Modular Version

This is a modularized version of the Clevo fan control daemon, designed following Unix and C best practices. The original 2164-line monolithic file has been broken down into focused, maintainable modules.

## Architecture Overview

### Module Structure

```
src/
├── main_new.c                    # Entry point and orchestration
├── utils.h/c                     # Common utilities
├── logging.h/c                   # Centralized logging system
├── config.h/c                    # Configuration management
├── ec_interface.h/c              # Hardware interface (EC)
├── temperature_monitor.h/c       # Temperature reading and validation
├── pid_controller.h/c            # PID control algorithm
├── fan_health.h/c               # Fan monitoring and recovery
├── daemon.h/c                   # Daemon functionality
├── live_stats.h/c               # Real-time display (ncurses)
├── clevo-daemon-socket.h/c      # Socket communication (existing)
└── privilege_manager.h/c         # Privilege management (existing)
```

### Module Responsibilities

1. **utils.h/c** - Common utilities (time functions, process checking, signal handling)
2. **logging.h/c** - Centralized logging with levels and quiet mode support
3. **config.h/c** - Configuration management with validation and command-line parsing
4. **ec_interface.h/c** - Hardware communication with Embedded Controller
5. **temperature_monitor.h/c** - Temperature reading from multiple sources with validation
6. **pid_controller.h/c** - PID control algorithm with adaptive tuning
7. **fan_health.h/c** - Fan RPM monitoring and recovery procedures
8. **daemon.h/c** - Daemonization, signal handling, and shared memory
9. **live_stats.h/c** - Real-time ncurses-based statistics display

## Benefits of Modularization

### 1. Maintainability
- Each module has a single responsibility
- Smaller, focused files are easier to understand and modify
- Clear interfaces between modules

### 2. Testability
- Individual modules can be unit tested
- Dependencies are explicit and manageable
- Mock interfaces can be created for testing

### 3. Reusability
- Modules can be reused in other projects
- Clean APIs make integration easier
- Separation of concerns allows selective use

### 4. Debugging
- Issues can be isolated to specific modules
- Clear error boundaries
- Easier to trace problems

### 5. Team Development
- Multiple developers can work on different modules
- Reduced merge conflicts
- Clear ownership boundaries

## Building the Modular Version

### Prerequisites
```bash
# Install required packages
sudo apt-get install build-essential libncurses5-dev
```

### Build Commands
```bash
# Build the modular version
make -f Makefile.modular

# Build with debug symbols
make -f Makefile.modular debug

# Build optimized release version
make -f Makefile.modular release

# Clean build artifacts
make -f Makefile.modular clean
```

### Installation
```bash
# Install the modular daemon
make -f Makefile.modular install

# Uninstall
make -f Makefile.modular uninstall
```

## Usage

The modular version maintains the same command-line interface as the original:

```bash
# Daemon mode with default settings
sudo ./bin/clevo-daemon-modular

# Daemon mode with custom target temperature
sudo ./bin/clevo-daemon-modular --target-temp 55

# Debug mode with live stats
sudo ./bin/clevo-daemon-modular --debug --live-stats

# CLI mode for testing fan
sudo ./bin/clevo-daemon-modular 50

# Show help
./bin/clevo-daemon-modular --help
```

## Module Interfaces

### Configuration Module
```c
// Initialize configuration
clevo_config_t* config = config_init();

// Parse command line arguments
config_parse_args(config, argc, argv);

// Access configuration values
int target_temp = config->target_temperature;
double interval = config->status_interval;
```

### Logging Module
```c
// Initialize logging
logging_init("program-name", LOG_INFO, false);

// Log messages
logging_info("System started");
logging_debug("Debug information");
logging_warning("Warning message");
logging_error("Error occurred");
```

### EC Interface Module
```c
// Initialize hardware interface
ec_init();

// Read hardware values
int temp = ec_query_cpu_temp();
int duty = ec_query_fan_duty();
int rpm = ec_query_fan_rpms();

// Control fan
ec_write_fan_duty(50);
```

### Temperature Monitor Module
```c
// Initialize temperature monitor
temperature_monitor_t* monitor = temperature_monitor_init(10, true);

// Get CPU temperature
int temp = temperature_monitor_get_cpu_temp(monitor);

// Check if temperature is stuck
bool stuck = temperature_monitor_is_stuck(monitor);
```

### PID Controller Module
```c
// Initialize PID controller
pid_controller_t* pid = pid_controller_init(4.0, 0.3, 1.0, 15, 10, 30, 15);

// Calculate fan duty
int duty = pid_controller_calculate_duty(pid, current_temp, target_temp, 
                                       current_duty, current_rpm);
```

## Migration from Original

The modular version is designed to be a drop-in replacement for the original daemon. Key differences:

1. **Cleaner Architecture** - Well-defined module boundaries
2. **Better Error Handling** - Consistent error codes across modules
3. **Improved Logging** - Centralized logging with levels
4. **Enhanced Configuration** - Structured configuration management
5. **Modular Testing** - Individual modules can be tested

## Development Guidelines

### Adding New Features
1. Identify which module the feature belongs to
2. Add the feature to the appropriate module
3. Update the module's interface if needed
4. Update main_new.c to use the new feature
5. Add tests for the new functionality

### Module Dependencies
- Keep dependencies minimal and explicit
- Use opaque pointers to hide implementation details
- Provide clear initialization and cleanup functions
- Document all public interfaces

### Error Handling
- Use consistent return codes across modules
- Log errors at the appropriate level
- Provide meaningful error messages
- Handle resource cleanup on errors

## Testing

### Unit Testing
Each module can be tested independently:

```c
// Example: Testing temperature monitor
#include "temperature_monitor.h"

void test_temperature_monitor() {
    temperature_monitor_t* monitor = temperature_monitor_init(10, true);
    
    // Test temperature validation
    bool valid = temperature_monitor_validate_reading(monitor, 50, 45, "CPU");
    assert(valid == true);
    
    temperature_monitor_cleanup(monitor);
}
```

### Integration Testing
Test the complete system:

```bash
# Test with live stats
sudo ./bin/clevo-daemon-modular --live-stats --debug

# Test fan control
sudo ./bin/clevo-daemon-modular 50

# Test daemon mode
sudo ./bin/clevo-daemon-modular --target-temp 60
```

## Performance Considerations

The modular version maintains the same performance characteristics as the original:

- **Memory Usage** - Similar to original, with slight overhead for module structures
- **CPU Usage** - Identical to original, no performance penalty
- **Response Time** - Same real-time characteristics
- **Resource Usage** - Efficient module initialization and cleanup

## Future Enhancements

The modular architecture enables several future improvements:

1. **Plugin System** - Additional temperature sensors or control algorithms
2. **Configuration Files** - JSON/XML configuration support
3. **Web Interface** - HTTP API for remote monitoring
4. **Database Logging** - Persistent logging and statistics
5. **Machine Learning** - Adaptive control algorithms
6. **Multi-Fan Support** - Support for multiple fans

## Contributing

When contributing to the modular version:

1. Follow the existing module structure
2. Add appropriate error handling
3. Update documentation
4. Add tests for new functionality
5. Maintain backward compatibility
6. Follow the established coding style

## License

This modular version maintains the same license as the original project. 