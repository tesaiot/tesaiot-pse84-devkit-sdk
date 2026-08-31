/*******************************************************************************
 * File:    motor_shield_v2.h
 *
 * Purpose: Adafruit Motor Shield v2 driver -- 4 DC motors or 2 stepper motors
 *          driven through a PCA9685 and two TB6612FNG H-bridges.
 *
 * Design:  Like the pca9685 layer beneath it, this file calls no platform API.
 *          Time enters through the caller: motor_shield_v2_tick() is handed a
 *          monotonic millisecond count. There is no delay, no sleep and no
 *          busy-wait anywhere in the implementation.
 *
 *          That is not stylistic. On the TESAIoT Dev Kit this device sits on
 *          SCB5, shared with the display and touch panel, and the driver runs
 *          inside the CM55 GFX task. Any blocking wait here freezes the screen.
 *
 * Budget:  At 100 kHz one PCA9685 channel write costs ~553 us. A DC command
 *          touches three channels; a microstep touches up to six. tick()
 *          therefore advances AT MOST ONE step per call, across all steppers,
 *          bounding its worst case to ~3.3 ms.
 *
 *          The consequence is a real speed ceiling, and it is a property of the
 *          bus rather than of the schedule -- moving this driver to its own
 *          task would not buy a single extra step per second. Callers should
 *          ask motor_shield_v2_max_rpm() and show the honest limit rather than
 *          silently running slower than requested.
 *
 * Safety:  The shield has NO over-current protection: no polyfuse, no sense
 *          resistor, and the TB6612's thermal shutdown is invisible to
 *          firmware. STBY is hard-wired to VCC, so there is no hardware
 *          standby either. motor_shield_v2_stop_all() is the only emergency
 *          stop that exists, and current limiting must be a policy this driver
 *          and its callers enforce.
 ******************************************************************************/

#ifndef MOTOR_SHIELD_V2_H
#define MOTOR_SHIELD_V2_H

#include "pca9685.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Constants
 ******************************************************************************/

/** Factory I2C address. Solder pads A0..A4 move it within 0x60..0x7F. */
#define MOTOR_SHIELD_V2_ADDR_DEFAULT    0x60U

#define MOTOR_SHIELD_V2_DC_COUNT        4U
#define MOTOR_SHIELD_V2_STEPPER_COUNT   2U

/** Microsteps per full step. The curve table is sized for this value. */
#define MOTOR_SHIELD_V2_MICROSTEPS      16U

/** DC speed is 8-bit for API compatibility with the Arduino library. */
#define MOTOR_SHIELD_V2_SPEED_MAX       255U

/**
 * Default PWM frequency.
 *
 * 1526 Hz is the highest the PCA9685 can produce: its minimum legal prescaler
 * of 3 fixes the ceiling at 25 MHz / (4096 * 4) = 1525.879 Hz. Adafruit's
 * library asks for 1600 Hz, which the part silently clamps to this same value.
 *
 * Be aware that this frequency sits in the band the human ear is most
 * sensitive to, and the PCA9685 cannot reach an inaudible one. Motor whine on
 * this shield is architectural, not a tuning mistake.
 */
#define MOTOR_SHIELD_V2_FREQ_DEFAULT_HZ 1526U

/**
 * Wall-clock cost of one PCA9685 channel write at 100 kHz, in microseconds.
 * Used by motor_shield_v2_max_rpm(); exposed so a UI can show its own budget.
 */
#define MOTOR_SHIELD_V2_CHANNEL_US      553U

/*******************************************************************************
 * Types
 ******************************************************************************/

typedef enum
{
    MOTOR_SHIELD_V2_OK = 0,
    MOTOR_SHIELD_V2_ERR_PARAM,      /**< Argument out of range. */
    MOTOR_SHIELD_V2_ERR_IO,         /**< Bus transfer failed. */
    MOTOR_SHIELD_V2_ERR_NOT_READY,  /**< Used before a successful init. */
    MOTOR_SHIELD_V2_ERR_NO_DEVICE,  /**< Nothing answered at the address. */
    MOTOR_SHIELD_V2_ERR_PORT_BUSY,  /**< Channel belongs to an attached stepper. */
    MOTOR_SHIELD_V2_ERR_NOT_ATTACHED, /**< Stepper port has no motor attached. */
} motor_shield_v2_status_t;

typedef enum
{
    MOTOR_SHIELD_V2_FORWARD = 1,
    MOTOR_SHIELD_V2_BACKWARD,
    /** Both inputs high: TB6612 short brake. Actively resists rotation.
     *  Adafruit's library defines this constant and then ignores it -- run()
     *  has no case for it. Here it does what its name says. */
    MOTOR_SHIELD_V2_BRAKE,
    /** Both inputs low AND duty forced to zero: the motor free-wheels.
     *  Adafruit's RELEASE clears the direction bits but leaves the PWM duty
     *  untouched, which is a brake, not a coast. */
    MOTOR_SHIELD_V2_RELEASE,
} motor_shield_v2_run_t;

