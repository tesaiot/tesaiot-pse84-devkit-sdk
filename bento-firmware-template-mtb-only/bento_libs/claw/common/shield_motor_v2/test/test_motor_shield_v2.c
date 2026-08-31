/*******************************************************************************
 * File:    test_motor_shield_v2.c
 *
 * Purpose: Host unit tests for the Motor Shield v2 driver.
 *
 *          These assert behaviour that is invisible from the outside but
 *          decides whether hardware survives: the ORDER in which direction and
 *          duty channels are written, that a stepper's channels cannot be
 *          hijacked by a DC call, that tick() never does unbounded work, and
 *          that the corrected microstep curve is the one actually emitted.
 ******************************************************************************/

#include "../motor_shield_v2.h"

#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Mock bus: decodes channel writes into (channel, on, off) operations
 ******************************************************************************/

#define MAX_OPS 256

typedef struct
{
    uint8_t  ch;
    uint16_t on;
    uint16_t off;
} op_t;

typedef struct
{
    op_t    ops[MAX_OPS];
    size_t  count;
    uint8_t mode1;
    int     fail_next;
} mock_t;

static mock_t g_mock;

static int mock_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
    mock_t *m = (mock_t *)ctx;

    (void)addr7;

    if (m->fail_next) { m->fail_next = 0; return -1; }

    if ((len >= 2U) && (data[0] == 0x00U))
    {
        m->mode1 = data[1] & 0x7FU;
    }

    /* Decode a 5-byte LEDn write into a logical operation. 0xFA is ALL_LED. */
    if ((len == 5U) && (m->count < MAX_OPS) &&
        (((data[0] >= 0x06U) && (data[0] < 0x46U)) || (data[0] == 0xFAU)))
    {
        op_t *o = &m->ops[m->count++];

        o->ch  = (data[0] == 0xFAU) ? 0xFFU : (uint8_t)((data[0] - 0x06U) / 4U);
        o->on  = (uint16_t)(data[1] | ((uint16_t)(data[2] & 0x1FU) << 8));
        o->off = (uint16_t)(data[3] | ((uint16_t)(data[4] & 0x1FU) << 8));
    }

    return 0;
}

static int mock_write_read(void *ctx, uint8_t addr7, const uint8_t *w, size_t wl,
                           uint8_t *r, size_t rl)
{
    mock_t *m = (mock_t *)ctx;

    (void)addr7; (void)wl;

    if ((w[0] == 0x00U) && (rl == 1U)) { r[0] = m->mode1; return 0; }

    return -1;
}

static void mock_delay(void *ctx, uint32_t us) { (void)ctx; (void)us; }

static pca9685_bus_t mock_bus(void)
{
    pca9685_bus_t b;

    b.write      = mock_write;
    b.write_read = mock_write_read;
    b.delay_us   = mock_delay;
    b.ctx        = &g_mock;

    return b;
}

/*******************************************************************************
 * Assertions
 ******************************************************************************/

static int g_fail;
static int g_checks;

static void ok(const char *what)   { g_checks++; printf("  ok   %s\n", what); }
static void bad(const char *what)  { g_checks++; g_fail++; printf("  FAIL %s\n", what); }
static void check(int cond, const char *what) { if (cond) { ok(what); } else { bad(what); } }

/** Assert operation `idx` targeted `ch` with the given on/off pair. */
static void expect_op(size_t idx, uint8_t ch, uint16_t on, uint16_t off,
                      const char *what)
{
    g_checks++;

    if (idx >= g_mock.count)
    {
        printf("  FAIL %s : only %zu ops, wanted #%zu\n", what, g_mock.count, idx);
        g_fail++;
        return;
    }

    if ((g_mock.ops[idx].ch != ch) ||
        (g_mock.ops[idx].on != on) || (g_mock.ops[idx].off != off))
    {
        printf("  FAIL %s :\n        got  ch%u on=%u off=%u\n"
               "        want ch%u on=%u off=%u\n",
               what, g_mock.ops[idx].ch, g_mock.ops[idx].on, g_mock.ops[idx].off,
               ch, on, off);
        g_fail++;
        return;
    }

    printf("  ok   %s\n", what);
}

