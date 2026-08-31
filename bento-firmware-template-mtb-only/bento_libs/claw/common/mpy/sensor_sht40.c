/*******************************************************************************
 * File Name: sensor_sht40.c
 *
 * Description: SHT40 humidity + temperature sensor driver.
 *              Uses command-response I2C protocol (no register addressing).
 *
 * Reference: SHT4x datasheet (Sensirion)
 *
 *******************************************************************************/

#include "sensor_sht40.h"
#include "sensor_i2c.h"
#include "cy_syslib.h"

/* SHT40 Commands */
#define SHT40_CMD_MEASURE_HIGH  (0xFD) /* High precision measurement */
#define SHT40_CMD_MEASURE_MED   (0xF6) /* Medium precision measurement */
#define SHT40_CMD_MEASURE_LOW   (0xE0) /* Low precision measurement */
#define SHT40_CMD_READ_SERIAL   (0x89) /* Read serial number */
#define SHT40_CMD_SOFT_RESET    (0x94) /* Soft reset */

static bool sht40_initialized = false;

/* CRC-8 check (polynomial 0x31, init 0xFF) */
static uint8_t sht40_crc8(const uint8_t *data, uint16_t len) {
    uint8_t crc = 0xFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

bool sht40_init(void) {
    if (sht40_initialized) {
        return true;
    }

    if (!sensor_i2c_is_init()) {
        if (!sensor_i2c_init()) {
            return false;
        }
    }

    /* Soft reset */
    uint8_t cmd = SHT40_CMD_SOFT_RESET;
    if (!sensor_i2c_write_raw(SHT40_I2C_ADDR, &cmd, 1)) {
        return false;
    }
    Cy_SysLib_Delay(2); /* Wait for reset */

    sht40_initialized = true;
    return true;
}

static bool sht40_measure(float *temperature, float *humidity) {
    /* Send high-precision measurement command */
    uint8_t cmd = SHT40_CMD_MEASURE_HIGH;
    if (!sensor_i2c_write_raw(SHT40_I2C_ADDR, &cmd, 1)) {
        return false;
    }

    /* Wait for measurement (high precision takes ~8.3ms, use 10ms) */
    Cy_SysLib_Delay(10);

    /* Read 6 bytes: [temp_msb, temp_lsb, temp_crc, hum_msb, hum_lsb, hum_crc] */
    uint8_t data[6];
    if (!sensor_i2c_read_raw(SHT40_I2C_ADDR, data, 6)) {
        return false;
    }

    /* Verify CRC for temperature */
    if (sht40_crc8(&data[0], 2) != data[2]) {
        return false;
    }

    /* Verify CRC for humidity */
    if (sht40_crc8(&data[3], 2) != data[5]) {
        return false;
    }

    /* Convert raw to physical values */
    uint16_t raw_temp = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_hum = ((uint16_t)data[3] << 8) | data[4];

    if (temperature != NULL) {
        *temperature = -45.0f + 175.0f * (float)raw_temp / 65535.0f;
    }

    if (humidity != NULL) {
        float rh = -6.0f + 125.0f * (float)raw_hum / 65535.0f;
        /* Clamp to valid range */
        if (rh < 0.0f) rh = 0.0f;
        if (rh > 100.0f) rh = 100.0f;
        *humidity = rh;
    }

    return true;
}

bool sht40_read_temperature(float *temperature) {
    return sht40_measure(temperature, NULL);
}

bool sht40_read_humidity(float *humidity) {
    float temp;
    return sht40_measure(&temp, humidity);
}

bool sht40_read_both(float *temperature, float *humidity) {
    return sht40_measure(temperature, humidity);
}

bool sht40_read_serial(uint32_t *serial) {
    uint8_t cmd = SHT40_CMD_READ_SERIAL;
    if (!sensor_i2c_write_raw(SHT40_I2C_ADDR, &cmd, 1)) {
        return false;
    }
    Cy_SysLib_Delay(1);

    uint8_t data[6];
    if (!sensor_i2c_read_raw(SHT40_I2C_ADDR, data, 6)) {
        return false;
    }

    /* Verify CRCs */
    if (sht40_crc8(&data[0], 2) != data[2]) return false;
    if (sht40_crc8(&data[3], 2) != data[5]) return false;

    *serial = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
              ((uint32_t)data[3] << 8) | data[4];
    return true;
}
