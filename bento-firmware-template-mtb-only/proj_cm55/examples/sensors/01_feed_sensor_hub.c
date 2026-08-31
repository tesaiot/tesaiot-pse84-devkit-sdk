/* sdk-example: core=cm55 variant=both group=sensors
 * id:      cm55/sensors/01_feed_sensor_hub
 * title:   Publish a locally-read sensor into the hub
 * teaches: the Eva Kit path - CM55 reads the bus, feeds the hub, and every consumer sees it; plus who owns the sequence number
 * apis:    ipc_sensorhub_feed_bmi270, ipc_sensorhub_feed_bmm350, ipc_sensorhub_feed_capsense, ipc_sensorhub_feed_pot, ipc_sensorhub_snapshot
 * entry:   example_ipc_core_sensor_feed
 */
/*******************************************************************************
 * ipc_core/07_sensor_feed — the hub read from the writing end.
 *
 * WHY THIS API EXISTS
 * -------------------
 * The normal direction is CM33_NS -> IPC -> hub -> your page. On some boards
 * it cannot be: the Eva Kit puts the sensors on SCB0, the display owns SCB0,
 * and SCB0 has exactly one master. CM33_NS physically cannot read them. So
 * CM55 reads them locally and feeds the hub, and from that moment the
 * Dashboard, the Smart Watch face and MicroPython's `sensors` module see
 * those readings without knowing or caring where they came from.
 *
 * THE SEQUENCE NUMBER IS YOURS
 * ----------------------------
 * ipc_sensorhub_feed_*() memcpy your struct in whole, sequence field included.
 * It does not stamp one for you. ipc_sensorhub_snapshot() derives the
 * *_changed flag by comparing that field against a last-seen value the hub
 * keeps once per sensor, module-wide — so a feeder that leaves sequence at a
 * constant makes every consumer believe the sensor is frozen, while the values
 * on screen keep moving. Advance it on every sample.
 *
 * FEEDING ALSO ASSERTS PRESENCE
 * -----------------------------
 * Each feed sets the hub's has_<sensor> flag true and nothing ever sets it
 * back. Feed a CapSense sample on a board with no CapSense and the Dashboard
 * grows a CapSense row for the rest of the power cycle. That is why the button
 * below only fabricates readings for absent sensors when the switch beside it
 * says so: it is a deliberate act with a label on it, not a side effect of
 * tapping an example.
 *
 * For sensors the board DOES have, this example re-publishes the values it
 * just read, with the sequence advanced. Nothing on screen changes, and the
 * round trip proves the path.
 *
 * NOT BLOCKING
 * ------------
 * No I2C here, on purpose: the sensor buses have one owner each and an example
 * page is not it. Replace read_local_*() with your driver call.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_communication.h"
#include "ipc_sensorhub.h"

static lv_obj_t *s_report;
static lv_obj_t *s_allow_sw;

/* One monotonically increasing counter per feeder. A real driver would use
 * its own sample counter; what matters is only that it changes. */
static uint16_t s_seq_bmi270;
static uint16_t s_seq_bmm350;
static uint16_t s_seq_capsense;
static uint16_t s_seq_pot;

static void report(const char *fmt, int a, int b)
{
    if (s_report == NULL) {
        return;
    }
    lv_obj_t *lbl = lv_label_create(s_report);
    lv_label_set_text_fmt(lbl, fmt, a, b);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xD8E0F0), 0);
}

/*******************************************************************************
 * Stand-ins for your driver. On a real Eva Kit these are the I2C reads, and
 * they run in whichever task owns that bus — NOT here.
 *******************************************************************************/
static void read_local_bmi270(ipc_sensor_bmi270_t *out)
{
    memset(out, 0, sizeof(*out));
    out->az = (int16_t)IPC_BMI270_ACCEL_LSB_PER_G;   /* 1 g, flat on a desk */
    out->sequence = ++s_seq_bmi270;
}

static void read_local_bmm350(ipc_sensor_bmm350_t *out)
{
    memset(out, 0, sizeof(*out));
    out->heading_x10 = 900;                          /* 90.0 deg, east      */
    out->sequence = ++s_seq_bmm350;
}

static void read_local_capsense(ipc_sensor_capsense_t *out)
{
    memset(out, 0, sizeof(*out));
    out->slider = 50;
    out->sequence = ++s_seq_capsense;
}

static void read_local_pot(ipc_sensor_pot_t *out)
{
    memset(out, 0, sizeof(*out));
    out->raw = 32768u;
    out->percent_x10 = 500u;                         /* 50.0 %              */
    out->sequence = ++s_seq_pot;
}

/*******************************************************************************
 * Publish one round. Present sensors are re-published with their own values;
 * absent ones are only fabricated when `fabricate` is true.
 * Returns how many feeds were made.
 *******************************************************************************/
