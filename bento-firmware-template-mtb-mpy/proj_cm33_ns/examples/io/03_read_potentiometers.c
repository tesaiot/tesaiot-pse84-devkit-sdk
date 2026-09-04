/* sdk-example: core=cm33 variant=both group=io
 * id:      cm33/io/03_read_potentiometers
 * title:   Read the potentiometer three ways
 * teaches: raw counts, percent and volts off one SAR channel — and which
 *          channel that actually is, which is not the one most people assume
 * apis:    potentiometer_init, potentiometer_read_raw,
 *          potentiometer_read_percent, potentiometer_read_voltage
 * entry:   example_io_read_potentiometers
 */
/*******************************************************************************
 * io/03 — the analogue knob.
 *
 * Three readers, one conversion each, all off the SAME channel:
 *
 *   potentiometer_read_raw()      uint16_t   0 .. 65535
 *   potentiometer_read_percent()  float      0.0 .. 100.0
 *   potentiometer_read_voltage()  float      0.0 .. 3.3
 *
 * percent and voltage are arithmetic on the raw count, not extra hardware
 * work, so calling all three costs three conversions and gives you three views
 * of three DIFFERENT instants. If you need them consistent, read raw once and
 * do the maths yourself — the scaling is 100.0 * raw / 65535 and
 * 3.3 * raw / 65535.
 *
 * READ THIS BEFORE YOU LABEL THE NUMBER "VR1"
 * -------------------------------------------
 * This driver reads exactly one channel: the first one the BSP's SAR
 * configuration hands back. sensor_potentiometer.c passes a one-element
 * channel array to mtb_hal_adc_setup() and uses adc_channels[0], which on the
 * KIT_PSE84_AI BSP is SAR GPIO channel 0 — posPin GPIO1, i.e. P15.1
 * (bsps/TARGET_KIT_PSE84_AI/config/GeneratedSource/cycfg_peripherals.c,
 * CYBSP_SAR_ADC_gpio_ch_cfg[0]).
 *
 * On the TESAIoT Dev Kit that is NOT a potentiometer. The QWA309's four knobs
 * VR1-VR4 are SAR GPIO channels 4-7, P15.4-P15.7
 * (bsps/TARGET_KIT_PSE84_AI/bsp_features.mk, and cm55_sensor_poll.h), and they
 * are read on CM55 — the base-board polling loop feeds them into the sensorhub
 * snapshot, which is where the Controls UI and the MicroPython `pots` API get
 * them. Nothing on CM33_NS reads VR1-VR4.
 *
 * Worse, on that board P15.1 is ARDUINO_D1 on the expansion header
 * (arduino_shield_qwa309.c). So this call will happily succeed and return the
 * voltage on a header pin. Reporting that as "VR1 = 43 %" is exactly the kind
 * of confident wrong number this corpus exists to prevent.
 *
 * So:
 *   * Eva Kit / any board whose single pot is on SAR channel 0 — this is your
 *     driver, and the reading is the knob.
 *   * TESAIoT Dev Kit / QWA309 — this reads a header pin. For VR1-4, go to
 *     CM55 (cm55_pot_read_all) or read the sensorhub snapshot.
 *
 * WHAT "FAILURE" MEANS HERE
 * -------------------------
 * Only potentiometer_init() can really fail — ADC setup, ADC enable, or a BSP
 * with no channel configured. Once it is up, mtb_hal_adc_read_u16() has no
 * error return, so read_raw() returns true for every call including the ones
 * taken from a floating pin. The tell is in the data, not the return code:
 * a pin with nothing on it wanders over a wide range, and a hard 0 or a hard
 * 0xFFFF that never moves is a pin tied to a rail, not a knob at an end stop.
 * This example therefore reports the SPREAD it saw and lets you judge.
 *
 * VDDA IS ASSUMED, NOT MEASURED
 * -----------------------------
 * read_voltage() multiplies by a hard-coded 3.3 V. If your board runs VDDA at
 * 3.0 or 3.6 the volts are wrong by that ratio while the percent stays right,
 * because percent is a ratio of full scale and needs no reference.
 *
 *     make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=cm33/io/03_read_potentiometers
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_POTENTIOMETER

#include "../sdk_examples_cm33.h"

#include "sensor_potentiometer.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

#define POT_SAMPLE_MS      (100U)
#define POT_SAMPLE_COUNT   (30U)        /* 3 s */

/* Counts of spread below which the input is almost certainly not a knob.
 * A 12-bit conversion scaled to 16 bits moves in steps of 16, so a live pot
 * being turned covers thousands of counts; a pin tied to a rail covers a
 * handful of LSBs of noise. */
