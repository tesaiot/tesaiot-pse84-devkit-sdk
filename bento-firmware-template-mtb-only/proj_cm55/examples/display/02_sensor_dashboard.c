/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/02_sensor_dashboard
 * title:   Put the CM33 sensor snapshot on the display
 * teaches: read every sensor CM33_NS publishes, tell live data from stale, and keep a panel refreshing without blocking the GFX task
 * apis:    ipc_sensorhub_snapshot, ipc_sensorhub_wifi_connected, ipc_sensorhub_ble_connected, ipc_sensorhub_ntp_synced, ipc_sensorhub_get_time_str, ipc_ui_set_container, ui_widget_mgr_create, ui_widget_mgr_set_text, ui_widget_mgr_get_object
 * entry:   example_ipc_core_sensor_dashboard
 */
/*******************************************************************************
 * ipc_core/02_sensor_dashboard — the job everybody writes first.
 *
 * CM33_NS owns the sensors on this board and pushes each new sample over the
 * IPC pipe. ipc_sensorhub caches the latest of each and hands it over in one
 * struct. CM55 never touches the sensor bus to draw this screen — that is the
 * whole point of the hub, and on Eva Kit it is not optional (SCB0 has exactly
 * one owner).
 *
 * THE `changed` FLAGS ARE NOT A GARNISH, AND THEY ARE NOT PER-CALLER
 * ------------------------------------------------------------------
 * `snap.bmi270_changed` is the only way to tell a live sensor from one whose
 * last value is three minutes old. It is computed by comparing the sample's
 * sequence number against a last-seen value the hub keeps ONCE PER SENSOR,
 * MODULE-WIDE (s_last_bmi270_seq and friends in ipc_sensorhub.c) — not once
 * per caller.
 *
 * So snapshot() consumes the change for EVERYBODY. Two screens polling at the
 * same time steal each other's flags: whichever calls first sees changed ==
 * true and the other sees false for a sample that is genuinely new. Call it
 * from ONE place and pass the struct around. This example polls at 250 ms,
 * which is worth remembering if you leave it running and then wonder why
 * another page has gone quiet.
 *
 * NOT BLOCKING
 * ------------
 * run() paints once and returns. An lv_timer does the refreshing. The timer
 * is owned by the panel widget: when the panel dies — page nav-away, or
 * another example calling ipc_ui_set_container() — LV_EVENT_DELETE tears the
 * timer down with it. A timer that outlives the widgets it writes to would
 * find recycled handles and paint into someone else's screen.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_communication.h"
#include "ipc_sensorhub.h"
#include "ipc_ui.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_mgr.h"

#define DASH_REFRESH_MS   (250)

/* One row per reading. Handles are ints because that is what
 * ui_widget_mgr_create() returns; -1 is "not created". */
typedef enum {
    ROW_CLOCK = 0,
    ROW_LINK,
    ROW_ACCEL,
    ROW_GYRO,
    ROW_MAG,
    ROW_BARO,
    ROW_HUMID,
    ROW_TOUCH,
    ROW_POT,
    ROW_LED,
    ROW_COUNT
} dash_row_t;

static int          s_panel = -1;
static int          s_row[ROW_COUNT];
static lv_timer_t  *s_timer;

static const char *const s_row_name[ROW_COUNT] = {
    "time  ", "link  ", "accel ", "gyro  ", "mag   ",
    "baro  ", "humid ", "touch ", "pot   ", "leds  ",
};

/*******************************************************************************
 * Formatting. lv_snprintf() rather than the C library's: it is the formatter
 * LVGL itself uses, it is already linked, and it never reaches newlib's
 * locale machinery from the GFX task.
 *******************************************************************************/
static void set_row(dash_row_t r, const char *value, bool fresh)
{
    char line[80];
    lv_snprintf(line, sizeof(line), "%s %s %s",
                s_row_name[r], fresh ? "*" : " ", value);
    ui_widget_mgr_set_text(s_row[r], line);
}

