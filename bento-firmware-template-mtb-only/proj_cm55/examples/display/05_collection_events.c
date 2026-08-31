/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/05_collection_events
 * title:   Fill a table and a list, then read the taps back
 * teaches: append rows to collection widgets one item at a time, subscribe to input events, and drain the event ring
 * apis:    ui_widget_mgr_create, ui_widget_mgr_item_add, ui_widget_mgr_item_clear, ui_widget_mgr_set_prop, ui_widget_mgr_event_push, ui_widget_mgr_event_drain, ui_widget_mgr_get_object, ui_widget_mgr_get_value, ui_widget_mgr_set_text
 * entry:   example_ipc_core_collection_events
 */
/*******************************************************************************
 * ipc_core/05_collection_events — content in, taps out.
 *
 * WHY COLLECTIONS ARE FILLED ONE ITEM AT A TIME
 * ---------------------------------------------
 * ipc_ui_create_t has one 96-byte text field. A table with twelve cells does
 * not fit it, and neither does a list with six entries, so collection content
 * arrives through a second call — ui_widget_mgr_item_add() — carrying an
 * ipc_ui_item_add_t. That struct is exactly the 128-byte IPC payload, and its
 * a / b / flags fields mean whatever the target widget needs:
 *
 *   TABLE      a = row,     b = column,  text = the cell
 *   LIST       a = icon id (UI_LIST_ICON_*),      text = the entry label
 *   DROPDOWN   text appends one option
 *
 * The return value is not a handle for these: it is UI_ITEM_ADD_OK_NO_CHILD
 * (-3), which means "appended, and no new object was made". Only the three
 * CONTAINER types (Tabview / Tileview / Win) return a real child handle, and
 * -2 always means the add was refused. Testing `>= 0` for success is the bug
 * this paragraph exists to prevent.
 *
 * EVENTS: THREE ARE FREE, TWELVE ARE OPT-IN
 * -----------------------------------------
 * CLICKED, VALUE_CHANGED and TOGGLED are wired at create time per widget type
 * and always fire. Everything else — PRESSED, RELEASED, LONG_PRESSED, GESTURE
 * and the rest — is silent until UI_PROP_EVENT_MASK subscribes that widget,
 * because a held button raises LONG_PRESSED_REPEAT ten times a second and the
 * ring is sixteen entries deep.
 *
 * WHO OWNS THE RING
 * -----------------
 * ONE reader. ui_widget_mgr_event_drain() empties the ring, so a screen that
 * drains it is taking the events MicroPython's POLL_EVENTS would otherwise
 * have received. That is correct for a C-owned screen like this one and wrong
 * while a Python program is driving the same widgets. Do not run both.
 *
 * NOT BLOCKING
 * ------------
 * run() builds the screen and returns; a 100 ms lv_timer does the draining,
 * and the list widget's own delete event stops it.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_ui.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_mgr.h"

#define DRAIN_PERIOD_MS   (100)
#define DRAIN_BATCH       (UI_MAX_EVENTS_PER_POLL)   /* 8 — same as one poll */

static int         s_table    = -1;
static int         s_list     = -1;
static int         s_dropdown = -1;
static int         s_clearbtn = -1;
static int         s_status   = -1;
static int         s_readout  = -1;
static lv_timer_t *s_timer;
static uint32_t    s_events_seen;

static void container_deleted_cb(lv_event_t *e)
{
    (void)e;
    ipc_ui_set_container(NULL);
}

static void list_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_list = -1;
}

static const char *event_name(uint8_t type)
{
    switch (type) {
    case UI_EVENT_CLICKED:              return "CLICKED";
    case UI_EVENT_VALUE_CHANGED:        return "VALUE_CHANGED";
    case UI_EVENT_TOGGLED:              return "TOGGLED";
    case UI_EVENT_PRESSED:              return "PRESSED";
    case UI_EVENT_RELEASED:             return "RELEASED";
    case UI_EVENT_PRESS_LOST:           return "PRESS_LOST";
    case UI_EVENT_LONG_PRESSED:         return "LONG_PRESSED";
    case UI_EVENT_LONG_PRESSED_REPEAT:  return "LONG_REPEAT";
    case UI_EVENT_READY:                return "READY";
    case UI_EVENT_CANCEL:               return "CANCEL";
    case UI_EVENT_FOCUSED:              return "FOCUSED";
    case UI_EVENT_DEFOCUSED:            return "DEFOCUSED";
    case UI_EVENT_SCROLL_BEGIN:         return "SCROLL_BEGIN";
    case UI_EVENT_SCROLL_END:           return "SCROLL_END";
    case UI_EVENT_GESTURE:              return "GESTURE";
    default:                            return "?";
    }
}