#define POT_FLAT_SPREAD    (64U)

int example_io_read_potentiometers(void)
{
    /* Idempotent: brings up the SAR block through the BSP configurator on the
     * first call and returns the cached result afterwards. The readers call it
     * for you, but doing it here means a setup failure is reported once, with
     * its own message, instead of as a silent false from a reader. */
    if (!potentiometer_init()) {
        printf("[io/03] potentiometer_init() failed.\r\n");
        printf("        Causes, in the order they actually happen:\r\n");
        printf("        1. the BSP configured no SAR GPIO channel — "
               "adc_channels[0] came back NULL;\r\n");
        printf("        2. mtb_hal_adc_setup() rejected CYBSP_SAR_ADC_hal_config;\r\n");
        printf("        3. mtb_hal_adc_enable() failed.\r\n");
        printf("        The driver prints the CY_RSLT code for 2 and 3.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    /* --- one instant, three views ------------------------------------------
     * Read them back-to-back so the three numbers are at least adjacent in
     * time, and print them together so the relationship is visible: percent is
     * raw/655.35, volts is raw/19859. Deriving them from one raw read is what
     * you want in real code; three calls is what you want when you are
     * checking that all three work. */
    uint16_t raw     = 0U;
    float    percent = 0.0f;
    float    voltage = 0.0f;

    const bool ok_raw = potentiometer_read_raw(&raw);
    const bool ok_pct = potentiometer_read_percent(&percent);
    const bool ok_v   = potentiometer_read_voltage(&voltage);

    if (!ok_raw || !ok_pct || !ok_v) {
        /* After a successful init these cannot fail today, so if one does the
         * driver changed under you — say so rather than printing a zero. */
        printf("[io/03] a reader returned false after a good init "
               "(raw=%d percent=%d voltage=%d)\r\n",
               (int)ok_raw, (int)ok_pct, (int)ok_v);
        return SDK_EX_REFUSED;
    }

    /* printf on this core is newlib-nano's, which does not format %f. Scale to
     * integers and print the decimal by hand — every float in this file goes
     * through this, and that is the house pattern, not a shortcut. */
    printf("[io/03] raw %5u   percent %3d.%01d %%   voltage %d.%03d V\r\n",
           (unsigned)raw,
           (int)percent, (int)((percent - (float)(int)percent) * 10.0f),
           (int)voltage, (int)((voltage - (float)(int)voltage) * 1000.0f));

    /* --- now watch it move -------------------------------------------------
     * The spread over a few seconds is what separates "a knob nobody touched"
     * from "a pin that is not a knob". Report it either way; do not decide for
     * the user which one they have. */
    printf("[io/03] turn the knob for the next %u s...\r\n",
           (unsigned)((POT_SAMPLE_MS * POT_SAMPLE_COUNT) / 1000U));

    uint16_t lo = 0xFFFFU, hi = 0U;
    uint32_t sum = 0U;
    unsigned n = 0U;

    for (unsigned i = 0U; i < POT_SAMPLE_COUNT; i++) {
        uint16_t v = 0U;
        if (potentiometer_read_raw(&v)) {
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
            sum += (uint32_t)v;
            n++;
        }
        vTaskDelay(pdMS_TO_TICKS(POT_SAMPLE_MS));
    }

    if (n == 0U) {
        printf("[io/03] no samples — the reader stopped returning true\r\n");
        return SDK_EX_NO_DATA;
    }

    const unsigned spread = (unsigned)(hi - lo);
    printf("[io/03] %u samples: min %u, max %u, mean %u, spread %u counts\r\n",
           n, (unsigned)lo, (unsigned)hi, (unsigned)(sum / n), spread);

    if (spread < POT_FLAT_SPREAD) {
        printf("[io/03] spread below %u counts. Either nothing was turned, or\r\n",
               (unsigned)POT_FLAT_SPREAD);
        printf("        this channel is not a potentiometer — see the header\r\n");
        printf("        comment: on a QWA309 board it is P15.1 / ARDUINO_D1,\r\n");
        printf("        and VR1-VR4 live on CM55.\r\n");
    }

    return SDK_EX_OK;
}

#else  /* !BSP_HAS_POTENTIOMETER */

#include "../sdk_examples_cm33.h"
#include <stdio.h>

int example_io_read_potentiometers(void)
{
    printf("[io/03] built with BSP_HAS_POTENTIOMETER=0 — no ADC knob on this board\r\n");
    return SDK_EX_UNAVAILABLE;
}

#endif /* BSP_HAS_POTENTIOMETER */
