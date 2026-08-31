/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/03_widget_gallery
 * title:   Build a widget gallery on the display
 * teaches: create ten widget types through one struct, then move, resize, recolour, hide and delete them by handle
 * apis:    ui_widget_mgr_needs_container, ui_widget_mgr_get_parent, ui_widget_mgr_set_parent, ui_widget_mgr_clear_all, ui_widget_mgr_set_screen, ui_widget_mgr_create, ui_widget_mgr_set_text, ui_widget_mgr_set_value, ui_widget_mgr_set_position, ui_widget_mgr_set_size, ui_widget_mgr_set_color, ui_widget_mgr_set_visible, ui_widget_mgr_set_dotmatrix, ui_widget_mgr_set_image, ui_widget_mgr_delete, ui_widget_mgr_count, ui_widget_mgr_get_object
 * entry:   example_ipc_core_widget_gallery
 */
/*******************************************************************************
 * ipc_core/03_widget_gallery — every widget is one struct and one call.
 *
 * ui_widget_mgr_create() takes an ipc_ui_create_t and hands back a handle
 * 0..63. That struct is the SAME one MicroPython's `ui` module sends over the
 * IPC pipe, which is why the fields are terse and why several of them mean
 * different things per widget type. The mapping is not folklore: it is
 * tabulated beside ui_widget_type_t in ipc_ui_protocol.h, and the cases below
 * name the field they are using every time.
 *
 * THREE RULES THAT SAVE AN AFTERNOON
 * ----------------------------------
 * 1. memset the struct. Fields you leave alone must be zero, not stack junk.
 *    `parent_plus1 == 0` in particular is what means "the page container", so
 *    a dirty byte there reparents your widget somewhere invisible.
 * 2. x = -1 and y = -1 together mean auto-layout; anything else is literal.
 *    A widget created at auto position advances a grid cursor, so mixing the
 *    two is fine but the auto ones land wherever the cursor happens to be.
 * 3. -1 back means the handle table is full (64 slots), -2 means the type is
 *    unknown OR there is no container bound. Check it. A widget you did not
 *    get is a widget you cannot delete.
 *
 * LIFETIME
 * --------
 * The manager owns these objects. It does not own the container: it keeps a
 * raw pointer to the one you give it. The Examples page destroys its content
 * container when you navigate away, so this file unbinds on LV_EVENT_DELETE.
 * Skip that and the next create writes through a freed pointer.
 *******************************************************************************/

#include "../sdk_examples.h"

#include <string.h>

#include "ipc_ui.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_defaults.h"
#include "ui_widget_mgr.h"

/*******************************************************************************
 * A 16 x 8 face for the dot matrix. One bit per dot, row-major, MSB is the
 * leftmost dot of the byte — the layout ui_widget_mgr_set_dotmatrix() walks.
 * 16 columns is two bytes a row, eight rows, sixteen bytes total.
 *******************************************************************************/
static const uint8_t s_face[16] = {
    0x00, 0x00,
    0x30, 0x0C,   /* eyes                                                    */
    0x30, 0x0C,
    0x00, 0x00,
    0x40, 0x02,   /* cheeks                                                  */
    0x20, 0x04,
    0x1F, 0xF8,   /* mouth                                                   */
    0x00, 0x00,
};

/* The dynamic-canvas image is capped at UI_DEF_IMAGE_SIZE (48) on both axes by
 * ui_widget_mgr_create(); asking for more silently gets you 48. */
#define GAL_IMG_W   (48)
#define GAL_IMG_H   (48)

static int s_h_title  = -1;
static int s_h_seg7   = -1;
static int s_h_button = -1;
static int s_h_switch = -1;
static int s_h_check  = -1;
static int s_h_slider = -1;
static int s_h_bar    = -1;
static int s_h_arc    = -1;
static int s_h_led    = -1;
static int s_h_icon   = -1;
static int s_h_canvas = -1;
static int s_h_matrix = -1;

