/*******************************************************************************
 * hid_f310_report.h — Layer 1: Logitech F310 HID report layout (pure data).
 *
 * Defines the raw 8-byte HID report structure emitted by Logitech F310 in
 * DirectInput mode (VID 0x046D / PID 0xC216), plus the bit-mask constants
 * for buttons and hat-switch values.
 *
 * This header is L1 PUBLIC API — libc only, no USB stack, no FreeRTOS,
 * no LVGL, no Cypress HAL. Suitable for host-side unit tests.
 ******************************************************************************/
#ifndef HID_F310_REPORT_H
#define HID_F310_REPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* F310 USB identification — DirectInput mode only (XInput mode emits a
 * different report layout and is not supported here). */
#define F310_VID                (0x046D)    /* Logitech */
#define F310_PID_DINPUT         (0xC216)    /* F310 DirectInput */
#define F310_PID_XINPUT         (0xC21D)    /* F310 XInput (not supported) */

/* F310 report size (bytes). HID reports shorter than this are rejected. */
#define F310_REPORT_SIZE        (8)

/* Byte 0..3: analog sticks (0x00-0xFF, neutral 0x80 for X-axes / 0x7F for Y).
 *
 * Byte 4: hat[0:3] + face buttons X[4] A[5] B[6] Y[7]
 * Byte 5: shoulders + system  LB[0] RB[1] LT[2] RT[3] Back[4] Start[5] L3[6] R3[7]
 * Byte 6: Mode switch (1 = mode LED on)
 * Byte 7: Status / reserved
 */

/* Button masks for byte 4 (face buttons + hat in low nibble) */
#define F310_HAT_MASK           (0x0F)
#define F310_BTN_X              (1u << 4)
#define F310_BTN_A              (1u << 5)
#define F310_BTN_B              (1u << 6)
#define F310_BTN_Y              (1u << 7)

/* Button masks for byte 5 (shoulders + system) */
#define F310_BTN_LB             (1u << 0)
#define F310_BTN_RB             (1u << 1)
#define F310_BTN_LT             (1u << 2)
#define F310_BTN_RT             (1u << 3)
#define F310_BTN_BACK           (1u << 4)
#define F310_BTN_START          (1u << 5)
#define F310_BTN_L3             (1u << 6)
#define F310_BTN_R3             (1u << 7)

/* Hat switch enumeration (clockwise from up; 8 = neutral / released) */
#define F310_HAT_UP             (0)
#define F310_HAT_UP_RIGHT       (1)
#define F310_HAT_RIGHT          (2)
#define F310_HAT_DOWN_RIGHT     (3)
#define F310_HAT_DOWN           (4)
#define F310_HAT_DOWN_LEFT      (5)
#define F310_HAT_LEFT           (6)
#define F310_HAT_UP_LEFT        (7)
#define F310_HAT_NEUTRAL        (8)

/*******************************************************************************
 * NUBWO NJ43 DirectInput Report (8 bytes) — DragonRise / "PC TWIN SHOCK"
 *
 * The NJ43 is a plain USB HID Joystick (Usage Page 0x01 / Usage 0x04), so it
 * rides the SAME emUSB-Host USBH_HID path as the F310 — no XInput, no GIP/BULK.
 * Its byte layout DIFFERS from the F310, so it is translated into f310_report_t
 * by nj43_decode_to_f310() (L2 USB adapter) before the rest of the system sees
 * it. These constants describe that raw NJ43 byte layout.
 *
 * Byte 0: Left Stick X   (0x00-0xFF, ~0x80 center)
 * Byte 1: Left Stick Y   (0x00-0xFF, ~0x80 center)
 * Byte 2: constant / padding (ignored)
 * Byte 3: Right Stick X  (Usage Z)
 * Byte 4: Right Stick Y  (Usage Rz)
 * Byte 5: low nibble = Hat (0..7, 8=neutral); high nibble = Button 1..4
 * Byte 6: Button 5..12
 * Byte 7: vendor bits (ignored)
 *
 * HW status (TESAIoT Dev Kit + NUBWO NJ43, 2026-07-17, via openocd on the
 * live driver state): the stream and decode path are PROVEN — sequence ==
 * report_cnt (mod 256) across samples, ~33 reports/s, zero size-guard
 * rejects; idle decodes to sticks 0x7F/0x7F 0x80/0x80 + hat neutral, all
 * buttons released. So the AXIS offsets (0/1/3/4) and the hat nibble are
 * consistent with this map at idle. The BUTTON source bytes (5 high nibble,
 * 6) and the button ORDER below still need a physical button press to
 * verify — read joystick_state_t.raw[] (last wire bytes, pre-decode) while
 * pressing each button, or the "raw:" diag line on the Joystick page.
 *******************************************************************************/

