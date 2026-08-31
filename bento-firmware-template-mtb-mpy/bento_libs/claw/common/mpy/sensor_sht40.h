/*******************************************************************************
 * File Name: sensor_sht40.h
 *
 * Description: SHT40 humidity + temperature sensor driver.
 *              I2C address 0x44 on sensor bus (P8[0]/P8[1]).
 *              Uses command-response protocol (no register addressing).
 *
 *******************************************************************************/

#ifndef SENSOR_SHT40_H
#define SENSOR_SHT40_H

#include <stdint.h>
#include <stdbool.h>

/* SHT40 I2C address */
#define SHT40_I2C_ADDR          (0x44)

/* Initialize SHT40 (soft reset) */
bool sht40_init(void);

/* Read temperature in Celsius */
bool sht40_read_temperature(float *temperature);

/* Read relative humidity in %RH */
bool sht40_read_humidity(float *humidity);

/* Read both temperature and humidity */
bool sht40_read_both(float *temperature, float *humidity);

/* Read serial number */
bool sht40_read_serial(uint32_t *serial);

#endif /* SENSOR_SHT40_H */
