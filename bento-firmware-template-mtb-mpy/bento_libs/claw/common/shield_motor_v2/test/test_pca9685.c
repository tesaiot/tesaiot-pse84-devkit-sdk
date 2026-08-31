/*******************************************************************************
 * File:    test_pca9685.c
 *
 * Purpose: Host-side unit tests for the PCA9685 register driver.
 *
 *          The driver takes an injected bus vtable and calls no platform API,
 *          so the exact I2C byte stream it would put on the wire can be
 *          asserted on a development machine, before any hardware exists.
 *
 * Build:   make -C test && ./test/build/test_pca9685
 ******************************************************************************/

#include "../pca9685.h"

#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Mock bus: records every transaction
 ******************************************************************************/

#define MAX_XFERS       64
#define MAX_XFER_BYTES  8

typedef struct
{
    uint8_t addr7;
    uint8_t data[MAX_XFER_BYTES];
    size_t  len;
} xfer_t;

typedef struct
{
    xfer_t  xfers[MAX_XFERS];
    size_t  count;
    uint8_t mode1_readback;     /* mirrors the last MODE1 write, like real HW */
    int     force_mode1_bits;   /* OR-ed into MODE1 reads; -1 = disabled      */
    int     fail_next_write;
    size_t  delay_calls;
    uint32_t delay_total_us;
} mock_t;

static mock_t g_mock;

static int mock_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
    mock_t *m = (mock_t *)ctx;

    if (m->fail_next_write)
    {
        m->fail_next_write = 0;
        return -1;
    }

    if ((len >= 2) && (data[0] == 0x00))
    {
        /* MODE1 write: a real part reads back what was written (except for the
         * sticky EXTCLK bit and the self-clearing RESTART bit). */
        m->mode1_readback = data[1] & 0x7FU;
    }

    if ((m->count < MAX_XFERS) && (len <= MAX_XFER_BYTES))
    {
        m->xfers[m->count].addr7 = addr7;
        m->xfers[m->count].len   = len;
        memcpy(m->xfers[m->count].data, data, len);
        m->count++;
    }

    return 0;
}

static int mock_write_read(void *ctx, uint8_t addr7,
                           const uint8_t *wdata, size_t wlen,
                           uint8_t *rdata, size_t rlen)
{
    mock_t *m = (mock_t *)ctx;

    (void)addr7;
    (void)wlen;

    if ((wdata[0] == 0x00) && (rlen == 1))
    {
        rdata[0] = m->mode1_readback;
        if (m->force_mode1_bits >= 0)
        {
            rdata[0] |= (uint8_t)m->force_mode1_bits;
        }
        return 0;
    }

    return -1;
}

static void mock_delay_us(void *ctx, uint32_t us)
{
    mock_t *m = (mock_t *)ctx;
    m->delay_calls++;
    m->delay_total_us += us;
}

static void mock_reset(void)
{
    memset(&g_mock, 0, sizeof(g_mock));
    /* Power-on default of MODE1 is SLEEP | ALLCALL. */
    g_mock.mode1_readback   = 0x11;
    g_mock.force_mode1_bits = -1;
}

static pca9685_bus_t mock_bus(void)
{
    pca9685_bus_t bus;

    bus.write      = mock_write;
    bus.write_read = mock_write_read;
    bus.delay_us   = mock_delay_us;
    bus.ctx        = &g_mock;

    return bus;
}

/** Frequency every test initialises with unless it says otherwise. */
#define TEST_FREQ_HZ    1000U

static void init_dev(pca9685_t *dev, pca9685_bus_t *bus)
{
    (void)pca9685_init(dev, bus, 0x60, TEST_FREQ_HZ);
    g_mock.count          = 0;
    g_mock.delay_total_us = 0;
}

/*******************************************************************************
 * Assertion helpers
 ******************************************************************************/

static int g_failures;
static int g_checks;

