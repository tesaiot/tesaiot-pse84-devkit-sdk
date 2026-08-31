/*******************************************************************************
 * File Name: sensor_dps368.c
 *
 * Description: DPS368 barometric pressure + temperature sensor driver.
 *              Register-level I2C driver with compensation calculation.
 *
 * Reference: DPS368 datasheet (Infineon Technologies)
 *
 *******************************************************************************/

#include "sensor_dps368.h"
#include "sensor_i2c.h"
#include "cy_syslib.h"

/* DPS368 Register Map */
#define DPS368_REG_PSR_B2       (0x00) /* Pressure data [23:16] */
#define DPS368_REG_PSR_B1       (0x01) /* Pressure data [15:8] */
#define DPS368_REG_PSR_B0       (0x02) /* Pressure data [7:0] */
#define DPS368_REG_TMP_B2       (0x03) /* Temperature data [23:16] */
#define DPS368_REG_TMP_B1       (0x04) /* Temperature data [15:8] */
#define DPS368_REG_TMP_B0       (0x05) /* Temperature data [7:0] */
#define DPS368_REG_PRS_CFG      (0x06) /* Pressure config */
#define DPS368_REG_TMP_CFG      (0x07) /* Temperature config */
#define DPS368_REG_MEAS_CFG     (0x08) /* Measurement config */
#define DPS368_REG_CFG_REG      (0x09) /* Configuration */
#define DPS368_REG_RESET        (0x0C) /* Soft reset */
#define DPS368_REG_PRODUCT_ID   (0x0D) /* Product ID */
#define DPS368_REG_COEF         (0x10) /* Calibration coefficients start (18 bytes) */
#define DPS368_REG_COEF_SRCE    (0x28) /* Coefficient source */

/* Expected product revision ID */
#define DPS368_PRODUCT_ID       (0x10)

/* Oversampling rates */
#define DPS368_OSR_1            (0x00) /* 1x oversampling */
#define DPS368_OSR_8            (0x03) /* 8x oversampling */
#define DPS368_OSR_16           (0x04) /* 16x oversampling */

/* Measurement modes */
#define DPS368_MEAS_IDLE        (0x00)
#define DPS368_MEAS_PRS         (0x01) /* Single pressure measurement */
#define DPS368_MEAS_TMP         (0x02) /* Single temperature measurement */
#define DPS368_MEAS_BOTH        (0x07) /* Continuous pressure + temperature */

/* Scale factors for oversampling rates */
static const int32_t dps368_scale_factors[] = {
    524288,  /* OSR = 1 */
    1572864, /* OSR = 2 */
    3670016, /* OSR = 4 */
    7864320, /* OSR = 8 */
    253952,  /* OSR = 16 */
    516096,  /* OSR = 32 */
    1040384, /* OSR = 64 */
    2088960  /* OSR = 128 */
};

/* Calibration coefficients */
typedef struct {
    int32_t c0, c1;
    int32_t c00, c10, c01, c11, c20, c21, c30;
} dps368_coef_t;

static dps368_coef_t coef;
static bool dps368_initialized = false;

/* Cached raw temperature for pressure compensation */
static float last_temp_scaled = 0.0f;

/* Convert twos complement with given bit width to int32 */
static int32_t twos_complement(uint32_t val, uint8_t bits) {
    if (val & (1UL << (bits - 1))) {
        return (int32_t)(val | (~0UL << bits));
    }
    return (int32_t)val;
}

static bool dps368_read_coefficients(void) {
    uint8_t buf[18];
    if (!sensor_i2c_read_reg(DPS368_I2C_ADDR, DPS368_REG_COEF, buf, 18)) {
        return false;
    }

    /* Parse coefficients from raw bytes (see DPS368 datasheet Table 9) */
    coef.c0  = twos_complement(((uint32_t)buf[0] << 4) | (buf[1] >> 4), 12);
    coef.c1  = twos_complement(((uint32_t)(buf[1] & 0x0F) << 8) | buf[2], 12);
    coef.c00 = twos_complement(((uint32_t)buf[3] << 12) | ((uint32_t)buf[4] << 4) | (buf[5] >> 4), 20);
    coef.c10 = twos_complement(((uint32_t)(buf[5] & 0x0F) << 16) | ((uint32_t)buf[6] << 8) | buf[7], 20);
    coef.c01 = twos_complement(((uint32_t)buf[8] << 8) | buf[9], 16);
    coef.c11 = twos_complement(((uint32_t)buf[10] << 8) | buf[11], 16);
    coef.c20 = twos_complement(((uint32_t)buf[12] << 8) | buf[13], 16);
    coef.c21 = twos_complement(((uint32_t)buf[14] << 8) | buf[15], 16);
    coef.c30 = twos_complement(((uint32_t)buf[16] << 8) | buf[17], 16);

    return true;
}

