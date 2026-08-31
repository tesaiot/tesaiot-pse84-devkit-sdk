/*******************************************************************************
 * File:    motor_shield_v2.c
 *
 * Purpose: Adafruit Motor Shield v2 driver. See motor_shield_v2.h for the
 *          design contract; in short, no platform API and no blocking waits.
 *
 * Sources: Channel assignments and the stepping state machine follow
 *          Adafruit_Motor_Shield_V2_Library. Where this file departs from that
 *          library the comment says so and why -- there are six such places,
 *          each a defect verified against the NXP PCA9685 (Rev. 4) and Toshiba
 *          TB6612FNG (2012-11-01) data sheets.
 ******************************************************************************/

#include "motor_shield_v2.h"

/*******************************************************************************
 * Hardware map
 *
 * Which PCA9685 channel drives which TB6612 input, per motor terminal.
 * Stepper port 1 is built from terminals M1 (coil A) and M2 (coil B);
 * port 2 from M3 and M4.
 ******************************************************************************/

typedef struct
{
    uint8_t pwm;
    uint8_t in1;
    uint8_t in2;
} channel_map_t;

static const channel_map_t s_map[MOTOR_SHIELD_V2_DC_COUNT] =
{
    { 8U, 10U,  9U },   /* M1 */
    { 13U, 11U, 12U },  /* M2 */
    { 2U,  4U,  3U },   /* M3 */
    { 7U,  5U,  6U },   /* M4 */
};

/** Terminal indices that make up each stepper port: {coil A, coil B}. */
static const uint8_t s_stepper_terminals[MOTOR_SHIELD_V2_STEPPER_COUNT][2] =
{
    { 0U, 1U },         /* port 1 -> M1, M2 */
    { 2U, 3U },         /* port 2 -> M3, M4 */
};

/**
 * Sine-weighted microstepping amplitudes, 17 points for MICROSTEPS = 16.
 *
 * This is round(255 * sin(pi/2 * i/16)) computed correctly. Adafruit's table
 * has two transcription errors -- index 6 reads 141 (should be 142) and index
 * 15 reads 253 (should be 254) -- in BOTH the v1 and v2 libraries. The proof
 * that these are typos rather than a different scale factor is that the
 * MICROSTEPS = 8 table in the same source file lists 142 for the same 33.75
 * degree point, contradicting itself; and no amplitude between 250 and 260
 * reproduces the published numbers.
 *
 * The practical effect is small -- under 0.30% torque error and 0.122 degrees
 * of electrical phase -- so this is a correctness fix, not a performance one.
 */
static const uint8_t s_microstep_curve[MOTOR_SHIELD_V2_MICROSTEPS + 1U] =
{
    0U, 25U, 50U, 74U, 98U, 120U, 142U, 162U, 180U,
    197U, 212U, 225U, 236U, 244U, 250U, 254U, 255U
};

/** Full electrical cycle length in microsteps. */
#define PHASE_MODULO        (MOTOR_SHIELD_V2_MICROSTEPS * 4U)

/** Half a full step, in microsteps. */
#define HALF_STEP           (MOTOR_SHIELD_V2_MICROSTEPS / 2U)

/** Worst-case channel writes per step, used only for the honest RPM ceiling. */
#define CHANNELS_PER_STEP_PLAIN     4U
#define CHANNELS_PER_STEP_MICRO     6U

/*******************************************************************************
 * Small helpers
 ******************************************************************************/

static motor_shield_v2_status_t map_status(pca9685_status_t s)
{
    motor_shield_v2_status_t out;

    switch (s)
    {
        case PCA9685_OK:            out = MOTOR_SHIELD_V2_OK;            break;
        case PCA9685_ERR_PARAM:     out = MOTOR_SHIELD_V2_ERR_PARAM;     break;
        case PCA9685_ERR_NOT_READY: out = MOTOR_SHIELD_V2_ERR_NOT_READY; break;
        case PCA9685_ERR_NO_DEVICE: out = MOTOR_SHIELD_V2_ERR_NO_DEVICE; break;
        default:                    out = MOTOR_SHIELD_V2_ERR_IO;        break;
    }

    return out;
}

/**
 * Map an 8-bit speed onto the PCA9685's 12-bit compare.
 *
 * (s * 4095 + 127) / 255 is monotonic over all 256 inputs and hits both
 * endpoints exactly. Adafruit computes `s * 16`, which caps at 4080 -- 99.63%
 * -- so their full speed is not full.
 */
static uint16_t scale_speed(uint8_t speed)
{
    return (uint16_t)(((uint32_t)speed * 4095U + 127U) / 255U);
}

