/*******************************************************************************
 * File Name: sensor_i2c.h
 *
 * Description: Shared I2C bus driver for on-board sensors (1.8V bus).
 *              Uses SCB0 on P8[0]=SCL, P8[1]=SDA.
 *              Separate from machine.I2C (SCB5, P17[0]/P17[1], 3.3V).
 *
 *******************************************************************************/

#ifndef SENSOR_I2C_H
#define SENSOR_I2C_H

#include <stdint.h>
#include <stdbool.h>
#include "cy_pdl.h"

/* Initialize SCB0 I2C master for sensor bus (400kHz) */
bool sensor_i2c_init(void);

/* Check if sensor bus is initialized */
bool sensor_i2c_is_init(void);

/* Write register(s) to sensor */
bool sensor_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

/* Read register(s) from sensor */
bool sensor_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);

/* Write a single byte to a register */
bool sensor_i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value);

/* Read a single byte from a register */
bool sensor_i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value);

/* Send raw bytes (no register address — for SHT40 command protocol) */
bool sensor_i2c_write_raw(uint8_t addr, const uint8_t *data, uint16_t len);

/* Read raw bytes (no register address — for SHT40 response) */
bool sensor_i2c_read_raw(uint8_t addr, uint8_t *data, uint16_t len);

/* Scan bus and return list of detected addresses */
int sensor_i2c_scan(uint8_t *addrs, int max_addrs);

/* Thread-safety: lock/unlock I2C bus mutex (for multi-task access).
 * timeout_ms: max wait time in milliseconds, 0 = no wait.
 * Safe to call before sensor_i2c_init() (returns true immediately). */
bool sensor_i2c_lock(uint32_t timeout_ms);
void sensor_i2c_unlock(void);

#endif /* SENSOR_I2C_H */
