#include "ec_interface.h"
#include "logging.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>

// Internal helper functions
static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value);
static uint8_t ec_io_read(const uint32_t port);
static int ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value);
static int calculate_fan_duty(int raw_duty);
static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low);

int ec_init(void) {
    if (ioperm(EC_DATA, 1, 1) != 0) {
        logging_error("Failed to get I/O permissions for EC_DATA: %s", strerror(errno));
        return -1;
    }
    if (ioperm(EC_SC, 1, 1) != 0) {
        logging_error("Failed to get I/O permissions for EC_SC: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int ec_query_cpu_temp(void) {
    return ec_io_read(EC_REG_CPU_TEMP);
}

int ec_query_fan_duty(void) {
    int raw_duty = ec_io_read(EC_REG_FAN_DUTY);
    return calculate_fan_duty(raw_duty);
}

int ec_query_fan_rpms(void) {
    int raw_rpm_hi = ec_io_read(EC_REG_FAN_RPMS_HI);
    int raw_rpm_lo = ec_io_read(EC_REG_FAN_RPMS_LO);
    return calculate_fan_rpms(raw_rpm_hi, raw_rpm_lo);
}

int ec_write_fan_duty(int duty_percentage) {
    if (duty_percentage < 1 || duty_percentage > 100) {
        logging_error("Invalid fan duty to write: %d", duty_percentage);
        return -1;
    }
    
    double v_d = ((double) duty_percentage) / 100.0 * 255.0;
    int v_i = (int) v_d;
    return ec_io_do(0x99, 0x01, v_i);
}

int ec_write_fan_duty_with_retry(int duty_percentage, int max_retries) {
    if (duty_percentage < 1 || duty_percentage > 100) {
        logging_error("Invalid fan duty to write: %d", duty_percentage);
        return -1;
    }
    
    double v_d = ((double) duty_percentage) / 100.0 * 255.0;
    int v_i = (int) v_d;
    
    // Use the standard ec_io_do function with multiple attempts
    for (int attempt = 0; attempt < max_retries; attempt++) {
        int result = ec_io_do(0x99, 0x01, v_i);
        if (result == 0) {
            if (attempt > 0) {
                logging_debug("Fan duty write succeeded on retry %d/%d: %d%% (raw: %d)", 
                             attempt + 1, max_retries, duty_percentage, v_i);
            }
            return 0;
        }
        
        logging_debug("Fan duty write attempt %d/%d failed: %d%% (raw: %d)", 
                     attempt + 1, max_retries, duty_percentage, v_i);
        
        if (attempt < max_retries - 1) {
            // Wait before next retry with exponential backoff
            int retry_delay_ms = (1 << attempt) * 5;  // 5ms, 10ms, 20ms...
            usleep(retry_delay_ms * 1000);
        }
    }
    
    logging_error("Fan duty write failed after %d retries: %d%% (raw: %d)", 
                 max_retries, duty_percentage, v_i);
    return -1;
}

int ec_test_fan(int duty_percentage) {
    logging_info("Testing fan duty: %d%%", duty_percentage);
    int result = ec_write_fan_duty(duty_percentage);
    if (result == 0) {
        ec_dump_fan();
    }
    return result;
}

int ec_dump_fan(void) {
    printf("Fan Information:\n");
    printf("  FAN Duty: %d%%\n", ec_query_fan_duty());
    printf("  FAN RPMs: %d RPM\n", ec_query_fan_rpms());
    printf("  CPU Temp: %d°C\n", ec_query_cpu_temp());
    return 0;
}

void ec_cleanup(void) {
    // Release I/O permissions
    ioperm(EC_DATA, 1, 0);
    ioperm(EC_SC, 1, 0);
}

static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 500)) {  // Increased from 100 to 500
        usleep(1000);
        data = inb(port);
    }
    if (i >= 500) {  // Updated timeout check
        logging_error("EC I/O wait error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x",
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



static int ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(cmd, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, IBF, 0);
    outb(value, EC_DATA);

    return ec_io_wait(EC_SC, IBF, 0);
}



static int calculate_fan_duty(int raw_duty) {
    return (int) ((double) raw_duty / 255.0 * 100.0);
}

static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    
    // Handle edge cases to prevent nonsensical RPM values
    if (raw_rpm <= 0) {
        return 0;
    }
    
    // Prevent division by very small numbers that could cause overflow
    if (raw_rpm < 10) {
        return 0;  // Fan is likely stopped or in error state
    }
    
    int calculated_rpm = 2156220 / raw_rpm;
    
    // Sanity check: RPM should be reasonable (0-10000 for laptop fans)
    if (calculated_rpm < 0 || calculated_rpm > 10000) {
        logging_debug("Invalid RPM calculation: raw_rpm=%d, calculated_rpm=%d", raw_rpm, calculated_rpm);
        return 0;  // Return 0 for invalid readings
    }
    
    return calculated_rpm;
} 