/** Advance a phase index by a signed amount, staying inside [0, PHASE_MODULO). */
static uint16_t phase_advance(uint16_t phase, uint16_t delta, bool forward)
{
    uint16_t next;

    if (forward)
    {
        next = (uint16_t)(phase + delta);
    }
    else
    {
        /* Add the modulus before subtracting so the intermediate never goes
         * negative -- this runs on unsigned types. */
        next = (uint16_t)(phase + PHASE_MODULO - (delta % PHASE_MODULO));
    }

    return (uint16_t)(next % PHASE_MODULO);
}

static bool dc_index_valid(uint8_t motor)
{
    return (motor >= 1U) && (motor <= MOTOR_SHIELD_V2_DC_COUNT);
}

static bool port_index_valid(uint8_t port)
{
    return (port >= 1U) && (port <= MOTOR_SHIELD_V2_STEPPER_COUNT);
}

/** True while terminal idx is energised as a DC motor -- driving, or shorted by
 *  BRAKE with a rotor that may still be turning. RELEASE and a zero duty are
 *  both "not energised". */
static bool dc_terminal_driving(const motor_shield_v2_t *dev, uint8_t idx)
{
    if (idx >= MOTOR_SHIELD_V2_DC_COUNT)
    {
        return false;
    }

    if (dev->dc[idx].run == MOTOR_SHIELD_V2_BRAKE)
    {
        return true;
    }

    return (dev->dc[idx].speed > 0U) &&
           ((dev->dc[idx].run == MOTOR_SHIELD_V2_FORWARD) ||
            (dev->dc[idx].run == MOTOR_SHIELD_V2_BACKWARD));
}

/** Is this DC terminal claimed by an attached stepper port? */
static bool terminal_owned_by_stepper(const motor_shield_v2_t *dev, uint8_t idx)
{
    uint8_t p;

    for (p = 0U; p < MOTOR_SHIELD_V2_STEPPER_COUNT; p++)
    {
        if (dev->st[p].attached &&
            ((s_stepper_terminals[p][0] == idx) ||
             (s_stepper_terminals[p][1] == idx)))
        {
            return true;
        }
    }

    return false;
}

/** Steps per revolution for a style, in that style's own step units. */
static uint32_t steps_per_rev(uint16_t revsteps, motor_shield_v2_style_t style)
{
    uint32_t n = revsteps;

    if (style == MOTOR_SHIELD_V2_INTERLEAVE)
    {
        n *= 2U;
    }
    else if (style == MOTOR_SHIELD_V2_MICROSTEP)
    {
        n *= MOTOR_SHIELD_V2_MICROSTEPS;
    }
    else
    {
        /* SINGLE and DOUBLE both advance one full step at a time. */
    }

    return n;
}

/* Defined below, next to the watchdog it serves. Declared here because
 * note_command() needs it and sits above it in command order. */
static bool anything_running(const motor_shield_v2_t *dev);

/* Stamp the dead-man timer. Every command path is handed now_ms, matching the
 * header's rule that time enters this driver only through the caller.
 *
 * The DC paths previously called this as note_command(dev, dev->last_cmd_ms) --
 * a self-assignment, so last_cmd_ms never moved. On a DC motor the watchdog
 * therefore measured from zero and stopped the motor the moment the timeout
 * elapsed, however recently the caller had commanded it. */
static void note_command(motor_shield_v2_t *dev, uint32_t now_ms)
{
    dev->last_cmd_ms = now_ms;

    /* Clear the trip only once something is actually running again.
     *
     * Clearing it unconditionally meant a slider dragged to zero, or any command
     * to an unrelated motor, erased the "stopped by watchdog" state -- and since
     * the page samples that state on its 33 ms render, the warning could vanish
     * before it was ever drawn. The user would see a motor stop with no
     * explanation on screen, which is the one thing the flag exists to prevent.
     * A trip is history until the shield is driving again. */
    if (anything_running(dev))
    {
        dev->watchdog_tripped = false;
    }
}


/*******************************************************************************
 * Lifecycle
 ******************************************************************************/

