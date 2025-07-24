#!/bin/bash

echo "Testing Clevo Daemon Live Stats Mode"
echo "====================================="
echo ""
echo "This will start the daemon in live stats mode with target temperature 60°C"
echo "The display will update every 100ms with real-time fan control statistics"
echo ""
echo "Controls:"
echo "  - Press 'q' to quit"
echo "  - Press 'r' to refresh display"
echo "  - Ctrl+C to force exit"
echo ""
echo "Choose mode:"
echo "  1) Normal live stats (clean display)"
echo "  2) Debug live stats (with debug log area)"
echo ""
read -p "Enter choice (1 or 2): " choice

if [ "$choice" = "2" ]; then
    echo "Starting debug live stats mode..."
    sudo ./bin/clevo-daemon --live-stats --debug --target-temp 60
else
    echo "Starting normal live stats mode..."
    sudo ./bin/clevo-daemon --live-stats --target-temp 60
fi

echo ""
echo "Daemon stopped." 