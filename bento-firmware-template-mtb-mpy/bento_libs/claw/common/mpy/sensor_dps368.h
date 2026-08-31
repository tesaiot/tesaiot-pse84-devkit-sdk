/*******************************************************************************
 * File Name: sensor_dps368.h
 *
 * Description: DPS368 barometric pressure + temperature sensor driver.
 *              I2C address 0x77 on sensor bus (P8[0]/P8[1]).
 *
 *******************************************************************************/

#ifndef SENSOR_DPS368_H
#define SENSOR_DPS368_H

#include <stdint.h>
#include <stdbool.h>

/* DPS368 I2C address */
#define DPS368_I2C_ADDR         (0x77)

/* Initialize DPS368 (read calibration coefficients, configure oversampling) */
bool dps368_init(void);

/* Read compensated pressure in hPa (mbar) */
bool dps368_read_pressure(float *pressure);

/* Read compensated temperature in Celsius */
bool dps368_read_temperature(float *temperature);

/* Read both pressure and temperature (single measurement cycle) */
bool dps368_read_both(float *pressure, float *temperature);

/* Read product ID (should return 0x10) */
bool dps368_read_product_id(uint8_t *product_id);

#endif /* SENSOR_DPS368_H */