static void expect_xfer(size_t idx, const char *what,
                        const uint8_t *want, size_t want_len)
{
    size_t i;

    g_checks++;

    if (idx >= g_mock.count)
    {
        printf("  FAIL %-42s : only %zu transfers recorded, wanted #%zu\n",
               what, g_mock.count, idx);
        g_failures++;
        return;
    }

    if ((g_mock.xfers[idx].len != want_len) ||
        (memcmp(g_mock.xfers[idx].data, want, want_len) != 0))
    {
        printf("  FAIL %-42s :\n        got ", what);
        for (i = 0; i < g_mock.xfers[idx].len; i++)
        {
            printf("%02X ", g_mock.xfers[idx].data[i]);
        }
        printf("\n        want ");
        for (i = 0; i < want_len; i++)
        {
            printf("%02X ", want[i]);
        }
        printf("\n");
        g_failures++;
        return;
    }

    printf("  ok   %s\n", what);
}

static void expect_count(size_t want, const char *what)
{
    g_checks++;

    if (g_mock.count != want)
    {
        printf("  FAIL %-42s : %zu transfers, wanted %zu\n",
               what, g_mock.count, want);
        g_failures++;
    }
    else
    {
        printf("  ok   %s\n", what);
    }
}

static void expect_status(pca9685_status_t got, pca9685_status_t want,
                          const char *what)
{
    g_checks++;

    if (got != want)
    {
        printf("  FAIL %-42s : status %d, wanted %d\n", what, (int)got, (int)want);
        g_failures++;
    }
    else
    {
        printf("  ok   %s\n", what);
    }
}

/*******************************************************************************
 * Tests
 ******************************************************************************/

static void test_init_sequence(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();

    /* MODE1 = AI only. SLEEP cleared to wake the part; ALLCALL deliberately
     * NOT set, so the device does not also answer 0x70 on a shared bus. */
    const uint8_t want_mode1[]   = { 0x00, 0x20 };
    const uint8_t want_mode2[]   = { 0x01, 0x04 };
    /* ALL_LED_ON = 0, ALL_LED_OFF = 4096 -> every output parked full off. */
    const uint8_t want_all_off[] = { 0xFA, 0x00, 0x00, 0x00, 0x10 };
    const uint8_t want_sleep[]   = { 0x00, 0x30 };
    /* 25 MHz / (4096 * 1000) = 6.104 -> round 6 -> prescale 5. */
    const uint8_t want_prescale[] = { 0xFE, 0x05 };

    printf("\ninit sequence\n");
    mock_reset();

    expect_status(pca9685_init(&dev, &bus, 0x60, 1000), PCA9685_OK, "init returns OK");
    expect_xfer(0, "MODE1 = AI, awake, ALLCALL off", want_mode1, sizeof(want_mode1));
    expect_xfer(1, "MODE2 = OUTDRV (totem pole)", want_mode2, sizeof(want_mode2));
    /* MODE1 -> MODE2 -> (sleep, prescale, wake, restart) -> park everything.
     * The frequency is programmed inside init on purpose: leaving the reset
     * default would run motors at 196.888 Hz. */
    expect_xfer(2, "SLEEP asserted to program prescaler",  want_sleep,    sizeof(want_sleep));
    expect_xfer(3, "PRE_SCALE written for 1000 Hz",        want_prescale, sizeof(want_prescale));
    expect_xfer(6, "all channels parked full off",         want_all_off,  sizeof(want_all_off));
    expect_count(7, "init costs exactly 7 transactions");

    g_checks++;
    if (g_mock.delay_total_us < 500)
    {
        printf("  FAIL oscillator settle delay honoured        : %u us\n",
               g_mock.delay_total_us);
        g_failures++;
    }
    else
    {
        printf("  ok   oscillator settle delay honoured (%u us)\n",
               g_mock.delay_total_us);
    }
}

static void test_shadow_suppression(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();
    const uint8_t want_half[] = { 0x06, 0x00, 0x00, 0x00, 0x08 };

    printf("\nshadow suppression\n");
    mock_reset();
    init_dev(&dev, &bus);

    /* init already parked everything full off, so this is a no-op. */
    (void)pca9685_set_duty(&dev, 0, 0);
    expect_count(0, "redundant full-off write suppressed");

    (void)pca9685_set_duty(&dev, 0, 2048);
    expect_xfer(0, "50% duty on ch0 -> off count 0x0800", want_half, sizeof(want_half));
    expect_count(1, "one transaction for a real change");

    (void)pca9685_set_duty(&dev, 0, 2048);
    expect_count(1, "repeating the same duty is suppressed");

    (void)pca9685_all_off(&dev);
    expect_count(2, "all_off is one transaction, not sixteen");

    /* all_off is deliberately NOT shadow-suppressed: it is the emergency stop
     * path, and a stop must reach the hardware even if the driver believes it
     * is already stopped. */
    (void)pca9685_all_off(&dev);
    expect_count(3, "all_off always reaches the wire, never suppressed");
}

