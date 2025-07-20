#!/bin/bash

# Galago Pro Fan Control Daemon Installation Script
# This script installs the fan control daemon to /opt/galago-pro-fan-control-daemon
# and configures it to start at boot with low CPU priority and 45°C target temperature

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
INSTALL_DIR="/opt/galago-pro-fan-control-daemon"
SERVICE_NAME="galago-pro-fan-daemon"
DEFAULT_TEMP=45

echo -e "${BLUE}Galago Pro Fan Control Daemon Installer${NC}"
echo "=============================================="
echo

# Check if running as root
if [[ $EUID -eq 0 ]]; then
   echo -e "${RED}Error: This script should not be run as root${NC}"
   echo "The script will use sudo when needed for specific operations."
   exit 1
fi

# Check dependencies
echo -e "${YELLOW}Checking dependencies...${NC}"
if ! command -v gcc &> /dev/null; then
    echo -e "${RED}Error: gcc is not installed${NC}"
    echo "Please install build-essential: sudo apt install build-essential"
    exit 1
fi

if ! command -v pkg-config &> /dev/null; then
    echo -e "${RED}Error: pkg-config is not installed${NC}"
    echo "Please install pkg-config: sudo apt install pkg-config"
    exit 1
fi

# Check for libcap
if ! pkg-config --exists libcap 2>/dev/null && [ ! -f /usr/lib/x86_64-linux-gnu/libcap.so ]; then
    echo -e "${YELLOW}Warning: libcap not found. Installing...${NC}"
    sudo apt update
    sudo apt install -y libcap-dev
fi

echo -e "${GREEN}Dependencies OK${NC}"
echo

# Build the project
echo -e "${YELLOW}Building project...${NC}"
make clean
make all

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful${NC}"
echo

# Install to /opt
echo -e "${YELLOW}Installing to ${INSTALL_DIR}...${NC}"
sudo make install-opt

if [ $? -ne 0 ]; then
    echo -e "${RED}Installation failed!${NC}"
    exit 1
fi

echo -e "${GREEN}Installation successful${NC}"
echo

# Set capabilities
echo -e "${YELLOW}Setting capabilities for EC access...${NC}"
sudo setcap cap_sys_rawio+ep ${INSTALL_DIR}/bin/clevo-daemon
sudo setcap cap_sys_rawio+ep ${INSTALL_DIR}/bin/clevo-indicator

echo -e "${GREEN}Capabilities set${NC}"
echo

# Install systemd service
echo -e "${YELLOW}Installing systemd service...${NC}"
sudo install -m 644 ${INSTALL_DIR}/systemd/clevo-daemon.service /etc/systemd/system/${SERVICE_NAME}.service

# Reload systemd
sudo systemctl daemon-reload

echo -e "${GREEN}Systemd service installed${NC}"
echo

# Enable service
echo -e "${YELLOW}Enabling service to start at boot...${NC}"
sudo systemctl enable ${SERVICE_NAME}.service

echo -e "${GREEN}Service enabled for boot startup${NC}"
echo

# Create configuration directory and default config
echo -e "${YELLOW}Creating configuration...${NC}"
sudo mkdir -p ${INSTALL_DIR}/etc
sudo tee ${INSTALL_DIR}/etc/default.conf > /dev/null <<EOF
# Galago Pro Fan Control Daemon Configuration
# Default target temperature (can be overridden via command line)
DEFAULT_TARGET_TEMP=${DEFAULT_TEMP}

# PID Controller settings
PID_KP=2.0
PID_KI=0.1
PID_KD=0.5

# Adaptive PID settings
ADAPTIVE_PID_ENABLED=1
ADAPTIVE_TUNING_INTERVAL=30
ADAPTIVE_TARGET_PERFORMANCE=0.8

# Logging
LOG_LEVEL=LOG_INFO
STATUS_INTERVAL=2.0
EOF

echo -e "${GREEN}Configuration created${NC}"
echo

