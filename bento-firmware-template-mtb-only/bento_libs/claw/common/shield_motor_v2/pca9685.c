/*******************************************************************************
 * File:    pca9685.c
 *
 * Purpose: Register-level PCA9685 driver. See pca9685.h for the design notes;
 *          in short, no platform API appears in this file by design.
 *
 * Ref:     NXP PCA9685 data sheet Rev. 4 (16 April 2015). Section numbers in
 *          the comments below refer to that revision.
 ******************************************************************************/

#include "pca9685.h"

/*******************************************************************************
 * Register map (section 7.3)
 ******************************************************************************/

#define REG_MODE1               0x00U
#define REG_MODE2               0x01U
#define REG_SUBADR1             0x02U
#define REG_SUBADR2             0x03U
#define REG_SUBADR3             0x04U
#define REG_ALLCALLADR          0x05U
#define REG_LED0_ON_L           0x06U
#define REG_ALL_LED_ON_L        0xFAU
#define REG_PRE_SCALE           0xFEU

/** Each channel occupies four consecutive registers: ON_L ON_H OFF_L OFF_H. */
#define REG_BYTES_PER_CHANNEL   4U

/* MODE1 bits (Table 5) */
#define MODE1_RESTART           0x80U
#define MODE1_EXTCLK            0x40U
#define MODE1_AI                0x20U
#define MODE1_SLEEP             0x10U
#define MODE1_ALLCALL           0x01U

/* MODE2 bits (Table 6) */
#define MODE2_OUTDRV            0x04U

/** Oscillator stabilisation time after SLEEP is cleared (section 7.3.1.1). */
#define OSC_SETTLE_US           500U

/** Counter resolution used by the prescaler equation (section 7.3.5). */
#define COUNTER_STEPS           4096UL

/*******************************************************************************
 * Local helpers
 ******************************************************************************/

/**
 * Stand-in delay used only when the caller supplies no delay_us callback.
 *
 * Deliberately over-delays. The loop is sized for the fastest core in this
 * family (CM55 at 400 MHz) assuming an unrealistically cheap two-cycle
 * iteration, so on any slower core, or with any real instruction timing, the
 * wait comes out longer than requested. Over-waiting 500 us once per frequency
 * change is free; under-waiting risks programming the prescaler before the
 * oscillator has settled.
 */
static void fallback_delay_us(uint32_t us)
{
    volatile uint32_t spin;

    while (us-- > 0U)
    {
        for (spin = 0U; spin < 200U; spin++)
        {
            /* Intentionally empty. `spin` is volatile so this is not elided. */
        }
    }
}

static void delay_us(const pca9685_t *dev, uint32_t us)
{
    if (dev->bus.delay_us != NULL)
    {
        dev->bus.delay_us(dev->bus.ctx, us);
    }
    else
    {
        fallback_delay_us(us);
    }
}

static pca9685_status_t write_reg(pca9685_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t frame[2];

    frame[0] = reg;
    frame[1] = value;

    if (dev->bus.write(dev->bus.ctx, dev->addr7, frame, sizeof(frame)) != 0)
    {
        /* The device state is now unknown -- a partial transfer may have
         * landed. Force the next channel write to go out unconditionally. */
        dev->shadow_valid = false;
        return PCA9685_ERR_IO;
    }

    return PCA9685_OK;
}

static pca9685_status_t read_reg(pca9685_t *dev, uint8_t reg, uint8_t *value)
{
    if (dev->bus.write_read(dev->bus.ctx, dev->addr7,
                            &reg, 1U, value, 1U) != 0)
    {
        return PCA9685_ERR_IO;
    }

    return PCA9685_OK;
}

/**
 * Write one channel's four registers in a single 5-byte transaction.
 *
 * Relies on MODE1.AI being set, which pca9685_init() guarantees.
 */
