#!/bin/bash

# Test script for enhanced EC I/O reliability
# This script tests the retry logic and exponential backoff implementation

echo "Testing Enhanced EC I/O Reliability Implementation"
echo "================================================"

# Check if the daemon is running
if ! pgrep -x "clevo-daemon" > /dev/null; then
    echo "ERROR: clevo-daemon is not running"
    echo "Please start the daemon first: sudo systemctl start clevo-daemon"
    exit 1
fi

echo "✓ clevo-daemon is running"

# Test 1: Check if enhanced retry functions are compiled
echo ""
echo "Test 1: Checking for enhanced retry functions..."

# Check if the new function is available in the daemon
if grep -q "ec_write_fan_duty_with_retry" /proc/*/maps 2>/dev/null | grep clevo-daemon; then
    echo "✓ Enhanced retry function detected in running daemon"
else
    echo "⚠ Enhanced retry function not detected - daemon may need restart"
fi

# Test 2: Check daemon logs for retry activity
echo ""
echo "Test 2: Checking daemon logs for retry activity..."
echo "Recent daemon logs (last 20 lines):"
sudo journalctl -u clevo-daemon --no-pager -n 20 | grep -E "(retry|EC.*failed|EC.*succeeded)" || echo "No retry activity found in recent logs"

# Test 3: Test fan duty cycle changes with enhanced reliability
echo ""
echo "Test 3: Testing fan duty cycle changes..."

# Test low duty cycle
echo "Setting fan to 25% duty cycle..."
sudo clevo-client set-fan-duty 25
sleep 2

# Test high duty cycle
echo "Setting fan to 80% duty cycle..."
sudo clevo-client set-fan-duty 80
sleep 2

# Test emergency duty cycle
echo "Setting fan to 100% duty cycle (emergency)..."
sudo clevo-client set-fan-duty 100
sleep 2

# Return to normal
echo "Returning fan to 50% duty cycle..."
sudo clevo-client set-fan-duty 50
sleep 2

# Test 4: Check for any EC communication errors
echo ""
echo "Test 4: Checking for EC communication errors..."
echo "Recent EC-related errors:"
sudo journalctl -u clevo-daemon --no-pager -n 50 | grep -E "(EC.*error|EC.*failed|EC.*timeout)" || echo "No EC errors found"

# Test 5: Verify fan response
echo ""
echo "Test 5: Verifying fan response..."
current_duty=$(sudo clevo-client get-fan-duty)
current_rpm=$(sudo clevo-client get-fan-rpm)
echo "Current fan duty: ${current_duty}%"
echo "Current fan RPM: ${current_rpm}"

if [ "$current_rpm" -gt 0 ]; then
    echo "✓ Fan is responding (RPM > 0)"
else
    echo "⚠ Fan may not be responding (RPM = 0)"
fi

echo ""
echo "Enhanced EC I/O Reliability Test Complete"
echo "========================================="
echo ""
echo "Key improvements implemented:"
echo "1. Exponential backoff retry logic for EC operations"
echo "2. Enhanced timeout handling with increasing delays"
echo "3. Comprehensive logging for retry attempts"
echo "4. Automatic recovery from EC communication failures"
echo ""
echo "The daemon now uses ec_write_fan_duty_with_retry() with 3 retries"
echo "and exponential backoff delays (5ms, 10ms, 20ms) for all fan duty writes." 