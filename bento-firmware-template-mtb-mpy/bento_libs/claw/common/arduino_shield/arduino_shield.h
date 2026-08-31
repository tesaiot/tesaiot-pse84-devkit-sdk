/*******************************************************************************
 * File:    arduino_shield.h
 *
 * Purpose: Makes a board's Arduino Uno R3 expansion header a first-class thing
 *          the firmware can reason about, so that shield drivers can be written
 *          against logical pin names instead of PSoC ports.
 *
 * Shape:   Three layers, and this is the middle one.
 *
 *            shield driver        (shield_motor_v2, shield_mikrobus, ...)
 *                  |               speaks ARDUINO_D9, never P15.2
 *            arduino_shield        <- this file: table, capabilities, registry
 *                  |
 *            board descriptor      (arduino_shield_qwa309.c and friends)
 *                                   the only place physical pins are named
 *
 * Why capabilities are data:
 *          A header pin can be missing for several unrelated reasons -- the
 *          board never routed it, the silicon has no PWM path to it, or the
 *          part has an erratum that stops it driving at all. On the QWA309 all
 *          three occur. Encoding that as a bitmask per pin means a shield
 *          driver can be told "no" at attach time, once, with a reason --
 *          instead of every driver growing its own #if ladder and discovering
 *          the problem as silence on a bench.
 *
 * Portability:
 *          This translation unit contains no platform API. Physical access is
 *          delegated to an ops vtable supplied by the board descriptor, which
 *          keeps the table logic host-testable.
 ******************************************************************************/

#ifndef ARDUINO_SHIELD_H
#define ARDUINO_SHIELD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Logical pins
 ******************************************************************************/

typedef enum
{
    ARDUINO_D0 = 0, ARDUINO_D1, ARDUINO_D2,  ARDUINO_D3,
    ARDUINO_D4,     ARDUINO_D5, ARDUINO_D6,  ARDUINO_D7,
    ARDUINO_D8,     ARDUINO_D9, ARDUINO_D10, ARDUINO_D11,
    ARDUINO_D12,    ARDUINO_D13,
    ARDUINO_A0,     ARDUINO_A1, ARDUINO_A2,  ARDUINO_A3,
    ARDUINO_A4,     ARDUINO_A5,
    ARDUINO_PIN_COUNT
} arduino_pin_t;

/*******************************************************************************
 * Capabilities
 *
 * A pin is usable for a purpose only if the corresponding bit is set AND
 * ARDUINO_CAP_ERRATUM is clear.
 ******************************************************************************/

#define ARDUINO_CAP_ROUTED       0x0001U  /**< Physically connected to the MCU. */
#define ARDUINO_CAP_DIGITAL_IN   0x0002U
#define ARDUINO_CAP_DIGITAL_OUT  0x0004U
#define ARDUINO_CAP_PWM          0x0008U  /**< A hardware timer can reach it. */
#define ARDUINO_CAP_ADC          0x0010U
#define ARDUINO_CAP_I2C          0x0020U
#define ARDUINO_CAP_SPI          0x0040U
#define ARDUINO_CAP_UART         0x0080U

/**
 * Known-broken despite everything else the mask claims.
 *
 * Reserved for defects proven on hardware, not for suspicions. On the QWA309
 * this marks D2 and D3, whose port configuration register does not latch an
 * output drive -- the pin looks perfectly ordinary in the datasheet and simply
 * does not work.
 */
#define ARDUINO_CAP_ERRATUM      0x8000U

/**
 * Level shifter in the path.
 *
 * Not a defect, but it constrains what a driver may do: a pin behind an
 * auto-direction shifter must not carry a pull-up or pull-down stiffer than
 * about 50 kOhm, or the shifter's direction sensing latches the wrong way.
 */
#define ARDUINO_CAP_LEVEL_SHIFTED 0x0100U

/*******************************************************************************
 * Status
 ******************************************************************************/

typedef enum
{
    ARDUINO_OK = 0,
    ARDUINO_ERR_PARAM,        /**< Bad pin id or null argument. */
    ARDUINO_ERR_NO_BOARD,     /**< arduino_shield_set_board() not called. */
    ARDUINO_ERR_NOT_ROUTED,   /**< The board does not connect this pin. */
    ARDUINO_ERR_NO_CAP,       /**< Routed, but cannot do what was asked. */
    ARDUINO_ERR_ERRATUM,      /**< Routed and capable on paper; known broken. */
    ARDUINO_ERR_IN_USE,       /**< Already claimed by another shield. */
    ARDUINO_ERR_IO,           /**< The board's ops reported a failure. */
    ARDUINO_ERR_FULL,         /**< Shield registry is full. */
    /** The board is capable, but this board's driver has not implemented it.
     *  Kept distinct from ARDUINO_ERR_NO_CAP on purpose: "the hardware cannot"
     *  and "nobody has written it yet" lead to completely different fixes, and
     *  conflating them would send someone hunting the schematic for a pin that
     *  is fine. */
    ARDUINO_ERR_UNSUPPORTED,
} arduino_status_t;

typedef enum
{
    ARDUINO_MODE_INPUT = 0,
    ARDUINO_MODE_INPUT_PULLUP,
    ARDUINO_MODE_OUTPUT,
    ARDUINO_MODE_ANALOG,
} arduino_mode_t;

/*******************************************************************************
 * Board-supplied physical access
 *
 * Every callback returns 0 on success, non-zero on failure. The core never
 * interprets the failure value.
 ******************************************************************************/

