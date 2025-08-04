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
)

set(HEADERS
    src/clevo-gui.h
    src/gui_display.h
    src/gui_config.h
    src/gui_socket.h
)

# Create executable
add_executable(clevo-gui ${SOURCES} ${HEADERS})

# Enable Qt MOC
set_target_properties(clevo-gui PROPERTIES
    AUTOMOC ON
)

# Link Qt6 libraries
target_link_libraries(clevo-gui Qt6::Widgets)

# Set compiler flags
target_compile_options(clevo-gui PRIVATE -Wall -Wextra)

# Set output directory
set_target_properties(clevo-gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install target
install(TARGETS clevo-gui
    RUNTIME DESTINATION bin
) 