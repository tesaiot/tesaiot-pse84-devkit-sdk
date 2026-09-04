/* sdk-example: core=cm33 variant=both group=io
 * id:      cm33/io/02_capsense_slider
 * title:   Read the CapSense slider position
 * teaches: the slider is 0..100 with no "not touched" value, so you need a
 *          release test and a hysteresis band before you drive anything with it
 * apis:    capsense_init, capsense_read_slider
 * entry:   example_io_capsense_slider
 */
/*******************************************************************************
 * io/02 — the CapSense slider, and the two things it does not tell you.
 *
 * capsense_read_slider() gives you one byte, 0..100, left to right along the
 * pad. That is the whole API. Two facts are not in it and both bite:
 *
 * 1. THERE IS NO "NOT TOUCHED" VALUE.
 *    0 is a real position — the far left of the pad — not "released". When
 *    you lift your finger the last position simply stops changing. So a
 *    volume control written as `volume = slider;` snaps to whatever the pad
 *    last saw and stays there, and nothing in the reading says the user let
 *    go. If you need "touched or not", read the whole frame with
 *    capsense_read() and use the buttons, or treat "value unchanged for N
 *    polls" as released — which is what this example demonstrates.
 *
 * 2. THE LAST DIGIT IS NOISE.
 *    A finger resting still on the pad wanders by a count or two. Feed that
 *    straight into a setpoint and you get a value that never settles, a
 *    display that flickers, and — if it drives a motor or a servo — an output
 *    that hunts. The fix is a hysteresis band, not a faster poll: this file
 *    only accepts a new position once it has moved SLIDER_HYST counts away
 *    from the one it is holding.
 *
 * COST
 * ----
 * capsense_read_slider() is not cheaper than capsense_read(). Both do the same
 * one 3-byte transaction; this one discards the two button bytes. Use it when
 * the slider is genuinely all you want, and call capsense_read() once when you
 * want buttons too, rather than calling both.
 *
 * BACKENDS
 * --------
 * Same two-backend story as io/01, and the same consequence: on a QWA309
 * board this read is an IPC round trip to CM55's 50 ms cache, so polling at
 * 200 Hz gets you the same number forty times over. 50 ms is the floor worth
 * asking for.
 *
 *     make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=cm33/io/02_capsense_slider
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if BSP_HAS_CAPSENSE

#include "../sdk_examples_cm33.h"

#include "sensor_capsense.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

#define SLIDER_POLL_MS      (50U)
#define SLIDER_POLL_COUNT   (100U)      /* 5 s */

/* Counts the position must move before it is accepted. 3 of 101 is about 3 %
 * of the pad: wide enough to sit still through finger tremor, narrow enough
 * that a deliberate slide feels continuous. Widen it if your output is
 * expensive to change (an I2C write, a servo step); never set it to 0. */
#define SLIDER_HYST         (3)

/* Polls with no movement at all before the pad is called released. At 50 ms a
 * count of 6 is 300 ms — longer than the gap between two samples of a moving
 * finger, shorter than a human would notice. */
#define SLIDER_IDLE_POLLS   (6U)

/* Absolute difference of two 0..100 positions, as an int so the subtraction
 * cannot wrap. Doing this with uint8_t is the classic version of this bug:
 * (uint8_t)(2 - 5) is 253, which passes every "moved a lot" test. */
static int slider_delta(uint8_t a, uint8_t b)
{
    const int d = (int)a - (int)b;
    return (d < 0) ? -d : d;
}

int example_io_capsense_slider(void)
{
    /* Fingers OFF the pad for this call — the driver captures its idle
     * baseline here. See io/01 for why, and for what a bad baseline looks
     * like. */
    if (!capsense_init()) {
        printf("[io/02] capsense_init() failed — see io/01 for the two causes\r\n");
        return SDK_EX_UNAVAILABLE;
    }

    printf("[io/02] Slide a finger along the CapSense pad for the next %u s.\r\n",
           (unsigned)((SLIDER_POLL_MS * SLIDER_POLL_COUNT) / 1000U));

    uint8_t  raw       = 0U;     /* what the pad said this poll               */
    uint8_t  held      = 0U;     /* the position we are actually acting on    */
    uint8_t  last_raw  = 0U;
    bool     have_held = false;
    unsigned still     = 0U;     /* consecutive polls with no movement        */
    bool     touching  = false;

    unsigned reads = 0U, failures = 0U, accepted = 0U;

    for (unsigned i = 0U; i < SLIDER_POLL_COUNT; i++) {
        if (!capsense_read_slider(&raw)) {
            failures++;
            vTaskDelay(pdMS_TO_TICKS(SLIDER_POLL_MS));
            continue;
        }
        reads++;

        /* --- release detection ------------------------------------------
         * The driver cannot tell us. All we have is "the number stopped
         * moving", so that is the test, and it is honest about being a
         * heuristic rather than a touch flag. The first read establishes
         * last_raw and nothing else — with only one sample there is no
         * movement to speak of either way. */
        if (reads > 1U) {
            if (slider_delta(raw, last_raw) != 0) {
                still = 0U;
                if (!touching) {
                    touching = true;
                    printf("[io/02] touch   (pad went live at %u)\r\n",
                           (unsigned)raw);
                }
            } else {
                if (still < SLIDER_IDLE_POLLS) {
                    still++;
                }
                if (still >= SLIDER_IDLE_POLLS && touching) {
                    touching = false;
                    printf("[io/02] release (holding %u)\r\n", (unsigned)held);
                }
            }
        }
        last_raw = raw;

        /* --- hysteresis --------------------------------------------------
         * Accept the first reading unconditionally so the control starts
         * somewhere real, then only on a move bigger than the band. */
        if (!have_held) {
            held      = raw;
            have_held = true;
            accepted++;
            printf("[io/02] start at %u\r\n", (unsigned)held);
        } else if (slider_delta(raw, held) >= SLIDER_HYST) {
            held = raw;
            accepted++;
            printf("[io/02] slider -> %3u  (raw %3u)\r\n",
                   (unsigned)held, (unsigned)raw);
        }

        vTaskDelay(pdMS_TO_TICKS(SLIDER_POLL_MS));
    }

    printf("[io/02] done: %u reads, %u failed, %u position change(s) accepted "
           "with a +/-%d band\r\n",
           reads, failures, accepted, SLIDER_HYST);
    printf("[io/02] final position %u of 100\r\n", (unsigned)held);

    if (reads == 0U) {
        return SDK_EX_NO_DATA;
    }
    return SDK_EX_OK;
}

#else  /* !BSP_HAS_CAPSENSE */

#include "../sdk_examples_cm33.h"
#include <stdio.h>

int example_io_capsense_slider(void)
{
    printf("[io/02] built with BSP_HAS_CAPSENSE=0 — no CapSense on this board\r\n");
    return SDK_EX_UNAVAILABLE;
}

#endif /* BSP_HAS_CAPSENSE */
