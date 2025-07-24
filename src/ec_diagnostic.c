/*
 * EC Diagnostic Tool
 * 
 * This tool helps debug EC reading issues by:
 * 1. Dumping raw register values
 * 2. Testing different RPM calculation formulas
 * 3. Identifying correct register addresses
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

#define EC_REG_SIZE 0x100
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

static int calculate_fan_rpms_original(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    if (raw_rpm <= 0) return 0;
    if (raw_rpm < 10) return 0;
    int calculated_rpm = 2156220 / raw_rpm;
    if (calculated_rpm < 0 || calculated_rpm > 10000) return 0;
    return calculated_rpm;
}

static int calculate_fan_rpms_alternative1(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    if (raw_rpm <= 0) return 0;
    if (raw_rpm < 10) return 0;
    // Alternative formula: 60000000 / raw_rpm (common for some laptops)
    int calculated_rpm = 60000000 / raw_rpm;
    if (calculated_rpm < 0 || calculated_rpm > 10000) return 0;
    return calculated_rpm;
}

static int calculate_fan_rpms_alternative2(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    if (raw_rpm <= 0) return 0;
    if (raw_rpm < 10) return 0;
    // Alternative formula: 48000000 / raw_rpm (another common formula)
    int calculated_rpm = 48000000 / raw_rpm;
    if (calculated_rpm < 0 || calculated_rpm > 10000) return 0;
    return calculated_rpm;
}

static int calculate_fan_rpms_direct(int raw_rpm_high, int raw_rpm_low) {
    // Some laptops use direct RPM values
    int rpm = (raw_rpm_high << 8) + raw_rpm_low;
    if (rpm < 0 || rpm > 10000) return 0;
    return rpm;
}

static void dump_ec_registers(void) {
    printf("=== EC Register Dump ===\n");
    
    // Dump all registers from 0x00 to 0xFF
    for (int i = 0; i < 256; i++) {
        uint8_t value = ec_io_read(i);
        if (i % 16 == 0) {
            printf("\n0x%02X: ", i);
        }
        printf("%02X ", value);
    }
    printf("\n\n");
    
    // Focus on specific registers
    printf("=== Key Registers ===\n");
    printf("CPU Temp (0x%02X): %d\n", EC_REG_CPU_TEMP, ec_io_read(EC_REG_CPU_TEMP));
    printf("GPU Temp (0x%02X): %d\n", EC_REG_GPU_TEMP, ec_io_read(EC_REG_GPU_TEMP));
    printf("Fan Duty (0x%02X): %d\n", EC_REG_FAN_DUTY, ec_io_read(EC_REG_FAN_DUTY));
    printf("Fan RPM High (0x%02X): %d\n", EC_REG_FAN_RPMS_HI, ec_io_read(EC_REG_FAN_RPMS_HI));
    printf("Fan RPM Low (0x%02X): %d\n", EC_REG_FAN_RPMS_LO, ec_io_read(EC_REG_FAN_RPMS_LO));
    
    // Test different RPM calculation formulas
    int rpm_hi = ec_io_read(EC_REG_FAN_RPMS_HI);
    int rpm_lo = ec_io_read(EC_REG_FAN_RPMS_LO);
    
    printf("\n=== RPM Calculation Tests ===\n");
    printf("Raw RPM High: %d (0x%02X)\n", rpm_hi, rpm_hi);
    printf("Raw RPM Low: %d (0x%02X)\n", rpm_lo, rpm_lo);
    printf("Raw RPM Combined: %d (0x%04X)\n", (rpm_hi << 8) + rpm_lo, (rpm_hi << 8) + rpm_lo);
    printf("Original formula (2156220/raw): %d RPM\n", calculate_fan_rpms_original(rpm_hi, rpm_lo));
    printf("Alternative 1 (60000000/raw): %d RPM\n", calculate_fan_rpms_alternative1(rpm_hi, rpm_lo));
    printf("Alternative 2 (48000000/raw): %d RPM\n", calculate_fan_rpms_alternative2(rpm_hi, rpm_lo));
    printf("Direct RPM: %d RPM\n", calculate_fan_rpms_direct(rpm_hi, rpm_lo));
}

static void search_gpu_temp_register(void) {
    printf("\n=== GPU Temperature Register Search ===\n");
    printf("Searching for non-zero temperature readings...\n");
    
    for (int i = 0; i < 256; i++) {
        uint8_t value = ec_io_read(i);
        if (value > 0 && value < 120) {  // Reasonable temperature range
            printf("Potential temp register 0x%02X: %d°C\n", i, value);
        }
    }
}

static void monitor_registers(int duration) {
    printf("\n=== Register Monitoring (for %d seconds) ===\n", duration);
    printf("Time\tCPU\tGPU\tDuty\tRPM_HI\tRPM_LO\tRPM_Calc\n");
    
    for (int i = 0; i < duration; i++) {
        int cpu_temp = ec_io_read(EC_REG_CPU_TEMP);
        int gpu_temp = ec_io_read(EC_REG_GPU_TEMP);
        int fan_duty = ec_io_read(EC_REG_FAN_DUTY);
        int rpm_hi = ec_io_read(EC_REG_FAN_RPMS_HI);
        int rpm_lo = ec_io_read(EC_REG_FAN_RPMS_LO);
        int rpm_calc = calculate_fan_rpms_original(rpm_hi, rpm_lo);
        
        printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\n", 
               i, cpu_temp, gpu_temp, fan_duty, rpm_hi, rpm_lo, rpm_calc);
        
        sleep(1);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <command>\n", argv[0]);
        printf("Commands:\n");
        printf("  dump     - Dump all EC registers\n");
        printf("  search   - Search for GPU temperature register\n");
        printf("  monitor  - Monitor key registers for 10 seconds\n");
        printf("  monitor <seconds> - Monitor for specified duration\n");
        return 1;
    }
    
    // Setup I/O privileges
    if (ioperm(EC_DATA, 1, 1) != 0) {
        printf("Failed to get I/O permissions for EC_DATA: %s\n", strerror(errno));
        return 1;
    }
    if (ioperm(EC_SC, 1, 1) != 0) {
        printf("Failed to get I/O permissions for EC_SC: %s\n", strerror(errno));
        return 1;
    }
    
    if (strcmp(argv[1], "dump") == 0) {
        dump_ec_registers();
    } else if (strcmp(argv[1], "search") == 0) {
        search_gpu_temp_register();
    } else if (strcmp(argv[1], "monitor") == 0) {
        int duration = (argc > 2) ? atoi(argv[2]) : 10;
        monitor_registers(duration);
    } else {
        printf("Unknown command: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
} 