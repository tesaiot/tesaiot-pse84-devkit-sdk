/*******************************************************************************
 * capsense_i2c.c — Layer 2: I2C driver for the Infineon PSoC 4000T CapSense
 * slave on the shared sensor I2C bus.
 *
 * Workflow:
 *   1. capsense_init()      — ensures sensor_i2c bus is up, reads one
 *                             baseline frame, stashes idle codes via
 *                             L1 capsense_capture_baseline().
 *   2. capsense_read*()     — reads 3 bytes raw, decodes via L1
 *                             capsense_decode_frame() using stashed
 *                             baseline, returns capsense_data_t.
 *
 * Depends on: sensor_i2c bus wrapper (lives in common/mpy/sensor_i2c.h,
 *             shared with BMI270 / DPS368 / etc.). Vendor HAL touched
 *             only through that wrapper — this file stays portable as
 *             long as the wrapper is provided.
 ******************************************************************************/
#include "capsense_i2c.h"
#include "capsense_decode.h"
#include "capsense_protocol.h"
#include "sensor_i2c.h"

#include <stdio.h>
#include <string.h>

/* Module state — single device per board, so static is fine. */
static capsense_baseline_t s_baseline;
static bool s_initialized = false;

/* Raw I2C read of 3 bytes (no register addressing — PSoC 4000T uses
 * direct read protocol). */
static bool capsense_read_raw(uint8_t buf[CAPSENSE_FRAME_BYTES])
{
    return sensor_i2c_read_raw(CAPSENSE_I2C_ADDR, buf, CAPSENSE_FRAME_BYTES);
}

bool capsense_init(void)
{
    if (s_initialized) {
        return true;
    }

    /* Ensure I2C bus is initialized (idempotent on the wrapper side too). */
    if (!sensor_i2c_is_init()) {
        if (!sensor_i2c_init()) {
            printf("[CAPSENSE] I2C bus init failed\r\n");
            return false;
        }
    }

    /* Read baseline (idle state) — also serves as device presence check.
     * Failure here keeps s_initialized=false; subsequent capsense_read()
     * will not return spurious "all pressed" results. */
    capsense_frame_t frame;
    if (capsense_read_raw((uint8_t *)&frame)) {
        capsense_capture_baseline(&frame, &s_baseline);
        printf("[CAPSENSE] Init OK (baseline: btn0=%u, btn1=%u, slider=%u)\r\n",
               s_baseline.btn0_idle, s_baseline.btn1_idle, frame.slider_raw);
    } else {
        printf("[CAPSENSE] Warning: baseline read failed, using zero idle\r\n");
        memset(&s_baseline, 0, sizeof(s_baseline));
        s_baseline.valid = true;   /* allow reads but with zero baseline */
    }

    s_initialized = true;
    return true;
}

bool capsense_read(capsense_data_t *data)
{
    if (data == NULL) {
        return false;
    }
    if (!s_initialized && !capsense_init()) {
        return false;
    }

    capsense_frame_t frame;
    if (!capsense_read_raw((uint8_t *)&frame)) {
        return false;
    }

    return capsense_decode_frame(&frame, &s_baseline, data);
}

bool capsense_read_buttons(bool *btn0, bool *btn1)
{
    if (btn0 == NULL || btn1 == NULL) {
        return false;
    }
    capsense_data_t d;
    if (!capsense_read(&d)) {
        return false;
    }
    *btn0 = d.btn0_pressed;
    *btn1 = d.btn1_pressed;
    return true;
}

bool capsense_read_slider(uint8_t *position)
{
    if (position == NULL) {
        return false;
    }
    capsense_data_t d;
    if (!capsense_read(&d)) {
        return false;
    }
    *position = d.slider;
    return true;
}