motor_shield_v2_status_t motor_shield_v2_init(motor_shield_v2_t *dev,
                                              const pca9685_bus_t *bus,
                                              uint8_t addr7,
                                              uint16_t freq_hz)
{
    pca9685_status_t status;
    uint8_t          i;

    if (dev == NULL)
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    dev->ready            = false;
    dev->watchdog_ms      = 0U;
    dev->last_cmd_ms      = 0U;
    dev->watchdog_tripped = false;

    for (i = 0U; i < MOTOR_SHIELD_V2_DC_COUNT; i++)
    {
        dev->dc[i].speed = 0U;
        dev->dc[i].run   = MOTOR_SHIELD_V2_RELEASE;
    }

    for (i = 0U; i < MOTOR_SHIELD_V2_STEPPER_COUNT; i++)
    {
        dev->st[i].attached    = false;
        dev->st[i].revsteps    = 0U;
        dev->st[i].rpm         = 0U;
        dev->st[i].cur_micro   = 0U;
        dev->st[i].style       = (uint8_t)MOTOR_SHIELD_V2_DOUBLE;
        dev->st[i].dir         = (uint8_t)MOTOR_SHIELD_V2_FORWARD;
        dev->st[i].running     = false;
        dev->st[i].steps_left  = 0U;
        dev->st[i].interval_ms = 0U;
        dev->st[i].next_due_ms = 0U;
    }

    /* pca9685_init() programs the frequency and parks every output before
     * returning, which is what makes a firmware reload safe: a warm reset does
     * not reset this part, so a motor commanded by the previous image is still
     * turning until that park lands. */
    status = pca9685_init(&dev->pwm, bus, addr7, freq_hz);
    if (status != PCA9685_OK)
    {
        return map_status(status);
    }

    dev->ready = true;

    return MOTOR_SHIELD_V2_OK;
}

motor_shield_v2_status_t motor_shield_v2_probe(motor_shield_v2_t *dev)
{
    if (dev == NULL)
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    return map_status(pca9685_probe(&dev->pwm));
}

motor_shield_v2_status_t motor_shield_v2_stop_all(motor_shield_v2_t *dev)
{
    pca9685_status_t status;
    uint8_t          i;

    if (dev == NULL)
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_READY;
    }

    /* A single ALL_LED transaction rather than sixteen channel writes: ~0.55 ms
     * instead of ~8.8 ms. That difference is what lets an emergency stop run
     * from a UI callback without visibly stalling the display. */
    status = pca9685_all_off(&dev->pwm);

    if (status != PCA9685_OK)
    {
        /* Keep the cached state on a failed write, and say so.
         *
         * The state used to be zeroed before the transaction, so a stop that did
         * not reach the shield left the driver believing nothing was running
         * while the hardware kept driving. anything_running() then returned
         * false forever and the watchdog never tried again -- the one emergency
         * stop this shield offers, retired by the very failure it exists for. A
         * wedged shared SCB5 is the realistic cause, and it recovers, so the
         * next tick must be able to try again. */
        return map_status(status);
    }

    for (i = 0U; i < MOTOR_SHIELD_V2_DC_COUNT; i++)
    {
        dev->dc[i].speed = 0U;
        dev->dc[i].run   = MOTOR_SHIELD_V2_RELEASE;
    }

    for (i = 0U; i < MOTOR_SHIELD_V2_STEPPER_COUNT; i++)
    {
        dev->st[i].running    = false;
        dev->st[i].steps_left = 0U;
    }

    return MOTOR_SHIELD_V2_OK;
}

/*******************************************************************************
 * DC motors
 ******************************************************************************/

motor_shield_v2_status_t motor_shield_v2_dc_speed(motor_shield_v2_t *dev,
                                                  uint8_t motor,
                                                  uint8_t speed,
                                                  uint32_t now_ms)
{
    uint8_t          idx;
    pca9685_status_t status;

    if ((dev == NULL) || !dc_index_valid(motor))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_READY;
    }

    idx = (uint8_t)(motor - 1U);

    if (terminal_owned_by_stepper(dev, idx))
    {
        return MOTOR_SHIELD_V2_ERR_PORT_BUSY;
    }

    status = pca9685_set_duty(&dev->pwm, s_map[idx].pwm, scale_speed(speed));
    if (status != PCA9685_OK)
    {
        return map_status(status);
    }

    dev->dc[idx].speed = speed;
    note_command(dev, now_ms);

    return MOTOR_SHIELD_V2_OK;
}

/* Coast to zero duty before a direction flip.
 *
 * Flipping IN1/IN2 while the duty is still high is a plugging brake: the bridge
 * drives against back-EMF, so the winding briefly sees close to twice supply.
 * This shield has no polyfuse, no sense resistor, and the TB6612's thermal trip
 * is invisible to firmware, so that spike is unrecoverable if it damages
 * something. Adafruit's library does not handle this either.
 *
 * The duty is left at zero rather than restored after the flip: re-applying it
 * instantly into a still-spinning motor is the same plugging event one bus
 * transaction later. The caller must set the speed again, which is deliberate --
 * this file already prefers refusing over silently doing something the UI would
 * then misreport. */