bool dps368_read_product_id(uint8_t *product_id) {
    return sensor_i2c_read_byte(DPS368_I2C_ADDR, DPS368_REG_PRODUCT_ID, product_id);
}

bool dps368_init(void) {
    if (dps368_initialized) {
        return true;
    }

    if (!sensor_i2c_is_init()) {
        if (!sensor_i2c_init()) {
            return false;
        }
    }

    /* Soft reset */
    sensor_i2c_write_byte(DPS368_I2C_ADDR, DPS368_REG_RESET, 0x89);
    Cy_SysLib_Delay(40); /* Wait for sensor ready */

    /* Verify product ID */
    uint8_t id = 0;
    if (!dps368_read_product_id(&id)) {
        return false;
    }
    if ((id & 0xF0) != DPS368_PRODUCT_ID) {
        return false;
    }

    /* Read calibration coefficients */
    if (!dps368_read_coefficients()) {
        return false;
    }

    /* Determine temperature coefficient source (ASIC or MEMS) */
    uint8_t coef_srce = 0;
    sensor_i2c_read_byte(DPS368_I2C_ADDR, DPS368_REG_COEF_SRCE, &coef_srce);

    /* Configure pressure: rate=1Hz, oversampling=8x */
    if (!sensor_i2c_write_byte(DPS368_I2C_ADDR, DPS368_REG_PRS_CFG,
                               (0x00 << 4) | DPS368_OSR_8)) {
        return false;
    }

    /* Configure temperature: rate=1Hz, oversampling=8x, source from coef_srce */
    uint8_t tmp_cfg = (0x00 << 4) | DPS368_OSR_8;
    if (coef_srce & 0x80) {
        tmp_cfg |= 0x80; /* Use external MEMS temperature sensor */
    }
    if (!sensor_i2c_write_byte(DPS368_I2C_ADDR, DPS368_REG_TMP_CFG, tmp_cfg)) {
        return false;
    }

    /* P_SHIFT/T_SHIFT only needed for oversampling > 8x (PRC > 3).
     * We use 8x (PRC=3), so both shifts must be OFF. */
    if (!sensor_i2c_write_byte(DPS368_I2C_ADDR, DPS368_REG_CFG_REG, 0x00)) {
        return false;
    }

    /* Temperature-anomaly correction (Infineon micropython-xensiv-dps3xx
     * correctTemperature): after a soft reset the sensor can report wildly
     * high temperatures (60C+ in a 25C room). This undocumented register
     * sequence (empirical, from the vendor driver) recalibrates the
     * temperature path. Best-effort — a NAK here must not fail init. */
    (void)sensor_i2c_write_byte(DPS368_I2C_ADDR, 0x0E, 0xA5);
    (void)sensor_i2c_write_byte(DPS368_I2C_ADDR, 0x0F, 0x96);
    (void)sensor_i2c_write_byte(DPS368_I2C_ADDR, 0x62, 0x02);
    (void)sensor_i2c_write_byte(DPS368_I2C_ADDR, 0x0E, 0x00);
    (void)sensor_i2c_write_byte(DPS368_I2C_ADDR, 0x0F, 0x00);

    dps368_initialized = true;
    return true;
}

