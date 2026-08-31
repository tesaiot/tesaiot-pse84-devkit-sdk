/*******************************************************************************
 * File:    pca9685.h
 *
 * Purpose: Register-level driver for the NXP PCA9685 16-channel 12-bit PWM
 *          controller, as used by the Adafruit Motor Shield v2.
 *
 * Design:  This translation unit contains NO PSoC / ModusToolbox / FreeRTOS API
 *          calls. The caller injects a bus vtable, so the same object code runs
 *          on CM33_NS, on CM55, behind an IPC proxy, or against a host mock in
 *          unit tests.
 *
 *          All writes pass through a shadow copy of the 16 channel register
 *          pairs. A write whose value already matches the shadow is dropped.
 *          This is not an optimisation for its own sake: on the TESAIoT Dev Kit
 *          this device shares SCB5 with the display and touch panel, and the
 *          driver executes inside the CM55 GFX task, so every avoided 0.6 ms
 *          transaction is 0.6 ms the display does not stall for.
 *
 * Ref:     NXP PCA9685 data sheet Rev. 4 (16 April 2015).
 ******************************************************************************/

#ifndef PCA9685_H
#define PCA9685_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Device constants
 ******************************************************************************/

/** Number of PWM output channels. */
#define PCA9685_CHANNEL_COUNT       16U

/** The counter is 12-bit; 4096 is not a duty value but the "full on"/"full off"
 *  flag bit (bit 12 of the ON_H / OFF_H register pair). */
#define PCA9685_FULL                4096U

/** Duty range accepted by pca9685_set_duty(): 0 .. PCA9685_FULL inclusive. */
#define PCA9685_DUTY_MAX            PCA9685_FULL

/** Internal oscillator nominal frequency. Real parts are specified at 25 MHz
 *  +/- 2 MHz, so a caller that needs an accurate period should measure one
 *  part and pass the measured value to pca9685_set_osc_freq(). */
#define PCA9685_OSC_HZ_NOMINAL      25000000UL

/** Prescaler limits from the data sheet (section 7.3.5). PRE_SCALE < 3 is
 *  rejected by the hardware. */
#define PCA9685_PRESCALE_MIN        3U
#define PCA9685_PRESCALE_MAX        255U

/** Output frequency bounds implied by the prescaler limits at 25 MHz. */
#define PCA9685_FREQ_HZ_MIN         24U
#define PCA9685_FREQ_HZ_MAX         1526U

/*******************************************************************************
 * Status
 ******************************************************************************/

typedef enum
{
    PCA9685_OK = 0,          /**< Success. */
    PCA9685_ERR_PARAM,       /**< A caller argument was out of range. */
    PCA9685_ERR_IO,          /**< The bus vtable reported a transfer failure. */
    PCA9685_ERR_NOT_READY,   /**< Device used before a successful init. */
    PCA9685_ERR_NO_DEVICE,   /**< Probe found nothing at the configured address. */
    PCA9685_ERR_EXTCLK,      /**< MODE1.EXTCLK is set; see pca9685_init(). */
} pca9685_status_t;

/*******************************************************************************
 * Bus abstraction
 *
 * Both transfer callbacks return 0 for success and any non-zero value for
 * failure; the driver does not interpret the non-zero value. Addresses are
 * 7-bit and unshifted -- the transport is responsible for placing the R/W bit.
 ******************************************************************************/

typedef struct
{
    int  (*write)(void *ctx, uint8_t addr7,
                  const uint8_t *data, size_t len);

    int  (*write_read)(void *ctx, uint8_t addr7,
                       const uint8_t *wdata, size_t wlen,
                       uint8_t *rdata, size_t rlen);

    /** Busy-tolerant short delay. Must be safe to call from the caller's task
     *  context; the driver only ever asks for delays of a few hundred
     *  microseconds. May be NULL, in which case the driver substitutes a
     *  conservative software spin -- see pca9685_set_freq(). */
    void (*delay_us)(void *ctx, uint32_t us);

    void *ctx;
} pca9685_bus_t;

/*******************************************************************************
 * Device instance
 *
 * Treat as opaque. Declared here so callers can embed it by value rather than
 * heap-allocating; the motor shield driver holds one.
 ******************************************************************************/

typedef struct
{
    pca9685_bus_t bus;
    uint8_t       addr7;
    bool          ready;

    uint32_t      osc_hz;      /**< Oscillator frequency used for prescaler maths. */
    uint16_t      freq_hz;     /**< Last frequency successfully programmed. */

    /* Shadow of the 16 LEDn_ON / LEDn_OFF register pairs. Only meaningful
     * while shadow_valid is true; any I/O error invalidates it so the next
     * write is unconditional. */
    uint16_t      shadow_on[PCA9685_CHANNEL_COUNT];
    uint16_t      shadow_off[PCA9685_CHANNEL_COUNT];
    bool          shadow_valid;
} pca9685_t;

/*******************************************************************************
 * API
 ******************************************************************************/