static pca9685_status_t write_channel_regs(pca9685_t *dev, uint8_t base_reg,
                                           uint16_t on, uint16_t off)
{
    uint8_t frame[5];

    frame[0] = base_reg;
    frame[1] = (uint8_t)(on & 0xFFU);
    frame[2] = (uint8_t)((on >> 8) & 0x1FU);
    frame[3] = (uint8_t)(off & 0xFFU);
    frame[4] = (uint8_t)((off >> 8) & 0x1FU);

    if (dev->bus.write(dev->bus.ctx, dev->addr7, frame, sizeof(frame)) != 0)
    {
        dev->shadow_valid = false;
        return PCA9685_ERR_IO;
    }

    return PCA9685_OK;
}

/**
 * Compute PRE_SCALE for a target frequency (section 7.3.5):
 *
 *     prescale = round(osc / (4096 * freq)) - 1
 *
 * Rounding is done in integer arithmetic by adding half the divisor before the
 * division, so no floating point is pulled into the link.
 */
static uint8_t compute_prescale(uint32_t osc_hz, uint16_t freq_hz)
{
    uint32_t divisor;
    uint32_t ratio;

    divisor = COUNTER_STEPS * (uint32_t)freq_hz;
    ratio   = (osc_hz + (divisor / 2U)) / divisor;

    if (ratio == 0U)
    {
        ratio = 1U;
    }

    ratio -= 1U;

    if (ratio < PCA9685_PRESCALE_MIN)
    {
        ratio = PCA9685_PRESCALE_MIN;
    }
    else if (ratio > PCA9685_PRESCALE_MAX)
    {
        ratio = PCA9685_PRESCALE_MAX;
    }
    else
    {
        /* In range. */
    }

    return (uint8_t)ratio;
}

/**
 * Invert the prescaler equation: the frequency a given PRE_SCALE really yields.
 *
 *     freq = osc / (4096 * (prescale + 1))
 */
static uint16_t achieved_freq(uint32_t osc_hz, uint8_t prescale)
{
    uint32_t divisor;
    uint32_t freq;

    divisor = COUNTER_STEPS * ((uint32_t)prescale + 1U);
    freq    = (osc_hz + (divisor / 2U)) / divisor;

    return (uint16_t)freq;
}

/*******************************************************************************
 * API
 ******************************************************************************/

pca9685_status_t pca9685_init(pca9685_t *dev,
                              const pca9685_bus_t *bus,
                              uint8_t addr7,
                              uint16_t freq_hz)
{
    pca9685_status_t status;
    uint8_t          mode1;
    uint8_t          i;

    if ((dev == NULL) || (bus == NULL) ||
        (bus->write == NULL) || (bus->write_read == NULL) ||
        (addr7 > 0x7FU))
    {
        return PCA9685_ERR_PARAM;
    }

    dev->bus          = *bus;
    dev->addr7        = addr7;
    dev->ready        = false;
    dev->osc_hz       = PCA9685_OSC_HZ_NOMINAL;
    dev->freq_hz      = 0U;
    dev->shadow_valid = false;

    for (i = 0U; i < PCA9685_CHANNEL_COUNT; i++)
    {
        dev->shadow_on[i]  = 0U;
        dev->shadow_off[i] = 0U;
    }

    /* Wake the part and enable register auto-increment. ALLCALL is left
     * CLEARED on purpose: it defaults to on, and an enabled ALLCALL means this
     * device answers 0x70 as well as its own address. On the TESAIoT Dev Kit
     * this bus is shared with the display and touch controllers, so quietly
     * occupying a second address is not acceptable. */
    status = write_reg(dev, REG_MODE1, MODE1_AI);
    if (status != PCA9685_OK)
    {
        return status;
    }

    /* Clearing SLEEP restarts the oscillator; it needs time before the outputs
     * are trustworthy (section 7.3.1.1). */
    delay_us(dev, OSC_SETTLE_US);

    /* EXTCLK survives the write above -- it is sticky and only a power cycle or
     * a general-call software reset clears it (section 7.3.1). A part in that
     * state answers its address and passes a probe, but the prescaler means
     * nothing and no PWM comes out. Fail loudly here instead. */
    status = read_reg(dev, REG_MODE1, &mode1);
    if (status != PCA9685_OK)
    {
        return status;
    }
    if ((mode1 & MODE1_EXTCLK) != 0U)
    {
        return PCA9685_ERR_EXTCLK;
    }

    /* Totem-pole outputs. The TB6612 inputs on the v2 shield are driven
     * directly, with no external pull-ups, so open-drain would leave them
     * floating. */
    status = write_reg(dev, REG_MODE2, MODE2_OUTDRV);
    if (status != PCA9685_OK)
    {
        return status;
    }

    dev->ready = true;

    /* Program the frequency before anything can be driven. Leaving the reset
     * default in place would run motors at 196.888 Hz. */
    status = pca9685_set_freq(dev, freq_hz);
    if (status != PCA9685_OK)
    {
        dev->ready = false;
        return status;
    }

    /* Park every output before anything downstream gets a chance to command a
     * motor. This also primes the shadow, so the motor driver's own start-up
     * "everything off" call costs nothing.
     *
     * This matters more than it looks: a warm reset does NOT reset the PCA9685
     * (its own reset needs VDD below 0.2 V), so a motor commanded before a
     * firmware reload is still turning when we get here. */
    status = pca9685_all_off(dev);
    if (status != PCA9685_OK)
    {
        dev->ready = false;
        return status;
    }

    return PCA9685_OK;
}