static void refresh(void)
{
    sensorhub_snapshot_t snap;
    char buf[64];

    /* ONE snapshot per refresh. Calling it twice in the same pass would clear
     * the change flags for the second half of the screen. */
    ipc_sensorhub_snapshot(&snap);

    /* --- clock ------------------------------------------------------------ */
    if (ipc_sensorhub_get_time_str(buf, sizeof(buf))) {
        set_row(ROW_CLOCK, buf, true);
    } else {
        /* ntp_synced() and get_time_str() answer the same question from two
         * directions: the first is a cheap poll for a status bar, the second
         * also gives you the text. Both are false until CM33 has NTP. */
        set_row(ROW_CLOCK, ipc_sensorhub_ntp_synced() ? "synced, no string yet"
                                                      : "no NTP yet", false);
    }

    /* --- link state ------------------------------------------------------- */
    lv_snprintf(buf, sizeof(buf), "wifi %s   ble %s",
                ipc_sensorhub_wifi_connected() ? "up" : "down",
                ipc_sensorhub_ble_connected()  ? "host connected" : "no host");
    set_row(ROW_LINK, buf, false);

    /* --- BMI270 ----------------------------------------------------------- */
    if (snap.has_bmi270) {
        /* Raw counts, not milli-g, is what crosses the pipe. The scale factor
         * depends on the range the CM33 encoder was built with, so use the
         * shared constant and never a hard-coded 16384. */
        int ax = (int)((float)snap.bmi270.ax * 1000.0f / IPC_BMI270_ACCEL_LSB_PER_G);
        int ay = (int)((float)snap.bmi270.ay * 1000.0f / IPC_BMI270_ACCEL_LSB_PER_G);
        int az = (int)((float)snap.bmi270.az * 1000.0f / IPC_BMI270_ACCEL_LSB_PER_G);
        lv_snprintf(buf, sizeof(buf), "%d %d %d mg  seq %u",
                    ax, ay, az, (unsigned)snap.bmi270.sequence);
        set_row(ROW_ACCEL, buf, snap.bmi270_changed);

        int gx = (int)((float)snap.bmi270.gx / IPC_BMI270_GYRO_LSB_PER_DPS);
        int gy = (int)((float)snap.bmi270.gy / IPC_BMI270_GYRO_LSB_PER_DPS);
        int gz = (int)((float)snap.bmi270.gz / IPC_BMI270_GYRO_LSB_PER_DPS);
        lv_snprintf(buf, sizeof(buf), "%d %d %d dps", gx, gy, gz);
        set_row(ROW_GYRO, buf, snap.bmi270_changed);
    } else {
        set_row(ROW_ACCEL, "no BMI270 sample yet", false);
        set_row(ROW_GYRO,  "-", false);
    }

    /* --- BMM350 ----------------------------------------------------------- */
    if (snap.has_bmm350) {
        lv_snprintf(buf, sizeof(buf), "hdg %d.%d deg   z %d.%02d uT",
                    (int)(snap.bmm350.heading_x10 / 10),
                    (int)(snap.bmm350.heading_x10 % 10),
                    (int)(snap.bmm350.mz_x100 / 100),
                    (int)(snap.bmm350.mz_x100 < 0 ? -snap.bmm350.mz_x100 % 100
                                                  :  snap.bmm350.mz_x100 % 100));
        set_row(ROW_MAG, buf, snap.bmm350_changed);
    } else {
        set_row(ROW_MAG, "absent on this board", false);
    }

    /* --- DPS368 (AI Kit only) --------------------------------------------- */
    if (snap.has_dps368) {
        lv_snprintf(buf, sizeof(buf), "%d.%02d hPa   %d.%02d C",
                    (int)(snap.dps368.pressure_x100 / 100),
                    (int)(snap.dps368.pressure_x100 % 100),
                    (int)(snap.dps368.temperature_x100 / 100),
                    (int)(snap.dps368.temperature_x100 % 100));
        set_row(ROW_BARO, buf, snap.dps368_changed);
    } else {
        set_row(ROW_BARO, "absent on this board (BSP_HAS_DPS368=0)", false);
    }

    /* --- SHT40 (AI Kit only) ---------------------------------------------- */
    if (snap.has_sht40) {
        lv_snprintf(buf, sizeof(buf), "%u.%02u %%RH   %d.%02d C",
                    (unsigned)(snap.sht40.humidity_x100 / 100),
                    (unsigned)(snap.sht40.humidity_x100 % 100),
                    (int)(snap.sht40.temperature_x100 / 100),
                    (int)(snap.sht40.temperature_x100 % 100));
        set_row(ROW_HUMID, buf, snap.sht40_changed);
    } else {
        set_row(ROW_HUMID, "absent on this board (BSP_HAS_SHT40=0)", false);
    }

    /* --- CapSense (Eva Kit / QWA309 base) --------------------------------- */
    if (snap.has_capsense) {
        lv_snprintf(buf, sizeof(buf), "btn0 %u  btn1 %u  slider %u%%",
                    (unsigned)snap.capsense.btn0_pressed,
                    (unsigned)snap.capsense.btn1_pressed,
                    (unsigned)snap.capsense.slider);
        set_row(ROW_TOUCH, buf, snap.capsense_changed);
    } else {
        set_row(ROW_TOUCH, "absent on this board (BSP_HAS_CAPSENSE=0)", false);
    }

    /* --- Potentiometer ----------------------------------------------------- */
    if (snap.has_pot) {
        lv_snprintf(buf, sizeof(buf), "%u.%u %%   raw %u",
                    (unsigned)(snap.pot.percent_x10 / 10),
                    (unsigned)(snap.pot.percent_x10 % 10),
                    (unsigned)snap.pot.raw);
        set_row(ROW_POT, buf, snap.pot_changed);
    } else {
        set_row(ROW_POT, "absent on this board (BSP_HAS_POTENTIOMETER=0)", false);
    }

    /* --- GPIO LED bitmask -------------------------------------------------- */
    if (snap.has_led_state) {
        lv_snprintf(buf, sizeof(buf), "0x%02X", (unsigned)snap.led_state_bitmask);
        set_row(ROW_LED, buf, snap.led_state_changed);
    } else {
        set_row(ROW_LED, "no LED state pushed yet", false);
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    refresh();
}

/* The panel owns the timer. Nothing else can be trusted to: the Examples page
 * destroys its container on nav-away and another example may call
 * ipc_ui_set_container(), and both routes end in LVGL deleting this object. */
static void panel_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_panel = -1;
}

