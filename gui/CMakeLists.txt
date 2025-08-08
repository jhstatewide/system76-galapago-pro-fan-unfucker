cmake_minimum_required(VERSION 3.16)
project(clevo-gui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Set up source files
set(SOURCES
    src/clevo-gui.cpp
    src/gui_display.cpp
    src/gui_config.cpp
    src/gui_socket.cpp
    src/clevo-settings.cpp
)

# Set up DBus source files (without socket dependency)
set(SOURCES_DBUS
    src/clevo-gui-dbus.cpp
    src/gui_display.cpp
    src/gui_config.cpp
    src/clevo-settings.cpp
)

set(HEADERS
    src/clevo-gui.h
    src/gui_display.h
    src/gui_config.h
    src/gui_socket.h
    src/clevo-settings.h
)

set(HEADERS_DBUS
    src/clevo-gui.h
    src/gui_display.h
    src/gui_config.h
    src/clevo-settings.h
)

# Create executable
add_executable(clevo-gui ${SOURCES} ${HEADERS})

# Enable Qt MOC
set_target_properties(clevo-gui PROPERTIES
    AUTOMOC ON
)

# Link Qt6 libraries
target_link_libraries(clevo-gui Qt6::Widgets)

# Create DBus executable
add_executable(clevo-gui-dbus ${SOURCES_DBUS} ${HEADERS_DBUS})

# Enable Qt MOC for DBus version
set_target_properties(clevo-gui-dbus PROPERTIES
    AUTOMOC ON
)

# Link Qt6 libraries for DBus version
target_link_libraries(clevo-gui-dbus Qt6::Widgets Qt6::DBus)

# Set compiler flags
target_compile_options(clevo-gui PRIVATE -Wall -Wextra)

# Set output directory
set_target_properties(clevo-gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install target
install(TARGETS clevo-gui clevo-gui-dbus
    RUNTIME DESTINATION bin
) 