/**
 * Bind a device instance to a bus and bring it to a known state.
 *
 * On success the output frequency has been programmed and every channel is
 * driven full off.
 *
 * The frequency is a REQUIRED argument rather than a follow-up call. After a
 * power-on reset the part runs at its default prescaler, which is 200 Hz
 * nominal and 196.888 Hz in reality -- a 5.08 ms period against a small brushed
 * motor's 0.2-1 ms electrical time constant. That is maximum current ripple and
 * an audible growl. Making the caller pass a frequency removes the failure mode
 * where a driver forgets to set one and ships something that merely sounds bad.
 *
 * Also rejects a device whose MODE1.EXTCLK is set. EXTCLK is a sticky bit that
 * cannot be cleared by writing zero -- only a power cycle or a general-call
 * software reset clears it. On a part in that state the prescaler maths is
 * meaningless and PWM does not run, yet pca9685_probe() still passes, so the
 * failure would otherwise be silent.
 *
 * Does NOT emit an I2C general-call software reset. The general-call address
 * 0x00 is answered by every device on the bus, and on this board that bus is
 * shared with the display and touch controllers.
 *
 * @param dev      Instance to initialise. Must not be NULL.
 * @param bus      Bus vtable. write and write_read must both be non-NULL.
 * @param addr7    Unshifted 7-bit address. The Motor Shield v2 ships at 0x60.
 * @param freq_hz  Output frequency; clamped as documented on pca9685_set_freq().
 */
pca9685_status_t pca9685_init(pca9685_t *dev,
                              const pca9685_bus_t *bus,
                              uint8_t addr7,
                              uint16_t freq_hz);

/**
 * Check that a device actually answers at the configured address.
 *
 * Reads MODE1 back. Returns PCA9685_ERR_NO_DEVICE when the read fails, which
 * on a shared bus is the only honest presence test available -- the v2 shield
 * carries no identification beyond this ACK.
 */
pca9685_status_t pca9685_probe(pca9685_t *dev);

/**
 * Override the assumed oscillator frequency before calling pca9685_set_freq().
 *
 * Parts vary by +/- 2 MHz. Passing a per-unit measured value is the only way to
 * hit an exact output period; it matters for RC servos and not at all for DC
 * motor drive.
 */
pca9685_status_t pca9685_set_osc_freq(pca9685_t *dev, uint32_t osc_hz);

/**
 * Program the PWM output frequency, in hertz.
 *
 * Clamped to [PCA9685_FREQ_HZ_MIN, PCA9685_FREQ_HZ_MAX]. The upper bound is a
 * property of the part, not a policy choice: the minimum legal prescaler of 3
 * puts the ceiling at 25 MHz / (4096 * 4) = 1526 Hz. Adafruit's motor library
 * defaults to 1600 Hz, which this part cannot produce -- upstream has always
 * silently run at 1526 Hz. Call pca9685_get_freq() to read back what was
 * actually programmed rather than assuming the request was honoured.
 *
 * Changing the
 * prescaler requires the oscillator to be stopped, so this call puts the part
 * to sleep, writes PRE_SCALE, wakes it, and honours the data-sheet requirement
 * to wait 500 us for the oscillator to stabilise before setting RESTART.
 *
 * Note: upstream's Adafruit_MS_PWMServoDriver multiplies the requested
 * frequency by 0.9 before computing the prescaler. That is an undocumented
 * fudge for one batch of parts and it makes the programmed frequency differ
 * from the requested one by 11% with no way for the caller to find out. It is
 * deliberately not reproduced here; use pca9685_set_osc_freq() instead, which
 * corrects the same error at its actual source.
 */
pca9685_status_t pca9685_set_freq(pca9685_t *dev, uint16_t freq_hz);

/**
 * Write a channel's raw ON and OFF phase values.
 *
 * @param on   Rising-edge count, 0..4095, or PCA9685_FULL for "full on".
 * @param off  Falling-edge count, 0..4095, or PCA9685_FULL for "full off".
 *
 * "Full off" wins over "full on" in the hardware, matching the data sheet.
 * Suppressed entirely when the shadow already holds these values.
 */
pca9685_status_t pca9685_set_channel(pca9685_t *dev, uint8_t ch,
                                     uint16_t on, uint16_t off);

/**
 * Set a channel's duty cycle with the rising edge at phase 0.
 *
 * @param duty  0 = full off, PCA9685_FULL = full on, anything between is a
 *              12-bit duty. Using the full-on/full-off flags at the extremes
 *              avoids the 1-count glitch a plain compare value would leave.
 */
pca9685_status_t pca9685_set_duty(pca9685_t *dev, uint8_t ch, uint16_t duty);

/**
 * Convenience wrapper: drive a channel as a logic level.
 * true  -> full on, false -> full off.
 */
pca9685_status_t pca9685_set_pin(pca9685_t *dev, uint8_t ch, bool level);

/**
 * Drive every channel full off.
 *
 * Uses the ALL_LED registers, so this is a single 5-byte transaction rather
 * than 16 -- which is what makes it safe to call from an emergency stop path
 * inside the GFX task. The shadow is updated to match.
 */
pca9685_status_t pca9685_all_off(pca9685_t *dev);

/**
 * Discard the shadow so the next write to every channel is unconditional.
 * Call after anything that may have changed the device state behind the
 * driver's back.
 */
void pca9685_invalidate_shadow(pca9685_t *dev);

/**
 * The frequency the part is ACTUALLY producing, or 0 before init.
 *
 * This is recomputed from the prescaler that was written, not echoed back from
 * the request, because the prescaler is an integer and most requests are not
 * representable. Asking for 1000 Hz yields prescale 5, which is 1017 Hz. A
 * caller that needs to display or reason about the real period must use this.
 */
uint16_t pca9685_get_freq(const pca9685_t *dev);

/** True once pca9685_init() has succeeded. */
bool pca9685_is_ready(const pca9685_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* PCA9685_H */