static pca9685_status_t coast_before_reversal(motor_shield_v2_t *dev,
                                              uint8_t idx,
                                              motor_shield_v2_run_t cmd)
{
    pca9685_status_t status = PCA9685_OK;
    motor_shield_v2_run_t cur = dev->dc[idx].run;
    bool reversing = ((cur == MOTOR_SHIELD_V2_FORWARD) &&
                      (cmd == MOTOR_SHIELD_V2_BACKWARD)) ||
                     ((cur == MOTOR_SHIELD_V2_BACKWARD) &&
                      (cmd == MOTOR_SHIELD_V2_FORWARD)) ||
                     /* BRAKE shorts the winding and the rotor keeps turning
                      * while it decelerates, but it tells us nothing about which
                      * way. Driving either direction out of BRAKE at the cached
                      * duty can therefore be a plugging event, so coast first and
                      * make the caller re-apply the speed. On the screen BRAKE
                      * sits directly beside BACK, so brake-then-reverse is a
                      * two-tap gesture, not a corner case. */
                     (cur == MOTOR_SHIELD_V2_BRAKE);

    if (reversing && (dev->dc[idx].speed != 0U))
    {
        status = pca9685_set_duty(&dev->pwm, s_map[idx].pwm, 0U);
        if (status == PCA9685_OK)
        {
            dev->dc[idx].speed = 0U;
        }
    }

    return status;
}

motor_shield_v2_status_t motor_shield_v2_dc_run(motor_shield_v2_t *dev,
                                                uint8_t motor,
                                                motor_shield_v2_run_t cmd,
                                                uint32_t now_ms)
{
    uint8_t          idx;
    pca9685_status_t status = PCA9685_OK;

    if ((dev == NULL) || !dc_index_valid(motor))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_READY;
    }

    idx = (uint8_t)(motor - 1U);

    if (terminal_owned_by_stepper(dev, idx))
    {
        return MOTOR_SHIELD_V2_ERR_PORT_BUSY;
    }

    switch (cmd)
    {
        case MOTOR_SHIELD_V2_FORWARD:
            /* Duty to zero first if this is a reversal -- see the helper. */
            status = coast_before_reversal(dev, idx, cmd);
            /* Drop the opposite input next. Raising one before lowering the
             * other would put both high for the duration of an I2C
             * transaction, which is a brake pulse into a spinning motor. */
            if (status == PCA9685_OK)
            {
                status = pca9685_set_pin(&dev->pwm, s_map[idx].in2, false);
            }
            if (status == PCA9685_OK)
            {
                status = pca9685_set_pin(&dev->pwm, s_map[idx].in1, true);
            }
            break;

        case MOTOR_SHIELD_V2_BACKWARD:
            status = coast_before_reversal(dev, idx, cmd);
            if (status == PCA9685_OK)
            {
                status = pca9685_set_pin(&dev->pwm, s_map[idx].in1, false);
            }
            if (status == PCA9685_OK)
            {
                status = pca9685_set_pin(&dev->pwm, s_map[idx].in2, true);
            }
            break;

        case MOTOR_SHIELD_V2_BRAKE:
            /* Both inputs high is the TB6612's short brake. Adafruit defines
             * this constant and then has no case for it, so upstream BRAKE
             * silently does nothing at all. */
            status = pca9685_set_pin(&dev->pwm, s_map[idx].in1, true);
            if (status == PCA9685_OK)
            {
                status = pca9685_set_pin(&dev->pwm, s_map[idx].in2, true);
            }
            break;

        case MOTOR_SHIELD_V2_RELEASE:
            /* Duty to zero FIRST, then the direction inputs. The reverse order
             * leaves the bridge enabled with both inputs low for one
             * transaction. Upstream's RELEASE never touches the duty at all,
             * which makes their "release" a brake rather than a coast. */
            status = pca9685_set_duty(&dev->pwm, s_map[idx].pwm, 0U);
            if (status == PCA9685_OK)
            {
                status = pca9685_set_pin(&dev->pwm, s_map[idx].in1, false);
            }
            if (status == PCA9685_OK)
            {
                status = pca9685_set_pin(&dev->pwm, s_map[idx].in2, false);
            }
            if (status == PCA9685_OK)
            {
                dev->dc[idx].speed = 0U;
            }
            break;

        default:
            return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    if (status != PCA9685_OK)
    {
        return map_status(status);
    }

    dev->dc[idx].run = cmd;
    note_command(dev, now_ms);

    return MOTOR_SHIELD_V2_OK;
}

