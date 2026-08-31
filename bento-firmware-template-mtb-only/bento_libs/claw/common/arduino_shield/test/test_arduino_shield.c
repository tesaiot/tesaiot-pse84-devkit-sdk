/*******************************************************************************
 * File:    test_arduino_shield.c
 *
 * Purpose: Host unit tests for the Arduino header abstraction, including the
 *          real QWA309 capability table.
 *
 *          The table is the part of this stack that is easiest to get wrong and
 *          most expensive to get wrong -- a mistake here sends a driver at a
 *          pin the board never wired, and the symptom is silence on a bench.
 *          Testing it needs no hardware, so it happens here.
 ******************************************************************************/

#include "../arduino_shield.h"

#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Stub physical layer
 *
 * The QWA309 descriptor references its ops and I2C vtables as externs so the
 * table can be built without the PSoC HAL. Supplying stubs here is what lets
 * the real production table into a host test.
 ******************************************************************************/

static int  s_last_level = -1;
static int  stub_mode(void *c, arduino_pin_t p, arduino_mode_t m)
{ (void)c; (void)p; (void)m; return 0; }
static int  stub_write(void *c, arduino_pin_t p, bool l)
{ (void)c; (void)p; s_last_level = l ? 1 : 0; return 0; }
static int  stub_read(void *c, arduino_pin_t p, bool *l)
{ (void)c; (void)p; *l = true; return 0; }
static int  stub_pwm(void *c, arduino_pin_t p, uint32_t f, uint16_t d)
{ (void)c; (void)p; (void)f; (void)d; return 0; }
static int  stub_pwm_stop(void *c, arduino_pin_t p) { (void)c; (void)p; return 0; }
static int  stub_adc(void *c, arduino_pin_t p, uint16_t *r)
{ (void)c; (void)p; *r = 0; return 0; }

const arduino_ops_t arduino_qwa309_ops =
{
    .pin_mode = stub_mode, .digital_write = stub_write, .digital_read = stub_read,
    .pwm_set = stub_pwm, .pwm_stop = stub_pwm_stop, .adc_read = stub_adc,
    .ctx = NULL,
};

static int stub_i2c_w(void *c, uint8_t a, const uint8_t *d, size_t n)
{ (void)c; (void)a; (void)d; (void)n; return 0; }
static int stub_i2c_wr(void *c, uint8_t a, const uint8_t *w, size_t wn,
                       uint8_t *r, size_t rn)
{ (void)c; (void)a; (void)w; (void)wn; (void)r; (void)rn; return 0; }
static void stub_i2c_delay(void *c, uint32_t us) { (void)c; (void)us; }

const arduino_i2c_t arduino_qwa309_i2c =
{
    .write = stub_i2c_w, .write_read = stub_i2c_wr,
    .delay_us = stub_i2c_delay, .ctx = NULL,
};

extern const arduino_board_t arduino_board_qwa309;

/*******************************************************************************
 * Assertions
 ******************************************************************************/

static int g_fail;
static int g_checks;

static void check(int cond, const char *what)
{
    g_checks++;
    if (cond) { printf("  ok   %s\n", what); }
    else      { printf("  FAIL %s\n", what); g_fail++; }
}

static void check_status(arduino_status_t got, arduino_status_t want,
                         const char *what)
{
    g_checks++;
    if (got == want)
    {
        printf("  ok   %s\n", what);
    }
    else
    {
        printf("  FAIL %s : got \"%s\", wanted \"%s\"\n",
               what, arduino_shield_strerror(got), arduino_shield_strerror(want));
        g_fail++;
    }
}

/*******************************************************************************
 * Tests
 ******************************************************************************/

static void test_no_board(void)
{
    printf("\nnothing works before a board is installed\n");
    arduino_shield_reset();

    check_status(arduino_shield_check(ARDUINO_D4, ARDUINO_CAP_DIGITAL_OUT),
                 ARDUINO_ERR_NO_BOARD, "check refuses without a board");
    check_status(arduino_shield_write(ARDUINO_D4, true),
                 ARDUINO_ERR_NO_BOARD, "write refuses without a board");
    check(arduino_shield_i2c() == NULL, "no I2C without a board");
    check(strcmp(arduino_shield_pin_name(ARDUINO_D4), "?") == 0,
          "pin name degrades to \"?\" rather than crashing");
}