/* Unbinding one layer lower than ipc_ui_set_container(NULL). Same effect —
 * ipc_ui_set_container() is a one-line forwarder to this — and it is the call
 * to use when your project drives the widget table directly and never
 * initialises the IPC receiver at all (a native simulator, a C-only screen). */
static void container_deleted_cb(lv_event_t *e)
{
    (void)e;
    ui_widget_mgr_set_parent(NULL);
}

/*******************************************************************************
 * Fill the dynamic image with a vertical ramp.
 *
 * ui_widget_mgr_set_image() writes raw bytes at a byte offset into the
 * canvas's draw buffer, so you need that buffer's STRIDE — which is not
 * width * 2. lv_draw_buf_create() aligns rows, and assuming otherwise gives a
 * picture that shears one pixel further right on every row. The stride is on
 * the buffer, and the buffer is reachable from the handle:
 *
 *     handle -> ui_widget_mgr_get_object() -> lv_canvas_get_draw_buf()
 *
 * `len` is a uint8_t, so a row must be <= 255 bytes. 48 px of RGB565 is 96.
 *******************************************************************************/
static bool paint_ramp(int handle)
{
    lv_obj_t *obj = ui_widget_mgr_get_object(handle);
    if (obj == NULL) {
        return false;
    }
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(obj);
    if (buf == NULL || buf->data == NULL) {
        return false;
    }
    const uint32_t stride = buf->header.stride;
    const uint32_t w      = buf->header.w;
    const uint32_t h      = buf->header.h;
    uint8_t row[GAL_IMG_W * 2];      /* one row of RGB565, the widest legal */
    if (stride == 0u || w == 0u || h == 0u || (w * 2u) > sizeof(row)) {
        return false;
    }
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            /* teal -> magenta down the canvas, packed RGB565 little-endian,
             * which is the byte order LV_COLOR_FORMAT_RGB565 stores. */
            const uint32_t r = (y * 255u) / h;
            const uint32_t g = 200u - ((y * 160u) / h);
            const uint32_t b = 180u;
            const uint16_t c = (uint16_t)(((r & 0xF8u) << 8)
                                        | ((g & 0xFCu) << 3)
                                        |  (b >> 3));
            row[x * 2u]      = (uint8_t)(c & 0xFFu);
            row[x * 2u + 1u] = (uint8_t)(c >> 8);
        }
        ui_widget_mgr_set_image(handle, (uint16_t)(y * stride),
                                row, (uint8_t)(w * 2u));
    }
    return true;
}