pca9685_status_t pca9685_probe(pca9685_t *dev)
{
    uint8_t mode1;

    if (dev == NULL)
    {
        return PCA9685_ERR_PARAM;
    }

    if (read_reg(dev, REG_MODE1, &mode1) != PCA9685_OK)
    {
        return PCA9685_ERR_NO_DEVICE;
    }

    return PCA9685_OK;
}

pca9685_status_t pca9685_set_osc_freq(pca9685_t *dev, uint32_t osc_hz)
{
    if ((dev == NULL) || (osc_hz == 0U))
    {
        return PCA9685_ERR_PARAM;
    }

    dev->osc_hz = osc_hz;

    return PCA9685_OK;
}

pca9685_status_t pca9685_set_freq(pca9685_t *dev, uint16_t freq_hz)
{
    pca9685_status_t status;
    uint8_t          prescale;
    uint8_t          mode1_old;
    uint8_t          mode1_sleep;

    if (dev == NULL)
    {
        return PCA9685_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return PCA9685_ERR_NOT_READY;
    }

    if (freq_hz < PCA9685_FREQ_HZ_MIN)
    {
        freq_hz = PCA9685_FREQ_HZ_MIN;
    }
    else if (freq_hz > PCA9685_FREQ_HZ_MAX)
    {
        freq_hz = PCA9685_FREQ_HZ_MAX;
    }
    else
    {
        /* In range. */
    }

    prescale = compute_prescale(dev->osc_hz, freq_hz);

    status = read_reg(dev, REG_MODE1, &mode1_old);
    if (status != PCA9685_OK)
    {
        return status;
    }

    /* RESTART reads back as a status bit; never write it back verbatim or the
     * part restarts at an unintended moment. */
    mode1_old  &= (uint8_t)~MODE1_RESTART;
    mode1_sleep = (uint8_t)(mode1_old | MODE1_SLEEP);

    /* PRE_SCALE is writable only while the oscillator is stopped
     * (section 7.3.5). */
    status = write_reg(dev, REG_MODE1, mode1_sleep);
    if (status != PCA9685_OK)
    {
        return status;
    }

    status = write_reg(dev, REG_PRE_SCALE, prescale);
    if (status != PCA9685_OK)
    {
        return status;
    }

    status = write_reg(dev, REG_MODE1, mode1_old);
    if (status != PCA9685_OK)
    {
        return status;
    }

    delay_us(dev, OSC_SETTLE_US);

    /* Writing RESTART as 1 resumes the PWM channels that were active when
     * SLEEP was asserted. Auto-increment is re-asserted here because every
     * multi-byte write in this driver depends on it. */
    status = write_reg(dev, REG_MODE1,
                       (uint8_t)(mode1_old | MODE1_RESTART | MODE1_AI));
    if (status != PCA9685_OK)
    {
        return status;
    }

    /* Record what the part is actually producing, not what was asked for. The
     * prescaler is an integer, so most requests are not representable: 1000 Hz
     * lands on prescale 5, which is 1017 Hz. Callers that display or reason
     * about the real period need the achieved value. */
    dev->freq_hz = achieved_freq(dev->osc_hz, prescale);

    return PCA9685_OK;
}