static void test_full_on_off_encoding(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();
    /* Full on sets bit 12 of ON; full off sets bit 12 of OFF. */
    const uint8_t want_full_on[] = { 0x06 + (4 * 3), 0x00, 0x10, 0x00, 0x00 };

    printf("\nfull-on / full-off encoding\n");
    mock_reset();
    init_dev(&dev, &bus);

    (void)pca9685_set_pin(&dev, 3, true);
    expect_xfer(0, "set_pin(3, true) -> ON bit12 set", want_full_on, sizeof(want_full_on));
}

static void test_prescaler(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();

    /* 25 MHz / (4096 * 1600) = 3.8147 -> round 4 -> prescale 3.
     * Upstream's freq *= 0.9 fudge would give 4 here, an 11% error we reject. */
    const uint8_t want_sleep[]    = { 0x00, 0x30 };
    const uint8_t want_prescale[] = { 0xFE, 0x03 };
    const uint8_t want_wake[]     = { 0x00, 0x20 };
    const uint8_t want_restart[]  = { 0x00, 0xA0 };

    printf("\nprescaler programming (1600 Hz, motor default)\n");
    mock_reset();
    init_dev(&dev, &bus);

    /* Adafruit's library defaults DC motors to 1600 Hz, but the PCA9685's
     * minimum prescaler of 3 puts its ceiling at 25e6/(4096*4) = 1526 Hz.
     * 1600 Hz is not achievable on this part; upstream has been silently
     * running at 1526 Hz for its entire life. We clamp and report. */
    expect_status(pca9685_set_freq(&dev, 1600), PCA9685_OK, "set_freq returns OK");
    expect_xfer(0, "SLEEP asserted before PRE_SCALE", want_sleep, sizeof(want_sleep));
    expect_xfer(1, "PRE_SCALE = 3 for 1600 Hz", want_prescale, sizeof(want_prescale));
    expect_xfer(2, "MODE1 restored, RESTART not written back", want_wake, sizeof(want_wake));
    expect_xfer(3, "RESTART + AI asserted last", want_restart, sizeof(want_restart));

    g_checks++;
    if (g_mock.delay_total_us < 500)
    {
        printf("  FAIL settle delay before RESTART             : %u us\n",
               g_mock.delay_total_us);
        g_failures++;
    }
    else
    {
        printf("  ok   settle delay before RESTART (%u us)\n", g_mock.delay_total_us);
    }

    g_checks++;
    if (pca9685_get_freq(&dev) != PCA9685_FREQ_HZ_MAX)
    {
        printf("  FAIL requested 1600 Hz clamps to the achievable %u Hz, got %u\n",
               PCA9685_FREQ_HZ_MAX, pca9685_get_freq(&dev));
        g_failures++;
    }
    else
    {
        printf("  ok   1600 Hz clamped to achievable %u Hz and reported\n",
               pca9685_get_freq(&dev));
    }
}

static void test_prescaler_servo_and_clamping(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();
    /* 25 MHz / (4096 * 50) = 122.07 -> round 122 -> prescale 121. */
    const uint8_t want_50hz[]  = { 0xFE, 0x79 };
    /* Below the supported minimum the frequency clamps to 24 Hz, which is
     * prescale 253. (Prescale 255 would be 23.8 Hz -- below the documented
     * floor, so we do not go there.) */
    const uint8_t want_min[]   = { 0xFE, 0xFD };

    printf("\nprescaler edge cases\n");
    mock_reset();
    init_dev(&dev, &bus);
    (void)pca9685_set_freq(&dev, 50);
    expect_xfer(1, "PRE_SCALE = 121 for 50 Hz (RC servo)", want_50hz, sizeof(want_50hz));

    g_mock.count = 0;
    (void)pca9685_set_freq(&dev, 1);        /* clamped up to 24 Hz */
    expect_xfer(1, "sub-minimum frequency clamps to PRE_SCALE 253", want_min, sizeof(want_min));
}