# Create uninstall script
echo -e "${YELLOW}Creating uninstall script...${NC}"
sudo tee ${INSTALL_DIR}/uninstall.sh > /dev/null <<'EOF'
#!/bin/bash
# Uninstall script for Galago Pro Fan Control Daemon

set -e

INSTALL_DIR="/opt/galago-pro-fan-control-daemon"
SERVICE_NAME="galago-pro-fan-daemon"

echo "Uninstalling Galago Pro Fan Control Daemon..."

# Stop and disable service
sudo systemctl stop ${SERVICE_NAME}.service 2>/dev/null || true
sudo systemctl disable ${SERVICE_NAME}.service 2>/dev/null || true

# Remove systemd service file
sudo rm -f /etc/systemd/system/${SERVICE_NAME}.service

# Reload systemd
sudo systemctl daemon-reload

# Remove installation directory
sudo rm -rf ${INSTALL_DIR}

echo "Uninstallation complete!"
EOF

sudo chmod +x ${INSTALL_DIR}/uninstall.sh

echo -e "${GREEN}Uninstall script created${NC}"
echo

# Create usage documentation
echo -e "${YELLOW}Creating usage documentation...${NC}"
sudo tee ${INSTALL_DIR}/docs/USAGE.md > /dev/null <<EOF
# Galago Pro Fan Control Daemon Usage

## Service Management

Start the service:
\`\`\`bash
sudo systemctl start galago-pro-fan-daemon
\`\`\`

Stop the service:
\`\`\`bash
sudo systemctl stop galago-pro-fan-daemon
\`\`\`

Check status:
\`\`\`bash
sudo systemctl status galago-pro-fan-daemon
\`\`\`

View logs:
\`\`\`bash
sudo journalctl -u galago-pro-fan-daemon -f
\`\`\`

## Configuration

The daemon is configured to run with:
- Target temperature: 45°C (default)
- Low CPU priority (Nice=19)
- Automatic restart on failure
- SYS_RAWIO capabilities for EC access

## Manual Control

To manually control the fan, use the client:
\`\`\`bash
${INSTALL_DIR}/bin/clevo-client
\`\`\`

## Uninstallation

To uninstall:
\`\`\`bash
sudo ${INSTALL_DIR}/uninstall.sh
\`\`\`

## Troubleshooting

1. Check if the service is running:
   \`\`\`bash
   sudo systemctl status galago-pro-fan-daemon
   \`\`\`

2. Check logs for errors:
   \`\`\`bash
   sudo journalctl -u galago-pro-fan-daemon -n 50
   \`\`\`

3. Verify EC access:
   \`\`\`bash
   ls -la /sys/kernel/debug/ec/ec0/io
   \`\`\`

4. Check capabilities:
   \`\`\`bash
   getcap ${INSTALL_DIR}/bin/clevo-daemon
   \`\`\`
EOF

echo -e "${GREEN}Documentation created${NC}"
echo

# Final status
echo -e "${GREEN}Installation completed successfully!${NC}"
echo
echo -e "${BLUE}Summary:${NC}"
echo "  - Installed to: ${INSTALL_DIR}"
echo "  - Service name: ${SERVICE_NAME}"
echo "  - Default temperature: ${DEFAULT_TEMP}°C"
echo "  - CPU priority: Low (Nice=19)"
echo "  - Auto-start: Enabled"
echo
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Start the service: sudo systemctl start ${SERVICE_NAME}"
echo "  2. Check status: sudo systemctl status ${SERVICE_NAME}"
echo "  3. View logs: sudo journalctl -u ${SERVICE_NAME} -f"
echo
echo -e "${BLUE}Documentation:${NC}"
echo "  - Usage guide: ${INSTALL_DIR}/docs/USAGE.md"
echo "  - Uninstall: sudo ${INSTALL_DIR}/uninstall.sh"
echo
echo -e "${GREEN}The fan control daemon will now start automatically at boot!${NC}" 