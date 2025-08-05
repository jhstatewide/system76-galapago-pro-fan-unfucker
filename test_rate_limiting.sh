#!/bin/bash

# Test script for fan rate limiting improvements
# This script helps verify that the new softer rate limiting is working correctly
# and tests the fan stuck recovery mechanisms

echo "=== Fan Rate Limiting and Stall Prevention Test ==="
echo "Testing the new softer rate limiting settings and stall prevention..."
echo ""

# Check if daemon is running
if ! pgrep -x "clevo-daemon" > /dev/null; then
    echo "Starting daemon in test mode..."
    sudo ./bin/clevo-daemon --debug --foreground --interval 1.0 &
    DAEMON_PID=$!
    sleep 3
else
    echo "Daemon is already running"
    DAEMON_PID=$(pgrep -x "clevo-daemon")
fi

echo "Current daemon PID: $DAEMON_PID"
echo ""

# Test 1: Check current settings
echo "=== Test 1: Current Rate Limiting Settings ==="
if command -v ./bin/clevo-client > /dev/null; then
    echo "Max duty change rate:"
    ./bin/clevo-client --get-max-duty-change 2>/dev/null || echo "Command not available"
    echo "Max duty increase rate:"
    ./bin/clevo-client --get-max-duty-increase 2>/dev/null || echo "Command not available"
    echo "Max duty decrease rate:"
    ./bin/clevo-client --get-max-duty-decrease 2>/dev/null || echo "Command not available"
else
    echo "clevo-client not found - using socket interface"
    echo "SET_MAX_DUTY_CHANGE" | nc -U /tmp/clevo-daemon.sock 2>/dev/null || echo "Socket not available"
fi
echo ""

# Test 2: Monitor fan behavior during temperature changes
echo "=== Test 2: Fan Behavior Monitoring ==="
echo "This will monitor fan speed changes for 60 seconds..."
echo "Try running a CPU-intensive task to see how the fan responds."
echo "Watch for stall prevention messages in the logs."
echo ""

# Monitor fan speed for 60 seconds with more detailed output
for i in {1..60}; do
    if [ -f /tmp/clevo-daemon.sock ]; then
        echo "Time: ${i}s - Fan RPM: $(echo "GET_FAN_RPM" | nc -U /tmp/clevo-daemon.sock 2>/dev/null | head -1)"
        echo "Time: ${i}s - Fan Duty: $(echo "GET_FAN_DUTY" | nc -U /tmp/clevo-daemon.sock 2>/dev/null | head -1)"
        echo "Time: ${i}s - CPU Temp: $(echo "GET_CPU_TEMP" | nc -U /tmp/clevo-daemon.sock 2>/dev/null | head -1)"
        echo "---"
    else
        echo "Socket not available for monitoring"
        break
    fi
    sleep 1
done

echo ""
echo "=== Test 3: Stall Prevention Test ==="
echo "Testing stall prevention by monitoring logs for stall prevention messages..."
echo "Run this in another terminal to see the logs:"
echo "sudo journalctl -u clevo-daemon -f"
echo ""

# Test 4: Recovery Test
echo "=== Test 4: Fan Recovery Test ==="
echo "If the fan gets stuck, the daemon should automatically attempt recovery."
echo "Look for these messages in the logs:"
echo "- 'Fan may be stuck: RPM=0'"
echo "- 'Attempting fan recovery'"
echo "- 'Fan responding at X RPM - recovery successful'"
echo ""

echo "=== Test Complete ==="
echo ""
echo "Expected improvements with new settings:"
echo "- Faster response to temperature increases (15% vs 10% per cycle)"
echo "- More gradual decreases to prevent fan stall (20% vs 30% per cycle)"
echo "- Better emergency response (35% vs 25% for critical temps)"
echo "- Stall prevention threshold at 25% minimum duty"
echo "- More frequent health checks (10s vs 30s)"
echo "- Improved recovery with 5-step process"
echo "- Cooldown between recovery attempts (60s)"
echo ""
echo "If the fan still gets stuck, try:"
echo "1. Increase the status interval: --interval 1.0"
echo "2. Increase max duty increase: --max-duty-increase 20"
echo "3. Decrease max duty decrease: --max-duty-decrease 15"
echo "4. Increase stall prevention threshold: --stall-prevention-threshold 30"
echo ""

# Clean up if we started the daemon
if [ ! -z "$DAEMON_PID" ] && [ "$DAEMON_PID" != "$(pgrep -x "clevo-daemon")" ]; then
    echo "Stopping test daemon..."
    kill $DAEMON_PID
fi 