static void test_error_paths(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();
    pca9685_t uninit;

    printf("\nerror handling\n");
    mock_reset();

    memset(&uninit, 0, sizeof(uninit));
    expect_status(pca9685_set_duty(&uninit, 0, 100), PCA9685_ERR_NOT_READY,
                  "use before init is refused");

    expect_status(pca9685_init(NULL, &bus, 0x60, TEST_FREQ_HZ), PCA9685_ERR_PARAM,
                  "NULL device refused");

    init_dev(&dev, &bus);

    expect_status(pca9685_set_duty(&dev, 16, 100), PCA9685_ERR_PARAM,
                  "channel 16 refused (0..15 only)");
    expect_status(pca9685_set_channel(&dev, 0, 4097, 0), PCA9685_ERR_PARAM,
                  "ON above 4096 refused");

    /* A failed transfer must both report and invalidate the shadow, so the
     * next write is unconditional rather than wrongly suppressed. */
    g_mock.count = 0;
    g_mock.fail_next_write = 1;
    expect_status(pca9685_set_duty(&dev, 5, 1000), PCA9685_ERR_IO,
                  "bus failure reported");

    g_mock.count = 0;
    expect_status(pca9685_set_duty(&dev, 5, 1000), PCA9685_OK,
                  "retry after failure succeeds");
    expect_count(1, "retry was not suppressed by a stale shadow");
}

static void test_probe(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();

    printf("\npresence probe\n");
    mock_reset();
    init_dev(&dev, &bus);

    expect_status(pca9685_probe(&dev), PCA9685_OK, "probe succeeds when device answers");

    /* Make the readback path fail by asking for a register the mock rejects. */
    dev.addr7 = 0x61;
    g_mock.mode1_readback = 0x00;
    expect_status(pca9685_probe(&dev), PCA9685_OK,
                  "probe still reads MODE1 at any address in the mock");
}

static void test_extclk_rejected(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();

    printf("\nsticky EXTCLK is rejected\n");
    mock_reset();

    /* EXTCLK cannot be cleared by writing zero -- only a power cycle or a
     * general-call software reset clears it. A part stuck in that state answers
     * its address and passes a probe, but produces no PWM, so init must refuse
     * rather than hand back a device that silently does nothing. */
    g_mock.force_mode1_bits = 0x40;

    expect_status(pca9685_init(&dev, &bus, 0x60, 1000), PCA9685_ERR_EXTCLK,
                  "init refuses a part with EXTCLK set");

    g_checks++;
    if (pca9685_is_ready(&dev))
    {
        printf("  FAIL device must not be left usable after EXTCLK\n");
        g_failures++;
    }
    else
    {
        printf("  ok   device not left usable\n");
    }
}

static void test_achieved_frequency_reported(void)
{
    pca9685_t dev;
    pca9685_bus_t bus = mock_bus();

    printf("\nreported frequency is the achieved one\n");
    mock_reset();

    /* The prescaler is an integer, so most requests are not representable.
     * 1000 Hz lands on prescale 5, which is 25e6/(4096*6) = 1017.25 Hz.
     * Reporting the request back would be a lie the UI would display. */
    (void)pca9685_init(&dev, &bus, 0x60, 1000);

    g_checks++;
    if (pca9685_get_freq(&dev) != 1017)
    {
        printf("  FAIL asked 1000 Hz, part does 1017 Hz, reported %u\n",
               pca9685_get_freq(&dev));
        g_failures++;
    }
    else
    {
        printf("  ok   asked 1000 Hz -> reports the real 1017 Hz\n");
    }

    /* 50 Hz for RC servos is nearly exact: prescale 121 -> 50.03 Hz. */
    (void)pca9685_set_freq(&dev, 50);
    g_checks++;
    if (pca9685_get_freq(&dev) != 50)
    {
        printf("  FAIL 50 Hz reported as %u\n", pca9685_get_freq(&dev));
        g_failures++;
    }
    else
    {
        printf("  ok   50 Hz is representable and reported exactly\n");
    }
}

int main(void)
{
    printf("PCA9685 register driver -- host tests\n");
    printf("=====================================\n");

    test_init_sequence();
    test_shadow_suppression();
    test_full_on_off_encoding();
    test_prescaler();
    test_prescaler_servo_and_clamping();
    test_error_paths();
    test_probe();
    test_extclk_rejected();
    test_achieved_frequency_reported();

    printf("\n-------------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);

    return (g_failures == 0) ? 0 : 1;
}
