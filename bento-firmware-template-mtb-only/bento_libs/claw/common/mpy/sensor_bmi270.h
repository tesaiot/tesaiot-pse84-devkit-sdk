/*******************************************************************************
 * File Name: sensor_bmi270.h
 *
 * Description: BMI270 6-axis IMU (accel + gyro) register-level driver.
 *              I2C address 0x68 on sensor bus (P8[0]/P8[1]).
 *
 *******************************************************************************/

#ifndef SENSOR_BMI270_H
#define SENSOR_BMI270_H

#include <stdint.h>
#include <stdbool.h>

/* BMI270 I2C address */
#define BMI270_I2C_ADDR         (0x68)

/* Initialize BMI270 (soft reset, load config, enable accel+gyro) */
bool bmi270_init(void);

/* Read accelerometer data in m/s^2 */
bool bmi270_read_accel(float *ax, float *ay, float *az);

/* Read gyroscope data in deg/s */
bool bmi270_read_gyro(float *gx, float *gy, float *gz);

/* Read chip temperature in Celsius */
bool bmi270_read_temperature(float *temp);

/* Read chip ID (should return 0x24) */
bool bmi270_read_chip_id(uint8_t *chip_id);

#endif /* SENSOR_BMI270_H */
