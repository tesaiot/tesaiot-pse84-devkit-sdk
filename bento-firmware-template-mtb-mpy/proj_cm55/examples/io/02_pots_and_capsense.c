/* sdk-example: core=cm55 variant=both group=io
 * id:      cm55/io/02_pots_and_capsense
 * title:   Read the four knobs and the CapSense pad from CM55
 * teaches: why these two live on CM55 and not on the core MicroPython runs on, the knob-to-channel table that is NOT the identity, and the two tick rates the driver deliberately keeps apart
 * apis:    cm55_sensor_poll_init, cm55_sensor_poll_tick, cm55_capsense_tick, cm55_sensor_poll_status, cm55_pot_read_all, cm55_sensor_poll_diag, ipc_sensorhub_snapshot
 * entry:   example_cm55_pots_and_capsense
 */
/*******************************************************************************
 * io/02 — VR1-4 and the CapSense pad, read where they actually live.
 *
 * WHY CM55 OWNS BOTH
 * ------------------
 * This is the trap that costs an afternoon. The QWA309 CapSense part is an
 * external PSoC 4000T at I2C 0x08 — on the DISPLAY bus, whose controller CM55
 * owns. It is NOT on the bus `sensors.scan()` walks from MicroPython, so a
 * developer who scans that bus and sees nothing at 0x08 concludes the pad is
 * dead. It is not; it is on the other bus, and the other core.
 *
 *   proj_cm55/modules/cm55_sensor_poll/cm55_sensor_poll.c:4-8    both devices
 *   proj_cm55/modules/cm55_sensor_poll/cm55_sensor_poll.c:65     0x08
 *   proj_cm55/modules/cm55_sensor_poll/cm55_sensor_poll.c:43-47  the bus is
 *       DISPLAY_I2C_CONTROLLER_HW + disp_touch_i2c_controller_context
 *
 * The pots are the same story from the other direction: they are on the
 * AutAnalog SAR, GPIO channels 4-7 = P15.4-7, and that block is in the CM55
 * domain (cm55_sensor_poll.h:16, cm55_sensor_poll.c:69-72). CM33_NS runs
 * Cy_AutAnalog_Init + StartAutonomousControl at boot; CM55 reads the results.
 *
 * THE KNOB ORDER IS NOT THE CHANNEL ORDER
 * ---------------------------------------
 * s_pot_adc_ch[] = { 5, 4, 6, 7 }.
 *
 *   logical VR1 -> SAR GPIO channel 5 (P15.5)
 *   logical VR2 -> SAR GPIO channel 4 (P15.4)
 *   logical VR3 -> channel 6, VR4 -> channel 7, both straight through
 *
 * The first two traces are swapped on the QWA309 PCB — verified on hardware
 * 2026-07-31, cm55_sensor_poll.c:74-78. The MicroPython `pots` module carries
 * the identical table for the identical reason, and says to keep the two in
 * lockstep (kit-tesaiot-pse84-ai/mpy/mod_qwa309_pots.c:48-55).
 *
 * TWO SOURCES IN THIS WORKSPACE DISAGREE, AND ONLY ONE OF THEM COMPILES
 * ---------------------------------------------------------------------
 * The board errata note says the opposite:
 *
 *   docs/BentoClaw_Integration/en/04_capability_pinmap.md:52-56
 *     "The PCBA assembly silkscreen prints VR1 = P15.5, VR2 = P15.4 —
 *      swapped. The schematic (sheet 19) and the pin docs agree that
 *      VR1 = P15.4, VR2 = P15.5 ... The schematic is authoritative:
 *      firmware maps VR1 -> SAR ch4 -> P15.4."
 *
 *   qwa309-training-base/docs/PIN_MAP.md:37 lists P15.4 VR1, P15.5 VR2 —
 *   the same net-name order as the schematic.
 *
 * That last sentence is not what the firmware does. The compiled table maps
 * logical index 0 to channel 5. Both statements are on record here and this
 * example does not pretend they agree: the array is quoted verbatim from the
 * code, which is the version that was measured on a board, and the doc is
 * quoted so the next reader is not surprised by it. If a knob moves the wrong
 * bar on YOUR unit, that is the question to settle with a meter — not by
 * editing one of these two files to match the other.
 *
 * cm55_pot_read_all() hands you LOGICAL order — the swap is already applied —
 * so a caller of this API never has to know any of this.
 *
 * TWO TICKS, ON PURPOSE
 * ---------------------
 *   cm55_sensor_poll_tick()   every ~200 ms   pots + CapSense + hub feed
 *   cm55_capsense_tick()      every ~50 ms    CapSense only, game rate
 *
 * They are separate because the CapSense transaction is the risky one. It is
 * time-bounded at 2 ms per PDL byte-call and backs off ~500 ms after a failure
 * (cm55_sensor_poll.c:50-58). Before that bound existed, a wedged 4000T parked
 * the GFX task on the bus at MAX-1 priority and starved every lower-priority
 * CM55 task — that was the prime suspect for the USB joystick HID stall. Do
 * not "simplify" this by calling the 200 ms tick at 50 ms: that also re-reads
 * the SAR and re-feeds the hub four times as often for no gain.
 *
 * NOT BLOCKING
 * ------------
 * Both ticks are bounded by construction. This example drives them from an
 * LVGL timer at the rates the driver asks for and returns immediately.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "bsp_feature_flags.h"
#include "ipc_communication.h"
#include "ipc_sensorhub.h"

#if BSP_HAS_QWA309_BASEBOARD
#include "cm55_sensor_poll.h"
#endif

#if BSP_HAS_QWA309_BASEBOARD

/* The driver's own documented rates. Nothing here is a guess:
 * cm55_sensor_poll.h:20 (~200 ms) and cm55_sensor_poll.h:40-44 (~50 ms). */
