/*******************************************************************************
 * cm55_sensor_poll.h — CM55-local base-board sensor reading for the
 *                       TESAIoT Dev Kit (AI Kit SoM + QWA309 base board).
 *
 * On the mated board the QWA309 CapSense (external PSoC 4000T) and the four
 * potentiometers (VR1-4) are read directly on CM55, sharing the display I2C /
 * AutAnalog-SAR context the way the Eva Kit does. Data is fed into
 * ipc_sensorhub via the local feed API (no CM33->CM55 IPC needed), which
 * populates the Controls page and the pot/capsense MicroPython snapshot.
 *
 * Unlike the Eva Kit driver this variant does NOT read the BMI270 — the AI Kit
 * already services the IMU on CM33_NS, so reading it here would double-drive
 * the sensor. Only the QWA309-specific pots + CapSense live here.
 *
 * Capabilities (guarded by BSP_HAS_QWA309_BASEBOARD + BSP_HAS_* flags):
 *   - Potentiometers x4 (VR1-4): AutAnalog SAR GPIO ch 4-7  (P15.4-7)
 *   - CapSense PSoC 4000T (I2C 0x08): 2 buttons + slider (+ SW1-SW4, 0x0D/0x0E fw)
 *
 * Call cm55_sensor_poll_init() after display I2C is ready.
 * Call cm55_sensor_poll_tick() from an LVGL timer (~200ms).
 ******************************************************************************/

#ifndef CM55_SENSOR_POLL_H
#define CM55_SENSOR_POLL_H

#include <stdbool.h>
#include <stdint.h>

/** Number of QWA309 potentiometers (VR1-4 on SAR GPIO ch 4-7). */
#define QWA309_POT_COUNT   (4U)

/** Initialize CM55-local base-board sensor reading. Call after display I2C is
 *  ready. Returns true if at least one capability initialized. */
bool cm55_sensor_poll_init(void);

/** Poll base-board sensors and feed data to sensorhub. Call every ~200ms. */
void cm55_sensor_poll_tick(void);

/**
 * CapSense-only tick — call from its OWN LVGL timer at ~50 ms (game rate).
 * The I2C transaction is time-bounded (2 ms/byte-call) and backs off for
 * ~500 ms after a failure, so a sick 4000T cannot starve the GFX task
 * (the old unbounded read at MAX-1 priority was the prime suspect for the
 * USB joystick HID stream stall).
 */
void cm55_capsense_tick(void);

/** Return init status flags: bit1=capsense, bit2=pot. */
uint8_t cm55_sensor_poll_status(void);

/** Copy the latest raw 12-bit reading of all four pots into out[QWA309_POT_COUNT].
 *  Returns the number of channels written (0 if pots not enabled/ready).
 *  Intended for the QWA309 MicroPython pot API (pots.read_all()). */
uint8_t cm55_pot_read_all(uint16_t out[QWA309_POT_COUNT]);

/** Write diagnostic info into buf. Returns bytes written. */
uint16_t cm55_sensor_poll_diag(uint8_t *buf, uint16_t max_len);

/** What the CapSense controller at I2C 0x08 reported, for a CM55 page.
 *  SW1-SW4 are wired to the CapSense controller, not to the E84, so they
 *  are known only when its firmware speaks the 0x0D/0x0E snapshot protocol
 *  (see cm55_capsense_frame.h). */
typedef struct {
    bool    answered;        /**< the controller answered the start-up probe */
    bool    live;            /**< answered, and the last read succeeded */
    uint8_t proto;           /**< 0x0D or 0x0E; 0 = legacy frame or unknown */
    bool    switches_valid;  /**< proto is 0x0D/0x0E, so `switches` is meaningful */
    uint8_t switches;        /**< CAPS_SW_* bits: SW1/SW2 pressed, SW3/SW4 ON */
    bool    slider_touched;  /**< false while a snapshot slider is untouched */
    bool    fw_error;        /**< the controller sets its own error flag */
} cm55_capsense_info_t;

/** Fill *out from the last CapSense read. CM55 only; call from the GFX task,
 *  the task that performs the reads. Returns false only when out is NULL.
 *  The probe runs once at start-up: a controller that did not answer then
 *  (SW12 OFF) stays unanswered until reset. */
bool cm55_capsense_info(cm55_capsense_info_t *out);

#endif /* CM55_SENSOR_POLL_H */
