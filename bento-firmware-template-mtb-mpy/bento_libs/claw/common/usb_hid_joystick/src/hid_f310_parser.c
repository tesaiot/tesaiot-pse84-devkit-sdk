/*******************************************************************************
 * hid_f310_parser.c — L1 decoder implementation. Pure C, libc only.
 ******************************************************************************/
#include "hid_f310_parser.h"

#include <stddef.h>
#include <stdlib.h>     /* abs() */

bool f310_parse(const f310_report_t *raw, f310_decoded_t *out)
{
    if (raw == NULL || out == NULL) {
        return false;
    }

    /* Analog axes — F310 uses unsigned 0..255 with neutral around 0x80 (X)
     * and 0x7F (Y, inverted). Recenter and flip Y so positive = up. */
    out->lx = (int16_t)raw->left_x  - 0x80;
    out->ly = 0x7F - (int16_t)raw->left_y;
    out->rx = (int16_t)raw->right_x - 0x80;
    out->ry = 0x7F - (int16_t)raw->right_y;

    /* Face buttons (byte 4) */
    out->x = (raw->buttons1 & F310_BTN_X) != 0;
    out->a = (raw->buttons1 & F310_BTN_A) != 0;
    out->b = (raw->buttons1 & F310_BTN_B) != 0;
    out->y = (raw->buttons1 & F310_BTN_Y) != 0;

    /* Shoulders + system (byte 5) */
    out->lb    = (raw->buttons2 & F310_BTN_LB)    != 0;
    out->rb    = (raw->buttons2 & F310_BTN_RB)    != 0;
    out->lt    = (raw->buttons2 & F310_BTN_LT)    != 0;
    out->rt    = (raw->buttons2 & F310_BTN_RT)    != 0;
    out->back  = (raw->buttons2 & F310_BTN_BACK)  != 0;
    out->start = (raw->buttons2 & F310_BTN_START) != 0;
    out->l3    = (raw->buttons2 & F310_BTN_L3)    != 0;
    out->r3    = (raw->buttons2 & F310_BTN_R3)    != 0;

    out->mode = raw->mode != 0;

    /* Hat → dpad_x/dpad_y. F310 hat is 0..8 (8 = neutral). */
    out->hat = raw->buttons1 & F310_HAT_MASK;
    switch (out->hat) {
    case F310_HAT_UP:         out->dpad_x =  0; out->dpad_y =  1; break;
    case F310_HAT_UP_RIGHT:   out->dpad_x =  1; out->dpad_y =  1; break;
    case F310_HAT_RIGHT:      out->dpad_x =  1; out->dpad_y =  0; break;
    case F310_HAT_DOWN_RIGHT: out->dpad_x =  1; out->dpad_y = -1; break;
    case F310_HAT_DOWN:       out->dpad_x =  0; out->dpad_y = -1; break;
    case F310_HAT_DOWN_LEFT:  out->dpad_x = -1; out->dpad_y = -1; break;
    case F310_HAT_LEFT:       out->dpad_x = -1; out->dpad_y =  0; break;
    case F310_HAT_UP_LEFT:    out->dpad_x = -1; out->dpad_y =  1; break;
    case F310_HAT_NEUTRAL:
    default:                  out->dpad_x =  0; out->dpad_y =  0; break;
    }

    return true;
}

int16_t f310_deadzone(int16_t value, int16_t threshold)
{
    if (threshold < 0) {
        threshold = (int16_t)-threshold;
    }
    if (abs(value) <= threshold) {
        return 0;
    }
    return value;
}