#define FULL_ON_ON      4096U
#define FULL_ON_OFF     0U
#define FULL_OFF_ON     0U
#define FULL_OFF_OFF    4096U

static motor_shield_v2_t g_dev;

static void setup(void)
{
    pca9685_bus_t bus = mock_bus();

    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.mode1 = 0x11U;
    memset(&g_dev, 0, sizeof(g_dev));

    (void)motor_shield_v2_init(&g_dev, &bus, MOTOR_SHIELD_V2_ADDR_DEFAULT,
                               MOTOR_SHIELD_V2_FREQ_DEFAULT_HZ);
    g_mock.count = 0;
}

/*******************************************************************************
 * Tests
 ******************************************************************************/

static void test_init_parks_outputs(void)
{
    pca9685_bus_t bus = mock_bus();
    motor_shield_v2_t dev;

    printf("\ninit parks every output\n");
    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.mode1 = 0x11U;
    memset(&dev, 0, sizeof(dev));

    check(motor_shield_v2_init(&dev, &bus, 0x60,
                               MOTOR_SHIELD_V2_FREQ_DEFAULT_HZ)
          == MOTOR_SHIELD_V2_OK, "init succeeds");

    /* A warm reset does not reset the PCA9685, so a motor commanded by the
     * previous firmware image is still turning until this lands. */
    check(g_mock.count == 1U && g_mock.ops[0].ch == 0xFFU &&
          g_mock.ops[0].off == FULL_OFF_OFF,
          "one ALL_LED transaction drives everything full off");
}

static void test_dc_direction_ordering(void)
{
    printf("\nDC direction changes never pulse the brake\n");
    setup();

    /* Raise IN2 first so the next command has to lower it. */
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_BACKWARD, 0U);
    g_mock.count = 0;

    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);

    /* M1: PWM ch8, IN1 ch10, IN2 ch9. Lowering IN2 must come first -- raising
     * IN1 while IN2 is still high would leave both high for one full I2C
     * transaction, which is a short brake into a spinning motor. */
    expect_op(0, 9U, FULL_OFF_ON, FULL_OFF_OFF, "IN2 dropped first");
    expect_op(1, 10U, FULL_ON_ON, FULL_ON_OFF, "then IN1 raised");
    check(g_mock.count == 2U, "exactly two channel writes");
}

static void test_reversal_does_not_plug_brake(void)
{
    printf("\ndirection reversal drops duty before it flips\n");
    setup();

    (void)motor_shield_v2_dc_speed(&g_dev, 1, 255U, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);
    g_mock.count = 0;

    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_BACKWARD, 0U);

    /* Flipping the inputs while the duty is still at full scale is a plugging
     * brake into a spinning motor: the bridge drives against back-EMF, so the
     * winding sees roughly twice supply. This shield has no polyfuse, no sense
     * resistor, and the TB6612's thermal trip is invisible to firmware, so it
     * is the one remaining current spike the driver can still permit. Duty must
     * reach zero first. M1 is PWM ch8, IN1 ch10, IN2 ch9. */
    expect_op(0, 8U, FULL_OFF_ON, FULL_OFF_OFF, "PWM duty zeroed first");
    check(g_mock.count >= 3U, "then the direction inputs move");
    check(motor_shield_v2_dc_state(&g_dev, 1)->speed == 0U,
          "cached speed reports the motor is no longer driven");
}

static void test_brake_actually_brakes(void)
{
    printf("\nBRAKE drives both inputs high\n");
    setup();

    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_BRAKE, 0U);

    /* Adafruit defines BRAKE and then has no case for it in run(), so upstream
     * BRAKE is silently a no-op. Here both inputs go high: TB6612 short brake. */
    expect_op(0, 10U, FULL_ON_ON, FULL_ON_OFF, "IN1 high");
    expect_op(1, 9U, FULL_ON_ON, FULL_ON_OFF, "IN2 high");
}