static void test_qwa309_table(void)
{
    printf("\nQWA309 capability table matches schematic sheet 17\n");
    arduino_shield_reset();
    check_status(arduino_shield_set_board(&arduino_board_qwa309), ARDUINO_OK,
                 "board installs");

    check(strcmp(arduino_shield_pin_name(ARDUINO_D6), "P13.3") == 0,
          "D6 names P13.3");
    check(strcmp(arduino_shield_pin_name(ARDUINO_D10), "P9.0") == 0,
          "D10 names P9.0");
    check(strcmp(arduino_shield_pin_name(ARDUINO_A4), "P17.1") == 0,
          "A4 names P17.1 (SDA)");

    /* The three distinct ways a pin can be unavailable, each reported
     * separately, because each sends you somewhere different to fix it. */
    check_status(arduino_shield_check(ARDUINO_A0, ARDUINO_CAP_DIGITAL_IN),
                 ARDUINO_ERR_NOT_ROUTED, "A0 is not routed at all");
    check_status(arduino_shield_check(ARDUINO_D4, ARDUINO_CAP_PWM),
                 ARDUINO_ERR_NO_CAP, "D4 exists but has no PWM path");
    check_status(arduino_shield_check(ARDUINO_D3, ARDUINO_CAP_DIGITAL_OUT),
                 ARDUINO_ERR_ERRATUM, "D3 is routed and capable on paper, but broken");
    check_status(arduino_shield_check(ARDUINO_D2, ARDUINO_CAP_DIGITAL_OUT),
                 ARDUINO_ERR_ERRATUM, "D2 likewise");

    check_status(arduino_shield_check(ARDUINO_D6, ARDUINO_CAP_PWM), ARDUINO_OK,
                 "D6 can do hardware PWM");
    check_status(arduino_shield_check(ARDUINO_D9, ARDUINO_CAP_PWM), ARDUINO_OK,
                 "D9 can do hardware PWM (servo 2 on the motor shield)");
    check_status(arduino_shield_check(ARDUINO_D10, ARDUINO_CAP_PWM), ARDUINO_OK,
                 "D10 can do hardware PWM (servo 1)");

    /* Claiming an unverified capability would push discovery of its absence out
     * to a bench session, so ADC is absent everywhere until measured. */
    check_status(arduino_shield_check(ARDUINO_D9, ARDUINO_CAP_ADC),
                 ARDUINO_ERR_NO_CAP, "ADC is not claimed anywhere yet");

    check(arduino_shield_board()->ioref_present == false,
          "IOREF is reported absent, as sheet 17 shows");
    check(arduino_shield_board()->logic_mv == 3300U, "header logic is 3.3 V");
    check(arduino_shield_i2c() != NULL, "header I2C is available");

    /* Every digital pin is behind a TXB0106; SDA/SCL are not. A driver that
     * wants to enable a pull needs to know which. */
    check((arduino_shield_caps(ARDUINO_D6) & ARDUINO_CAP_LEVEL_SHIFTED) != 0U,
          "D6 is flagged level-shifted");
    check((arduino_shield_caps(ARDUINO_A4) & ARDUINO_CAP_LEVEL_SHIFTED) == 0U,
          "A4/SDA is direct 3.3 V, not shifted");
}

static void test_access_is_gated(void)
{
    bool level = false;

    printf("\npin access is refused before it reaches hardware\n");
    arduino_shield_reset();
    (void)arduino_shield_set_board(&arduino_board_qwa309);

    s_last_level = -1;
    check_status(arduino_shield_write(ARDUINO_D3, true), ARDUINO_ERR_ERRATUM,
                 "writing the erratum pin is refused");
    check(s_last_level == -1, "and the hardware op was never called");

    check_status(arduino_shield_write(ARDUINO_D4, true), ARDUINO_OK,
                 "a good pin writes");
    check(s_last_level == 1, "and reaches the hardware op");

    check_status(arduino_shield_read(ARDUINO_A2, &level), ARDUINO_ERR_NOT_ROUTED,
                 "reading an unrouted pin is refused");
    check_status(arduino_shield_pwm(ARDUINO_D4, 1000U, 500U), ARDUINO_ERR_NO_CAP,
                 "PWM on a non-PWM pin is refused");
    check_status(arduino_shield_pwm(ARDUINO_D6, 1000U, 1100U), ARDUINO_ERR_PARAM,
                 "duty above 100.0% is refused");
}

/*******************************************************************************
 * Shield registry
 ******************************************************************************/

static int  s_attach_calls;
static int  s_detach_calls;
static int  s_attach_result;

static int  cb_attach(void *ctx) { (void)ctx; s_attach_calls++; return s_attach_result; }
static void cb_detach(void *ctx) { (void)ctx; s_detach_calls++; }

static arduino_shield_desc_t make_shield(const char *name, arduino_pin_t pin,
                                         uint16_t cap, bool i2c)
{
    arduino_shield_desc_t s;

    memset(&s, 0, sizeof(s));
    s.name        = name;
    s.pins[0]     = pin;
    s.pin_caps[0] = cap;
    s.pin_count   = 1U;
    s.needs_i2c   = i2c;
    s.on_attach   = cb_attach;
    s.on_detach   = cb_detach;

    return s;
}

