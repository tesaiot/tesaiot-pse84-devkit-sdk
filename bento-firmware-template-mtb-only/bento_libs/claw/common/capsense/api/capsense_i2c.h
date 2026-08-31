/*******************************************************************************
 * capsense_i2c.h — Layer 2: I2C-bound CAPSENSE driver public API.
 *
 * Reads the Infineon PSoC 4000T CapSense slave at I2C address 0x08 via the
 * shared `sensor_i2c` bus wrapper (the same bus used by BMI270, DPS368, etc.).
 *
 * Public API matches the historical BENTO `sensor_capsense.h` exactly so
 * MicroPython binding + UI page code continues to work unchanged after the
 * Phase 2 librarification.
 ******************************************************************************/
#ifndef CAPSENSE_I2C_H
#define CAPSENSE_I2C_H

#include "capsense_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize CapSense (I2C bus init if needed, baseline capture).
 * Idempotent — calling repeatedly is safe and returns the cached state. */
bool capsense_init(void);

/* Read all CapSense data (buttons + slider) in one I2C transaction. */
bool capsense_read(capsense_data_t *data);

/* Convenience: read button states only (still does a full I2C read internally). */
bool capsense_read_buttons(bool *btn0, bool *btn1);

/* Convenience: read slider position only (0..100). */
bool capsense_read_slider(uint8_t *position);

#ifdef __cplusplus
}
#endif

#endif /* CAPSENSE_I2C_H */
