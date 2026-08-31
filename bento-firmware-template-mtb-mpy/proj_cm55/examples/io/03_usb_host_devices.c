/* sdk-example: core=cm55 variant=both group=io
 * id:      cm55/io/03_usb_host_devices
 * title:   Read a USB gamepad, and a smart card, from the host port
 * teaches: why init is a REQUEST and never a call from the GFX task, how to tell a controller that is absent from one that is present and silent, and the two decode layers
 * apis:    usb_hid_joystick_init, usb_hid_joystick_request_init, usb_hid_joystick_is_connected, usb_hid_joystick_get_state, f310_parse, f310_deadzone, usb_ccid_smartcard_request_init, usb_ccid_smartcard_get_state, usb_ccid_smartcard_trigger_read
 * entry:   example_cm55_usb_host_devices
 */
/*******************************************************************************
 * io/03 — the USB host port: a Logitech F310 and an ACR39U card reader.
 *
 * WHICH CORE, AND WHY
 * -------------------
 * Both drivers are CM55. proj_cm55/Makefile:225 (reference:
 * proj_cm55/Makefile:194) pulls in usb_hid_joystick.mk, and the line after it
 * pulls in usb_ccid_smartcard.mk, under COMPONENTS+=USBH_BASE and
 * DEFINES+=ENABLE_USB_HOST=1. There is no CM33 copy: MicroPython reaches the
 * joystick over IPC, and the enumeration still happens here.
 *
 * INIT IS A REQUEST, NOT A CALL
 * -----------------------------
 * usb_hid_joystick_init() creates the emUSB-Host tasks and enumerates. That
 * takes hundreds of milliseconds and it blocks. Calling it from an LVGL event
 * callback — which is where this function runs — freezes the display for the
 * whole of enumeration, and freezes the busy overlay that would have explained
 * the freeze along with it.
 *
 * usb_hid_joystick_request_init() exists for exactly that reason
 * (usb_hid_joystick.h:94-95): it hands the work to a worker task and returns
 * a bool immediately. This example uses the request form and polls. The
 * blocking form is referenced below in the branch that a boot-time caller —
 * main(), before the scheduler is busy — would use, so the symbol is here and
 * so is the warning.
 *
 * ABSENT IS NOT THE SAME AS SILENT
 * --------------------------------
 * joystick_state_t carries the counters that separate the two
 * (usb_hid_joystick.h:38-73):
 *
 *   init_stage      USB_STAGE_* — where enumeration got to. 7 = COMPLETE.
 *   add_event_cnt   the host saw a device attach at all
 *   report_cnt      HID reports that arrived
 *   sequence        reports that PASSED the size guard
 *   reject_cnt      reports the size guard DROPPED
 *
 * report_cnt == sequence + reject_cnt must hold, modulo wrap. When it does and
 * reject_cnt is climbing, the controller is talking and the byte map is wrong
 * — which is a different bug from "nothing is plugged in", and raw[] holds the
 * last report exactly as received so it can be settled without guessing.
 *
 * TWO DECODE LAYERS
 * -----------------
 *   L1  hid_f310_report.h    the raw 8-byte report, the button masks, VID/PID
 *   L2  hid_f310_parser.h    f310_parse() -> signed axes and named booleans
 *
 * f310_parse() does NOT check that the report came from an F310
 * (hid_f310_parser.h:33-35) — .source says which decoder filled .report, and
 * an NJ43 is remapped into the same struct at connect time. Check .source
 * before trusting a button name.
 *
 * DEADZONE
 * --------
 * f310_deadzone(value, threshold) clamps to 0 inside the band and does NOT
 * rescale outside it (hid_f310_parser.h:38-43). Threshold 8 is the value the
 * header names for a well-used stick, which is about 6 % of the +/-128 range.
 *
 * NOT BLOCKING
 * ------------
 * The request form returns at once; everything else here is a struct read.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "usb_hid_joystick.h"
#include "hid_f310_parser.h"
#include "usb_ccid_smartcard.h"

#define POLL_PERIOD_MS   (100u)
#define DEADZONE_COUNTS  (8)

static lv_obj_t   *s_state;
static lv_obj_t   *s_axes;
static lv_obj_t   *s_card;
static lv_timer_t *s_timer;

static void parent_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_state = NULL;
    s_axes  = NULL;
    s_card  = NULL;
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;

    const joystick_state_t *js = usb_hid_joystick_get_state();
    if ((js == NULL) || (s_state == NULL)) {
        return;
    }

    lv_label_set_text_fmt(s_state,
                          "stage %u/%u  add %u  reports %u  passed %u  rejected %u",
                          (unsigned)js->init_stage, (unsigned)USB_STAGE_COMPLETE,
                          (unsigned)js->add_event_cnt, (unsigned)js->report_cnt,
                          (unsigned)js->sequence, (unsigned)js->reject_cnt);

    if (!usb_hid_joystick_is_connected()) {
        lv_label_set_text(s_axes, "no controller on the port");
        return;
    }

    /* Copy the volatile report out before decoding it: the USB callback can
     * replace it mid-parse otherwise, and a half-old report is a phantom
     * button press that happens once an hour and never reproduces. */
    f310_report_t raw;
    (void)memcpy(&raw, (const void *)&js->report, sizeof(raw));

    f310_decoded_t d;
    if (!f310_parse(&raw, &d)) {
        lv_label_set_text(s_axes, "f310_parse() refused the report");
        return;
    }

    const int16_t lx = f310_deadzone(d.lx, DEADZONE_COUNTS);
    const int16_t ly = f310_deadzone(d.ly, DEADZONE_COUNTS);

    lv_label_set_text_fmt(s_axes,
                          "%s  L %+4d,%+4d  hat %u  A%d B%d X%d Y%d",
                          joystick_source_name(js->source),
                          (int)lx, (int)ly, (unsigned)d.hat,
                          d.a ? 1 : 0, d.b ? 1 : 0, d.x ? 1 : 0, d.y ? 1 : 0);

    if (s_card != NULL) {
        const smartcard_state_t *sc = usb_ccid_smartcard_get_state();
        if (sc == NULL) {
            lv_label_set_text(s_card, "card reader: driver not up");
        } else {
            lv_label_set_text_fmt(s_card,
                                  "card reader: ACR39U %04X:%04X — see state",
                                  (unsigned)ACR39U_VID, (unsigned)ACR39U_PID);
        }
    }
}

