/*******************************************************************************
 * File:    arduino_shield_qwa309_hal.c
 *
 * Purpose: Physical access layer behind the QWA309 Arduino header descriptor.
 *          Target-only: this is the file that touches the PSoC PDL.
 *
 *          The capability table lives next door in arduino_shield_qwa309.c and
 *          is deliberately free of platform API so it can be host-tested. This
 *          file supplies the two vtables that file declares extern.
 *
 * Core:    CM55. The header I2C is SCB5, which on this board is the display /
 *          touch controller's SCB, and CM55's GFX task already owns it. Reusing
 *          that controller rather than initialising our own is what keeps the
 *          bus single-mastered from this core -- exactly the arrangement
 *          dfr0522_rgb.c uses for the RGB matrix on the same bus.
 *
 *          Note there is no cross-core lock: CM33_NS also drives SCB5 for
 *          OPTIGA. That contention predates this driver and is not made worse
 *          by it, but it is the reason nothing here retries indefinitely.
 ******************************************************************************/

#include "arduino_shield.h"

#include "cybsp.h"
#include "cy_gpio.h"
#include "cy_scb_i2c.h"
#include "display_i2c_config.h"

/*******************************************************************************
 * Pin table
 *
 * The single mapping from logical Arduino pin to physical port and pin, taken
 * from schematic sheet 17. Entries with a NULL port are not routed on this
 * board; the capability table already refuses them, so reaching one here would
 * be a bug in the core rather than a user error -- hence the defensive check.
 ******************************************************************************/

typedef struct
{
    GPIO_PRT_Type *port;
    uint32_t       pin;
} phys_pin_t;

static const phys_pin_t s_phys[ARDUINO_PIN_COUNT] =
{
    [ARDUINO_D0]  = { P15_0_PORT, P15_0_PIN },
    [ARDUINO_D1]  = { P15_1_PORT, P15_1_PIN },
    [ARDUINO_D2]  = { P13_0_PORT, P13_0_PIN },
    [ARDUINO_D3]  = { P13_5_PORT, P13_5_PIN },
    [ARDUINO_D4]  = { P13_6_PORT, P13_6_PIN },
    [ARDUINO_D5]  = { P13_4_PORT, P13_4_PIN },
    [ARDUINO_D6]  = { P13_3_PORT, P13_3_PIN },
    [ARDUINO_D7]  = { P13_7_PORT, P13_7_PIN },
    [ARDUINO_D8]  = { P15_3_PORT, P15_3_PIN },
    [ARDUINO_D9]  = { P15_2_PORT, P15_2_PIN },
    [ARDUINO_D10] = { P9_0_PORT,  P9_0_PIN  },
    [ARDUINO_D11] = { P9_2_PORT,  P9_2_PIN  },
    [ARDUINO_D12] = { P9_1_PORT,  P9_1_PIN  },
    [ARDUINO_D13] = { P9_3_PORT,  P9_3_PIN  },
    /* A0-A3 unrouted; A4/A5 are the I2C pins and are never driven as GPIO. */
};

static const phys_pin_t *phys(arduino_pin_t pin)
{
    if (((unsigned)pin >= (unsigned)ARDUINO_PIN_COUNT) ||
        (s_phys[pin].port == NULL))
    {
        return NULL;
    }

    return &s_phys[pin];
}

/*******************************************************************************
 * Digital I/O
 *
 * Every one of these pins sits behind a TXB0106 auto-direction level shifter.
 * That is why ARDUINO_MODE_INPUT_PULLUP maps to a plain high-impedance input
 * rather than enabling the internal pull-up: the internal pull is far stiffer
 * than the ~50 kOhm ceiling the shifter tolerates, and enabling it would latch
 * the shifter's direction sensing the wrong way. A shield that needs a pull on
 * a header pin has to provide it externally, at 50 kOhm or weaker.
 ******************************************************************************/