const motor_shield_v2_dc_t *motor_shield_v2_dc_state(const motor_shield_v2_t *dev,
                                                     uint8_t motor)
{
    if ((dev == NULL) || !dc_index_valid(motor))
    {
        return NULL;
    }

    return &dev->dc[motor - 1U];
}

/*******************************************************************************
 * Steppers
 ******************************************************************************/

/**
 * Drive one microstep of the electrical cycle and update the coil outputs.
 *
 * Transcribed from Adafruit_StepperMotor::onestep(), with the corrected curve
 * table and with the amplitude scaled to full 12-bit range instead of their
 * `value * 16`.
 */
static pca9685_status_t stepper_emit(motor_shield_v2_t *dev, uint8_t port)
{
    const motor_shield_v2_stepper_t *st = &dev->st[port];
    const channel_map_t *a = &s_map[s_stepper_terminals[port][0]];
    const channel_map_t *b = &s_map[s_stepper_terminals[port][1]];
    pca9685_status_t status;
    uint16_t phase = st->cur_micro;
    uint8_t  amp_a = 255U;
    uint8_t  amp_b = 255U;
    uint8_t  latch = 0U;
    const uint16_t M = MOTOR_SHIELD_V2_MICROSTEPS;

    if (st->style == (uint8_t)MOTOR_SHIELD_V2_MICROSTEP)
    {
        if (phase < M)
        {
            amp_a = s_microstep_curve[M - phase];
            amp_b = s_microstep_curve[phase];
        }
        else if (phase < (2U * M))
        {
            amp_a = s_microstep_curve[phase - M];
            amp_b = s_microstep_curve[(2U * M) - phase];
        }
        else if (phase < (3U * M))
        {
            amp_a = s_microstep_curve[(3U * M) - phase];
            amp_b = s_microstep_curve[phase - (2U * M)];
        }
        else
        {
            amp_a = s_microstep_curve[phase - (3U * M)];
            amp_b = s_microstep_curve[(4U * M) - phase];
        }

        /* Both coils energised in quadrature throughout the cycle. */
        if (phase < M)                  { latch = 0x03U; }
        else if (phase < (2U * M))      { latch = 0x06U; }
        else if (phase < (3U * M))      { latch = 0x0CU; }
        else                            { latch = 0x09U; }
    }
    else
    {
        static const uint8_t k_latch[8] =
        {
            0x1U,   /* coil 1        */
            0x3U,   /* coil 1 + 2    */
            0x2U,   /* coil 2        */
            0x6U,   /* coil 2 + 3    */
            0x4U,   /* coil 3        */
            0xCU,   /* coil 3 + 4    */
            0x8U,   /* coil 4        */
            0x9U,   /* coil 4 + 1    */
        };

        latch = k_latch[(phase / HALF_STEP) & 0x7U];
    }

    /* Amplitude before direction: raising a coil's drive after its polarity is
     * already set is the benign order. */
    status = pca9685_set_duty(&dev->pwm, a->pwm, scale_speed(amp_a));
    if (status == PCA9685_OK)
    {
        status = pca9685_set_duty(&dev->pwm, b->pwm, scale_speed(amp_b));
    }
    if (status == PCA9685_OK)
    {
        status = pca9685_set_pin(&dev->pwm, a->in2, (latch & 0x1U) != 0U);
    }
    if (status == PCA9685_OK)
    {
        status = pca9685_set_pin(&dev->pwm, b->in1, (latch & 0x2U) != 0U);
    }
    if (status == PCA9685_OK)
    {
        status = pca9685_set_pin(&dev->pwm, a->in1, (latch & 0x4U) != 0U);
    }
    if (status == PCA9685_OK)
    {
        status = pca9685_set_pin(&dev->pwm, b->in2, (latch & 0x8U) != 0U);
    }

    return status;
}