/* Append one row of two cells to the table. */
static bool table_row(int handle, uint8_t row, const char *left, const char *right)
{
    ipc_ui_item_add_t it;

    memset(&it, 0, sizeof(it));
    it.handle = (uint8_t)handle;
    it.a = row;                 /* row    */
    it.b = 0;                   /* column */
    lv_snprintf(it.text, sizeof(it.text), "%s", left);
    if (ui_widget_mgr_item_add(&it) == -2) {
        return false;
    }

    memset(&it, 0, sizeof(it));
    it.handle = (uint8_t)handle;
    it.a = row;
    it.b = 1;
    lv_snprintf(it.text, sizeof(it.text), "%s", right);
    return (ui_widget_mgr_item_add(&it) != -2);
}

static bool list_entry(int handle, uint8_t icon, const char *label)
{
    ipc_ui_item_add_t it;
    memset(&it, 0, sizeof(it));
    it.handle = (uint8_t)handle;
    it.a = icon;                /* UI_LIST_ICON_* — an id, never LV_SYMBOL_* */
    lv_snprintf(it.text, sizeof(it.text), "%s", label);
    return (ui_widget_mgr_item_add(&it) != -2);
}

static bool dropdown_option(int handle, const char *label)
{
    ipc_ui_item_add_t it;
    memset(&it, 0, sizeof(it));
    it.handle = (uint8_t)handle;
    lv_snprintf(it.text, sizeof(it.text), "%s", label);
    return (ui_widget_mgr_item_add(&it) != -2);
}

static void drain_cb(lv_timer_t *t)
{
    (void)t;
    ipc_ui_event_t ev[DRAIN_BATCH];

    const int n = ui_widget_mgr_event_drain(ev, DRAIN_BATCH);
    if (n <= 0) {
        return;
    }
    s_events_seen += (uint32_t)n;

    for (int i = 0; i < n; i++) {
        char line[80];
        lv_snprintf(line, sizeof(line), "h%u  %s  value %d   (%u total)",
                    (unsigned)ev[i].handle_id,
                    event_name(ev[i].event_type),
                    (int)ev[i].value,
                    (unsigned)s_events_seen);
        ui_widget_mgr_set_text(s_status, line);

        /* The Clear button empties the table without touching the widget. */
        if (ev[i].handle_id == (uint8_t)s_clearbtn &&
            ev[i].event_type == UI_EVENT_CLICKED) {
            ui_widget_mgr_item_clear(s_table);
            ui_widget_mgr_set_text(s_readout, "table cleared - rows gone, handle alive");
        }
    }

    /* get_value() answers whatever "value" means for the type: a Dropdown's
     * selected index, a Slider's position, a Switch's checked state. It is a
     * poll, not an event — the event only told us something moved. */
    char line[80];
    lv_snprintf(line, sizeof(line), "dropdown selection index = %d",
                (int)ui_widget_mgr_get_value(s_dropdown));
    ui_widget_mgr_set_text(s_readout, line);
}