static void on_read_card(lv_event_t *e)
{
    (void)e;
    sdk_example_log_clear();

    /* Also a REQUEST. The CCID exchange is seconds long on a Thai national ID
     * card — the photo alone is read in 64-byte chunks because larger ones hang
     * T=0 chaining (usb_ccid_smartcard.h:50-52). It cannot happen here. */
    usb_ccid_smartcard_trigger_read();
    sdk_example_logf("read requested; the CCID task does the exchange");
    sdk_example_logf("  photo is pulled in %u-byte chunks — larger chunks",
                     (unsigned)THAI_ID_PHOTO_CHUNK);
    sdk_example_logf("  hang T=0 chaining on this reader");
    sdk_example_logf("  poll usb_ccid_smartcard_get_state() for the result");
}

int example_cm55_usb_host_devices(lv_obj_t *parent)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    lv_obj_remove_event_cb(parent, parent_deleted_cb);
    lv_obj_add_event_cb(parent, parent_deleted_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *caption = lv_label_create(parent);
    lv_label_set_text(caption, "USB host: HID gamepad + CCID card reader (CM55)");
    lv_obj_set_style_text_color(caption, lv_color_hex(0xD8E0F0), 0);
    lv_obj_set_pos(caption, 10, 8);

    s_state = lv_label_create(parent);
    lv_obj_set_pos(s_state, 10, 36);
    lv_obj_set_style_text_color(s_state, lv_color_hex(0xB8C4D8), 0);
    lv_label_set_text(s_state, "...");

    s_axes = lv_label_create(parent);
    lv_obj_set_pos(s_axes, 10, 58);
    lv_obj_set_style_text_color(s_axes, lv_color_hex(0xB8C4D8), 0);
    lv_label_set_text(s_axes, "...");

    s_card = lv_label_create(parent);
    lv_obj_set_pos(s_card, 10, 80);
    lv_obj_set_style_text_color(s_card, lv_color_hex(0xB8C4D8), 0);
    lv_label_set_text(s_card, "...");

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, 10, 110);
    lv_obj_set_size(btn, 180, 44);
    lv_obj_add_event_cb(btn, on_read_card, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "read the card");
    lv_obj_center(lbl);

    /* Both are requests. Neither blocks. Neither is idempotent-unsafe: the
     * joystick driver documents its init as idempotent (usb_hid_joystick.h:91). */
    const bool asked = usb_hid_joystick_request_init();
    usb_ccid_smartcard_request_init();

    const joystick_state_t *js = usb_hid_joystick_get_state();
    sdk_example_logf("usb_hid_joystick_request_init() = %s",
                     asked ? "queued" : "refused (already running?)");
    sdk_example_logf("expecting F310 %04X:%04X (DirectInput) or NJ43 %04X:%04X",
                     (unsigned)F310_VID, (unsigned)F310_PID_DINPUT,
                     (unsigned)NJ43_VID, (unsigned)NJ43_PID);
    sdk_example_logf("  F310 in XInput mode (%04X) is NOT supported — flip the",
                     (unsigned)F310_PID_XINPUT);
    sdk_example_logf("  switch on the back of the pad to D");

    if (js != NULL) {
        sdk_example_logf("init stage on entry: %u (%u = COMPLETE)",
                         (unsigned)js->init_stage, (unsigned)USB_STAGE_COMPLETE);
    }

    if (false) {
        /* Never reached, and deliberately present. usb_hid_joystick_init() is
         * the blocking form. It is correct at boot, from main(), before the
         * GFX task owns the screen — and it is wrong here, which is what this
         * dead branch is for: the symbol is named where the warning is. */
        usb_hid_joystick_init();
    }

    s_timer = lv_timer_create(poll_cb, POLL_PERIOD_MS, NULL);
    if (s_timer == NULL) {
        sdk_example_logf("lv_timer_create failed — nothing will refresh");
        return SDK_EX_REFUSED;
    }
    return SDK_EX_STARTED;
}