static void test_shield_registry(void)
{
    arduino_shield_desc_t good  = make_shield("good",  ARDUINO_D6, ARDUINO_CAP_PWM, false);
    arduino_shield_desc_t rival = make_shield("rival", ARDUINO_D6, ARDUINO_CAP_PWM, false);
    arduino_shield_desc_t bad   = make_shield("bad",   ARDUINO_D3, ARDUINO_CAP_DIGITAL_OUT, false);
    arduino_shield_desc_t gone  = make_shield("gone",  ARDUINO_A1, ARDUINO_CAP_DIGITAL_IN, false);
    arduino_pin_t failed = ARDUINO_PIN_COUNT;

    printf("\nshields are validated once, at attach\n");
    arduino_shield_reset();
    (void)arduino_shield_set_board(&arduino_board_qwa309);
    s_attach_calls = 0; s_detach_calls = 0; s_attach_result = 0;

    check_status(arduino_shield_attach(&good, &failed), ARDUINO_OK, "good shield attaches");
    check(s_attach_calls == 1, "its on_attach ran");
    check(arduino_shield_attached_count() == 1U, "counted as attached");

    /* Two shields cannot own the same pin. Without this a second driver would
     * quietly fight the first for the same output. */
    failed = ARDUINO_PIN_COUNT;
    check_status(arduino_shield_attach(&rival, &failed), ARDUINO_ERR_IN_USE,
                 "a second shield cannot claim the same pin");
    check(failed == ARDUINO_D6, "and it names the contested pin");

    /* A shield asking for a broken pin is told at attach, not at first use. */
    failed = ARDUINO_PIN_COUNT;
    check_status(arduino_shield_attach(&bad, &failed), ARDUINO_ERR_ERRATUM,
                 "a shield needing an erratum pin is refused");
    check(failed == ARDUINO_D3, "and it names the pin");

    failed = ARDUINO_PIN_COUNT;
    check_status(arduino_shield_attach(&gone, &failed), ARDUINO_ERR_NOT_ROUTED,
                 "a shield needing an unrouted pin is refused");

    check(arduino_shield_attached_count() == 1U,
          "no failed attach left anything behind");

    check_status(arduino_shield_detach(&good), ARDUINO_OK, "detach succeeds");
    check(s_detach_calls == 1, "its on_detach ran");
    check(arduino_shield_attached_count() == 0U, "count drops");

    check_status(arduino_shield_attach(&rival, NULL), ARDUINO_OK,
                 "the pin is free for the rival once released");
}

static void test_attach_failure_releases_pins(void)
{
    arduino_shield_desc_t s = make_shield("fails", ARDUINO_D6, ARDUINO_CAP_PWM, false);
    arduino_shield_desc_t other = make_shield("other", ARDUINO_D6, ARDUINO_CAP_PWM, false);

    printf("\na shield whose on_attach fails leaves no claim behind\n");
    arduino_shield_reset();
    (void)arduino_shield_set_board(&arduino_board_qwa309);
    s_attach_result = -1;

    check_status(arduino_shield_attach(&s, NULL), ARDUINO_ERR_IO,
                 "attach reports the callback failure");
    check(arduino_shield_attached_count() == 0U, "not counted as attached");

    s_attach_result = 0;
    check_status(arduino_shield_attach(&other, NULL), ARDUINO_OK,
                 "the pin was released, so another shield can take it");
}

static void test_motor_shield_v2_fits(void)
{
    arduino_shield_desc_t ms;

    printf("\nthe Motor Shield v2 profile fits this board\n");
    arduino_shield_reset();
    (void)arduino_shield_set_board(&arduino_board_qwa309);
    s_attach_result = 0;

    memset(&ms, 0, sizeof(ms));
    ms.name      = "motor_shield_v2";
    ms.needs_i2c = true;
    /* Motor control is entirely over I2C. The only header pins the shield uses
     * are the two servo signals, which bypass the PCA9685 and hang off D9/D10. */
    ms.pins[0]     = ARDUINO_D9;  ms.pin_caps[0] = ARDUINO_CAP_PWM;
    ms.pins[1]     = ARDUINO_D10; ms.pin_caps[1] = ARDUINO_CAP_PWM;
    ms.pin_count   = 2U;

    check_status(arduino_shield_attach(&ms, NULL), ARDUINO_OK,
                 "attaches: I2C plus two PWM-capable servo pins");

    /* The v1 shield, for contrast, needs D3 for one of its four motor PWMs --
     * and D3 is one of this board's two broken pins. This is the concrete
     * reason the v1 shield was abandoned in favour of the v2. */
    {
        arduino_shield_desc_t v1;
        arduino_pin_t failed = ARDUINO_PIN_COUNT;

        memset(&v1, 0, sizeof(v1));
        v1.name        = "motor_shield_v1";
        v1.pins[0]     = ARDUINO_D3;  v1.pin_caps[0] = ARDUINO_CAP_PWM;
        v1.pin_count   = 1U;

        check_status(arduino_shield_attach(&v1, &failed), ARDUINO_ERR_ERRATUM,
                     "the v1 shield is refused, naming the reason");
        check(failed == ARDUINO_D3, "on D3");
    }
}

int main(void)
{
    printf("Arduino shield core -- host tests\n");
    printf("=================================\n");

    test_no_board();
    test_qwa309_table();
    test_access_is_gated();
    test_shield_registry();
    test_attach_failure_releases_pins();
    test_motor_shield_v2_fits();

    printf("\n---------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_fail);

    return (g_fail == 0) ? 0 : 1;
}