static void test_release_is_a_coast(void)
{
    printf("\nRELEASE zeroes duty before dropping direction\n");
    setup();

    (void)motor_shield_v2_dc_speed(&g_dev, 1, 255U, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);
    g_mock.count = 0;

    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_RELEASE, 0U);

    /* Upstream's RELEASE never touches the duty, which makes it a brake rather
     * than a coast. Duty must go first: the reverse order leaves the bridge
     * enabled with both inputs low for one transaction. */
    expect_op(0, 8U, FULL_OFF_ON, FULL_OFF_OFF, "PWM duty zeroed first");
    expect_op(1, 10U, FULL_OFF_ON, FULL_OFF_OFF, "then IN1 dropped");

    check(motor_shield_v2_dc_state(&g_dev, 1)->speed == 0U,
          "cached speed reflects the release");
}

static void test_speed_scaling(void)
{
    printf("\nspeed 255 reaches full scale\n");
    setup();

    (void)motor_shield_v2_dc_speed(&g_dev, 2, 255U, 0U);
    /* Adafruit computes speed*16 = 4080, which is 99.63% and never 100%. */
    expect_op(0, 13U, 0U, 4095U, "255 -> compare 4095 exactly");

    g_mock.count = 0;
    (void)motor_shield_v2_dc_speed(&g_dev, 2, 128U, 0U);
    expect_op(0, 13U, 0U, 2056U, "128 -> 2056 (monotonic midpoint)");

    g_mock.count = 0;
    (void)motor_shield_v2_dc_speed(&g_dev, 2, 0U, 0U);
    expect_op(0, 13U, FULL_OFF_ON, FULL_OFF_OFF,
              "0 uses the full-off flag, not a zero compare");
}

static void test_stepper_owns_its_channels(void)
{
    printf("\nstepper ports lock out their DC channels\n");
    setup();

    check(motor_shield_v2_stepper_attach(&g_dev, 1, 200U) == MOTOR_SHIELD_V2_OK,
          "attach stepper port 1");

    /* Port 1 is built from terminals M1 and M2. Letting a DC call touch them
     * while a stepper holds coil state would corrupt the phase silently. */
    check(motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U)
          == MOTOR_SHIELD_V2_ERR_PORT_BUSY, "M1 refused while port 1 attached");
    check(motor_shield_v2_dc_speed(&g_dev, 2, 100U, 0U)
          == MOTOR_SHIELD_V2_ERR_PORT_BUSY, "M2 refused while port 1 attached");
    check(motor_shield_v2_dc_run(&g_dev, 3, MOTOR_SHIELD_V2_FORWARD, 0U)
          == MOTOR_SHIELD_V2_OK, "M3 still free");

    (void)motor_shield_v2_stepper_detach(&g_dev, 1);
    check(motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U)
          == MOTOR_SHIELD_V2_OK, "M1 free again after detach");
}

static void test_rpm_ceiling_is_honest(void)
{
    uint16_t max_double;
    uint16_t max_micro;

    printf("\nRPM ceiling reflects the bus, and is enforced\n");
    setup();
    (void)motor_shield_v2_stepper_attach(&g_dev, 1, 200U);

    max_double = motor_shield_v2_max_rpm(200U, MOTOR_SHIELD_V2_DOUBLE);
    max_micro  = motor_shield_v2_max_rpm(200U, MOTOR_SHIELD_V2_MICROSTEP);

    printf("       200-step motor: DOUBLE <= %u rpm, MICROSTEP <= %u rpm\n",
           max_double, max_micro);

    check(max_double > max_micro, "microstepping is the slower ceiling");
    check((max_micro >= 4U) && (max_micro <= 7U),
          "microstep ceiling near 5 rpm (one rev is ~10.5 s of pure I2C)");

    check(motor_shield_v2_stepper_rpm(&g_dev, 1, max_double,
                                      MOTOR_SHIELD_V2_DOUBLE)
          == MOTOR_SHIELD_V2_OK, "the ceiling itself is accepted");

    /* Clamping silently would let a UI display a speed the motor never reaches. */
    check(motor_shield_v2_stepper_rpm(&g_dev, 1, (uint16_t)(max_double + 1U),
                                      MOTOR_SHIELD_V2_DOUBLE)
          == MOTOR_SHIELD_V2_ERR_PARAM, "above the ceiling is refused, not clamped");
}

