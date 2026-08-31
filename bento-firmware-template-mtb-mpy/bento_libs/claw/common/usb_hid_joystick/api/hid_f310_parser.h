/*******************************************************************************
 * hid_f310_parser.h — Layer 1: pure decoder for Logitech F310 HID reports.
 *
 * Translates the raw 8-byte report (f310_report_t) into a more convenient
 * decoded form: centered/signed analog axes, individual booleans per button,
 * and a 2-axis dpad representation. Pure C — no USB, no FreeRTOS, no LVGL.
 * Host-unit-testable.
 ******************************************************************************/
#ifndef HID_F310_PARSER_H
#define HID_F310_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include "hid_f310_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decoded form — convenience for consumers who don't want to bit-twiddle. */
typedef struct {
    int16_t lx, ly;     /* -128..+127 around neutral; sign follows screen
                         * convention (right = positive X, up = positive Y) */
    int16_t rx, ry;
    bool    x, a, b, y;             /* face buttons */
    bool    lb, rb, lt, rt;         /* shoulders */
    bool    back, start, l3, r3;    /* system + stick clicks */
    bool    mode;                   /* mode LED */
    int8_t  dpad_x, dpad_y;         /* -1, 0, +1 per axis */
    uint8_t hat;                    /* raw hat code, 0..8 */
} f310_decoded_t;

/* Decode a raw 8-byte HID report. Caller-owned `out` is overwritten.
 * Returns false if `raw` is NULL. The function does not validate that the
 * report came from an F310 — the caller is responsible for that. */
bool f310_parse(const f310_report_t *raw, f310_decoded_t *out);

/* Apply a symmetric deadzone around the neutral position. Axis values
 * within [-threshold, +threshold] are clamped to 0; outside, the value is
 * preserved (no rescaling). Returns the deadzoned value.
 *
 * Typical use: `lx_dz = f310_deadzone(decoded.lx, 8);` to ignore ~6 % stick
 * drift on a well-used F310. */
int16_t f310_deadzone(int16_t value, int16_t threshold);

#ifdef __cplusplus
}
#endif

#endif /* HID_F310_PARSER_H */