typedef struct
{
    int (*pin_mode)(void *ctx, arduino_pin_t pin, arduino_mode_t mode);
    int (*digital_write)(void *ctx, arduino_pin_t pin, bool level);
    int (*digital_read)(void *ctx, arduino_pin_t pin, bool *level);

    /** Start or update hardware PWM. duty is in tenths of a percent, 0..1000. */
    int (*pwm_set)(void *ctx, arduino_pin_t pin, uint32_t freq_hz, uint16_t duty_permille);
    int (*pwm_stop)(void *ctx, arduino_pin_t pin);

    int (*adc_read)(void *ctx, arduino_pin_t pin, uint16_t *raw);

    void *ctx;
} arduino_ops_t;

/*******************************************************************************
 * Header I2C
 *
 * Deliberately the same shape as the PCA9685 driver's bus vtable so a shield
 * driver can hand it straight through. Kept here rather than imported so that
 * the core does not depend on any particular shield.
 ******************************************************************************/

typedef struct
{
    int  (*write)(void *ctx, uint8_t addr7, const uint8_t *data, size_t len);
    int  (*write_read)(void *ctx, uint8_t addr7,
                       const uint8_t *wdata, size_t wlen,
                       uint8_t *rdata, size_t rlen);
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} arduino_i2c_t;

/*******************************************************************************
 * Board descriptor
 ******************************************************************************/

typedef struct
{
    const char          *name;      /**< e.g. "QWA309". Shown in diagnostics. */

    /** Capability mask per logical pin, indexed by arduino_pin_t. */
    const uint16_t      *caps;

    /** Physical pin name per logical pin, e.g. "P13.3", or NULL. Diagnostics
     *  only -- being able to print the real pin turns a bring-up mystery into
     *  a one-line answer. */
    const char * const  *pin_names;

    const arduino_ops_t *ops;

    /** Header I2C, or NULL if the board does not route it. */
    const arduino_i2c_t *i2c;

    /** True when the header's IOREF pin is connected. Shields that auto-select
     *  their logic voltage from IOREF cannot do so on a board where this is
     *  false, and must be configured by hand. */
    bool                 ioref_present;

    /** Logic voltage the header actually presents, in millivolts. */
    uint16_t             logic_mv;
} arduino_board_t;

/*******************************************************************************
 * Shield registration
 ******************************************************************************/

#define ARDUINO_SHIELD_MAX          4U
#define ARDUINO_SHIELD_MAX_PINS     12U

typedef struct
{
    const char   *name;

    /** Pins the shield must have, with the capability each is needed for.
     *  Checked once at attach; a shield never has to test a pin itself. */
    arduino_pin_t pins[ARDUINO_SHIELD_MAX_PINS];
    uint16_t      pin_caps[ARDUINO_SHIELD_MAX_PINS];
    uint8_t       pin_count;

    /** True if the shield talks over the header I2C. */
    bool          needs_i2c;

    int  (*on_attach)(void *ctx);
    void (*on_detach)(void *ctx);
    void *ctx;
} arduino_shield_desc_t;

/*******************************************************************************
 * API
 ******************************************************************************/

/** Install the board descriptor. Call once during start-up, before any shield. */
arduino_status_t arduino_shield_set_board(const arduino_board_t *board);

/** The installed board, or NULL. */
const arduino_board_t *arduino_shield_board(void);

/** Capability mask for a pin; 0 if no board or the pin id is out of range. */
uint16_t arduino_shield_caps(arduino_pin_t pin);

/** Physical pin name for diagnostics, never NULL -- falls back to "?". */
const char *arduino_shield_pin_name(arduino_pin_t pin);

/**
 * Can this pin do this, right now, on this board?
 *
 * @param want  One or more ARDUINO_CAP_* bits, all of which must be present.
 * @return ARDUINO_OK, or the specific reason it cannot: ARDUINO_ERR_NOT_ROUTED,
 *         ARDUINO_ERR_NO_CAP or ARDUINO_ERR_ERRATUM. Distinguishing these
 *         matters -- "the board never wired it" and "the silicon is broken"
 *         lead to different fixes.
 */
arduino_status_t arduino_shield_check(arduino_pin_t pin, uint16_t want);

/* --- Pin access. Each validates capability before touching hardware. ------- */

arduino_status_t arduino_shield_pin_mode(arduino_pin_t pin, arduino_mode_t mode);
arduino_status_t arduino_shield_write(arduino_pin_t pin, bool level);
arduino_status_t arduino_shield_read(arduino_pin_t pin, bool *level);
arduino_status_t arduino_shield_pwm(arduino_pin_t pin, uint32_t freq_hz,
                                    uint16_t duty_permille);
arduino_status_t arduino_shield_pwm_stop(arduino_pin_t pin);
arduino_status_t arduino_shield_adc(arduino_pin_t pin, uint16_t *raw);

/** The header I2C vtable, or NULL if this board does not route it. */
const arduino_i2c_t *arduino_shield_i2c(void);

/* --- Shields --------------------------------------------------------------- */

/**
 * Attach a shield.
 *
 * Validates every pin the shield declared against the board, and claims them so
 * a second shield cannot take the same pin. On any failure nothing is claimed
 * and on_attach is not called.
 *
 * @param failed_pin  Optional; on a pin-related failure, receives the offending
 *                    pin so the caller can name it in a message.
 */
arduino_status_t arduino_shield_attach(arduino_shield_desc_t *shield,
                                       arduino_pin_t *failed_pin);

/** Detach and release the shield's pins. */
arduino_status_t arduino_shield_detach(arduino_shield_desc_t *shield);

/** Number of shields currently attached. */
uint8_t arduino_shield_attached_count(void);

/** Human-readable form of a status, for logs and UI. Never NULL. */
const char *arduino_shield_strerror(arduino_status_t status);

/** Forget every attachment and the board. Test and shutdown helper. */
void arduino_shield_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ARDUINO_SHIELD_H */
