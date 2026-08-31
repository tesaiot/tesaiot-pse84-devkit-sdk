/*******************************************************************************
 * capsense_decode.h — Layer 1: pure decoder for CAPSENSE 3-byte frames.
 *
 * Converts raw bytes from the I2C wire (capsense_frame_t) into a useful
 * decoded form (capsense_data_t — booleans + 0..100 slider). All logic is
 * pure C with libc only; no I2C, no MicroPython, no Cypress HAL. Host-unit-
 * testable.
 *
 * The PSoC 4000T may emit button codes in two encodings:
 *   - ASCII digits '0'-'9' (0x30-0x39)
 *   - Direct numeric (0..32)
 * The decoder normalises both into a 0..2 unsigned integer where 0 = idle.
 * A button is considered pressed when its current code differs from the
 * captured baseline (idle) code.
 ******************************************************************************/
#ifndef CAPSENSE_DECODE_H
#define CAPSENSE_DECODE_H

#include <stdbool.h>
#include <stdint.h>
#include "capsense_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decoded state — matches the long-standing BENTO public layout so existing
 * MicroPython binding + UI page code keeps working unchanged. */
typedef struct {
    bool    btn0_pressed;   /* Button 0 state (true = pressed) */
    bool    btn1_pressed;   /* Button 1 state (true = pressed) */
    uint8_t slider;         /* Slider position (0..100 percent) */
} capsense_data_t;

/* Baseline (idle) state captured on driver init. Required to compute press
 * vs released — PSoC 4000T does not reset its button code on release; the
 * "idle code" is the value seen with no fingers on the pad. */
typedef struct {
    uint8_t btn0_idle;      /* Normalised idle code (0..2) */
    uint8_t btn1_idle;
    bool    valid;          /* true once init successfully read baseline */
} capsense_baseline_t;

/* Normalise one raw button byte to a 0..2 integer.
 *
 *   ASCII '0'-'9' (0x30..0x39)    -> byte - '0'
 *   Direct numeric 0..2           -> pass through
 *   Anything else nonzero         -> 1 (treated as "pressed")
 *
 * Pure function. Host-testable. */
uint8_t capsense_decode_button(uint8_t raw);

/* Decode a 3-byte frame given a captured baseline. Writes the decoded state
 * to `out`. Returns false if `frame`, `baseline` or `out` is NULL — or if
 * baseline is not yet valid (init never captured a stable idle). */
bool capsense_decode_frame(const capsense_frame_t *frame,
                           const capsense_baseline_t *baseline,
                           capsense_data_t *out);

/* Capture a baseline from a freshly-read frame at idle. Use during init.
 * Always succeeds (no failure mode); the caller decides whether the read
 * was actually at idle. */
void capsense_capture_baseline(const capsense_frame_t *frame,
                               capsense_baseline_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CAPSENSE_DECODE_H */