#define POLL_PERIOD_MS       (200u)
#define CAPSENSE_PERIOD_MS   (50u)

/* cm55_sensor_poll_status() returns a bit field, documented at
 * cm55_sensor_poll.h:48 as "bit1=capsense, bit2=pot". */
#define STATUS_CAPSENSE      (0x02u)
#define STATUS_POT           (0x04u)

static lv_obj_t  *s_lines[QWA309_POT_COUNT + 2];
static lv_timer_t *s_slow;
static lv_timer_t *s_fast;

static void stop_timers(void)
{
    if (s_slow != NULL) { lv_timer_delete(s_slow); s_slow = NULL; }
    if (s_fast != NULL) { lv_timer_delete(s_fast); s_fast = NULL; }
}

static void parent_deleted_cb(lv_event_t *e)
{
    (void)e;
    stop_timers();
    (void)memset(s_lines, 0, sizeof(s_lines));
}

/* 200 ms: full poll, then read the values back OUT of the hub rather than out
 * of the driver. That round trip is the point — everything else on the board
 * (the Controls page, the pot MicroPython snapshot) sees them the same way. */
static void slow_cb(lv_timer_t *t)
{
    (void)t;
    cm55_sensor_poll_tick();

    uint16_t raw[QWA309_POT_COUNT];
    const uint8_t n = cm55_pot_read_all(raw);

    for (unsigned i = 0u; i < QWA309_POT_COUNT; i++) {
        if (s_lines[i] == NULL) {
            continue;
        }
        if (i < n) {
            /* 12-bit SAR, 0..4095, reference 1.8 V (cm55_sensor_poll.c:73). */
            const unsigned mv = ((unsigned)raw[i] * 1800u) / 4095u;
            lv_label_set_text_fmt(s_lines[i], "VR%u  raw %4u  %u.%03u V",
                                  i + 1u, (unsigned)raw[i],
                                  mv / 1000u, mv % 1000u);
        } else {
            lv_label_set_text_fmt(s_lines[i], "VR%u  not ready", i + 1u);
        }
    }

    sensorhub_snapshot_t snap;
    ipc_sensorhub_snapshot(&snap);
    if (s_lines[QWA309_POT_COUNT] != NULL) {
        lv_label_set_text_fmt(s_lines[QWA309_POT_COUNT],
                              "hub: pot %s  capsense %s",
                              snap.has_pot ? "yes" : "no",
                              snap.has_capsense ? "yes" : "no");
    }
}

/* 50 ms: CapSense only. */
static void fast_cb(lv_timer_t *t)
{
    (void)t;
    cm55_capsense_tick();

    sensorhub_snapshot_t snap;
    ipc_sensorhub_snapshot(&snap);
    if (s_lines[QWA309_POT_COUNT + 1] == NULL) {
        return;
    }
    if (snap.has_capsense) {
        lv_label_set_text_fmt(s_lines[QWA309_POT_COUNT + 1],
                              "pad: btn0 %u  btn1 %u  slider %u",
                              (unsigned)snap.capsense.btn0_pressed,
                              (unsigned)snap.capsense.btn1_pressed,
                              (unsigned)snap.capsense.slider);
    } else {
        lv_label_set_text(s_lines[QWA309_POT_COUNT + 1],
                          "pad: nothing published yet");
    }
}