static int qwa309_pin_mode(void *ctx, arduino_pin_t pin, arduino_mode_t mode)
{
    const phys_pin_t *p = phys(pin);
    uint32_t          drive;

    (void)ctx;

    if (p == NULL)
    {
        return -1;
    }

    switch (mode)
    {
        case ARDUINO_MODE_OUTPUT:
            drive = CY_GPIO_DM_STRONG_IN_OFF;
            break;

        case ARDUINO_MODE_INPUT_PULLUP:
            /* Deliberately NOT CY_GPIO_DM_PULLUP -- see the note above. */
            drive = CY_GPIO_DM_HIGHZ;
            break;

        case ARDUINO_MODE_INPUT:
        default:
            drive = CY_GPIO_DM_HIGHZ;
            break;
    }

    Cy_GPIO_SetHSIOM(p->port, p->pin, HSIOM_SEL_GPIO);
    Cy_GPIO_SetDrivemode(p->port, p->pin, drive);

    return 0;
}

static int qwa309_digital_write(void *ctx, arduino_pin_t pin, bool level)
{
    const phys_pin_t *p = phys(pin);

    (void)ctx;

    if (p == NULL)
    {
        return -1;
    }

    Cy_GPIO_Write(p->port, p->pin, level ? 1UL : 0UL);

    return 0;
}

static int qwa309_digital_read(void *ctx, arduino_pin_t pin, bool *level)
{
    const phys_pin_t *p = phys(pin);

    (void)ctx;

    if ((p == NULL) || (level == NULL))
    {
        return -1;
    }

    *level = (Cy_GPIO_Read(p->port, p->pin) != 0UL);

    return 0;
}

/*******************************************************************************
 * PWM -- not implemented on this board yet
 *
 * The board genuinely can do it: P15.2 offers tcpwm[0].line[275] and P9.0
 * offers tcpwm[0].line_compl[279], both in TCPWM group 1, and the capability
 * table says so. What is missing is a verified peripheral-divider assignment.
 *
 * The existing header PWM in mod_qwa309_header.c requests 16-bit divider
 * instance 7, and PERI0 group 1 provides only four -- so that code has never
 * produced a waveform. Copying its shape would reproduce the bug; picking a
 * different instance without confirming what else owns the group would be a
 * guess of exactly the kind that costs a bench session.
 *
 * Leaving these NULL makes arduino_shield_pwm() return
 * ARDUINO_ERR_UNSUPPORTED, which reads as "the board can, this driver does
 * not" rather than pretending the hardware is at fault.
 *
 * Only the two servo headers on the Motor Shield v2 need this. All four DC
 * motors and both steppers are driven entirely over I2C and are unaffected.
 ******************************************************************************/

const arduino_ops_t arduino_qwa309_ops =
{
    .pin_mode      = qwa309_pin_mode,
    .digital_write = qwa309_digital_write,
    .digital_read  = qwa309_digital_read,
    .pwm_set       = NULL,
    .pwm_stop      = NULL,
    .adc_read      = NULL,
    .ctx           = NULL,
};

/*******************************************************************************
 * Header I2C -- SCB5, shared with display and touch
 *
 * Borrows the controller the display already brought up, the same way
 * dfr0522_rgb.c does. Initialising a second controller on the same SCB would
 * reset the block underneath the display.
 ******************************************************************************/

extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

#define HDR_I2C_HW          DISPLAY_I2C_CONTROLLER_HW
#define HDR_I2C_CTX         (&disp_touch_i2c_controller_context)

/**
 * Per-byte timeout.
 *
 * The PCA9685 itself never stretches the clock, but the display and touch
 * controllers share this bus, and the SCB has its own 25 ms bus time-out. A
 * bounded wait here means a contended bus surfaces as a failed transfer the
 * caller can report, rather than as a stalled GFX task and a frozen screen.
 */
#define HDR_I2C_TIMEOUT_MS  (10U)