static void test_stepper_is_non_blocking(void)
{
    uint32_t t;
    int      steps_seen = 0;

    printf("\nstepping is non-blocking and bounded per tick\n");
    setup();
    (void)motor_shield_v2_stepper_attach(&g_dev, 1, 200U);
    (void)motor_shield_v2_stepper_rpm(&g_dev, 1, 30U, MOTOR_SHIELD_V2_DOUBLE);

    g_mock.count = 0;
    check(motor_shield_v2_stepper_start(&g_dev, 1, 4U, MOTOR_SHIELD_V2_FORWARD,
                                        MOTOR_SHIELD_V2_DOUBLE, 0U)
          == MOTOR_SHIELD_V2_OK, "start returns immediately");
    check(g_mock.count == 0U, "start issues no bus traffic of its own");
    check(motor_shield_v2_stepper_running(&g_dev, 1), "port reports running");

    /* 30 rpm on a 200-step motor in DOUBLE is one step every 10 ms. */
    for (t = 0U; t <= 40U; t++)
    {
        g_mock.count = 0;
        (void)motor_shield_v2_tick(&g_dev, t);

        if (g_mock.count > 0U)
        {
            steps_seen++;
            /* A microstep touches at most six channels; nothing may exceed it. */
            check(g_mock.count <= 6U, "tick did at most one step of work");
        }
    }

    check(steps_seen == 4, "exactly the four requested steps were emitted");
    check(!motor_shield_v2_stepper_running(&g_dev, 1), "port stops when done");
    check(motor_shield_v2_stepper_remaining(&g_dev, 1) == 0U, "no steps left");
}

static void test_double_step_sequence(void)
{
    static const uint8_t k_expect[4] = { 0x3U, 0x6U, 0xCU, 0x9U };
    uint32_t t;
    int      idx = 0;
    uint8_t  latch = 0;

    printf("\nDOUBLE stepping walks the two-coil sequence\n");
    setup();
    (void)motor_shield_v2_stepper_attach(&g_dev, 1, 200U);
    (void)motor_shield_v2_stepper_rpm(&g_dev, 1, 30U, MOTOR_SHIELD_V2_DOUBLE);
    (void)motor_shield_v2_stepper_start(&g_dev, 1, 4U, MOTOR_SHIELD_V2_FORWARD,
                                        MOTOR_SHIELD_V2_DOUBLE, 0U);

    /* Port 1: coil A = M1 (IN1 ch10, IN2 ch9), coil B = M2 (IN1 ch11, IN2 ch12).
     * The driver maps latch bit0->A.IN2, bit1->B.IN1, bit2->A.IN1, bit3->B.IN2. */
    for (t = 0U; t <= 40U; t++)
    {
        size_t i;

        g_mock.count = 0;
        (void)motor_shield_v2_tick(&g_dev, t);

        if (g_mock.count == 0U) { continue; }

        for (i = 0; i < g_mock.count; i++)
        {
            uint8_t bit;
            switch (g_mock.ops[i].ch)
            {
                case 9U:  bit = 0x1U; break;
                case 11U: bit = 0x2U; break;
                case 10U: bit = 0x4U; break;
                case 12U: bit = 0x8U; break;
                default:  continue;         /* a PWM amplitude write */
            }
            if (g_mock.ops[i].on == FULL_ON_ON) { latch |= bit; }
            else                                { latch = (uint8_t)(latch & ~bit); }
        }

        if (idx < 4)
        {
            char msg[64];
            (void)snprintf(msg, sizeof(msg), "step %d energises coils 0x%X",
                           idx + 1, k_expect[idx]);
            check(latch == k_expect[idx], msg);
            idx++;
        }
    }
}