/** Advance the phase index by one step of the configured style. */
static void stepper_advance_phase(motor_shield_v2_stepper_t *st)
{
    bool     forward = (st->dir == (uint8_t)MOTOR_SHIELD_V2_FORWARD);
    bool     on_half = ((st->cur_micro / HALF_STEP) % 2U) != 0U;
    uint16_t delta;

    switch ((motor_shield_v2_style_t)st->style)
    {
        case MOTOR_SHIELD_V2_SINGLE:
            /* Land on the even half-step boundaries: one coil energised. */
            delta = on_half ? HALF_STEP : MOTOR_SHIELD_V2_MICROSTEPS;
            break;

        case MOTOR_SHIELD_V2_DOUBLE:
            /* Land on the odd boundaries: two coils energised. */
            delta = on_half ? MOTOR_SHIELD_V2_MICROSTEPS : HALF_STEP;
            break;

        case MOTOR_SHIELD_V2_INTERLEAVE:
            delta = HALF_STEP;
            break;

        case MOTOR_SHIELD_V2_MICROSTEP:
        default:
            delta = 1U;
            break;
    }

    st->cur_micro = phase_advance(st->cur_micro, delta, forward);
}

motor_shield_v2_status_t motor_shield_v2_stepper_attach(motor_shield_v2_t *dev,
                                                        uint8_t port,
                                                        uint16_t revsteps)
{
    uint8_t p;

    if ((dev == NULL) || !port_index_valid(port) || (revsteps == 0U))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_READY;
    }

    p = (uint8_t)(port - 1U);

    /* Refuse while either of the port's two terminals is still driving as a DC
     * motor.
     *
     * The stepper sequencer writes IN1/IN2 directly. Taking over a terminal that
     * is at full duty in one direction flips the bridge under a spinning rotor,
     * which is the same plugging event coast_before_reversal() exists to prevent
     * -- and worse, the duty write is shadow-suppressed when the stepper's own
     * duty happens to match, so it does not even cost a visible transaction.
     * Refusing keeps the rule that a current spike is never one call away:
     * release or stop the DC motors, then attach. */
    if (dc_terminal_driving(dev, s_stepper_terminals[p][0]) ||
        dc_terminal_driving(dev, s_stepper_terminals[p][1]))
    {
        return MOTOR_SHIELD_V2_ERR_PORT_BUSY;
    }

    dev->st[p].attached  = true;
    dev->st[p].revsteps  = revsteps;
    dev->st[p].cur_micro = 0U;
    dev->st[p].running   = false;

    return MOTOR_SHIELD_V2_OK;
}

motor_shield_v2_status_t motor_shield_v2_stepper_detach(motor_shield_v2_t *dev,
                                                        uint8_t port)
{
    motor_shield_v2_status_t status;

    if ((dev == NULL) || !port_index_valid(port))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    status = motor_shield_v2_stepper_release(dev, port);

    dev->st[port - 1U].attached = false;

    return status;
}

uint16_t motor_shield_v2_max_rpm(uint16_t revsteps, motor_shield_v2_style_t style)
{
    uint32_t per_step_us;
    uint32_t spr;
    uint32_t rpm;

    if (revsteps == 0U)
    {
        return 0U;
    }

    per_step_us = (uint32_t)MOTOR_SHIELD_V2_CHANNEL_US *
                  ((style == MOTOR_SHIELD_V2_MICROSTEP) ? CHANNELS_PER_STEP_MICRO
                                                        : CHANNELS_PER_STEP_PLAIN);

    spr = steps_per_rev(revsteps, style);
    rpm = 60000000UL / (spr * per_step_us);

    return (uint16_t)rpm;
}

motor_shield_v2_status_t motor_shield_v2_stepper_rpm(motor_shield_v2_t *dev,
                                                     uint8_t port,
                                                     uint16_t rpm,
                                                     motor_shield_v2_style_t style)
{
    motor_shield_v2_stepper_t *st;
    uint32_t spr;

    if ((dev == NULL) || !port_index_valid(port) || (rpm == 0U))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    st = &dev->st[port - 1U];

    if (!st->attached)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_ATTACHED;
    }

    /* Refuse rather than clamp. Clamping would let a UI display 120 RPM while
     * the motor turned at 5, and the user would have no way to find out. */
    if (rpm > motor_shield_v2_max_rpm(st->revsteps, style))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    spr = steps_per_rev(st->revsteps, style);

    st->rpm         = rpm;
    st->style       = (uint8_t)style;
    st->interval_ms = 60000UL / (spr * rpm);

    if (st->interval_ms == 0U)
    {
        st->interval_ms = 1U;
    }

    return MOTOR_SHIELD_V2_OK;
}

