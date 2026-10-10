/*******************************************************************************
 * cm55_capsense_frame.h - decoder for the first bytes the QWA309 CapSense
 *                         controller (PSoC 4000T, I2C 0x08) returns.
 *
 * Two kinds of firmware can sit on that controller, and both are read the
 * same way: one I2C read of CAPS_FRAME_READ_LEN bytes from address 0x08 with
 * no register write before it, so the read starts at the controller's base
 * offset.
 *
 *   Snapshot firmware, protocol 0x0D or 0x0E. The first six bytes are:
 *     [0]    0xC5, a fixed marker
 *     [1]    protocol version, 0x0D or 0x0E
 *     [2]    a counter that advances with every update
 *     [3]    status bits:
 *              0x01 BTN0 touched      0x02 SW1 pressed     0x04 SW2 pressed
 *              0x08 SW3 in ON         0x10 SW4 in ON       0x20 BTN1 touched
 *              0x40 controller error  0x80 controller running
 *     [4..5] slider position, 16-bit little-endian, 0-100;
 *            0xFFFF means the slider is not being touched.
 *   SW1/SW2 are push-buttons and SW3/SW4 are slide switches on the base
 *   board; all four are wired to the CapSense controller, not to the E84.
 *
 *   Legacy firmware. Bytes 0, 1 and 2 are button 0, button 1 and the slider;
 *   it reports no SW1-SW4 state. Anything that is not a snapshot frame is
 *   decoded this way, which is how this driver has always read the chip.
 *
 * Header-only and free of PDL/LVGL so the decoder can be compiled and tested
 * on a host. The driver that owns the bus is cm55_sensor_poll.c.
 ******************************************************************************/

#ifndef CM55_CAPSENSE_FRAME_H
#define CM55_CAPSENSE_FRAME_H

#include <stdbool.h>
#include <stdint.h>

/** Bytes read from the controller on every poll. */
#define CAPS_FRAME_READ_LEN        (6U)

/** Snapshot-frame marker byte and the protocol versions decoded as one. */
#define CAPS_FRAME_MARKER          (0xC5U)
#define CAPS_FRAME_PROTO_0D        (0x0DU)
#define CAPS_FRAME_PROTO_0E        (0x0EU)

/** Status byte [3] bits (snapshot frame). */
#define CAPS_ST_BTN0               (0x01U)
#define CAPS_ST_SW1                (0x02U)
#define CAPS_ST_SW2                (0x04U)
#define CAPS_ST_SW3_ON             (0x08U)
#define CAPS_ST_SW4_ON             (0x10U)
#define CAPS_ST_BTN1               (0x20U)
#define CAPS_ST_ERROR              (0x40U)
#define CAPS_ST_RUNNING            (0x80U)

/** Slider value the snapshot firmware sends while nobody touches the slider. */
#define CAPS_SLIDER_UNTOUCHED      (0xFFFFU)
/** Highest slider position the snapshot firmware reports. */
#define CAPS_SLIDER_MAX            (100U)

/** Bits of caps_frame_t.switches (bit set = pressed for SW1/SW2, ON for SW3/SW4). */
#define CAPS_SW_SW1                (0x01U)
#define CAPS_SW_SW2                (0x02U)
#define CAPS_SW_SW3_ON             (0x04U)
#define CAPS_SW_SW4_ON             (0x08U)

typedef struct {
    bool     snapshot;        /**< true: a 0x0D/0x0E snapshot frame was decoded */
    uint8_t  proto;           /**< 0x0D or 0x0E for a snapshot frame, 0 for legacy */
    /* Snapshot frame: absolute button state from the status byte.
     * Legacy frame: the raw button bytes [0] and [1], which the caller
     * compares against the idle values it captured at start-up. */
    uint8_t  btn0;
    uint8_t  btn1;
    uint8_t  switches;        /**< CAPS_SW_* bits; 0 for a legacy frame */
    bool     slider_touched;  /**< false only for a snapshot frame carrying 0xFFFF */
    uint8_t  slider;          /**< 0-100 (snapshot, clamped) or raw byte [2] (legacy) */
    bool     error;           /**< snapshot status 0x40 */
    bool     running;         /**< snapshot status 0x80 */
} caps_frame_t;

/** True when buf[0..1] mark a snapshot frame this decoder understands. */
static inline bool caps_frame_is_snapshot(const uint8_t buf[CAPS_FRAME_READ_LEN])
{
    return (buf[0] == CAPS_FRAME_MARKER) &&
           ((buf[1] == CAPS_FRAME_PROTO_0D) || (buf[1] == CAPS_FRAME_PROTO_0E));
}

/** Decode CAPS_FRAME_READ_LEN bytes read from the controller into *out. */
static inline void caps_frame_decode(const uint8_t buf[CAPS_FRAME_READ_LEN],
                                     caps_frame_t *out)
{
    if (caps_frame_is_snapshot(buf)) {
        const uint8_t  st     = buf[3];
        const uint16_t slider = (uint16_t)((uint16_t)buf[4] |
                                           ((uint16_t)buf[5] << 8));

        out->snapshot = true;
        out->proto    = buf[1];
        out->btn0     = ((st & CAPS_ST_BTN0) != 0U) ? 1U : 0U;
        out->btn1     = ((st & CAPS_ST_BTN1) != 0U) ? 1U : 0U;
        /* SW1..SW4 are status bits 1..4: shift them down to bits 0..3. */
        out->switches = (uint8_t)((st >> 1) & 0x0FU);
        out->error    = (st & CAPS_ST_ERROR) != 0U;
        out->running  = (st & CAPS_ST_RUNNING) != 0U;
        if (slider == CAPS_SLIDER_UNTOUCHED) {
            out->slider_touched = false;
            out->slider         = 0U;
        } else {
            out->slider_touched = true;
            out->slider = (uint8_t)((slider > CAPS_SLIDER_MAX) ? CAPS_SLIDER_MAX
                                                               : slider);
        }
    } else {
        out->snapshot       = false;
        out->proto          = 0U;
        out->btn0           = buf[0];
        out->btn1           = buf[1];
        out->switches       = 0U;
        out->slider_touched = true;
        out->slider         = buf[2];
        out->error          = false;
        out->running        = false;
    }
}

#endif /* CM55_CAPSENSE_FRAME_H */