int example_ipc_core_widget_gallery(lv_obj_t *parent)
{
    /* Two ways in, and they are not interchangeable.
     *
     * needs_container() is true only when nothing is bound. If something IS
     * bound but it is another page, rebinding is the correct move and it
     * deletes that page's widgets on the way. If we are already bound HERE,
     * a plain clear_all() gives the same clean slate without disturbing the
     * binding or the delete hook we registered last time. */
    if (ui_widget_mgr_needs_container() || ui_widget_mgr_get_parent() != parent) {
        ipc_ui_set_container(parent);
        lv_obj_remove_event_cb(parent, container_deleted_cb);
        lv_obj_add_event_cb(parent, container_deleted_cb, LV_EVENT_DELETE, NULL);
    } else {
        ui_widget_mgr_clear_all();
    }

    /* Auto-layout wraps at width - 100 and the grid cursor resets here. Give
     * it the container's real content box, not the panel's 800 px: this page
     * is narrower and scrolls. */
    const int32_t cw = lv_obj_get_content_width(parent);
    const int32_t ch = lv_obj_get_content_height(parent);
    ui_widget_mgr_set_screen((int16_t)cw, (int16_t)ch);

    ipc_ui_create_t cfg;

    /* --- Label. init_val is the font size in px; color is the TEXT colour. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_LABEL;
    cfg.x = 10; cfg.y = 6;
    cfg.color = 0xFFFFFF;
    cfg.init_val = UI_DEF_LABEL_FONT_SIZE;
    lv_snprintf(cfg.text, sizeof(cfg.text), "Widget gallery");
    s_h_title = ui_widget_mgr_create(&cfg);

    /* --- Seg7. A label with the seven-segment styling; text is the reading. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_SEG7;
    cfg.x = 260; cfg.y = 6;
    cfg.init_val = 24;
    lv_snprintf(cfg.text, sizeof(cfg.text), "88:88");
    s_h_seg7 = ui_widget_mgr_create(&cfg);

    /* --- Button. color is the BACKGROUND; the label inside is made for you
     *     and gets init_val as its font size. Clicking it pushes
     *     UI_EVENT_CLICKED into the event ring (see 05). */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_BUTTON;
    cfg.x = 10; cfg.y = 44; cfg.w = 120; cfg.h = 42;
    cfg.color = 0x2D6CDF;
    cfg.init_val = 18;
    lv_snprintf(cfg.text, sizeof(cfg.text), "Press me");
    s_h_button = ui_widget_mgr_create(&cfg);

    /* --- Switch. init_val non-zero starts it checked. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_SWITCH;
    cfg.x = 150; cfg.y = 52;
    cfg.init_val = 1;
    s_h_switch = ui_widget_mgr_create(&cfg);

    /* --- Checkbox. text is the caption beside the box. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_CHECKBOX;
    cfg.x = 230; cfg.y = 52;
    cfg.init_val = UI_DEF_CHECKBOX_FONT_SIZE;
    lv_snprintf(cfg.text, sizeof(cfg.text), "Enable");
    s_h_check = ui_widget_mgr_create(&cfg);

    /* --- Slider. min_val/max_val are the range, init_val the start value.
     *     A zero max_val is read as UI_DEF_RANGE_MAX (100), not as zero. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_SLIDER;
    cfg.x = 10; cfg.y = 108; cfg.w = 200;
    cfg.min_val = 0; cfg.max_val = 100; cfg.init_val = 35;
    s_h_slider = ui_widget_mgr_create(&cfg);

    /* --- Bar. Same range fields, no touch handling. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_BAR;
    cfg.x = 240; cfg.y = 108; cfg.w = 200; cfg.h = 16;
    cfg.min_val = 0; cfg.max_val = 100; cfg.init_val = 20;
    s_h_bar = ui_widget_mgr_create(&cfg);

    /* --- Arc. w is the diameter; it is forced square. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_ARC;
    cfg.x = 10; cfg.y = 140; cfg.w = 100; cfg.h = 100;
    cfg.min_val = 0; cfg.max_val = 100; cfg.init_val = 65;
    s_h_arc = ui_widget_mgr_create(&cfg);

    /* --- LED. An annunciator lamp; color is the lit colour. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_LED;
    cfg.x = 130; cfg.y = 160; cfg.w = 34; cfg.h = 34;
    cfg.color = 0x00C853;
    s_h_led = ui_widget_mgr_create(&cfg);

    /* --- Image, icon mode. Non-empty text is a BUILT-IN ICON NAME, and an
     *     unknown name is refused with -2 rather than drawn blank. The names
     *     are in ui_builtin_icons.h: heart, star, flag, trophy, skull,
     *     arrow_*, check, cross, smiley, car, boat, plane, home. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_IMAGE;
    cfg.x = 190; cfg.y = 150; cfg.w = 48; cfg.h = 48;
    cfg.color = 0xFF4D6D;
    lv_snprintf(cfg.text, sizeof(cfg.text), "heart");
    s_h_icon = ui_widget_mgr_create(&cfg);

    /* --- Image, dynamic mode. EMPTY text plus w/h gives a blank RGB565
     *     canvas you fill with set_image(). */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_IMAGE;
    cfg.x = 260; cfg.y = 150; cfg.w = GAL_IMG_W; cfg.h = GAL_IMG_H;
    s_h_canvas = ui_widget_mgr_create(&cfg);

    /* --- Dot matrix. min_val is COLUMNS, max_val is ROWS (both capped at 16);
     *     w/h size the whole panel and the dot pitch follows. Created at auto
     *     position (-1,-1) on purpose, then placed by hand below — that is the
     *     usual pattern when a layout is half grid and half hand-tuned. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_DOTMATRIX;
    cfg.x = -1; cfg.y = -1;
    cfg.min_val = 16; cfg.max_val = 8;
    cfg.w = 160; cfg.h = 80;
    s_h_matrix = ui_widget_mgr_create(&cfg);

    if (s_h_title < 0 || s_h_seg7 < 0 || s_h_button < 0 || s_h_switch < 0 ||
        s_h_check < 0 || s_h_slider < 0 || s_h_bar < 0 || s_h_arc < 0 ||
        s_h_led < 0 || s_h_icon < 0 || s_h_canvas < 0 || s_h_matrix < 0) {
        sdk_example_logf("a create failed: -1 = table full, -2 = bad type / no container");
        return SDK_EX_REFUSED;
    }

    /*-------------------------------------------------------------------------
     * Now the setters. Everything below addresses a widget by handle only; no
     * lv_obj_t crosses this boundary, which is exactly what lets MicroPython
     * on the other core drive the same screen.
     *-----------------------------------------------------------------------*/

    /* set_text is type-aware: on a Button it retargets the child label, on a
     * Checkbox the caption, on a Seg7 the digits. */
    ui_widget_mgr_set_text(s_h_seg7, "12:34");
    ui_widget_mgr_set_text(s_h_title, "Widget gallery - 12 handles");

    /* set_value drives whatever "value" means for the type: slider position,
     * bar fill, arc angle, switch/checkbox checked state. */
    ui_widget_mgr_set_value(s_h_bar, 72);
    ui_widget_mgr_set_value(s_h_arc, 40);

    /* set_position / set_size are plain LVGL geometry, in container pixels. */
    ui_widget_mgr_set_position(s_h_matrix, 340, 150);
    ui_widget_mgr_set_size(s_h_bar, 160, 20);

    /* set_color means BACKGROUND on control widgets and TEXT on label-like
     * ones. Zero is "leave the theme alone", so you cannot set black this way. */
    ui_widget_mgr_set_color(s_h_led, 0xFFB300);
    ui_widget_mgr_set_color(s_h_title, 0x9AD1FF);

    /* Dot matrix: one bit per dot. len is a byte count, not a dot count. */
    ui_widget_mgr_set_dotmatrix(s_h_matrix, s_face, (uint8_t)sizeof(s_face));

    const bool ramp_ok = paint_ramp(s_h_canvas);

    /*-------------------------------------------------------------------------
     * Visibility and deletion, on a scratch widget so the gallery survives.
     *-----------------------------------------------------------------------*/
    memset(&cfg, 0, sizeof(cfg));
    cfg.widget_type = UI_WIDGET_LABEL;
    cfg.x = 10; cfg.y = 250;
    cfg.color = 0x888888;
    cfg.init_val = 14;
    lv_snprintf(cfg.text, sizeof(cfg.text), "scratch widget");
    const int scratch = ui_widget_mgr_create(&cfg);

    const int count_with = ui_widget_mgr_count();

    /* set_visible only flips LV_OBJ_FLAG_HIDDEN. The handle stays valid, the
     * object stays allocated, and every other setter keeps working on it. */
    ui_widget_mgr_set_visible(scratch, false);
    ui_widget_mgr_set_visible(scratch, true);
    ui_widget_mgr_set_visible(scratch, false);

    /* delete really deletes. The slot is freed by LVGL's delete event, not by
     * this call, which is why count() is read afterwards and not assumed. */
    ui_widget_mgr_delete(scratch);
    const int count_without = ui_widget_mgr_count();

    sdk_example_logf("gallery: 12 widgets, %d handles live", count_without);
    sdk_example_logf("  scratch create/delete: %d -> %d", count_with, count_without);
    sdk_example_logf("  auto-layout width set to %d px", (int)cw);
    sdk_example_logf("  dynamic canvas ramp: %s",
                     ramp_ok ? "painted" : "FAILED (no draw buffer)");

    if (!ramp_ok) {
        return SDK_EX_REFUSED;
    }
    return SDK_EX_OK;
}
