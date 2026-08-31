/*******************************************************************************
 * File Name: sensor_bmm350.h
 *
 * Description: BMM350 3-axis magnetometer driver.
 *              I3C bus on P3[0]=SCL, P3[1]=SDA (CYBSP_I3C_CONTROLLER).
 *              Uses I3C MIXED_FAST mode (I2C legacy) at static address 0x15.
 *              Only compiled when BSP_HAS_BMM350=1.
 *
 *******************************************************************************/

#ifndef SENSOR_BMM350_H
#define SENSOR_BMM350_H

#include <stdint.h>
#include <stdbool.h>

/* BMM350 I2C static address (I3C legacy mode).
 * KIT_PSE84_AI has ADSEL=HIGH → address 0x15 (per user guide).
 * Bosch API: BMM350_I2C_ADSEL_SET_LOW=0x14, BMM350_I2C_ADSEL_SET_HIGH=0x15. */
#define BMM350_I2C_ADDR         (0x15)

/* Expected chip ID */
#define BMM350_CHIP_ID_VALUE    (0x33)

/* Initialize BMM350 (I3C bus init, sensor config, enable measurement) */
bool bmm350_init(void);

/* Full I3C + BMM350 re-init for error recovery (disables I3C, re-inits) */
bool bmm350_reinit(void);

/* Read magnetic field X, Y, Z in micro-Tesla (uT) */
bool bmm350_read_xyz(float *mx, float *my, float *mz);

/* Read compass heading in degrees (0-360, 0=North) */
bool bmm350_read_heading(float *heading);

/* Read chip ID (should return BMM350_CHIP_ID_VALUE) */
bool bmm350_read_chip_id(uint8_t *chip_id);

/* Diagnostic: test I3C communication with BMM350.
 * Full hardware reset + re-init with MIXED_FAST mode.
 * Attach I2C device BEFORE Enable, force IDLE, try chip ID read.
 * Returns: 0 = OK, non-zero = fail step. */
typedef struct {
    int      step;           /* 0=OK, 1=init fail, 2=attach fail, 3=read fail */
    uint32_t init_st;       /* Cy_I3C_Init return code */
    uint32_t attach_st;     /* I2C attach return code */
    uint32_t state_post_init; /* context->state after Init (expect 0x10000000) */
    uint32_t state_post_en;   /* context->state after Enable + 100ms */
    uint32_t present_st;     /* I3C_CORE PRESENT_STATE hardware register */
    uint32_t wr_st;          /* ControllerWrite return code for chip ID */
    uint32_t wr_ev;          /* Write transfer events (or error) */
    uint32_t rd_st;          /* ControllerRead return code */
    uint32_t rd_ev;          /* Read transfer events (or error) */
    uint8_t  chip_id;        /* Chip ID value read (expect 0x33) */
    bool     forced_idle;    /* true if we had to force context->state = IDLE */
} bmm350_diag_t;

int bmm350_diagnose(bmm350_diag_t *diag);

/* Debug: dump raw register values, test forced mode, print to console */
void bmm350_debug_read(void);

/* Hard iron calibration: auto-tracks min/max of mx, my to compute offset.
 * Call bmm350_cal_update() with each new raw reading to improve calibration.
 * The offset is automatically applied in bmm350_read_heading() and
 * bmm350_heading_from_xy(). */
void bmm350_cal_update(float mx, float my);
void bmm350_cal_reset(void);
void bmm350_cal_get_offsets(float *offset_x, float *offset_y, bool *valid);
float bmm350_heading_from_xy(float mx, float my);

#endif /* SENSOR_BMM350_H */
