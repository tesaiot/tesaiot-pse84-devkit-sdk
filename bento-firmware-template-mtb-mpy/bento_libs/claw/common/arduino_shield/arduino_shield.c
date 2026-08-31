/*******************************************************************************
 * File:    arduino_shield.c
 *
 * Purpose: Arduino Uno R3 header abstraction. See arduino_shield.h.
 *
 * Note:    No platform API appears here. Everything physical goes through the
 *          board descriptor's ops vtable, which is what lets the whole table
 *          and claim logic be tested on a development machine.
 ******************************************************************************/

#include "arduino_shield.h"

/*******************************************************************************
 * Module state
 *
 * A single header per board, so a single instance is the honest model. Making
 * this a handle would suggest a board could have two Arduino headers, which
 * none of ours does.
 ******************************************************************************/

static const arduino_board_t *s_board;

/** Which shield owns each pin, or NULL. Indexed by arduino_pin_t. */
static const arduino_shield_desc_t *s_owner[ARDUINO_PIN_COUNT];

static const arduino_shield_desc_t *s_attached[ARDUINO_SHIELD_MAX];
static uint8_t                      s_attached_count;

/*******************************************************************************
 * Helpers
 ******************************************************************************/

static bool pin_valid(arduino_pin_t pin)
{
    return ((unsigned)pin) < (unsigned)ARDUINO_PIN_COUNT;
}

static arduino_status_t require_board(void)
{
    return (s_board != NULL) ? ARDUINO_OK : ARDUINO_ERR_NO_BOARD;
}

/*******************************************************************************
 * Board
 ******************************************************************************/

arduino_status_t arduino_shield_set_board(const arduino_board_t *board)
{
    if ((board == NULL) || (board->caps == NULL) || (board->ops == NULL))
    {
        return ARDUINO_ERR_PARAM;
    }

    s_board = board;

    return ARDUINO_OK;
}

const arduino_board_t *arduino_shield_board(void)
{
    return s_board;
}

uint16_t arduino_shield_caps(arduino_pin_t pin)
{
    if ((s_board == NULL) || !pin_valid(pin))
    {
        return 0U;
    }

    return s_board->caps[pin];
}

const char *arduino_shield_pin_name(arduino_pin_t pin)
{
    if ((s_board == NULL) || !pin_valid(pin) || (s_board->pin_names == NULL) ||
        (s_board->pin_names[pin] == NULL))
    {
        return "?";
    }

    return s_board->pin_names[pin];
}

arduino_status_t arduino_shield_check(arduino_pin_t pin, uint16_t want)
{
    uint16_t caps;

    if (!pin_valid(pin))
    {
        return ARDUINO_ERR_PARAM;
    }

    if (s_board == NULL)
    {
        return ARDUINO_ERR_NO_BOARD;
    }

    caps = s_board->caps[pin];

    /* Report the most specific reason. "Not routed" sends someone to the
     * schematic; an erratum sends them to the errata list; "no capability"
     * sends them to the pin-function table. Collapsing all three into one
     * error would waste an afternoon.
     *
     * Order matters where a pin fails for more than one reason at once, which
     * is exactly the QWA309's D3: it has no TCPWM path AND its output does not
     * latch. Erratum is checked first because it is the stronger statement --
     * "this pin does not work" rather than "this pin cannot do that one
     * thing". Answering "no PWM" would invite the reader to reach for D3 as an
     * ordinary GPIO and fail again further downstream. */
    if ((caps & ARDUINO_CAP_ROUTED) == 0U)
    {
        return ARDUINO_ERR_NOT_ROUTED;
    }

    if ((caps & ARDUINO_CAP_ERRATUM) != 0U)
    {
        return ARDUINO_ERR_ERRATUM;
    }

    if ((caps & want) != want)
    {
        return ARDUINO_ERR_NO_CAP;
    }

    return ARDUINO_OK;
}

/*******************************************************************************
 * Pin access
 ******************************************************************************/

