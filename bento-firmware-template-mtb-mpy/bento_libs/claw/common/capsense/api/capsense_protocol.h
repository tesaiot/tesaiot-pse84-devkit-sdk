/*******************************************************************************
 * capsense_protocol.h — Layer 1: CAPSENSE I2C wire protocol constants.
 *
 * Defines the Infineon PSoC 4000T CapSense slave's I2C address + the 3-byte
 * frame layout it emits on direct read (no register addressing). Pure data
 * header — libc only, no driver state, no MicroPython.
 ******************************************************************************/
#ifndef CAPSENSE_PROTOCOL_H
#define CAPSENSE_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C slave address — PSoC 4000T programmed at 0x08 on the sensor bus. */
#define CAPSENSE_I2C_ADDR       (0x08)

/* Each read returns 3 bytes (no register addressing). */
#define CAPSENSE_FRAME_BYTES    (3)

/* The 3-byte frame layout. Note: byte values may be ASCII or numeric — the
 * decoder handles both. Slider is always 0..100 (percent). */
typedef struct __attribute__((packed)) {
    uint8_t btn0_raw;   /* Button 0 code (ASCII '0'-'9' or numeric 0-2) */
    uint8_t btn1_raw;   /* Button 1 code (same encoding) */
    uint8_t slider_raw; /* Slider position 0..100 */
} capsense_frame_t;

#ifdef __cplusplus
}
#endif

#endif /* CAPSENSE_PROTOCOL_H */