motor_shield_v2_status_t motor_shield_v2_stepper_start(motor_shield_v2_t *dev,
                                                       uint8_t port,
                                                       uint32_t steps,
                                                       motor_shield_v2_run_t dir,
                                                       motor_shield_v2_style_t style,
                                                       uint32_t now_ms)
{
    motor_shield_v2_stepper_t *st;
    motor_shield_v2_status_t   status;

    if ((dev == NULL) || !port_index_valid(port) ||
        ((dir != MOTOR_SHIELD_V2_FORWARD) && (dir != MOTOR_SHIELD_V2_BACKWARD)))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_READY;
    }

    st = &dev->st[port - 1U];

    if (!st->attached)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_ATTACHED;
    }

    if (st->style != (uint8_t)style)
    {
        /* Changing style changes steps-per-revolution, so the interval has to
         * be recomputed against the same RPM. */
        status = motor_shield_v2_stepper_rpm(dev, port,
                                             (st->rpm != 0U) ? st->rpm : 1U,
                                             style);
        if (status != MOTOR_SHIELD_V2_OK)
        {
            return status;
        }
    }

    if (st->interval_ms == 0U)
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;   /* rpm was never set */
    }

    st->dir         = (uint8_t)dir;
    st->steps_left  = steps;
    st->running     = (steps > 0U);
    st->next_due_ms = now_ms;

    note_command(dev, now_ms);

    return MOTOR_SHIELD_V2_OK;
}

motor_shield_v2_status_t motor_shield_v2_stepper_stop(motor_shield_v2_t *dev,
                                                      uint8_t port)
{
    if ((dev == NULL) || !port_index_valid(port))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    dev->st[port - 1U].running    = false;
    dev->st[port - 1U].steps_left = 0U;

    return MOTOR_SHIELD_V2_OK;
}

motor_shield_v2_status_t motor_shield_v2_stepper_release(motor_shield_v2_t *dev,
                                                         uint8_t port)
{
    const channel_map_t *a;
    const channel_map_t *b;
    pca9685_status_t     status;
    uint8_t              p;

    if ((dev == NULL) || !port_index_valid(port))
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_READY;
    }

    p = (uint8_t)(port - 1U);
    a = &s_map[s_stepper_terminals[p][0]];
    b = &s_map[s_stepper_terminals[p][1]];

    dev->st[p].running    = false;
    dev->st[p].steps_left = 0U;

    /* Duty first, then the direction inputs -- same ordering rule as a DC
     * release, for the same reason. */
    status = pca9685_set_duty(&dev->pwm, a->pwm, 0U);
    if (status == PCA9685_OK) { status = pca9685_set_duty(&dev->pwm, b->pwm, 0U); }
    if (status == PCA9685_OK) { status = pca9685_set_pin(&dev->pwm, a->in1, false); }
    if (status == PCA9685_OK) { status = pca9685_set_pin(&dev->pwm, a->in2, false); }
    if (status == PCA9685_OK) { status = pca9685_set_pin(&dev->pwm, b->in1, false); }
    if (status == PCA9685_OK) { status = pca9685_set_pin(&dev->pwm, b->in2, false); }

    return map_status(status);
}

bool motor_shield_v2_stepper_running(const motor_shield_v2_t *dev, uint8_t port)
{
    if ((dev == NULL) || !port_index_valid(port))
    {
        return false;
    }

    return dev->st[port - 1U].running;
}

uint32_t motor_shield_v2_stepper_remaining(const motor_shield_v2_t *dev, uint8_t port)
{
    if ((dev == NULL) || !port_index_valid(port))
    {
        return 0U;
    }

    return dev->st[port - 1U].steps_left;
}

/*******************************************************************************
 * Periodic service
 ******************************************************************************/

void motor_shield_v2_watchdog_set(motor_shield_v2_t *dev, uint32_t timeout_ms)
{
    if (dev != NULL)
    {
        dev->watchdog_ms = timeout_ms;
    }
}

bool motor_shield_v2_watchdog_tripped(const motor_shield_v2_t *dev)
{
    return (dev != NULL) && dev->watchdog_tripped;
}

/** Is anything actually moving right now? */
/* What the dead-man watchdog is allowed to stop: DC motors only, deliberately
 * not steppers.
 *
 * The watchdog answers one question -- is something running that nothing is
 * supervising? A DC RUN has no end; it turns until someone says otherwise, so
 * silence from the caller is exactly the danger and stopping is right.
 *
 * A stepper move is a different shape. stepper_start() takes a step count, so
 * the motion is bounded by what the caller asked for and it ends whether or not
 * anyone is watching. Counting steppers here meant a long single-command move
 * was aborted mid-way while tick() was demonstrably being serviced, and the
 * abort destroyed steps_left so the caller could not even resume: the watchdog
 * fired on a controller it had just been talking to. The exposure given up is
 * bounded by the step budget the caller chose.
 *
 * BRAKE counts, via dc_terminal_driving() -- a shorted winding on a turning
 * rotor dissipates through the bridge, so an unattended brake is a thermal
 * question, not a parked motor. */