/* The widget manager holds a raw pointer to `parent`. LVGL sends
 * LV_EVENT_DELETE to a container BEFORE it deletes the children
 * (lv_obj_tree.c, obj_delete_core), so unbinding from here is safe and is
 * exactly what page_playground does in its destroy hook. */
static void container_deleted_cb(lv_event_t *e)
{
    (void)e;
    ipc_ui_set_container(NULL);
}

int example_ipc_core_sensor_dashboard(lv_obj_t *parent)
{
    /* Bind the widget layer to this page. This also deletes every widget the
     * previous tenant left behind, which is what gives us a clean grid. */
    ipc_ui_set_container(parent);
    lv_obj_remove_event_cb(parent, container_deleted_cb);
    lv_obj_add_event_cb(parent, container_deleted_cb, LV_EVENT_DELETE, NULL);

    if (s_timer != NULL) {          /* re-tapped before the old panel died */
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    ipc_ui_create_t cfg;

    /* Backdrop. PANEL reads color as background, min_val as border colour,
     * max_val as corner radius and init_val as border width. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_PANEL;
    cfg.x = 6;  cfg.y = 6;
    cfg.w = 470; cfg.h = 300;
    cfg.color    = 0x101020;
    cfg.min_val  = 0x2A3550;
    cfg.max_val  = 10;
    cfg.init_val = 1;
    s_panel = ui_widget_mgr_create(&cfg);
    if (s_panel < 0) {
        sdk_example_logf("panel create failed (%d) - no container bound?", s_panel);
        return SDK_EX_REFUSED;
    }

    /* Rows, parented to the panel. parent_plus1 is the handle PLUS ONE so that
     * a zeroed struct still means "the page container". */
    for (int i = 0; i < ROW_COUNT; i++) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.widget_type  = UI_WIDGET_LABEL;
        cfg.x            = 10;
        cfg.y            = (int16_t)(8 + i * 28);
        cfg.color        = 0xD8E0F0;
        cfg.init_val     = 16;                       /* font px */
        cfg.parent_plus1 = (uint8_t)(s_panel + 1);
        lv_snprintf(cfg.text, sizeof(cfg.text), "%s ...", s_row_name[i]);
        s_row[i] = ui_widget_mgr_create(&cfg);
        if (s_row[i] < 0) {
            sdk_example_logf("row %d create failed (%d)", i, s_row[i]);
            return SDK_EX_REFUSED;
        }
    }

    /* Tie the refresh timer's life to the panel object. get_object() is how
     * you reach the LVGL object behind a handle when you need an LVGL-level
     * facility the handle API does not expose — here, a delete hook. */
    lv_obj_t *panel_obj = ui_widget_mgr_get_object(s_panel);
    if (panel_obj == NULL) {
        return SDK_EX_REFUSED;
    }
    lv_obj_add_event_cb(panel_obj, panel_deleted_cb, LV_EVENT_DELETE, NULL);

    refresh();                                   /* first paint, now */
    s_timer = lv_timer_create(tick_cb, DASH_REFRESH_MS, NULL);
    if (s_timer == NULL) {
        sdk_example_logf("lv_timer_create failed - panel is a still frame");
        return SDK_EX_REFUSED;
    }

    /* Report what the board actually has, so the developer can tell an empty
     * dashboard from a broken one. */
    sensorhub_snapshot_t probe;
    ipc_sensorhub_snapshot(&probe);
    const int present = (probe.has_bmi270   ? 1 : 0) + (probe.has_bmm350 ? 1 : 0)
                      + (probe.has_dps368   ? 1 : 0) + (probe.has_sht40  ? 1 : 0)
                      + (probe.has_capsense ? 1 : 0) + (probe.has_pot    ? 1 : 0);
    sdk_example_logf("dashboard live, refresh %d ms, %d sensor(s) publishing",
                     DASH_REFRESH_MS, present);
    sdk_example_logf("a '*' beside a row means it changed since the last read");

    if (present == 0) {
        sdk_example_logf("nothing has been pushed yet - is main.py importing sensors?");
        return SDK_EX_NO_DATA;   /* the panel is up, but it has nothing to say */
    }
    return SDK_EX_STARTED;       /* live view running on an lv_timer */
}