typedef enum
{
    MOTOR_SHIELD_V2_SINGLE = 1,     /**< One coil energised. Lowest torque. */
    MOTOR_SHIELD_V2_DOUBLE,         /**< Two coils. Default; best torque/cost. */
    MOTOR_SHIELD_V2_INTERLEAVE,     /**< Alternates: half-stepping. */
    MOTOR_SHIELD_V2_MICROSTEP,      /**< Sine-weighted. Smooth but bus-hungry. */
} motor_shield_v2_style_t;

/** Per-DC-channel state, readable for a status display. */
typedef struct
{
    uint8_t               speed;    /**< Last commanded, 0..255. */
    motor_shield_v2_run_t run;      /**< Last commanded direction/action. */
} motor_shield_v2_dc_t;

/** Per-stepper-port state. Treat as opaque. */
typedef struct
{
    bool     attached;
    uint16_t revsteps;              /**< Full steps per revolution. */
    uint16_t rpm;
    uint16_t cur_micro;             /**< Phase, 0 .. MICROSTEPS*4-1. */
    uint8_t  style;
    uint8_t  dir;
    bool     running;
    uint32_t steps_left;
    uint32_t interval_ms;           /**< Between steps of the chosen style. */
    uint32_t next_due_ms;
} motor_shield_v2_stepper_t;

typedef struct
{
    pca9685_t                 pwm;
    bool                      ready;
    motor_shield_v2_dc_t      dc[MOTOR_SHIELD_V2_DC_COUNT];
    motor_shield_v2_stepper_t st[MOTOR_SHIELD_V2_STEPPER_COUNT];

    /** Dead-man timer. Zero disables it. When non-zero, tick() stops
     *  everything if no command has arrived within this many milliseconds
     *  while something is running. The shield cannot report a stall or a
     *  thermal trip, so a lost controller would otherwise leave a motor
     *  driving into a jam indefinitely. */
    uint32_t                  watchdog_ms;
    uint32_t                  last_cmd_ms;
    bool                      watchdog_tripped;
} motor_shield_v2_t;

/*******************************************************************************
 * Lifecycle
 ******************************************************************************/

/**
 * Bring the shield up and park every output.
 *
 * @param freq_hz  PWM frequency. Pass MOTOR_SHIELD_V2_FREQ_DEFAULT_HZ unless
 *                 there is a reason not to; use pca9685_get_freq() on
 *                 &dev->pwm afterwards to learn what was actually achieved.
 *
 * Parking the outputs is not merely tidy. A warm reset does not reset the
 * PCA9685 -- its own reset needs VDD below 0.2 V -- so a motor commanded before
 * a firmware reload is still turning when this function is entered. Call it as
 * early in start-up as the bus allows.
 */
motor_shield_v2_status_t motor_shield_v2_init(motor_shield_v2_t *dev,
                                              const pca9685_bus_t *bus,
                                              uint8_t addr7,
                                              uint16_t freq_hz);

/** Does anything answer at the configured address? */
motor_shield_v2_status_t motor_shield_v2_probe(motor_shield_v2_t *dev);

/**
 * Stop everything: all duties to zero, all direction pins low, all steppers
 * halted and released.
 *
 * One PCA9685 ALL_LED transaction, ~0.55 ms, which is what makes it safe to
 * call from a UI event handler or a page-teardown path. This is the only
 * emergency stop the hardware offers.
 */
motor_shield_v2_status_t motor_shield_v2_stop_all(motor_shield_v2_t *dev);

/*******************************************************************************
 * DC motors -- ports 1..4
 ******************************************************************************/

/**
 * Set a DC motor's speed.
 *
 * @param speed 0..255, mapped to the 12-bit compare as (speed*4095+127)/255 so
 *              that 255 reaches full scale exactly. Adafruit's `speed * 16`
 *              tops out at 4080 and never reaches 100%.
 *
 * Returns MOTOR_SHIELD_V2_ERR_PORT_BUSY if the port belongs to an attached
 * stepper: motors 1-2 form stepper port 1, motors 3-4 form stepper port 2.
 */
motor_shield_v2_status_t motor_shield_v2_dc_speed(motor_shield_v2_t *dev,
                                                  uint8_t motor,
                                                  uint8_t speed,
                                                  uint32_t now_ms);

/**
 * Set direction / brake / release. See motor_shield_v2_run_t.
 *
 * A FORWARD<->BACKWARD reversal while the motor is driven COASTS TO ZERO DUTY
 * FIRST and leaves it there, so the cached speed becomes 0 and the caller must
 * command a speed again to resume. Flipping the inputs at speed is a plugging
 * brake -- the bridge drives against back-EMF and the winding sees close to
 * twice supply -- and on a shield with no polyfuse, no sense resistor and an
 * invisible thermal trip that spike has nothing to catch it. Restoring the duty
 * straight after the flip would simply be the same event one transaction later,
 * which is why it is not restored.
 */