static void test_microstep_curve_is_corrected(void)
{
    uint32_t t;
    int      saw_142 = 0;
    int      saw_141 = 0;

    printf("\nmicrostep amplitudes use the corrected curve\n");
    setup();
    (void)motor_shield_v2_stepper_attach(&g_dev, 2, 200U);
    (void)motor_shield_v2_stepper_rpm(&g_dev, 2, 1U, MOTOR_SHIELD_V2_MICROSTEP);
    (void)motor_shield_v2_stepper_start(&g_dev, 2, 40U, MOTOR_SHIELD_V2_FORWARD,
                                        MOTOR_SHIELD_V2_MICROSTEP, 0U);

    /* Curve index 6 is 142, not Adafruit's 141. Scaled to 12 bits by
     * (v*4095+127)/255 that is 2280; their 141 would give 2264. */
    for (t = 0U; t < 4000U; t += 10U)
    {
        size_t i;

        g_mock.count = 0;
        (void)motor_shield_v2_tick(&g_dev, t);

        for (i = 0; i < g_mock.count; i++)
        {
            if ((g_mock.ops[i].ch == 2U) || (g_mock.ops[i].ch == 7U))
            {
                if (g_mock.ops[i].off == 2280U) { saw_142 = 1; }
                if (g_mock.ops[i].off == 2264U) { saw_141 = 1; }
            }
        }
    }

    check(saw_142 == 1, "curve index 6 emits 142 (the correct sine value)");
    check(saw_141 == 0, "Adafruit's off-by-one 141 never appears");
}

static void test_watchdog(void)
{
    printf("\nwatchdog stops a motor nobody is servicing\n");
    setup();

    motor_shield_v2_watchdog_set(&g_dev, 500U);
    (void)motor_shield_v2_dc_speed(&g_dev, 1, 200U, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);

    (void)motor_shield_v2_tick(&g_dev, 400U);
    check(!motor_shield_v2_watchdog_tripped(&g_dev), "quiet before the timeout");

    g_mock.count = 0;
    (void)motor_shield_v2_tick(&g_dev, 501U);

    /* Neither a stall nor a thermal trip is visible to firmware on this shield,
     * so a controller that stops talking must not leave a motor driving. */
    check(motor_shield_v2_watchdog_tripped(&g_dev), "trips at the timeout");
    check(g_mock.count == 1U && g_mock.ops[0].ch == 0xFFU,
          "and issues a single ALL_LED stop");
}

static void test_watchdog_refreshed_by_dc_command(void)
{
    printf("\na fresh DC command postpones the watchdog\n");
    setup();

    motor_shield_v2_watchdog_set(&g_dev, 500U);
    (void)motor_shield_v2_dc_speed(&g_dev, 1, 200U, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);
    (void)motor_shield_v2_tick(&g_dev, 0U);

    /* The controller is still talking. A new speed at t=400 is what a slider
     * drag looks like, and it must push the dead-man timer out — otherwise the
     * watchdog stops a motor the user is actively driving.
     *
     * test_watchdog() above cannot catch this: it issues its only commands at
     * t=0, where last_cmd_ms is already 0, so a refresh that never happens is
     * indistinguishable from one that does. */
    (void)motor_shield_v2_tick(&g_dev, 300U);
    (void)motor_shield_v2_dc_speed(&g_dev, 1, 220U, 400U);
    (void)motor_shield_v2_tick(&g_dev, 400U);

    g_mock.count = 0;
    (void)motor_shield_v2_tick(&g_dev, 501U);

    check(!motor_shield_v2_watchdog_tripped(&g_dev),
          "still running 101 ms after the last command");
    check(g_mock.count == 0U, "and no stop was issued");
}

