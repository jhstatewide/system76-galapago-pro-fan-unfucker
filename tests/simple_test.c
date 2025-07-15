#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Test configuration
#define TEST_MODE 1

// Include the core functions we want to test
// We'll define these inline to avoid complex dependencies

// Mock EC registers for testing
static unsigned char mock_ec_registers[0x100] = {0};

// Mock temperature and fan values
static int mock_cpu_temp = 45;
static int mock_gpu_temp = 50;
static int mock_fan_duty = 60;
static int mock_fan_rpms = 2000;

// Define MAX macro
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

// Core functions to test
static int calculate_fan_duty(int raw_duty) {
    return (int) ((double) raw_duty / 255.0 * 100.0);
}

static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    return raw_rpm > 0 ? (2156220 / raw_rpm) : 0;
}

static void get_time_string(char* buffer, size_t max, const char* format) {
    time_t timer;
    struct tm tm_info;
    time(&timer);
    localtime_r(&timer, &tm_info);
    strftime(buffer, max, format, &tm_info);
}

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

// Individual test functions
void test_calculate_fan_duty(void) {
    printf("Testing calculate_fan_duty...\n");
    test_assert_int_equal(0, calculate_fan_duty(0), "calculate_fan_duty(0)");
    test_assert_int_equal(49, calculate_fan_duty(127), "calculate_fan_duty(127)"); // 127/255*100 = 49.8 -> 49
    test_assert_int_equal(100, calculate_fan_duty(255), "calculate_fan_duty(255)");
    test_assert_int_equal(25, calculate_fan_duty(64), "calculate_fan_duty(64)"); // 64/255*100 = 25.1 -> 25
    test_assert_int_equal(74, calculate_fan_duty(191), "calculate_fan_duty(191)"); // 191/255*100 = 74.9 -> 74
}

void test_calculate_fan_rpms(void) {
    printf("Testing calculate_fan_rpms...\n");
    test_assert_int_equal(0, calculate_fan_rpms(0, 0), "calculate_fan_rpms(0,0)");
    test_assert_int_equal(125, calculate_fan_rpms(0x43, 0x1A), "calculate_fan_rpms(0x43,0x1A)"); // (0x43<<8)+0x1A=17178, 2156220/17178=125
    test_assert_int_equal(251, calculate_fan_rpms(0x21, 0x8D), "calculate_fan_rpms(0x21,0x8D)"); // (0x21<<8)+0x8D=8589, 2156220/8589=251
}

void test_string_formatting(void) {
    printf("Testing string formatting...\n");
    char buffer[256];
    get_time_string(buffer, sizeof(buffer), "%Y-%m-%d");
    test_assert_true(strlen(buffer) > 0, "get_time_string produces output");
    test_assert_true(strlen(buffer) < sizeof(buffer), "get_time_string fits in buffer");
    
    // Test with different format
    get_time_string(buffer, sizeof(buffer), "%H:%M:%S");
    test_assert_true(strlen(buffer) > 0, "get_time_string with time format");
}

void test_mock_ec_functions(void) {
    printf("Testing mock EC functions...\n");
    test_init_mock_ec();
    
    test_assert_int_equal(45, mock_ec_registers[0x07], "mock CPU temp");
    test_assert_int_equal(50, mock_ec_registers[0xCD], "mock GPU temp");
    test_assert_int_equal((60 * 255) / 100, mock_ec_registers[0xCE], "mock fan duty");
    
    test_set_mock_temps(70, 75);
    test_assert_int_equal(70, mock_ec_registers[0x07], "mock CPU temp after set");
    test_assert_int_equal(75, mock_ec_registers[0xCD], "mock GPU temp after set");
    
    test_set_mock_fan(80, 3000);
    test_assert_int_equal((80 * 255) / 100, mock_ec_registers[0xCE], "mock fan duty after set");
}

void test_edge_cases(void) {
    printf("Testing edge cases...\n");
    
    // Test boundary values for fan duty calculation
    test_assert_int_equal(0, calculate_fan_duty(-1), "calculate_fan_duty negative");
    test_assert_int_equal(0, calculate_fan_duty(0), "calculate_fan_duty zero");
    test_assert_int_equal(100, calculate_fan_duty(255), "calculate_fan_duty max");
    test_assert_int_equal(100, calculate_fan_duty(256), "calculate_fan_duty overflow");
    
    // Test boundary values for RPM calculation
    test_assert_int_equal(0, calculate_fan_rpms(0, 0), "calculate_fan_rpms zero");
    test_assert_int_equal(0, calculate_fan_rpms(-1, 0), "calculate_fan_rpms negative");
}

// Test runner
void run_all_tests(void) {
    printf("Running Clevo Indicator Tests...\n");
    printf("================================\n");
    
    test_calculate_fan_duty();
    test_calculate_fan_rpms();
    test_string_formatting();
    test_mock_ec_functions();
    test_edge_cases();
    
    printf("================================\n");
    printf("All tests passed!\n");
}

int main(void) {
    run_all_tests();
    return 0;
} 