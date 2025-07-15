#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Mock EC registers for testing
static unsigned char mock_ec_registers[0x100] = {0};

// Mock temperature and fan values
static int mock_cpu_temp = 45;
static int mock_gpu_temp = 50;
static int mock_fan_duty = 60;
static int mock_fan_rpms = 2000;

// Test utilities
void test_init_mock_ec(void) {
    memset(mock_ec_registers, 0, sizeof(mock_ec_registers));
    mock_ec_registers[0x07] = mock_cpu_temp;   // CPU temp
    mock_ec_registers[0xCD] = mock_gpu_temp;   // GPU temp
    mock_ec_registers[0xCE] = (mock_fan_duty * 255) / 100;  // Fan duty
    mock_ec_registers[0xD0] = (2156220 / mock_fan_rpms) >> 8;  // Fan RPM high
    mock_ec_registers[0xD1] = (2156220 / mock_fan_rpms) & 0xFF; // Fan RPM low
}

void test_set_mock_temps(int cpu_temp, int gpu_temp) {
    mock_cpu_temp = cpu_temp;
    mock_gpu_temp = gpu_temp;
    mock_ec_registers[0x07] = cpu_temp;
    mock_ec_registers[0xCD] = gpu_temp;
}

void test_set_mock_fan(int duty, int rpms) {
    mock_fan_duty = duty;
    mock_fan_rpms = rpms;
    mock_ec_registers[0xCE] = (duty * 255) / 100;
    mock_ec_registers[0xD0] = (2156220 / rpms) >> 8;
    mock_ec_registers[0xD1] = (2156220 / rpms) & 0xFF;
}

// Mock EC I/O functions for testing
int mock_ec_io_read(uint32_t port) {
    if (port < sizeof(mock_ec_registers)) {
        return mock_ec_registers[port];
    }
    return 0;
}

int mock_ec_io_do(uint32_t cmd, uint32_t port, uint8_t value) {
    if (port == 0x01 && cmd == 0x99) {
        // Fan duty write
        mock_fan_duty = (value * 100) / 255;
        mock_ec_registers[0xCE] = value;
        return 0;
    }
    return -1;
}

// Test assertion helpers
void test_assert_int_equal(int expected, int actual, const char* test_name) {
    if (expected != actual) {
        printf("FAIL: %s - Expected %d, got %d\n", test_name, expected, actual);
        exit(1);
    }
    printf("PASS: %s\n", test_name);
}

void test_assert_true(int condition, const char* test_name) {
    if (!condition) {
        printf("FAIL: %s - Condition was false\n", test_name);
        exit(1);
    }
    printf("PASS: %s\n", test_name);
}

void test_assert_false(int condition, const char* test_name) {
    if (condition) {
        printf("FAIL: %s - Condition was true\n", test_name);
        exit(1);
    }
    printf("PASS: %s\n", test_name);
}

// Test runner
void run_all_tests(void) {
    printf("Running tests...\n");
    printf("================\n");
    
    // Test calculation functions
    test_calculate_fan_duty();
    test_calculate_fan_rpms();
    test_auto_duty_adjust();
    test_string_formatting();
    
    printf("================\n");
    printf("All tests passed!\n");
}

// Individual test functions
void test_calculate_fan_duty(void) {
    test_assert_int_equal(0, calculate_fan_duty(0), "calculate_fan_duty(0)");
    test_assert_int_equal(50, calculate_fan_duty(127), "calculate_fan_duty(127)");
    test_assert_int_equal(100, calculate_fan_duty(255), "calculate_fan_duty(255)");
}

void test_calculate_fan_rpms(void) {
    test_assert_int_equal(0, calculate_fan_rpms(0, 0), "calculate_fan_rpms(0,0)");
    test_assert_int_equal(2000, calculate_fan_rpms(0x43, 0x1A), "calculate_fan_rpms(0x43,0x1A)");
}

void test_auto_duty_adjust(void) {
    // Test low temperature
    test_set_mock_temps(40, 45);
    int duty = ec_auto_duty_adjust();
    test_assert_true(duty <= 60, "auto_duty_adjust low temp");
    
    // Test high temperature
    test_set_mock_temps(70, 75);
    duty = ec_auto_duty_adjust();
    test_assert_true(duty >= 70, "auto_duty_adjust high temp");
}

void test_string_formatting(void) {
    char buffer[256];
    get_time_string(buffer, sizeof(buffer), "%Y-%m-%d");
    test_assert_true(strlen(buffer) > 0, "get_time_string produces output");
    test_assert_true(strlen(buffer) < sizeof(buffer), "get_time_string fits in buffer");
} 