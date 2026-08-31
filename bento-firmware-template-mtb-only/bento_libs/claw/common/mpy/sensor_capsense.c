/*******************************************************************************
 * File Name: sensor_capsense.c
 *
 * Description: CapSense I2C driver (requires base board with PSoC 4000T).
 *              Reads 3 bytes from PSoC 4000T CapSense I2C slave at 0x08.
 *              No register addressing — direct read protocol.
 *              Uses sensor_i2c bus wrapper (shared with BMI270, etc.).
 *
 * Protocol:
 *   I2C Read 3 bytes from address 0x08:
 *     Byte 0: Button 0 code (ASCII or numeric)
 *     Byte 1: Button 1 code (ASCII or numeric)
 *     Byte 2: Slider position (0-100)
 *
 *   Button code normalization (PSoC 4000T sends various formats):
 *     ASCII '0'-'9' (0x30-0x39) -> subtract '0'
 *     Direct 0-2 -> use as-is
 *     Anything nonzero -> pressed (1)
 *
 *******************************************************************************/

#include "sensor_capsense.h"
#include "sensor_i2c.h"
#include "cy_syslib.h"
#include <stdio.h>

/* Baseline state (captured on first read) */
static uint8_t idle_btn0_code = 0;
static uint8_t idle_btn1_code = 0;
static bool have_baseline = false;
static bool capsense_initialized = false;

/* Normalize button code from PSoC 4000T */
static uint8_t normalize_btn(uint8_t raw)
{
    /* ASCII digits '0'-'9' (0x30-0x39) */
    if (raw >= 0x30 && raw <= 0x39) {
        return (uint8_t)(raw - 0x30);
    }
    /* ASCII encoding variant (30-32) */
    if (raw >= 30 && raw <= 32) {
        return (uint8_t)(raw - 30);
    }
    /* Direct numeric (0-2) */
    if (raw <= 2) {
        return raw;
    }
    /* Anything nonzero = pressed */
    return (raw != 0) ? 1 : 0;
}

/* Read raw 3 bytes from CapSense (direct I2C read, no register address) */
static bool capsense_read_raw(uint8_t buf[CAPSENSE_DATA_SIZE])
{
    /* Direct read: PSoC 4000T doesn't use register addressing.
     * Just I2C read [addr+R][data0][data1][data2] */
    return sensor_i2c_read_raw(CAPSENSE_I2C_ADDR, buf, CAPSENSE_DATA_SIZE);
}

bool capsense_init(void)
{
    if (capsense_initialized) {
        return true;
    }

    /* Ensure I2C bus is initialized */
    if (!sensor_i2c_is_init()) {
        if (!sensor_i2c_init()) {
            printf("[CAPSENSE] I2C bus init failed\r\n");
            return false;
        }
    }

    /* Read baseline (idle state) — also serves as device presence check */
    uint8_t buf[CAPSENSE_DATA_SIZE];
    if (capsense_read_raw(buf)) {
        idle_btn0_code = normalize_btn(buf[0]);
        idle_btn1_code = normalize_btn(buf[1]);
        have_baseline = true;
        printf("[CAPSENSE] Init OK (baseline: btn0=%u, btn1=%u, slider=%u)\r\n",
               idle_btn0_code, idle_btn1_code, buf[2]);
    } else {
        printf("[CAPSENSE] Warning: baseline read failed, using default\r\n");
        idle_btn0_code = 0;
        idle_btn1_code = 0;
        have_baseline = true;
    }

    capsense_initialized = true;
    return true;
}

bool capsense_read(capsense_data_t *data)
{
    if (!capsense_initialized && !capsense_init()) {
        return false;
    }

    uint8_t buf[CAPSENSE_DATA_SIZE];
    if (!capsense_read_raw(buf)) {
        return false;
    }

    uint8_t btn0_code = normalize_btn(buf[0]);
    uint8_t btn1_code = normalize_btn(buf[1]);

    /* Detect press: button code changed from idle baseline */
    data->btn0_pressed = (btn0_code != idle_btn0_code);
    data->btn1_pressed = (btn1_code != idle_btn1_code);
    data->slider = buf[2];

    return true;
}

bool capsense_read_buttons(bool *btn0, bool *btn1)
{
    capsense_data_t data;
    if (!capsense_read(&data)) {
        return false;
    }
    *btn0 = data.btn0_pressed;
    *btn1 = data.btn1_pressed;
    return true;
}

bool capsense_read_slider(uint8_t *position)
{
    capsense_data_t data;
    if (!capsense_read(&data)) {
        return false;
    }
    *position = data.slider;
    return true;
}
