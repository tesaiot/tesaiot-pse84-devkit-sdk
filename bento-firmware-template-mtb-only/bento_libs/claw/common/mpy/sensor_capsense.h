/*******************************************************************************
 * File Name: sensor_capsense.h
 *
 * Description: CapSense I2C driver (requires base board with PSoC 4000T).
 *              PSoC 4000T CapSense slave on I2C bus P8[0]/P8[1] at address 0x08.
 *              Protocol: direct 3-byte read (no register addressing).
 *              Byte 0: Button 0 state, Byte 1: Button 1 state, Byte 2: Slider (0-100).
 *
 *              Only compiled when BSP_HAS_CAPSENSE=1.
 *
 *******************************************************************************/

#ifndef SENSOR_CAPSENSE_H
#define SENSOR_CAPSENSE_H

#include <stdint.h>
#include <stdbool.h>

/* CapSense I2C address (PSoC 4000T slave) */
#define CAPSENSE_I2C_ADDR       (0x08)

/* CapSense data size (3 bytes: btn0, btn1, slider) */
#define CAPSENSE_DATA_SIZE      (3)

/* CapSense data structure */
typedef struct {
    bool btn0_pressed;      /* Button 0 state (true = pressed) */
    bool btn1_pressed;      /* Button 1 state (true = pressed) */
    uint8_t slider;         /* Slider position (0-100 %) */
} capsense_data_t;

/* Initialize CapSense (I2C bus init, baseline capture) */
bool capsense_init(void);

/* Read all CapSense data (buttons + slider) */
bool capsense_read(capsense_data_t *data);

/* Read button states only */
bool capsense_read_buttons(bool *btn0, bool *btn1);

/* Read slider position (0-100) */
bool capsense_read_slider(uint8_t *position);

#endif /* SENSOR_CAPSENSE_H */
