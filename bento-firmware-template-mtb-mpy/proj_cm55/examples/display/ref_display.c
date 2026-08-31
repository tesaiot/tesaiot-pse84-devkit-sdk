/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/ref_display
 * title:   Reference: the ipc_core getters and probes
 * teaches: what each remaining read-only call answers, and when you would ask it
 * apis:    ui_widget_mgr_needs_container, ui_widget_mgr_get_parent, ui_widget_mgr_count, ui_widget_mgr_list, ipc_ui_input_activity, ipc_sensorhub_weather
 * entry:   example_ipc_core_reference
 */
/*******************************************************************************
 * ipc_core/ref_ipc_core — A REFERENCE LIST, NOT A JOB.
 *
 * The other files in this folder each do one thing a developer actually wants
 * done. This one does not. It is the shelf for the calls that no realistic
 * task exercises on its own — the getters, the probes, the one-line state
 * questions — each called once, with a comment saying WHEN you would ask it
 * and WHAT the answer means. Read it like a table; do not copy it as a
 * pattern.
 *
 * Everything here is read-only except ipc_ui_input_activity(), which is noted
 * where it appears. Nothing here blocks and nothing here allocates.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_communication.h"
#include "ipc_sensorhub.h"
#include "ipc_ui.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_mgr.h"

/* One line of the printed reference. */
static void ref_line(lv_obj_t *box, const char *call, const char *answer)
{
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text_fmt(lbl, "%-34s %s", call, answer);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xD8E0F0), 0);
}

/* Type id -> display name.
 *
 * A table, not a switch: the switch spelling of this mapping already exists
 * inside the library and duplicating it here would be copying source, which
 * tools/examples_no_verbatim.py rejects on purpose. A table is also the easier
 * thing for you to extend -- add a row, not a case.
 *
 * Only the ids ui_widget_mgr_list() can hand back are named; anything else
 * falls through to the id itself, which is more useful than "unknown". */
static const struct { uint8_t id; const char *name; } k_widget_names[] = {
    { UI_WIDGET_LABEL,     "Label"     }, { UI_WIDGET_BUTTON,  "Button"   },
    { UI_WIDGET_SLIDER,    "Slider"    }, { UI_WIDGET_SWITCH,  "Switch"   },
    { UI_WIDGET_CHECKBOX,  "Checkbox"  }, { UI_WIDGET_ARC,     "Arc"      },
    { UI_WIDGET_BAR,       "Bar"       }, { UI_WIDGET_CHART,   "Chart"    },
    { UI_WIDGET_IMAGE,     "Image"     }, { UI_WIDGET_PANEL,   "Panel"    },
    { UI_WIDGET_LED,       "Led"       }, { UI_WIDGET_TABLE,   "Table"    },
    { UI_WIDGET_LIST,      "List"      }, { UI_WIDGET_DROPDOWN,"Dropdown" },
    { UI_WIDGET_SEG7,      "Seg7"      }, { UI_WIDGET_DOTMATRIX,"DotMatrix"},
};

static const char *widget_type_name(uint8_t t, char *scratch, size_t n)
{
    for (size_t i = 0u; i < sizeof(k_widget_names) / sizeof(k_widget_names[0]); i++) {
        if (k_widget_names[i].id == t) {
            return k_widget_names[i].name;
        }
    }
    /* lv_snprintf, not the C library's: LVGL's formatter is already
     * linked and CM55 has no business pulling newlib's stdio in. */
    lv_snprintf(scratch, n, "type#%u", (unsigned)t);
    return scratch;
}