static void test_reversal_out_of_brake_also_coasts(void)
{
    printf("\nreversing out of BRAKE coasts first\n");
    setup();

    /* On the screen BRAKE sits next to BACK, so this is a two-tap gesture.
     * BRAKE shorts the winding and the rotor keeps turning while it slows, but
     * it tells us nothing about which way -- so driving either direction at the
     * cached duty can be a plugging event.
     *
     * The original coast test cannot catch this: it only covers
     * FORWARD -> BACKWARD, where the previous run field still names a direction. */
    (void)motor_shield_v2_dc_speed(&g_dev, 1, 255U, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_BRAKE, 10U);

    check(motor_shield_v2_dc_state(&g_dev, 1)->speed == 255U,
          "BRAKE leaves the cached duty where it was");

    g_mock.count = 0;
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_BACKWARD, 20U);

    expect_op(0, 8U, FULL_OFF_ON, FULL_OFF_OFF,
              "the duty goes to zero before any direction pin moves");
    check(motor_shield_v2_dc_state(&g_dev, 1)->speed == 0U,
          "and the reported speed agrees, so the UI cannot show a stale number");
}

static void test_attach_refuses_a_driven_terminal(void)
{
    printf("\nstepper attach refuses a terminal still driving\n");
    setup();

    /* The stepper sequencer writes IN1/IN2 directly, so claiming M1 while it is
     * at full duty flips the bridge under a spinning rotor -- the same current
     * spike the reversal coast exists to prevent, on a path the coast never
     * sees. Reachable straight from MicroPython, which has no reason to stop
     * first. */
    (void)motor_shield_v2_dc_speed(&g_dev, 1, 255U, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);

    check(motor_shield_v2_stepper_attach(&g_dev, 1, 200U)
              == MOTOR_SHIELD_V2_ERR_PORT_BUSY,
          "attach over a driving M1 is refused, naming the reason");

    /* A brake is not a parked motor either -- the rotor is still turning. */
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_BRAKE, 10U);
    check(motor_shield_v2_stepper_attach(&g_dev, 1, 200U)
              == MOTOR_SHIELD_V2_ERR_PORT_BUSY,
          "and refused over a braking M1");

    /* Released is genuinely idle, so the port becomes available. */
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_RELEASE, 20U);
    (void)motor_shield_v2_dc_run(&g_dev, 2, MOTOR_SHIELD_V2_RELEASE, 20U);
    check(motor_shield_v2_stepper_attach(&g_dev, 1, 200U) == MOTOR_SHIELD_V2_OK,
          "stop the motors and the same attach succeeds");
}