static int publish_round(bool fabricate)
{
    sensorhub_snapshot_t snap;
    int fed = 0;

    ipc_sensorhub_snapshot(&snap);

    if (snap.has_bmi270) {
        ipc_sensor_bmi270_t s = snap.bmi270;   /* keep the real reading */
        s.sequence = ++s_seq_bmi270;           /* but move it forward   */
        ipc_sensorhub_feed_bmi270(&s);
        fed++;
    } else if (fabricate) {
        ipc_sensor_bmi270_t s;
        read_local_bmi270(&s);
        ipc_sensorhub_feed_bmi270(&s);
        fed++;
    }

    if (snap.has_bmm350) {
        ipc_sensor_bmm350_t s = snap.bmm350;
        s.sequence = ++s_seq_bmm350;
        ipc_sensorhub_feed_bmm350(&s);
        fed++;
    } else if (fabricate) {
        ipc_sensor_bmm350_t s;
        read_local_bmm350(&s);
        ipc_sensorhub_feed_bmm350(&s);
        fed++;
    }

    if (snap.has_capsense) {
        ipc_sensor_capsense_t s = snap.capsense;
        s.sequence = ++s_seq_capsense;
        ipc_sensorhub_feed_capsense(&s);
        fed++;
    } else if (fabricate) {
        ipc_sensor_capsense_t s;
        read_local_capsense(&s);
        ipc_sensorhub_feed_capsense(&s);
        fed++;
    }

    if (snap.has_pot) {
        ipc_sensor_pot_t s = snap.pot;
        s.sequence = ++s_seq_pot;
        ipc_sensorhub_feed_pot(&s);
        fed++;
    } else if (fabricate) {
        ipc_sensor_pot_t s;
        read_local_pot(&s);
        ipc_sensorhub_feed_pot(&s);
        fed++;
    }

    return fed;
}

static void publish_cb(lv_event_t *e)
{
    (void)e;
    const bool fabricate = (s_allow_sw != NULL) &&
                           lv_obj_has_state(s_allow_sw, LV_STATE_CHECKED);
    const int fed = publish_round(fabricate);

    /* Read it straight back. This is the proof: the hub reports the sensor
     * present and, because the sequence moved, CHANGED. */
    sensorhub_snapshot_t after;
    ipc_sensorhub_snapshot(&after);

    sdk_example_logf("published %d sample(s)%s", fed,
                     fabricate ? " (absent sensors fabricated)" : "");
    sdk_example_logf("  bmi270 present %d changed %d seq %u",
                     (int)after.has_bmi270, (int)after.bmi270_changed,
                     (unsigned)after.bmi270.sequence);
    sdk_example_logf("  pot    present %d changed %d seq %u",
                     (int)after.has_pot, (int)after.pot_changed,
                     (unsigned)after.pot.sequence);
}

int example_ipc_core_sensor_feed(lv_obj_t *parent)
{
    lv_obj_clean(parent);
    s_report = NULL;
    s_allow_sw = NULL;

    /* What is publishing right now, and at which sequence. */
    sensorhub_snapshot_t before;
    ipc_sensorhub_snapshot(&before);

    /* Continue from whatever CM33 last sent so the numbers stay monotonic.
     * Restarting at zero would make the next snapshot() see a LOWER sequence,
     * which reads as "changed" once and then as stuck. */
    s_seq_bmi270   = before.bmi270.sequence;
    s_seq_bmm350   = before.bmm350.sequence;
    s_seq_capsense = before.capsense.sequence;
    s_seq_pot      = before.pot.sequence;

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(96), 250);
    lv_obj_set_pos(card, 8, 8);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141428), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3550), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    s_report = card;

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Sensors the hub is holding");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    report("BMI270    present %d   seq %u", (int)before.has_bmi270,
           (int)before.bmi270.sequence);
    report("BMM350    present %d   seq %u", (int)before.has_bmm350,
           (int)before.bmm350.sequence);
    report("CapSense  present %d   seq %u", (int)before.has_capsense,
           (int)before.capsense.sequence);
    report("Pot       present %d   seq %u", (int)before.has_pot,
           (int)before.pot.sequence);

    /* One non-destructive round straight away: everything the board really has
     * is re-published with its own values and a fresh sequence. */
    const int fed = publish_round(false);

    sensorhub_snapshot_t after;
    ipc_sensorhub_snapshot(&after);
    report("re-published %d present sensor(s); BMI270 seq now %u",
           fed, (int)after.bmi270.sequence);

    /* The deliberate, labelled control for the destructive half. */
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(96), 60);
    lv_obj_set_pos(row, 8, 266);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_allow_sw = lv_switch_create(row);
    lv_obj_set_pos(s_allow_sw, 0, 8);

    lv_obj_t *sw_lbl = lv_label_create(row);
    lv_label_set_text(sw_lbl, "also fabricate ABSENT sensors\n(they stay 'present' until reboot)");
    lv_obj_set_style_text_color(sw_lbl, lv_color_hex(0xE8A33D), 0);
    lv_obj_set_pos(sw_lbl, 70, 2);

    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, 130, 40);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(btn, publish_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Publish now");
    lv_obj_center(btn_lbl);

    sdk_example_logf("re-published %d present sensor(s), sequence advanced", fed);
    sdk_example_logf("  the hub does NOT stamp sequence - the feeder owns it");
    sdk_example_logf("  feeding also sets has_<sensor> true, permanently");

    if (fed == 0) {
        sdk_example_logf("nothing is publishing yet - nothing to re-publish");
        return SDK_EX_NO_DATA;
    }
    return SDK_EX_OK;
}