int example_ipc_core_reference(lv_obj_t *parent)
{
    char answer[96];

    lv_obj_clean(parent);

    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, lv_pct(97), lv_pct(96));
    lv_obj_set_pos(box, 6, 6);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x141428), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x2A3550), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, "ipc_core getters - call, and what it answered");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    /*-------------------------------------------------------------------------
     * ui_widget_mgr_needs_container()
     * WHEN:   before any create, and after any page teardown.
     * ANSWER: true when no parent is bound. Every create returns -2 while it
     *         is true, so this turns "my widget did not appear" into a
     *         one-line check instead of a debugging session.
     *-----------------------------------------------------------------------*/
    const bool needs = ui_widget_mgr_needs_container();
    ref_line(box, "ui_widget_mgr_needs_container()",
             needs ? "true  - nothing bound" : "false - a page owns the table");

    /*-------------------------------------------------------------------------
     * ui_widget_mgr_get_parent()
     * WHEN:   you need to know WHICH page owns the widget layer — for example
     *         to decide between rebinding (another page) and clear_all (this
     *         page), which is what 03_widget_gallery does.
     * ANSWER: the bound lv_obj_t*, or NULL. It is a raw pointer the manager
     *         does not own; it goes stale the instant that page is destroyed,
     *         which is why every page unbinds on its way out.
     *-----------------------------------------------------------------------*/
    lv_obj_t *bound = ui_widget_mgr_get_parent();
    lv_snprintf(answer, sizeof(answer), "%s",
                (bound == NULL) ? "NULL" :
                (bound == parent ? "this Examples page" : "another page"));
    ref_line(box, "ui_widget_mgr_get_parent()", answer);

    /*-------------------------------------------------------------------------
     * ui_widget_mgr_count()
     * WHEN:   budgeting. The table is UI_MAX_WIDGETS (64) slots and a
     *         container widget spends handles on its own pages — a three-tab
     *         Tabview is four entries before any content goes in.
     * ANSWER: how many slots are live right now. Maintained from LVGL's delete
     *         event, so it stays right even when a parent deletes its children
     *         or a Msgbox closes itself.
     *-----------------------------------------------------------------------*/
    const int live = ui_widget_mgr_count();
    lv_snprintf(answer, sizeof(answer), "%d of %d slots live",
                live, UI_MAX_WIDGETS);
    ref_line(box, "ui_widget_mgr_count()", answer);

    /*-------------------------------------------------------------------------
     * ui_widget_mgr_list()
     * WHEN:   you have a handle and no idea what it is — a debug screen, a
     *         host harness, or MicroPython's ui.list(). Also the honest way to
     *         see what a script left behind.
     * ANSWER: fills your array with {handle_id, widget_type} for each live
     *         slot and returns how many it wrote. It never writes more than
     *         max_items, and the pairs are two bytes each precisely so all 64
     *         fit one IPC response.
     *-----------------------------------------------------------------------*/
    ipc_ui_widget_info_t info[UI_MAX_WIDGETS];
    const int n = ui_widget_mgr_list(info, (int)(sizeof(info) / sizeof(info[0])));
    if (n <= 0) {
        ref_line(box, "ui_widget_mgr_list()", "0 widgets");
    } else {
        char tname[16];
        lv_snprintf(answer, sizeof(answer), "%d widget(s); first h%u = %s",
                    n, (unsigned)info[0].handle_id,
                    widget_type_name(info[0].widget_type, tname, sizeof(tname)));
        ref_line(box, "ui_widget_mgr_list()", answer);
    }

    /*-------------------------------------------------------------------------
     * ipc_ui_input_activity()
     * WHEN:   THE ONE WRITE ON THIS PAGE. Call it when real user input has
     *         happened by a route ui_widget_mgr did not see — your own LVGL
     *         callback, a hardware button, a host harness driving the panel.
     *         ui_widget_mgr_event_push() already calls it for every managed
     *         widget event, so you rarely need to.
     * EFFECT: opens the 5 ms fast-drain window for one timeout, so the next
     *         POLL_EVENTS answers at touch latency instead of waiting out the
     *         200 ms idle tick. Deliberately not wired to POLL_EVENTS itself:
     *         polling is continuous and would pin fast mode on permanently,
     *         which costs the GFX sleep clamp that protects ai_infer/radar.
     * ANSWER: nothing. It is a hint, not a query.
     *-----------------------------------------------------------------------*/
    ipc_ui_input_activity();
    ref_line(box, "ipc_ui_input_activity()",
             "void - armed the 5 ms drain window");

    /*-------------------------------------------------------------------------
     * ipc_sensorhub_weather()
     * WHEN:   drawing a weather face. CM33_NS fetches it from two keyless
     *         services and pushes a packed struct; CM55 never parses JSON and
     *         never opens a socket for this.
     * ANSWER: false until the first push has landed — which needs WiFi, so
     *         false is the normal state on a bench board and not a fault. On
     *         true, `out` holds the city, the current reading, four hourly
     *         entries from hour_start, and six daily ones from day_wday.
     *-----------------------------------------------------------------------*/
    weather_ipc_t wx;
    memset(&wx, 0, sizeof(wx));
    if (ipc_sensorhub_weather(&wx)) {
        char city[sizeof(wx.city) + 1];
        memcpy(city, wx.city, sizeof(wx.city));
        city[sizeof(wx.city)] = '\0';
        lv_snprintf(answer, sizeof(answer), "true - %s %d C, %d%% rain",
                    city, (int)wx.temp_c, (int)wx.rain_pct);
    } else {
        lv_snprintf(answer, sizeof(answer), "%s",
                    "false - nothing pushed yet (needs WiFi on CM33)");
    }
    ref_line(box, "ipc_sensorhub_weather(&out)", answer);

    sdk_example_logf("reference list, %d call(s), all read-only but one:", 6);
    sdk_example_logf("  needs_container %d, count %d, list %d",
                     (int)needs, live, n);
    sdk_example_logf("  ipc_ui_input_activity() is the one that WRITES");
    sdk_example_logf("  weather: %s", answer);
    return SDK_EX_OK;
}