static void test_step_rate_follows_rpm_not_the_tick_rate(void)
{
    uint32_t t;
    uint32_t remaining;

    printf("\nstep rate follows the requested RPM, not how often tick runs\n");
    setup();

    /* The not-yet branch of tick() used to overwrite next_due_ms with now_ms,
     * conflating "the deadline is in the future" -- the ordinary case -- with "the
     * clock went backwards". The schedule was therefore rebuilt on every early
     * call, so the rate became one step per two calls to tick() and a requested
     * 30 RPM ran at about 150 when serviced every millisecond. The bug is
     * invisible at a coarse service period, which is why every other stepper test
     * missed it.
     *
     * 30 RPM x 200 steps/rev = 100 steps/s = one step every 10 ms. Over 1000 ms
     * that is 100 steps, whatever the tick period. */
    (void)motor_shield_v2_stepper_attach(&g_dev, 1, 200U);
    (void)motor_shield_v2_stepper_rpm(&g_dev, 1, 30U, MOTOR_SHIELD_V2_DOUBLE);
    (void)motor_shield_v2_stepper_start(&g_dev, 1, 400U,
                                        MOTOR_SHIELD_V2_FORWARD,
                                        MOTOR_SHIELD_V2_DOUBLE, 0U);

    for (t = 1U; t <= 1000U; t++)
    {
        (void)motor_shield_v2_tick(&g_dev, t);
    }

    remaining = motor_shield_v2_stepper_remaining(&g_dev, 1);

    /* 101, not 100: start() sets the first deadline to now_ms, so the first tick
     * is already due, then t = 10, 20 ... 1000. next_due_ms accumulates with
     * += interval_ms rather than from now_ms, so the schedule does not drift. */
    check(remaining == 299U, "1 ms ticks for 1 s advance exactly 101 steps");

    /* Same elapsed time, a quarter as many calls: same distance travelled. */
    setup();
    (void)motor_shield_v2_stepper_attach(&g_dev, 1, 200U);
    (void)motor_shield_v2_stepper_rpm(&g_dev, 1, 30U, MOTOR_SHIELD_V2_DOUBLE);
    (void)motor_shield_v2_stepper_start(&g_dev, 1, 400U,
                                        MOTOR_SHIELD_V2_FORWARD,
                                        MOTOR_SHIELD_V2_DOUBLE, 0U);

    for (t = 4U; t <= 1000U; t += 4U)
    {
        (void)motor_shield_v2_tick(&g_dev, t);
    }

    check(motor_shield_v2_stepper_remaining(&g_dev, 1) == 299U,
          "and 4 ms ticks over the same second advance the same 101 steps");
}

static void test_watchdog_lets_a_bounded_stepper_move_finish(void)
{
    uint32_t t;

    printf("\nthe watchdog does not abort a bounded stepper move\n");
    setup();

    /* A stepper move is bounded by the step count the caller asked for, so it
     * ends whether or not anyone is watching -- unlike a DC RUN, which never
     * ends on its own. Counting steppers in the watchdog meant one long move was
     * killed mid-way while tick() was demonstrably being serviced, and the kill
     * destroyed steps_left so the caller could not resume. */
    motor_shield_v2_watchdog_set(&g_dev, 500U);
    (void)motor_shield_v2_stepper_attach(&g_dev, 1, 200U);
    (void)motor_shield_v2_stepper_rpm(&g_dev, 1, 30U, MOTOR_SHIELD_V2_DOUBLE);
    (void)motor_shield_v2_stepper_start(&g_dev, 1, 400U,
                                        MOTOR_SHIELD_V2_FORWARD,
                                        MOTOR_SHIELD_V2_DOUBLE, 0U);

    for (t = 1U; t <= 2000U; t++)
    {
        (void)motor_shield_v2_tick(&g_dev, t);
    }

    check(!motor_shield_v2_watchdog_tripped(&g_dev),
          "no trip 1.5 s past the timeout, with no further commands");
    check(motor_shield_v2_stepper_running(&g_dev, 1),
          "the move is still going");
    check(motor_shield_v2_stepper_remaining(&g_dev, 1) > 0U,
          "and its step budget survived");
}

static void test_failed_stop_all_is_retried(void)
{
    printf("\na stop that never reached the shield is tried again\n");
    setup();

    /* A wedged shared SCB5 is the realistic reason an emergency stop fails, and
     * it recovers. Zeroing the cached state before the write meant the driver
     * then believed nothing was running while the hardware kept driving, so the
     * watchdog never fired again: the one stop this shield offers, retired by
     * the failure it exists for. */
    motor_shield_v2_watchdog_set(&g_dev, 100U);
    (void)motor_shield_v2_dc_speed(&g_dev, 1, 255U, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);

    g_mock.fail_next = 1;
    check(motor_shield_v2_tick(&g_dev, 200U) == MOTOR_SHIELD_V2_ERR_IO,
          "the failed stop is reported, not swallowed");
    check(motor_shield_v2_dc_state(&g_dev, 1)->speed == 255U,
          "and the driver still knows the motor is running");

    g_mock.count = 0;
    (void)motor_shield_v2_tick(&g_dev, 300U);
    check(g_mock.count == 1U && g_mock.ops[0].ch == 0xFFU,
          "so the next tick tries the ALL_LED stop again");
    check(motor_shield_v2_dc_state(&g_dev, 1)->speed == 0U,
          "and only now is the cached state cleared");
}

