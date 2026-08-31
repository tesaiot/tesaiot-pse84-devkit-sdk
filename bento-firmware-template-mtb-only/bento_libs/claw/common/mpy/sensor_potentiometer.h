/*******************************************************************************
 * File Name: sensor_potentiometer.h
 *
 * Description: Potentiometer ADC driver (requires base board with SAR ADC).
 *              Uses SAR ADC channel on P15[1] (CYBSP_ADC_6_POT).
 *              MTB HAL ADC API with BSP-generated configuration.
 *
 *              Only compiled when BSP_HAS_POTENTIOMETER=1.
 *
 *******************************************************************************/

#ifndef SENSOR_POTENTIOMETER_H
#define SENSOR_POTENTIOMETER_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize ADC for potentiometer reading */
bool potentiometer_init(void);

/* Read raw 16-bit value (0-65535) */
bool potentiometer_read_raw(uint16_t *value);

/* Read percentage (0.0 - 100.0) */
bool potentiometer_read_percent(float *percent);

/* Read voltage (0.0 - 3.3V, assuming VDDA = 3.3V) */
bool potentiometer_read_voltage(float *voltage);

#endif /* SENSOR_POTENTIOMETER_H */
