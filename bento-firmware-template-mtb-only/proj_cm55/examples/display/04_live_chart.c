/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/04_live_chart
 * title:   Stream three signals into a live chart
 * teaches: add series to a chart, push samples one at a time, widen the time window, and stop feeding when the chart dies
 * apis:    ui_widget_mgr_create, ui_widget_mgr_chart_add_series, ui_widget_mgr_chart_set_next, ui_widget_mgr_set_prop, ui_widget_mgr_get_object, ui_widget_mgr_set_text, ipc_sensorhub_snapshot
 * entry:   example_ipc_core_live_chart
 */
/*******************************************************************************
 * ipc_core/04_live_chart — a scrolling scope, three traces.
 *
 * A chart made by ui_widget_mgr_create() ALREADY HAS ONE SERIES. That is the
 * single most common surprise here: create() calls lv_chart_add_series() with
 * cfg.color (or UI_DEF_CHART_LINE_COLOR when cfg.color is 0) and records it as
 * series 0. So the first ui_widget_mgr_chart_add_series() you call returns 1,
 * not 0, and a chart tops out at UI_CHART_MAX_SERIES (4) INCLUDING that one.
 *
 * The x axis is a ring of fixed length. chart_set_next() shifts everything
 * left by one and appends; there is no timestamp anywhere. The visible time
 * span is therefore (point count) x (your feed period), and the point count is
 * UI_DEF_CHART_POINTS (50) until UI_PROP_CHART_POINTS says otherwise. CM55
 * clamps that prop to 10..400.
 *
 * WHERE THE DATA COMES FROM
 * -------------------------
 * The accelerometer, when CM33_NS is publishing one. Otherwise three phase
 * shifted triangle waves, so the example still draws on a bench board with no
 * sensor task running and the log says which of the two you are looking at.
 * No sin() and no float loop: this runs on the GFX task 20 times a second and
 * an integer ramp is exactly as convincing as a sinusoid at that job.
 *
 * NOT BLOCKING
 * ------------
 * run() creates the chart and one lv_timer, then returns. The timer is torn
 * down by the chart's own LV_EVENT_DELETE, so navigating away or running
 * another example stops the feed. A feed that outlives its chart writes into
 * a recycled handle, and a recycled handle belongs to somebody else.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_communication.h"
#include "ipc_sensorhub.h"
#include "ipc_ui.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_defaults.h"
#include "ui_widget_mgr.h"

#define CHART_POINTS      (120)   /* clamped to 10..400 by CM55              */
#define CHART_PERIOD_MS   (50)    /* 120 points x 50 ms = 6 s of history     */
#define CHART_RANGE_MG    (1500)  /* +/- full scale in milli-g               */

#define SERIES_X          (0)     /* created by ui_widget_mgr_create()       */
static int s_series_y = -1;
static int s_series_z = -1;

static int         s_chart  = -1;
static int         s_status = -1;
static lv_timer_t *s_timer;
static uint32_t    s_phase;
static bool        s_synthetic;

static void container_deleted_cb(lv_event_t *e)
{
    (void)e;
    ipc_ui_set_container(NULL);
}

static void chart_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_chart = -1;
}

/* A triangle wave in milli-g, period 64 steps, phase-shifted per trace. */
static int32_t triangle(uint32_t step, uint32_t shift)
{
    const uint32_t t = (step + shift) & 0x3Fu;         /* 0..63             */
    const int32_t  r = (t < 32u) ? (int32_t)t : (int32_t)(64u - t); /* 0..32 */
    return ((r - 16) * CHART_RANGE_MG) / 16;           /* -1500..+1500      */
}

static void feed_cb(lv_timer_t *t)
{
    (void)t;
    int32_t x, y, z;

    sensorhub_snapshot_t snap;
    ipc_sensorhub_snapshot(&snap);

    if (snap.has_bmi270) {
        /* Raw counts to milli-g with the range constant the CM33 encoder was
         * built against. Never hard-code 16384: BENTO_BMI270_ACCEL_8G halves
         * it, and the flag is set per project. */
        x = (int32_t)((float)snap.bmi270.ax * 1000.0f / IPC_BMI270_ACCEL_LSB_PER_G);
        y = (int32_t)((float)snap.bmi270.ay * 1000.0f / IPC_BMI270_ACCEL_LSB_PER_G);
        z = (int32_t)((float)snap.bmi270.az * 1000.0f / IPC_BMI270_ACCEL_LSB_PER_G);
        if (s_synthetic) {
            s_synthetic = false;
            ui_widget_mgr_set_text(s_status, "source: BMI270 (mg)");
        }
    } else {
        x = triangle(s_phase, 0);
        y = triangle(s_phase, 21);
        z = triangle(s_phase, 42);
        if (!s_synthetic) {
            s_synthetic = true;
            ui_widget_mgr_set_text(s_status, "source: synthetic (no BMI270 samples)");
        }
    }
    s_phase++;

    /* One call per series per sample. Each shifts that series' ring left by
     * one point; the chart redraws itself. */
    ui_widget_mgr_chart_set_next(s_chart, (uint8_t)SERIES_X, x);
    if (s_series_y >= 0) {
        ui_widget_mgr_chart_set_next(s_chart, (uint8_t)s_series_y, y);
    }
    if (s_series_z >= 0) {
        ui_widget_mgr_chart_set_next(s_chart, (uint8_t)s_series_z, z);
    }
}