static bool dc_running(const motor_shield_v2_t *dev)
{
    uint8_t i;

    for (i = 0U; i < MOTOR_SHIELD_V2_DC_COUNT; i++)
    {
        if (dc_terminal_driving(dev, i))
        {
            return true;
        }
    }

    return false;
}

/** Anything at all energised, steppers included. Used to decide when a watchdog
 *  trip stops being current news -- unlike the watchdog's own scope, a stepper
 *  starting up does mean the shield is live again. */
static bool anything_running(const motor_shield_v2_t *dev)
{
    uint8_t i;

    for (i = 0U; i < MOTOR_SHIELD_V2_STEPPER_COUNT; i++)
    {
        if (dev->st[i].running)
        {
            return true;
        }
    }

    return dc_running(dev);
}

motor_shield_v2_status_t motor_shield_v2_tick(motor_shield_v2_t *dev,
                                              uint32_t now_ms)
{
    motor_shield_v2_stepper_t *due = NULL;
    uint8_t  due_port = 0U;
    uint32_t due_lateness = 0U;
    uint8_t  i;

    if (dev == NULL)
    {
        return MOTOR_SHIELD_V2_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return MOTOR_SHIELD_V2_ERR_NOT_READY;
    }

    if ((dev->watchdog_ms != 0U) && dc_running(dev))
    {
        /* Unsigned subtraction gives the correct elapsed time across a 32-bit
         * millisecond wrap, which happens every 49.7 days. */
        if ((uint32_t)(now_ms - dev->last_cmd_ms) >= dev->watchdog_ms)
        {
            dev->watchdog_tripped = true;
            return motor_shield_v2_stop_all(dev);
        }
    }

    /* Pick the single most overdue stepper. Servicing only one per call caps
     * the bus time this function can consume at roughly one microstep, about
     * 3.3 ms, which is the whole point of the design. */
    for (i = 0U; i < MOTOR_SHIELD_V2_STEPPER_COUNT; i++)
    {
        motor_shield_v2_stepper_t *st = &dev->st[i];
        uint32_t lateness;

        if (!st->running)
        {
            continue;
        }

        /* A due time in the future means "not yet".
         *
         * This branch used to also assign next_due_ms = now_ms, conflating the
         * ordinary not-yet case with a clock that had gone backwards. The
         * deadline was therefore rewritten on every early call, so the step rate
         * became "one step per two calls to tick()" instead of one step per
         * interval_ms: at a 1 ms service period a requested 30 RPM ran at about
         * 150. The RPM the caller asked for is only honoured if the schedule
         * survives calls that arrive early, and early calls are the normal case.
         *
         * A genuinely backwards clock is still handled, distinguished by how far
         * in the future the deadline appears: more than one interval out cannot
         * happen from ordinary scheduling. */
        {
            int32_t ahead = (int32_t)(now_ms - st->next_due_ms);

            if (ahead < 0)
            {
                if ((uint32_t)(-ahead) > st->interval_ms)
                {
                    st->next_due_ms = now_ms;
                }
                continue;
            }
        }

        lateness = now_ms - st->next_due_ms;

        if ((due == NULL) || (lateness > due_lateness))
        {
            due          = st;
            due_lateness = lateness;
            due_port     = i;
        }
    }

    if (due == NULL)
    {
        return MOTOR_SHIELD_V2_OK;
    }

    stepper_advance_phase(due);

    {
        pca9685_status_t status = stepper_emit(dev, due_port);

        if (status != PCA9685_OK)
        {
            /* Stop rather than keep issuing steps that may not be reaching the
             * hardware -- an un-acked write means the coil state is unknown. */
            due->running    = false;
            due->steps_left = 0U;
            return map_status(status);
        }
    }

    if (due->steps_left > 0U)
    {
        due->steps_left--;
    }

    if (due->steps_left == 0U)
    {
        due->running = false;
    }
    else
    {
        /* Advance from the scheduled time, not from now, so a late tick does
         * not accumulate drift. If we have fallen more than one interval
         * behind, resynchronise instead of trying to catch up -- the bus cannot
         * go faster, and bunching steps would only make the motor stutter. */
        due->next_due_ms += due->interval_ms;

        if ((int32_t)(now_ms - due->next_due_ms) > (int32_t)due->interval_ms)
        {
            due->next_due_ms = now_ms + due->interval_ms;
        }
    }

    return MOTOR_SHIELD_V2_OK;
}