arduino_status_t arduino_shield_pin_mode(arduino_pin_t pin, arduino_mode_t mode)
{
    arduino_status_t status;
    uint16_t         want;

    switch (mode)
    {
        case ARDUINO_MODE_OUTPUT: want = ARDUINO_CAP_DIGITAL_OUT; break;
        case ARDUINO_MODE_ANALOG: want = ARDUINO_CAP_ADC;         break;
        default:                  want = ARDUINO_CAP_DIGITAL_IN;  break;
    }

    status = arduino_shield_check(pin, want);
    if (status != ARDUINO_OK)
    {
        return status;
    }

    if (s_board->ops->pin_mode == NULL)
    {
        return ARDUINO_ERR_UNSUPPORTED;
    }

    return (s_board->ops->pin_mode(s_board->ops->ctx, pin, mode) == 0)
           ? ARDUINO_OK : ARDUINO_ERR_IO;
}

arduino_status_t arduino_shield_write(arduino_pin_t pin, bool level)
{
    arduino_status_t status = arduino_shield_check(pin, ARDUINO_CAP_DIGITAL_OUT);

    if (status != ARDUINO_OK)
    {
        return status;
    }

    if (s_board->ops->digital_write == NULL)
    {
        return ARDUINO_ERR_UNSUPPORTED;
    }

    return (s_board->ops->digital_write(s_board->ops->ctx, pin, level) == 0)
           ? ARDUINO_OK : ARDUINO_ERR_IO;
}

arduino_status_t arduino_shield_read(arduino_pin_t pin, bool *level)
{
    arduino_status_t status;

    if (level == NULL)
    {
        return ARDUINO_ERR_PARAM;
    }

    status = arduino_shield_check(pin, ARDUINO_CAP_DIGITAL_IN);
    if (status != ARDUINO_OK)
    {
        return status;
    }

    if (s_board->ops->digital_read == NULL)
    {
        return ARDUINO_ERR_UNSUPPORTED;
    }

    return (s_board->ops->digital_read(s_board->ops->ctx, pin, level) == 0)
           ? ARDUINO_OK : ARDUINO_ERR_IO;
}

arduino_status_t arduino_shield_pwm(arduino_pin_t pin, uint32_t freq_hz,
                                    uint16_t duty_permille)
{
    arduino_status_t status;

    if (duty_permille > 1000U)
    {
        return ARDUINO_ERR_PARAM;
    }

    status = arduino_shield_check(pin, ARDUINO_CAP_PWM);
    if (status != ARDUINO_OK)
    {
        return status;
    }

    if (s_board->ops->pwm_set == NULL)
    {
        return ARDUINO_ERR_UNSUPPORTED;
    }

    return (s_board->ops->pwm_set(s_board->ops->ctx, pin, freq_hz, duty_permille) == 0)
           ? ARDUINO_OK : ARDUINO_ERR_IO;
}

arduino_status_t arduino_shield_pwm_stop(arduino_pin_t pin)
{
    arduino_status_t status = arduino_shield_check(pin, ARDUINO_CAP_PWM);

    if (status != ARDUINO_OK)
    {
        return status;
    }

    if (s_board->ops->pwm_stop == NULL)
    {
        return ARDUINO_ERR_UNSUPPORTED;
    }

    return (s_board->ops->pwm_stop(s_board->ops->ctx, pin) == 0)
           ? ARDUINO_OK : ARDUINO_ERR_IO;
}

arduino_status_t arduino_shield_adc(arduino_pin_t pin, uint16_t *raw)
{
    arduino_status_t status;

    if (raw == NULL)
    {
        return ARDUINO_ERR_PARAM;
    }

    status = arduino_shield_check(pin, ARDUINO_CAP_ADC);
    if (status != ARDUINO_OK)
    {
        return status;
    }

    if (s_board->ops->adc_read == NULL)
    {
        return ARDUINO_ERR_UNSUPPORTED;
    }

    return (s_board->ops->adc_read(s_board->ops->ctx, pin, raw) == 0)
           ? ARDUINO_OK : ARDUINO_ERR_IO;
}

const arduino_i2c_t *arduino_shield_i2c(void)
{
    return (s_board != NULL) ? s_board->i2c : NULL;
}

/*******************************************************************************
 * Shields
 ******************************************************************************/