/* NJ43 USB identification (DragonRise clone — gate on VID *and* PID since the
 * 0x0079 VID is shared across many unrelated clone pads). */
#define NJ43_VID                (0x0079)
#define NJ43_PID                (0x0006)

/* NJ43 raw report byte offsets */
#define NJ43_OFF_LEFT_X         (0)
#define NJ43_OFF_LEFT_Y         (1)
#define NJ43_OFF_RIGHT_X        (3)
#define NJ43_OFF_RIGHT_Y        (4)
#define NJ43_OFF_HAT_BTN14      (5)   /* low nibble hat, high nibble btn 1..4 */
#define NJ43_OFF_BTN512         (6)   /* btn 5..12 */
#define NJ43_HAT_MASK           (0x0F)
#define NJ43_HAT_NEUTRAL        (0x0F) /* some units report 0x0F when centered */

/* NJ43 HID button index -> physical button (1-indexed as in the HID descriptor).
 * ASSUMED DragonRise/TWIN-SHOCK order — VERIFY ON HARDWARE and swap if a press
 * lights the wrong action. Bit position within the source byte is (index-1). */
/* Face-button ORDER verified on HW 2026-07-18 (raw byte5 under live presses:
 * Y=0x10 B=0x20 A=0x40 X=0x80). Only the order changed; the byte offsets above
 * were already correct — do NOT shift them. */
#define NJ43_BTN_Y              (1)   /* byte5 bit4 (0x10) */
#define NJ43_BTN_B              (2)   /* byte5 bit5 (0x20) */
#define NJ43_BTN_A              (3)   /* byte5 bit6 (0x40) */
#define NJ43_BTN_X              (4)   /* byte5 bit7 (0x80) */
#define NJ43_BTN_LB             (5)   /* byte6 bit0 */
#define NJ43_BTN_RB             (6)   /* byte6 bit1 */
#define NJ43_BTN_LT             (7)   /* byte6 bit2 */
#define NJ43_BTN_RT             (8)   /* byte6 bit3 */
#define NJ43_BTN_BACK           (9)   /* byte6 bit4 */
#define NJ43_BTN_START          (10)  /* byte6 bit5 */
#define NJ43_BTN_L3             (11)  /* byte6 bit6 */
#define NJ43_BTN_R3             (12)  /* byte6 bit7 */

/* Raw 8-byte report struct as emitted by the device. */
typedef struct __attribute__((packed)) {
    uint8_t left_x;     /* analog X, 0x80 = neutral */
    uint8_t left_y;     /* analog Y, 0x7F = neutral (Y is inverted) */
    uint8_t right_x;
    uint8_t right_y;
    uint8_t buttons1;   /* hat[0:3] + X[4] A[5] B[6] Y[7] */
    uint8_t buttons2;   /* LB[0] RB[1] LT[2] RT[3] Back[4] Start[5] L3[6] R3[7] */
    uint8_t mode;
    uint8_t status;
} f310_report_t;

#ifdef __cplusplus
}
#endif

#endif /* HID_F310_REPORT_H */