int example_cm55_pots_and_capsense(lv_obj_t *parent)
{
    stop_timers();
    lv_obj_remove_event_cb(parent, parent_deleted_cb);
    lv_obj_add_event_cb(parent, parent_deleted_cb, LV_EVENT_DELETE, NULL);

    /* 1. Bring the local poller up. Idempotent, and it must happen AFTER the
     *    display I2C exists — which it does, because the Examples page is
     *    already on screen (cm55_sensor_poll.h:19). */
    const bool up = cm55_sensor_poll_init();

    /* 2. Ask what actually initialised. A board with the pad unpopulated
     *    still reports the pots, and vice versa; one bool would hide that. */
    const uint8_t st = cm55_sensor_poll_status();
    sdk_example_logf("cm55_sensor_poll_init() = %s", up ? "true" : "false");
    sdk_example_logf("  status 0x%02X -> capsense %s, pot %s",
                     (unsigned)st,
                     (st & STATUS_CAPSENSE) ? "up" : "absent",
                     (st & STATUS_POT) ? "up" : "absent");

    /* 3. The driver's own diagnostic string. Fixed buffer, and the call
     *    returns how many bytes it wrote so a short buffer truncates rather
     *    than overflowing. */
    uint8_t diag[128];
    const uint16_t n = cm55_sensor_poll_diag(diag, (uint16_t)sizeof(diag) - 1u);
    diag[(n < sizeof(diag)) ? n : (sizeof(diag) - 1u)] = 0u;
    sdk_example_logf("diag: %s", (const char *)diag);

    if (st == 0u) {
        sdk_example_logf("Neither capability came up. On this board that is");
        sdk_example_logf("almost always the base-board power switch, not a");
        sdk_example_logf("fault — the SAR needs CM33_NS to have started the");
        sdk_example_logf("autonomous scan, and the pad needs 3V3_BB.");
        return SDK_EX_UNAVAILABLE;
    }

    lv_obj_t *caption = lv_label_create(parent);
    lv_label_set_text(caption,
                      "VR1-4 on SAR GPIO ch 5,4,6,7 (VR1/VR2 swapped on the PCB)");
    lv_obj_set_style_text_color(caption, lv_color_hex(0xD8E0F0), 0);
    lv_obj_set_pos(caption, 10, 8);

    for (unsigned i = 0u; i < (unsigned)(QWA309_POT_COUNT + 2); i++) {
        s_lines[i] = lv_label_create(parent);
        lv_label_set_text(s_lines[i], "...");
        lv_obj_set_style_text_color(s_lines[i], lv_color_hex(0xB8C4D8), 0);
        lv_obj_set_pos(s_lines[i], 10, (int32_t)(36 + i * 22u));
    }

    s_slow = lv_timer_create(slow_cb, POLL_PERIOD_MS, NULL);
    s_fast = lv_timer_create(fast_cb, CAPSENSE_PERIOD_MS, NULL);
    if ((s_slow == NULL) || (s_fast == NULL)) {
        stop_timers();
        sdk_example_logf("lv_timer_create failed — nothing will refresh");
        return SDK_EX_REFUSED;
    }

    sdk_example_logf("polling: %u ms full tick, %u ms capsense-only tick",
                     POLL_PERIOD_MS, CAPSENSE_PERIOD_MS);
    return SDK_EX_STARTED;
}

#else  /* !BSP_HAS_QWA309_BASEBOARD */

int example_cm55_pots_and_capsense(lv_obj_t *parent)
{
    (void)parent;
    sdk_example_logf("No QWA309 base board in this build.");
    sdk_example_logf("  cm55_sensor_poll is the Dev Kit base-board driver.");
    sdk_example_logf("  On the plain AI Kit the IMU is serviced on CM33_NS");
    sdk_example_logf("  and there are no VR knobs and no CapSense pad.");
    return SDK_EX_UNAVAILABLE;
}

#endif /* BSP_HAS_QWA309_BASEBOARD */