static bool dps368_read_raw_temperature(int32_t *raw_temp) {
    /* Trigger single temperature measurement */
    if (!sensor_i2c_write_byte(DPS368_I2C_ADDR, DPS368_REG_MEAS_CFG, DPS368_MEAS_TMP)) {
        return false;
    }

    /* Wait for measurement complete (bit 5 = TMP_RDY) */
    uint8_t status = 0;
    for (int i = 0; i < 100; i++) {
        Cy_SysLib_Delay(5);
        if (!sensor_i2c_read_byte(DPS368_I2C_ADDR, DPS368_REG_MEAS_CFG, &status)) {
            return false;
        }
        if (status & 0x20) break; /* TMP_RDY */
    }
    if (!(status & 0x20)) {
        return false; /* Timeout */
    }

    /* Read 3 bytes of raw temperature */
    uint8_t buf[3];
    if (!sensor_i2c_read_reg(DPS368_I2C_ADDR, DPS368_REG_TMP_B2, buf, 3)) {
        return false;
    }

    *raw_temp = twos_complement(((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2], 24);
    return true;
}

static bool dps368_read_raw_pressure(int32_t *raw_prs) {
    /* Trigger single pressure measurement */
    if (!sensor_i2c_write_byte(DPS368_I2C_ADDR, DPS368_REG_MEAS_CFG, DPS368_MEAS_PRS)) {
        return false;
    }

    /* Wait for measurement complete (bit 4 = PRS_RDY) */
    uint8_t status = 0;
    for (int i = 0; i < 100; i++) {
        Cy_SysLib_Delay(5);
        if (!sensor_i2c_read_byte(DPS368_I2C_ADDR, DPS368_REG_MEAS_CFG, &status)) {
            return false;
        }
        if (status & 0x10) break; /* PRS_RDY */
    }
    if (!(status & 0x10)) {
        return false; /* Timeout */
    }

    /* Read 3 bytes of raw pressure */
    uint8_t buf[3];
    if (!sensor_i2c_read_reg(DPS368_I2C_ADDR, DPS368_REG_PSR_B2, buf, 3)) {
        return false;
    }

    *raw_prs = twos_complement(((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2], 24);
    return true;
}

bool dps368_read_temperature(float *temperature) {
    int32_t raw_temp;
    if (!dps368_read_raw_temperature(&raw_temp)) {
        return false;
    }

    /* Scale raw value by oversampling factor (8x → index 3) */
    float Traw_sc = (float)raw_temp / (float)dps368_scale_factors[3];

    /* Compensated temperature */
    *temperature = (float)coef.c0 * 0.5f + (float)coef.c1 * Traw_sc;

    /* Cache for pressure compensation */
    last_temp_scaled = Traw_sc;

    return true;
}

bool dps368_read_pressure(float *pressure) {
    /* Always read temperature first for compensation */
    float temp;
    if (!dps368_read_temperature(&temp)) {
        return false;
    }
    (void)temp; /* We only need last_temp_scaled to be updated */

    int32_t raw_prs;
    if (!dps368_read_raw_pressure(&raw_prs)) {
        return false;
    }

    /* Scale raw value by oversampling factor (8x → index 3) */
    float Praw_sc = (float)raw_prs / (float)dps368_scale_factors[3];

    /* Compensated pressure (second-order polynomial) */
    *pressure = (float)coef.c00
              + Praw_sc * ((float)coef.c10 + Praw_sc * ((float)coef.c20 + Praw_sc * (float)coef.c30))
              + last_temp_scaled * ((float)coef.c01 + Praw_sc * ((float)coef.c11 + Praw_sc * (float)coef.c21));

    /* Convert Pa to hPa */
    *pressure /= 100.0f;

    return true;
}

bool dps368_read_both(float *pressure, float *temperature) {
    if (!dps368_read_temperature(temperature)) {
        return false;
    }

    int32_t raw_prs;
    if (!dps368_read_raw_pressure(&raw_prs)) {
        return false;
    }

    float Praw_sc = (float)raw_prs / (float)dps368_scale_factors[3];

    *pressure = (float)coef.c00
              + Praw_sc * ((float)coef.c10 + Praw_sc * ((float)coef.c20 + Praw_sc * (float)coef.c30))
              + last_temp_scaled * ((float)coef.c01 + Praw_sc * ((float)coef.c11 + Praw_sc * (float)coef.c21));

    *pressure /= 100.0f;

    return true;
}