motor_shield_v2_status_t motor_shield_v2_dc_run(motor_shield_v2_t *dev,
                                                uint8_t motor,
                                                motor_shield_v2_run_t cmd,
                                                uint32_t now_ms);

/** Read back what was last commanded. NULL-safe; returns NULL if out of range. */
const motor_shield_v2_dc_t *motor_shield_v2_dc_state(const motor_shield_v2_t *dev,
                                                     uint8_t motor);

/*******************************************************************************
 * Steppers -- ports 1..2
 ******************************************************************************/

/**
 * Claim a stepper port. Port 1 takes motor terminals M1+M2, port 2 takes M3+M4;
 * those DC channels are refused while the port is attached.
 *
 * @param revsteps Full steps per revolution, e.g. 200 for a 1.8-degree motor.
 */
motor_shield_v2_status_t motor_shield_v2_stepper_attach(motor_shield_v2_t *dev,
                                                        uint8_t port,
                                                        uint16_t revsteps);

/** Release the port and free its DC channels. Coils are de-energised. */
motor_shield_v2_status_t motor_shield_v2_stepper_detach(motor_shield_v2_t *dev,
                                                        uint8_t port);

/**
 * Set speed in RPM.
 *
 * Silently clamping to what the bus can sustain would make the UI lie, so this
 * returns MOTOR_SHIELD_V2_ERR_PARAM when the request exceeds
 * motor_shield_v2_max_rpm() for the style last used. Ask first, then show the
 * limit.
 */
motor_shield_v2_status_t motor_shield_v2_stepper_rpm(motor_shield_v2_t *dev,
                                                     uint8_t port,
                                                     uint16_t rpm,
                                                     motor_shield_v2_style_t style);

/**
 * Begin a non-blocking move. Returns immediately; the motion is advanced by
 * motor_shield_v2_tick().
 *
 * @param steps   Number of steps in the chosen style, not microsteps.
 * @param now_ms  Caller's monotonic millisecond clock.
 */
motor_shield_v2_status_t motor_shield_v2_stepper_start(motor_shield_v2_t *dev,
                                                       uint8_t port,
                                                       uint32_t steps,
                                                       motor_shield_v2_run_t dir,
                                                       motor_shield_v2_style_t style,
                                                       uint32_t now_ms);

/** Halt a move where it is. Coils stay energised, holding position. */
motor_shield_v2_status_t motor_shield_v2_stepper_stop(motor_shield_v2_t *dev,
                                                      uint8_t port);

/** Halt and de-energise: the shaft free-wheels. */
motor_shield_v2_status_t motor_shield_v2_stepper_release(motor_shield_v2_t *dev,
                                                         uint8_t port);

bool     motor_shield_v2_stepper_running(const motor_shield_v2_t *dev, uint8_t port);
uint32_t motor_shield_v2_stepper_remaining(const motor_shield_v2_t *dev, uint8_t port);

/**
 * The highest RPM this bus can actually sustain for a given motor and style.
 *
 * Derived from MOTOR_SHIELD_V2_CHANNEL_US and the number of channel writes each
 * style needs per step, assuming the worst case where every channel changes.
 * Shadow suppression usually does better; this is the number you can promise.
 */
uint16_t motor_shield_v2_max_rpm(uint16_t revsteps, motor_shield_v2_style_t style);

/*******************************************************************************
 * Periodic service
 ******************************************************************************/

/**
 * Advance motion and enforce the watchdog. Call regularly, e.g. from a UI tick.
 *
 * Performs AT MOST ONE step across all stepper ports per call, so its worst
 * case is one microstep, about 3.3 ms of bus time. If both ports are due, the
 * one that has been waiting longest goes first; the other waits for the next
 * call. Passing time backwards is treated as a clock reset, not as a huge
 * elapsed interval.
 *
 * @return MOTOR_SHIELD_V2_OK even when nothing was due.
 */
motor_shield_v2_status_t motor_shield_v2_tick(motor_shield_v2_t *dev,
                                              uint32_t now_ms);

/**
 * Arm or disarm the dead-man timer. Zero disables.
 *
 * While any motor or stepper is running and no command has been issued for this
 * long, tick() calls stop_all() and latches a tripped flag. Intended for a UI
 * page or a script that might stop servicing the driver while a motor is
 * loaded -- neither a stall nor a thermal trip is visible to firmware.
 */
void motor_shield_v2_watchdog_set(motor_shield_v2_t *dev, uint32_t timeout_ms);

/** True if the watchdog stopped the motors. Cleared by any new command. */
bool motor_shield_v2_watchdog_tripped(const motor_shield_v2_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_SHIELD_V2_H */