arduino_status_t arduino_shield_attach(arduino_shield_desc_t *shield,
                                       arduino_pin_t *failed_pin)
{
    arduino_status_t status;
    uint8_t          i;

    if ((shield == NULL) || (shield->pin_count > ARDUINO_SHIELD_MAX_PINS))
    {
        return ARDUINO_ERR_PARAM;
    }

    status = require_board();
    if (status != ARDUINO_OK)
    {
        return status;
    }

    if (s_attached_count >= ARDUINO_SHIELD_MAX)
    {
        return ARDUINO_ERR_FULL;
    }

    if (shield->needs_i2c && (s_board->i2c == NULL))
    {
        return ARDUINO_ERR_NOT_ROUTED;
    }

    /* Validate everything before claiming anything, so a rejected attach leaves
     * no partial ownership behind. */
    for (i = 0U; i < shield->pin_count; i++)
    {
        arduino_pin_t pin = shield->pins[i];

        status = arduino_shield_check(pin, shield->pin_caps[i]);
        if (status != ARDUINO_OK)
        {
            if (failed_pin != NULL) { *failed_pin = pin; }
            return status;
        }

        if ((s_owner[pin] != NULL) && (s_owner[pin] != shield))
        {
            if (failed_pin != NULL) { *failed_pin = pin; }
            return ARDUINO_ERR_IN_USE;
        }
    }

    for (i = 0U; i < shield->pin_count; i++)
    {
        s_owner[shield->pins[i]] = shield;
    }

    if (shield->on_attach != NULL)
    {
        if (shield->on_attach(shield->ctx) != 0)
        {
            for (i = 0U; i < shield->pin_count; i++)
            {
                s_owner[shield->pins[i]] = NULL;
            }
            return ARDUINO_ERR_IO;
        }
    }

    s_attached[s_attached_count] = shield;
    s_attached_count++;

    return ARDUINO_OK;
}

arduino_status_t arduino_shield_detach(arduino_shield_desc_t *shield)
{
    uint8_t i;
    uint8_t j;

    if (shield == NULL)
    {
        return ARDUINO_ERR_PARAM;
    }

    if (shield->on_detach != NULL)
    {
        shield->on_detach(shield->ctx);
    }

    for (i = 0U; i < ARDUINO_PIN_COUNT; i++)
    {
        if (s_owner[i] == shield)
        {
            s_owner[i] = NULL;
        }
    }

    for (i = 0U; i < s_attached_count; i++)
    {
        if (s_attached[i] == shield)
        {
            for (j = (uint8_t)(i + 1U); j < s_attached_count; j++)
            {
                s_attached[j - 1U] = s_attached[j];
            }
            s_attached_count--;
            break;
        }
    }

    return ARDUINO_OK;
}

uint8_t arduino_shield_attached_count(void)
{
    return s_attached_count;
}

const char *arduino_shield_strerror(arduino_status_t status)
{
    const char *text;

    switch (status)
    {
        case ARDUINO_OK:             text = "ok";                              break;
        case ARDUINO_ERR_PARAM:      text = "invalid argument";                break;
        case ARDUINO_ERR_NO_BOARD:   text = "no board descriptor installed";   break;
        case ARDUINO_ERR_NOT_ROUTED: text = "pin not routed on this board";    break;
        case ARDUINO_ERR_NO_CAP:     text = "pin cannot do that";              break;
        case ARDUINO_ERR_ERRATUM:    text = "pin has a hardware erratum";      break;
        case ARDUINO_ERR_IN_USE:     text = "pin already claimed";             break;
        case ARDUINO_ERR_IO:         text = "hardware access failed";          break;
        case ARDUINO_ERR_FULL:       text = "too many shields attached";       break;
        case ARDUINO_ERR_UNSUPPORTED:
            text = "board can, but this driver does not implement it";         break;
        default:                     text = "unknown error";                   break;
    }

    return text;
}

void arduino_shield_reset(void)
{
    uint8_t i;

    for (i = 0U; i < ARDUINO_PIN_COUNT; i++)
    {
        s_owner[i] = NULL;
    }

    s_attached_count = 0U;
    s_board          = NULL;
}