static int qwa309_i2c_write(void *ctx, uint8_t addr7,
                            const uint8_t *data, size_t len)
{
    cy_en_scb_i2c_status_t status;
    cy_en_scb_i2c_status_t stop_status;
    size_t                 i;

    (void)ctx;

    if ((data == NULL) && (len > 0U))
    {
        return -1;
    }

    status = Cy_SCB_I2C_MasterSendStart(HDR_I2C_HW, addr7,
                                        CY_SCB_I2C_WRITE_XFER,
                                        HDR_I2C_TIMEOUT_MS, HDR_I2C_CTX);

    if (status == CY_SCB_I2C_SUCCESS)
    {
        for (i = 0U; i < len; i++)
        {
            status = Cy_SCB_I2C_MasterWriteByte(HDR_I2C_HW, data[i],
                                                HDR_I2C_TIMEOUT_MS, HDR_I2C_CTX);
            if (status != CY_SCB_I2C_SUCCESS)
            {
                break;
            }
        }
    }

    /* Always release the bus, even after a failure -- leaving it held would
     * take the display down with us. */
    stop_status = Cy_SCB_I2C_MasterSendStop(HDR_I2C_HW, HDR_I2C_TIMEOUT_MS,
                                            HDR_I2C_CTX);

    if (status == CY_SCB_I2C_SUCCESS)
    {
        status = stop_status;
    }

    return (status == CY_SCB_I2C_SUCCESS) ? 0 : -1;
}

static int qwa309_i2c_write_read(void *ctx, uint8_t addr7,
                                 const uint8_t *wdata, size_t wlen,
                                 uint8_t *rdata, size_t rlen)
{
    cy_en_scb_i2c_status_t status;
    cy_en_scb_i2c_status_t stop_status;
    size_t                 i;

    (void)ctx;

    if (((wdata == NULL) && (wlen > 0U)) || ((rdata == NULL) && (rlen > 0U)))
    {
        return -1;
    }

    status = Cy_SCB_I2C_MasterSendStart(HDR_I2C_HW, addr7,
                                        CY_SCB_I2C_WRITE_XFER,
                                        HDR_I2C_TIMEOUT_MS, HDR_I2C_CTX);

    if (status == CY_SCB_I2C_SUCCESS)
    {
        for (i = 0U; i < wlen; i++)
        {
            status = Cy_SCB_I2C_MasterWriteByte(HDR_I2C_HW, wdata[i],
                                                HDR_I2C_TIMEOUT_MS, HDR_I2C_CTX);
            if (status != CY_SCB_I2C_SUCCESS)
            {
                break;
            }
        }
    }

    /* Repeated START rather than STOP-then-START: releasing the bus between
     * the register write and the read would let another master interleave and
     * change the register pointer under us. */
    if (status == CY_SCB_I2C_SUCCESS)
    {
        status = Cy_SCB_I2C_MasterSendReStart(HDR_I2C_HW, addr7,
                                              CY_SCB_I2C_READ_XFER,
                                              HDR_I2C_TIMEOUT_MS, HDR_I2C_CTX);
    }

    if (status == CY_SCB_I2C_SUCCESS)
    {
        for (i = 0U; i < rlen; i++)
        {
            /* NAK the final byte, ACK the rest -- the slave needs to be told
             * when to stop driving. */
            cy_en_scb_i2c_command_t ack = ((i + 1U) < rlen) ? CY_SCB_I2C_ACK
                                                            : CY_SCB_I2C_NAK;

            status = Cy_SCB_I2C_MasterReadByte(HDR_I2C_HW, ack, &rdata[i],
                                               HDR_I2C_TIMEOUT_MS, HDR_I2C_CTX);
            if (status != CY_SCB_I2C_SUCCESS)
            {
                break;
            }
        }
    }

    stop_status = Cy_SCB_I2C_MasterSendStop(HDR_I2C_HW, HDR_I2C_TIMEOUT_MS,
                                            HDR_I2C_CTX);

    if (status == CY_SCB_I2C_SUCCESS)
    {
        status = stop_status;
    }

    return (status == CY_SCB_I2C_SUCCESS) ? 0 : -1;
}

/**
 * Short busy wait.
 *
 * Used only by the PCA9685 driver's oscillator settle, which asks for 500 us
 * once per frequency change. A task delay would round that up to a whole tick
 * and, worse, would yield the GFX task in the middle of bringing a device up.
 */
static void qwa309_i2c_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    Cy_SysLib_DelayUs((uint16_t)((us > 65535U) ? 65535U : us));
}

const arduino_i2c_t arduino_qwa309_i2c =
{
    .write      = qwa309_i2c_write,
    .write_read = qwa309_i2c_write_read,
    .delay_us   = qwa309_i2c_delay_us,
    .ctx        = NULL,
};
