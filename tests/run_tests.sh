#!/bin/bash

# Simple test runner for clevo-indicator
# Uses standard Debian tools: gcc, bash, make

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test configuration
TEST_DIR="tests"
BUILD_DIR="test_build"
SRC_DIR="src"

echo -e "${YELLOW}=== Clevo Indicator Test Suite ===${NC}"

# Check dependencies
echo "Checking dependencies..."
if ! command -v gcc &> /dev/null; then
    echo -e "${RED}ERROR: gcc not found${NC}"
    exit 1
fi

if ! pkg-config --exists ayatana-appindicator3-0.1; then
    echo -e "${YELLOW}WARNING: ayatana-appindicator3-0.1 not found, some tests may be limited${NC}"
fi

# Create test build directory
mkdir -p "$BUILD_DIR"

# Compile test version
echo "Compiling test version..."

# Compile the simple test
gcc -o "$BUILD_DIR/test_runner" \
    tests/simple_test.c \
    -Wall -std=gnu99 -lm

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Test compilation successful${NC}"
else
    echo -e "${RED}Test compilation failed${NC}"
    exit 1
fi

# Run tests
echo "Running tests..."
if "$BUILD_DIR/test_runner"; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed${NC}"
    exit 1
fi 