static int make_label(int16_t x, int16_t y, uint32_t color, const char *text)
{
    ipc_ui_create_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_LABEL;
    cfg.x = x; cfg.y = y;
    cfg.color = color;
    cfg.init_val = 16;
    lv_snprintf(cfg.text, sizeof(cfg.text), "%s", text);
    return ui_widget_mgr_create(&cfg);
}

int example_ipc_core_live_chart(lv_obj_t *parent)
{
    ipc_ui_set_container(parent);
    lv_obj_remove_event_cb(parent, container_deleted_cb);
    lv_obj_add_event_cb(parent, container_deleted_cb, LV_EVENT_DELETE, NULL);

    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_phase = 0;
    s_synthetic = false;
    s_series_y = -1;
    s_series_z = -1;

    /* min_val / max_val are the Y range. A zero max_val would be read as
     * UI_DEF_RANGE_MAX (100), so a symmetric range must set both. color is
     * the colour of the series create() makes for you. */
    ipc_ui_create_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_CHART;
    cfg.x = 10; cfg.y = 40;
    cfg.w = 440; cfg.h = 220;
    cfg.min_val = -CHART_RANGE_MG;
    cfg.max_val =  CHART_RANGE_MG;
    cfg.color   = 0xFF5C5C;                 /* series 0 — X */
    s_chart = ui_widget_mgr_create(&cfg);
    if (s_chart < 0) {
        sdk_example_logf("chart create failed (%d)", s_chart);
        return SDK_EX_REFUSED;
    }

    /* Widen the time window. Without this the chart holds UI_DEF_CHART_POINTS
     * points, which at this feed rate is 2.5 s of history. */
    ui_widget_mgr_set_prop(s_chart, UI_PROP_CHART_POINTS, CHART_POINTS);

    /* Series 1 and 2. The return value IS the index you pass to
     * chart_set_next() — do not assume it, and do not count from zero. */
    s_series_y = ui_widget_mgr_chart_add_series(s_chart, 0x5CFF8A);
    s_series_z = ui_widget_mgr_chart_add_series(s_chart, 0x5CB4FF);
    if (s_series_y < 0 || s_series_z < 0) {
        sdk_example_logf("add_series failed (%d, %d) - max is %d incl. series 0",
                         s_series_y, s_series_z, UI_CHART_MAX_SERIES);
        return SDK_EX_REFUSED;
    }

    (void)make_label(10, 10, 0xFF5C5C, "X");
    (void)make_label(40, 10, 0x5CFF8A, "Y");
    (void)make_label(70, 10, 0x5CB4FF, "Z");
    s_status = make_label(110, 10, 0xC8C8C8, "source: probing...");
    if (s_status < 0) {
        return SDK_EX_REFUSED;
    }

    lv_obj_t *chart_obj = ui_widget_mgr_get_object(s_chart);
    if (chart_obj == NULL) {
        return SDK_EX_REFUSED;
    }
    lv_obj_add_event_cb(chart_obj, chart_deleted_cb, LV_EVENT_DELETE, NULL);

    feed_cb(NULL);                          /* one sample immediately */
    s_timer = lv_timer_create(feed_cb, CHART_PERIOD_MS, NULL);
    if (s_timer == NULL) {
        sdk_example_logf("lv_timer_create failed - chart will not scroll");
        return SDK_EX_REFUSED;
    }

    sdk_example_logf("chart %d: series 0/%d/%d, %d points at %d ms = %d s window",
                     s_chart, s_series_y, s_series_z,
                     CHART_POINTS, CHART_PERIOD_MS,
                     (CHART_POINTS * CHART_PERIOD_MS) / 1000);
    sdk_example_logf("  default point count is %d; UI_PROP_CHART_POINTS clamps 10..400",
                     UI_DEF_CHART_POINTS);
    sdk_example_logf("  source: %s", s_synthetic ? "synthetic triangles" : "BMI270");
    return SDK_EX_STARTED;
}