static void test_trip_flag_survives_a_command_that_starts_nothing(void)
{
    printf("\na watchdog trip is not erased by a command that drives nothing\n");
    setup();

    /* The page samples this flag on its 33 ms render, so a flag cleared by an
     * unrelated command can vanish before it is ever drawn -- leaving the user
     * with a motor that stopped and no explanation on screen. */
    motor_shield_v2_watchdog_set(&g_dev, 100U);
    (void)motor_shield_v2_dc_speed(&g_dev, 1, 255U, 0U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 0U);
    (void)motor_shield_v2_tick(&g_dev, 200U);
    check(motor_shield_v2_watchdog_tripped(&g_dev), "it tripped");

    (void)motor_shield_v2_dc_speed(&g_dev, 1, 0U, 250U);
    check(motor_shield_v2_watchdog_tripped(&g_dev),
          "a slider dragged to zero does not erase it");

    (void)motor_shield_v2_dc_run(&g_dev, 3, MOTOR_SHIELD_V2_RELEASE, 260U);
    check(motor_shield_v2_watchdog_tripped(&g_dev),
          "nor does releasing an unrelated motor");

    (void)motor_shield_v2_dc_speed(&g_dev, 1, 200U, 270U);
    (void)motor_shield_v2_dc_run(&g_dev, 1, MOTOR_SHIELD_V2_FORWARD, 270U);
    check(!motor_shield_v2_watchdog_tripped(&g_dev),
          "driving again clears it, because now it is stale news");
}

static void test_clock_wrap(void)
{
    uint32_t t;
    int      steps = 0;

    printf("\nmillisecond counter wrap is survivable\n");
    setup();
    (void)motor_shield_v2_stepper_attach(&g_dev, 1, 200U);
    (void)motor_shield_v2_stepper_rpm(&g_dev, 1, 30U, MOTOR_SHIELD_V2_DOUBLE);

    /* Start 20 ms before the 32-bit wrap, which a device hits every 49.7 days. */
    (void)motor_shield_v2_stepper_start(&g_dev, 1, 3U, MOTOR_SHIELD_V2_FORWARD,
                                        MOTOR_SHIELD_V2_DOUBLE, 0xFFFFFFECUL);

    for (t = 0U; t < 60U; t++)
    {
        uint32_t now = 0xFFFFFFECUL + t;    /* wraps through zero */

        g_mock.count = 0;
        (void)motor_shield_v2_tick(&g_dev, now);
        if (g_mock.count > 0U) { steps++; }
    }

    check(steps == 3, "all three steps emitted across the wrap");
}

int main(void)
{
    printf("Motor Shield v2 driver -- host tests\n");
    printf("===================================\n");

    test_init_parks_outputs();
    test_dc_direction_ordering();
    test_reversal_does_not_plug_brake();
    test_brake_actually_brakes();
    test_release_is_a_coast();
    test_speed_scaling();
    test_stepper_owns_its_channels();
    test_rpm_ceiling_is_honest();
    test_stepper_is_non_blocking();
    test_double_step_sequence();
    test_microstep_curve_is_corrected();
    test_watchdog();
    test_watchdog_refreshed_by_dc_command();
    test_reversal_out_of_brake_also_coasts();
    test_attach_refuses_a_driven_terminal();
    test_step_rate_follows_rpm_not_the_tick_rate();
    test_watchdog_lets_a_bounded_stepper_move_finish();
    test_failed_stop_all_is_retried();
    test_trip_flag_survives_a_command_that_starts_nothing();
    test_clock_wrap();

    printf("\n-----------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_fail);

    return (g_fail == 0) ? 0 : 1;
}