pca9685_status_t pca9685_set_channel(pca9685_t *dev, uint8_t ch,
                                     uint16_t on, uint16_t off)
{
    pca9685_status_t status;
    uint8_t          base_reg;

    if ((dev == NULL) || (ch >= PCA9685_CHANNEL_COUNT) ||
        (on > PCA9685_FULL) || (off > PCA9685_FULL))
    {
        return PCA9685_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return PCA9685_ERR_NOT_READY;
    }

    if (dev->shadow_valid &&
        (dev->shadow_on[ch] == on) && (dev->shadow_off[ch] == off))
    {
        /* Already there. Skipping this costs the caller nothing and saves the
         * shared bus roughly 0.6 ms at 100 kHz. */
        return PCA9685_OK;
    }

    base_reg = (uint8_t)(REG_LED0_ON_L + (REG_BYTES_PER_CHANNEL * ch));

    status = write_channel_regs(dev, base_reg, on, off);
    if (status != PCA9685_OK)
    {
        return status;
    }

    dev->shadow_on[ch]  = on;
    dev->shadow_off[ch] = off;

    return PCA9685_OK;
}

pca9685_status_t pca9685_set_duty(pca9685_t *dev, uint8_t ch, uint16_t duty)
{
    uint16_t on;
    uint16_t off;

    if ((dev == NULL) || (duty > PCA9685_DUTY_MAX))
    {
        return PCA9685_ERR_PARAM;
    }

    if (duty == 0U)
    {
        /* Use the full-off flag rather than a compare value of 0. A compare of
         * 0 still produces a one-count pulse. */
        on  = 0U;
        off = PCA9685_FULL;
    }
    else if (duty >= PCA9685_FULL)
    {
        on  = PCA9685_FULL;
        off = 0U;
    }
    else
    {
        on  = 0U;
        off = duty;
    }

    return pca9685_set_channel(dev, ch, on, off);
}

pca9685_status_t pca9685_set_pin(pca9685_t *dev, uint8_t ch, bool level)
{
    return pca9685_set_duty(dev, ch, level ? PCA9685_FULL : 0U);
}

pca9685_status_t pca9685_all_off(pca9685_t *dev)
{
    pca9685_status_t status;
    uint8_t          i;

    if (dev == NULL)
    {
        return PCA9685_ERR_PARAM;
    }

    if (!dev->ready)
    {
        return PCA9685_ERR_NOT_READY;
    }

    /* One transaction for all sixteen channels. This is what makes an
     * emergency stop cheap enough to run inside the GFX task. */
    status = write_channel_regs(dev, REG_ALL_LED_ON_L, 0U, PCA9685_FULL);
    if (status != PCA9685_OK)
    {
        return status;
    }

    for (i = 0U; i < PCA9685_CHANNEL_COUNT; i++)
    {
        dev->shadow_on[i]  = 0U;
        dev->shadow_off[i] = PCA9685_FULL;
    }
    dev->shadow_valid = true;

    return PCA9685_OK;
}

void pca9685_invalidate_shadow(pca9685_t *dev)
{
    if (dev != NULL)
    {
        dev->shadow_valid = false;
    }
}

uint16_t pca9685_get_freq(const pca9685_t *dev)
{
    return (dev != NULL) ? dev->freq_hz : 0U;
}

bool pca9685_is_ready(const pca9685_t *dev)
{
    return (dev != NULL) && dev->ready;
}