int example_ipc_core_collection_events(lv_obj_t *parent)
{
    ipc_ui_set_container(parent);
    lv_obj_remove_event_cb(parent, container_deleted_cb);
    lv_obj_add_event_cb(parent, container_deleted_cb, LV_EVENT_DELETE, NULL);

    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_events_seen = 0;

    ipc_ui_create_t cfg;

    /* --- Table. Created empty; every cell arrives through item_add. -------- */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_TABLE;
    cfg.x = 10; cfg.y = 40; cfg.w = 300; cfg.h = 170;
    s_table = ui_widget_mgr_create(&cfg);

    /* --- List. Also empty at create. ------------------------------------- */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_LIST;
    cfg.x = 320; cfg.y = 40; cfg.w = 220; cfg.h = 170;
    s_list = ui_widget_mgr_create(&cfg);

    /* --- Dropdown. text at create is the CLOSED-state label; the options
     *     come from item_add. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_DROPDOWN;
    cfg.x = 10; cfg.y = 222; cfg.w = 200;
    s_dropdown = ui_widget_mgr_create(&cfg);

    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_BUTTON;
    cfg.x = 230; cfg.y = 218; cfg.w = 120; cfg.h = 38;
    cfg.color = 0xB3382F;
    cfg.init_val = 16;
    lv_snprintf(cfg.text, sizeof(cfg.text), "Clear table");
    s_clearbtn = ui_widget_mgr_create(&cfg);

    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_LABEL;
    cfg.x = 10; cfg.y = 10; cfg.color = 0xD8E0F0; cfg.init_val = 16;
    lv_snprintf(cfg.text, sizeof(cfg.text), "waiting for a tap...");
    s_status = ui_widget_mgr_create(&cfg);

    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_LABEL;
    cfg.x = 10; cfg.y = 270; cfg.color = 0x9AD1FF; cfg.init_val = 16;
    lv_snprintf(cfg.text, sizeof(cfg.text), "dropdown selection index = 0");
    s_readout = ui_widget_mgr_create(&cfg);

    if (s_table < 0 || s_list < 0 || s_dropdown < 0 || s_clearbtn < 0 ||
        s_status < 0 || s_readout < 0) {
        sdk_example_logf("create failed - table %d list %d dropdown %d",
                         s_table, s_list, s_dropdown);
        return SDK_EX_REFUSED;
    }

    /* --- Content -------------------------------------------------------- */
    bool ok = true;
    ok = ok && table_row(s_table, 0, "Sensor",  "State");
    ok = ok && table_row(s_table, 1, "BMI270",  "publishing");
    ok = ok && table_row(s_table, 2, "BMM350",  "publishing");
    ok = ok && table_row(s_table, 3, "DPS368",  "board dependent");
    ok = ok && table_row(s_table, 4, "CapSense","board dependent");

    ok = ok && list_entry(s_list, UI_LIST_ICON_WIFI,     "Network");
    ok = ok && list_entry(s_list, UI_LIST_ICON_SETTINGS, "Preferences");
    ok = ok && list_entry(s_list, UI_LIST_ICON_BELL,     "Alerts");
    ok = ok && list_entry(s_list, UI_LIST_ICON_BATTERY,  "Power");
    ok = ok && list_entry(s_list, UI_LIST_ICON_TRASH,    "Reset");

    ok = ok && dropdown_option(s_dropdown, "1 Hz");
    ok = ok && dropdown_option(s_dropdown, "10 Hz");
    ok = ok && dropdown_option(s_dropdown, "50 Hz");
    if (!ok) {
        sdk_example_logf("item_add refused (-2): wrong handle or wrong widget type");
        return SDK_EX_REFUSED;
    }

    /* --- Props ----------------------------------------------------------- */

    /* Two halves of ONE setting travel in one int32: column in the high 16
     * bits, width in the low 16. Build it with the macros the protocol header
     * ships rather than shifting by hand. */
    ui_widget_mgr_set_prop(s_table, UI_PROP_TABLE_COL_WIDTH,
                           (int32_t)((0u << 16) | 150u));
    ui_widget_mgr_set_prop(s_table, UI_PROP_TABLE_COL_WIDTH,
                           (int32_t)((1u << 16) | 140u));

    /* Subscribe the list to the input events it is worth waking up for.
     * PRESSED/RELEASED tell you a finger arrived and left; LONG_PRESSED is the
     * "hold to reveal" gesture. LONG_PRESSED_REPEAT is deliberately left out:
     * one hold would fill the sixteen-entry ring in under two seconds. */
    ui_widget_mgr_set_prop(s_list, UI_PROP_EVENT_MASK,
                           (int32_t)(UI_EVENT_MASK(UI_EVENT_PRESSED)  |
                                     UI_EVENT_MASK(UI_EVENT_RELEASED) |
                                     UI_EVENT_MASK(UI_EVENT_LONG_PRESSED)));

    /* --- Two ways to make an event happen without a finger ---------------- */

    /* (a) Straight into the ring. This is what an LVGL callback does, and it
     *     is how you inject a synthetic event for a test harness. The handle
     *     and type are yours to choose, so choose ones that exist. */
    ui_widget_mgr_event_push((uint8_t)s_clearbtn, UI_EVENT_PRESSED, 0);

    /* (b) Through LVGL, so the widget's own callback runs and every side
     *     effect a real tap would have happens too. get_object() is the only
     *     way across from a handle to the lv_obj_t this needs. */
    lv_obj_t *list_obj = ui_widget_mgr_get_object(s_list);
    if (list_obj != NULL) {
        lv_obj_add_event_cb(list_obj, list_deleted_cb, LV_EVENT_DELETE, NULL);
    }
    lv_obj_t *dd_obj = ui_widget_mgr_get_object(s_dropdown);
    if (dd_obj != NULL) {
        lv_obj_send_event(dd_obj, LV_EVENT_VALUE_CHANGED, NULL);
    }

    s_timer = lv_timer_create(drain_cb, DRAIN_PERIOD_MS, NULL);
    if (s_timer == NULL) {
        sdk_example_logf("lv_timer_create failed - events will pile up unread");
        return SDK_EX_REFUSED;
    }

    sdk_example_logf("table %d (5 rows), list %d (5 entries), dropdown %d (3 options)",
                     s_table, s_list, s_dropdown);
    sdk_example_logf("  item_add returns %d on success for a data widget",
                     UI_ITEM_ADD_OK_NO_CHILD);
    sdk_example_logf("  draining the ring every %d ms, %d events per pass",
                     DRAIN_PERIOD_MS, DRAIN_BATCH);
    sdk_example_logf("  this STEALS events from MicroPython POLL_EVENTS");
    sdk_example_logf("tap the list, the dropdown, or Clear table");
    return SDK_EX_STARTED;
}
