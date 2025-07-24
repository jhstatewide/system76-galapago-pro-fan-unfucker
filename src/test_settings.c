/*
 * Test Settings Tool
 * 
 * This tool helps verify the current daemon settings and debug issues
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/io.h>
#include <errno.h>

#define EC_SC 0x66
#define EC_DATA 0x62
#define IBF 1
#define OBF 0
#define EC_SC_READ_CMD 0x80

#define EC_REG_CPU_TEMP 0x07
#define EC_REG_GPU_TEMP 0xCD
#define EC_REG_FAN_DUTY 0xCE
#define EC_REG_FAN_RPMS_HI 0xD0
#define EC_REG_FAN_RPMS_LO 0xD1

static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    if (i >= 100) {
        printf("wait_ec error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x\n",
                port, data, flag, value);
        return -1;
    }
    return 0;
}

static uint8_t ec_io_read(const uint32_t port) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(EC_SC_READ_CMD, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, OBF, 1);
    uint8_t value = inb(EC_DATA);

    return value;
}

static int calculate_fan_duty(int raw_duty) {
    return (int) ((double) raw_duty / 255.0 * 100.0);
}

static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    if (raw_rpm <= 0) return 0;
    if (raw_rpm < 10) return 0;
    int calculated_rpm = 2156220 / raw_rpm;
    if (calculated_rpm < 0 || calculated_rpm > 10000) return 0;
    return calculated_rpm;
}

static void test_current_readings(void) {
    printf("=== Current EC Readings ===\n");
    
    int cpu_temp = ec_io_read(EC_REG_CPU_TEMP);
    int gpu_temp = ec_io_read(EC_REG_GPU_TEMP);
    int raw_duty = ec_io_read(EC_REG_FAN_DUTY);
    int rpm_hi = ec_io_read(EC_REG_FAN_RPMS_HI);
    int rpm_lo = ec_io_read(EC_REG_FAN_RPMS_LO);
    
    int fan_duty = calculate_fan_duty(raw_duty);
    int fan_rpm = calculate_fan_rpms(rpm_hi, rpm_lo);
    
    printf("CPU Temperature: %d°C (raw: %d)\n", cpu_temp, cpu_temp);
    printf("GPU Temperature: %d°C (raw: %d)\n", gpu_temp, gpu_temp);
    printf("Fan Duty: %d%% (raw: %d)\n", fan_duty, raw_duty);
    printf("Fan RPM: %d (raw: %d, %d)\n", fan_rpm, rpm_hi, rpm_lo);
    printf("Raw RPM combined: %d (0x%04X)\n", (rpm_hi << 8) + rpm_lo, (rpm_hi << 8) + rpm_lo);
}

static void test_rpm_formulas(void) {
    printf("\n=== RPM Formula Tests ===\n");
    
    int rpm_hi = ec_io_read(EC_REG_FAN_RPMS_HI);
    int rpm_lo = ec_io_read(EC_REG_FAN_RPMS_LO);
    int raw_rpm = (rpm_hi << 8) + rpm_lo;
    
    printf("Raw values: HI=%d, LO=%d, Combined=%d\n", rpm_hi, rpm_lo, raw_rpm);
    
    if (raw_rpm > 0) {
        printf("Original formula (2156220/raw): %d RPM\n", 2156220 / raw_rpm);
        printf("Alternative 1 (60000000/raw): %d RPM\n", 60000000 / raw_rpm);
        printf("Alternative 2 (48000000/raw): %d RPM\n", 48000000 / raw_rpm);
        printf("Direct RPM: %d RPM\n", raw_rpm);
    } else {
        printf("Raw RPM is 0, cannot calculate\n");
    }
}

static void test_rate_limiting_simulation(void) {
    printf("\n=== Rate Limiting Simulation ===\n");
    
    int max_duty_change_rate = 30;  // Default
    int current_duty = 70;
    int target_duty = 90;
    int temp_error = 6;
    
    printf("Simulating rate limiting with:\n");
    printf("  max_duty_change_rate: %d%%\n", max_duty_change_rate);
    printf("  current_duty: %d%%\n", current_duty);
    printf("  target_duty: %d%%\n", target_duty);
    printf("  temp_error: %d°C\n", temp_error);
    
    bool emergency_bypass = (temp_error >= 8) || (temp_error >= 5 && target_duty >= 80);
    printf("  emergency_bypass: %s\n", emergency_bypass ? "true" : "false");
    
    int max_duty_change = max_duty_change_rate;
    int new_duty = target_duty;
    
    if (!emergency_bypass) {
        if (new_duty > current_duty + max_duty_change) {
            new_duty = current_duty + max_duty_change;
            printf("  Normal rate limiting applied: %d%% -> %d%%\n", target_duty, new_duty);
        }
    } else {
        int emergency_max_change = max_duty_change * 2;
        if (new_duty > current_duty + emergency_max_change) {
            new_duty = current_duty + emergency_max_change;
            printf("  Emergency rate limiting applied: %d%% -> %d%% (emergency rate: %d)\n", 
                   target_duty, new_duty, emergency_max_change);
        } else {
            printf("  Emergency bypass allowed: %d%% -> %d%%\n", current_duty, new_duty);
        }
    }
    
    printf("  Final duty: %d%%\n", new_duty);
}

int main(int argc, char* argv[]) {
    // Setup I/O privileges
    if (ioperm(EC_DATA, 1, 1) != 0) {
        printf("Failed to get I/O permissions for EC_DATA: %s\n", strerror(errno));
        return 1;
    }
    if (ioperm(EC_SC, 1, 1) != 0) {
        printf("Failed to get I/O permissions for EC_SC: %s\n", strerror(errno));
        return 1;
    }
    
    test_current_readings();
    test_rpm_formulas();
    test_rate_limiting_simulation();
    
    return 0;
} 