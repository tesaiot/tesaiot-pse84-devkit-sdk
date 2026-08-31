/*******************************************************************************
 * capsense_decode.c — L1 pure decoder implementation. Libc only.
 ******************************************************************************/
#include "capsense_decode.h"

#include <stddef.h>

uint8_t capsense_decode_button(uint8_t raw)
{
    /* ASCII digits '0'..'9' (0x30..0x39) */
    if (raw >= 0x30 && raw <= 0x39) {
        return (uint8_t)(raw - 0x30);
    }
    /* Legacy ASCII-ish encoding observed on some PSoC 4000T units: 30..32
     * (decimal 30..32) — translate the same way. */
    if (raw >= 30 && raw <= 32) {
        return (uint8_t)(raw - 30);
    }
    /* Direct numeric 0..2 */
    if (raw <= 2) {
        return raw;
    }
    /* Anything else nonzero = pressed (value 1) */
    return (raw != 0) ? (uint8_t)1 : (uint8_t)0;
}

bool capsense_decode_frame(const capsense_frame_t *frame,
                           const capsense_baseline_t *baseline,
                           capsense_data_t *out)
{
    if (frame == NULL || baseline == NULL || out == NULL) {
        return false;
    }
    if (!baseline->valid) {
        return false;
    }

    const uint8_t b0 = capsense_decode_button(frame->btn0_raw);
    const uint8_t b1 = capsense_decode_button(frame->btn1_raw);

    /* Press detected when current code differs from idle. */
    out->btn0_pressed = (b0 != baseline->btn0_idle);
    out->btn1_pressed = (b1 != baseline->btn1_idle);
    out->slider       = frame->slider_raw;
    return true;
}

void capsense_capture_baseline(const capsense_frame_t *frame,
                               capsense_baseline_t *out)
{
    if (frame == NULL || out == NULL) {
        return;
    }
    out->btn0_idle = capsense_decode_button(frame->btn0_raw);
    out->btn1_idle = capsense_decode_button(frame->btn1_raw);
    out->valid     = true;
}
