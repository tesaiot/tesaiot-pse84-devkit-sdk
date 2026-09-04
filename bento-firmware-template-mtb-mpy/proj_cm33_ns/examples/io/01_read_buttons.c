/* sdk-example: core=cm33 variant=both group=io
 * id:      cm33/io/01_read_buttons
 * title:   Read the CapSense buttons
 * teaches: capture the idle baseline with fingers OFF the pad, poll the two
 *          buttons, and know which of the two CapSense backends your build
 *          linked — they read two different I2C buses
 * apis:    capsense_init, capsense_read
 * entry:   example_io_read_buttons
 */
/*******************************************************************************
 * io/01 — the two CapSense buttons on the PSoC 4000T.
 *
 * THE ONE THING THAT WILL WASTE YOUR AFTERNOON: THERE ARE TWO BACKENDS
 * -------------------------------------------------------------------
 * capsense_init() / capsense_read() are declared once, in sensor_capsense.h,
 * and implemented TWICE. Exactly one of the two is linked, and which one is a
 * BOARD fact decided in bento_libs/claw/common/mpy/capsense.mk:
 *
 *   BSP_HAS_QWA309_BASEBOARD=0   ->  sensor_capsense.c
 *        Direct I2C. CM33_NS talks to the 4000T at 0x08 on the SENSOR bus,
 *        SCB0, P8.0 = SCL / P8.1 = SDA, the same 1.8 V bus as the BMI270.
 *        This is the Eva Kit / bare AI-Kit-plus-CapSense arrangement.
 *
 *   BSP_HAS_QWA309_BASEBOARD=1   ->  sensor_capsense_ipc.c   <-- THIS BOARD
 *        No local I2C at all. On the TESAIoT Dev Kit and the Game Console the
 *        4000T sits on the DISPLAY / touch bus, P17.0 / P17.1, which CM55
 *        owns. CM55's cm55_sensor_poll reads it every 50 ms in GFX-task
 *        context; this backend fetches that cache in one IPC_CMD_CONTROLS_STATE
 *        round trip (0xC6), with a 100 ms response timeout.
 *
 * Picking the wrong one does NOT fail to build and does NOT fail to link. It
 * aims a perfectly good driver at a bus the chip is not on, so every read
 * returns false forever. That shipped once, in every mtb-only package, because
 * proj_cm33_ns/Makefile restated the choice by hand instead of including
 * capsense.mk. Do not restate it in your project either — include capsense.mk
 * and let it decide.
 *
 * Which one did YOUR build link? ref_io.c answers it with a bus scan.
 *
 * THE BASELINE
 * ------------
 * The 4000T does not return its button code to zero on release. The driver
 * therefore captures an IDLE baseline the first time capsense_init() succeeds
 * and reports "pressed" as "differs from baseline". So:
 *
 *     capsense_init() MUST run with no finger on the pad.
 *
 * Initialise during start-up, not on a button-shaped screen the user is
 * already touching. If a button reads permanently pressed and never releases,
 * a finger was down at init — power-cycle, or re-run init untouched.
 *
 * A baseline captured on this board's IPC backend is captured on CM55, in
 * cm55_sensor_poll, not here — but the rule is the same and the symptom is
 * identical.
 *
 * COST
 * ----
 * One capsense_read() is one full 3-byte frame: both buttons AND the slider,
 * one transaction. There is no cheaper "buttons only" path — capsense_read_
 * buttons() does the same read and throws the slider away (see ref_io.c). So
 * when you want more than one field, call capsense_read() once and use the
 * struct, which is what this file does.
 *
 * WHERE THIS RUNS
 * ---------------
 * The CM33 example runner task sits at tskIDLE_PRIORITY+1, below every other
 * printf user in the system, so printf and vTaskDelay are both safe here.
 * Neither would be safe in an IPC callback — that is ISR context.
 *
 *     make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=cm33/io/01_read_buttons
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_CAPSENSE

#include "../sdk_examples_cm33.h"

#include "sensor_capsense.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

/* How long to watch the pad, and how often. 50 ms is the rate CM55 refreshes
 * its CapSense cache at, so polling faster than that on a QWA309 board buys
 * nothing but IPC traffic. */
#define BTN_POLL_MS      (50U)
#define BTN_POLL_COUNT   (100U)          /* 100 * 50 ms = 5 s of watching */

int example_io_read_buttons(void)
{
    /* 1. Bring the driver up. Idempotent — a second call returns the cached
     *    result without re-capturing the baseline, which is why a failed first
     *    init is worth reporting rather than silently retrying forever. */
    if (!capsense_init()) {
        printf("[io/01] capsense_init() failed.\r\n");
        printf("[io/01] On a QWA309 board that means the CM55 CapSense cache\r\n");
        printf("        did not answer IPC_CMD_CONTROLS_STATE: either CM55 is\r\n");
        printf("        not up yet, or cm55_sensor_poll_init() never found the\r\n");
        printf("        4000T at 0x08 on the display bus.\r\n");
        printf("[io/01] On a non-QWA309 board it means nothing ACKed at 0x08\r\n");
        printf("        on the sensor bus, SCB0 / P8.0 / P8.1.\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    printf("[io/01] CapSense ready. Touch BTN0 / BTN1 for the next %u s.\r\n",
           (unsigned)((BTN_POLL_MS * BTN_POLL_COUNT) / 1000U));

    /* 2. Poll, and report EDGES rather than levels.
     *
     *    Printing a line per poll at 20 Hz drowns the console and tells you
     *    nothing you could not see on the pad. What a caller actually consumes
     *    is the transition, so that is what this tracks: the previous state is
     *    seeded from the first successful read so a finger already down when
     *    the loop starts does not register as a press. */
    capsense_data_t d;
    bool prev0 = false, prev1 = false;
    bool seeded = false;

    unsigned presses0 = 0U, presses1 = 0U;
    unsigned reads = 0U, failures = 0U;

    for (unsigned i = 0U; i < BTN_POLL_COUNT; i++) {
        /* One transaction, both buttons and the slider. Never assume the
         * struct was written: on a failed read it is untouched, and reusing
         * last loop's values as if they were fresh is how a stuck button gets
         * reported as a working one. */
        if (!capsense_read(&d)) {
            failures++;
            vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
            continue;
        }
        reads++;

        if (!seeded) {
            prev0  = d.btn0_pressed;
            prev1  = d.btn1_pressed;
            seeded = true;
            vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
            continue;
        }

        if (d.btn0_pressed && !prev0) {
            presses0++;
            printf("[io/01] BTN0 down   (slider now %u)\r\n", (unsigned)d.slider);
        } else if (!d.btn0_pressed && prev0) {
            printf("[io/01] BTN0 up\r\n");
        }

        if (d.btn1_pressed && !prev1) {
            presses1++;
            printf("[io/01] BTN1 down   (slider now %u)\r\n", (unsigned)d.slider);
        } else if (!d.btn1_pressed && prev1) {
            printf("[io/01] BTN1 up\r\n");
        }

        prev0 = d.btn0_pressed;
        prev1 = d.btn1_pressed;

        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }

    printf("[io/01] done: %u reads, %u failed, BTN0 pressed %u time(s), "
           "BTN1 pressed %u time(s)\r\n",
           reads, failures, presses0, presses1);

    /* 3. An honest result. Zero good reads is not "no presses" — it is a dead
     *    path, and saying so is the difference between debugging the driver
     *    and debugging your finger. */
    if (reads == 0U) {
        printf("[io/01] every read failed — see the two causes above.\r\n");
        return SDK_EX_NO_DATA;
    }
    return SDK_EX_OK;
}

#else  /* !BSP_HAS_CAPSENSE */

#include "../sdk_examples_cm33.h"
#include <stdio.h>

int example_io_read_buttons(void)
{
    /* No 4000T on this board. The entry point still exists because the
     * generated table names it unconditionally; what it must not do is invent
     * a reading. */
    printf("[io/01] built with BSP_HAS_CAPSENSE=0 — no CapSense on this board\r\n");
    return SDK_EX_UNAVAILABLE;
}

#endif /* BSP_HAS_CAPSENSE */
