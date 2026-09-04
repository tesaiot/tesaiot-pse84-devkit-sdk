/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/01_bringup
 * title:   Bring the CM55 IPC peers up in the right order
 * teaches: the exact init sequence a GFX task owes the IPC library, and how to read back what is already running
 * apis:    ipc_sensorhub_init, ipc_service_init, ipc_lcd_init, ipc_ui_init, ui_widget_mgr_init, ui_widget_mgr_get_parent, ui_widget_mgr_needs_container, cm55_ipc_pipe_isr, cm55_ipc_pipe_ep_busy, cm55_ipc_pipe_drain_release
 * entry:   example_ipc_core_bringup
 */
/*******************************************************************************
 * ipc_core/01_bringup — the first thing you write against libbento_ipc.
 *
 * Four independent receivers live in this library, they all sit on the same
 * CM55 IPC pipe endpoint, and each one claims a different client id:
 *
 *   ipc_sensorhub   CM55_IPC_SENSOR_CLIENT_ID   sensor samples from CM33_NS
 *   ipc_service     (its own task)              WiFi / MQTT / TESAIoT commands
 *   ipc_lcd         CM55_IPC_PIPE_CLIENT_ID     lcd.print() console text
 *   ipc_ui          CM55_IPC_UI_CLIENT_ID       the MicroPython `ui` module
 *
 * ORDER MATTERS, and only in one place: the pipe itself must exist before
 * anybody registers a callback on it. cm55_ipc_communication_setup() is what
 * calls Cy_IPC_Pipe_Init(). Registering first is the classic CM55 IPC
 * HardFault-at-boot in this workspace. After that the four are independent.
 *
 * WHY run() DOES NOT CALL tesaiot_ipc_core_bringup()
 * -------------------------------------------------
 * On any board running this firmware the GFX task already ran that sequence
 * before the display was up, so calling it again would create a SECOND
 * FreeRTOS queue in ipc_lcd and in ipc_ui, leak the first pair, and register a
 * second LVGL timer against each. ipc_service_init() would start a second
 * handler task. None of that is detectable from here — there is no
 * "is it up?" call in the API — so the honest thing is to not do it and to
 * report what IS up instead. Copy tesaiot_ipc_core_bringup() into your own
 * display task; do not call it from a page.
 *
 * Also note ipc_lcd_init() calls Cy_SysLib_Delay(50). That is fine in a boot
 * path and forbidden inside an LVGL event callback, which is the other reason
 * run() leaves it alone.
 *******************************************************************************/

#include "../sdk_examples.h"

#include "ipc_communication.h"   /* cm55_ipc_communication_setup()            */
#include "ipc_lcd.h"
#include "ipc_sensorhub.h"
#include "ipc_service.h"
#include "ipc_ui.h"
#include "ipc_ui_protocol.h"
#include "ui_widget_mgr.h"

/*******************************************************************************
 * Bits returned by tesaiot_ipc_core_bringup(). Each receiver reports for
 * itself: a board with no display still wants the sensor hub and the service
 * task, and losing one is not a reason to skip the other three.
 *******************************************************************************/
#define IPC_CORE_UP_SENSORHUB   (1u << 0)
#define IPC_CORE_UP_SERVICE     (1u << 1)
#define IPC_CORE_UP_LCD         (1u << 2)
#define IPC_CORE_UP_UI          (1u << 3)
#define IPC_CORE_UP_ALL         (IPC_CORE_UP_SENSORHUB | IPC_CORE_UP_SERVICE | \
                                 IPC_CORE_UP_LCD | IPC_CORE_UP_UI)

/*******************************************************************************
 * THE SEQUENCE. Copy this function into your GFX task, call it once, before
 * the LVGL main loop and after cybsp_init().
 *
 * Both container arguments may be NULL. That is the normal case and not a
 * degraded one: the pages that own those containers do not exist yet at boot,
 * so they bind themselves later with ipc_ui_set_container() and
 * ipc_lcd_set_container() when they are created, and pass NULL back when they
 * are destroyed. Binding at boot to a container you are about to destroy is
 * how you get writes into freed LVGL objects.
 *
 * @param ui_parent       container for MicroPython-created widgets, or NULL
 * @param console_parent  container for the lcd.print() terminal, or NULL
 * @return                OR of IPC_CORE_UP_*; IPC_CORE_UP_ALL on full success
 *******************************************************************************/
unsigned tesaiot_ipc_core_bringup(lv_obj_t *ui_parent, lv_obj_t *console_parent)
{
    unsigned up = 0u;

    /* 1. The pipe. Everything below registers a callback on it, so nothing
     *    below is legal until this returns. */
    cm55_ipc_communication_setup();

    /* 1b. Two plumbing symbols worth knowing while the pipe is still bare:
     *
     *     cm55_ipc_pipe_isr() is the pipe's interrupt service routine. The
     *     BSP wires it into the vector table during setup; you never call it,
     *     but if you relocate vectors at run time, this is the address that
     *     has to survive the move — taking it here is the honest use.
     *
     *     cm55_ipc_pipe_ep_busy() reads the endpoint's busy flag. Nonzero is
     *     normal while a message drains; nonzero STUCK across successive
     *     reads means the pipe is wedged and no receiver below will ever
     *     fire. Read it when a dashboard goes quiet before blaming a sensor. */
    void (*const pipe_vector)(void) = cm55_ipc_pipe_isr;
    (void)pipe_vector;
    uint32_t pipe_busy = cm55_ipc_pipe_ep_busy();
    if (pipe_busy != 0u && cm55_ipc_pipe_ep_busy() != 0u) {
        /* Busy across two reads this early is a wedge, not a drain in
         * progress. cm55_ipc_pipe_drain_release() is the designed remedy —
         * it force-processes any pending release callback and clears the
         * endpoint's busy flag; the firmware's own WiFi manager uses it the
         * same way before posting into the pipe. Harmless when not wedged. */
        cm55_ipc_pipe_drain_release();
    }

    /* 2. Sensor receiver. No LVGL, no display — safe even on a board whose
     *    panel failed to initialise, which is why it goes first. */
    if (ipc_sensorhub_init()) {
        up |= IPC_CORE_UP_SENSORHUB;
    }

    /* 3. Service task (WiFi / MQTT / TESAIoT command handling). Creates its
     *    own FreeRTOS task; do not call it twice. */
    if (ipc_service_init()) {
        up |= IPC_CORE_UP_SERVICE;
    }

    /* 4. Console receiver. Creates a queue and an LVGL timer, so LVGL must
     *    already be initialised. Blocks for 50 ms — boot path only. */
    if (ipc_lcd_init(console_parent)) {
        up |= IPC_CORE_UP_LCD;
    }

    /* 5. Widget receiver. Internally calls ui_widget_mgr_init(ui_parent), so
     *    the handle table is reset here and NOT separately. Requires a live
     *    display: skip it if your panel did not come up, exactly as the
     *    shipped GFX task does. */
    if (ipc_ui_init(ui_parent)) {
        up |= IPC_CORE_UP_UI;
    }

    return up;
}

/*******************************************************************************
 * The widget table WITHOUT the IPC receiver.
 *
 * ui_widget_mgr is usable on its own: it is a 64-slot handle table over LVGL
 * objects, and nothing in it needs CM33 to exist. Native simulators and
 * C-only screens take this path and never call ipc_ui_init().
 *
 * Use ui_widget_mgr_init() ONCE, at bring-up. Afterwards use
 * ui_widget_mgr_set_parent() / ipc_ui_set_container() to rebind, because those
 * delete the widgets they are dropping; init() only zeroes the table and would
 * strand every LVGL object and draw buffer it forgets.
 *******************************************************************************/
void tesaiot_ipc_core_bringup_widgets_only(lv_obj_t *parent)
{
    ui_widget_mgr_init(parent);
}

/*******************************************************************************
 * The example: report what can be OBSERVED, and nothing else.
 *
 * Three of the four receivers publish no state at all: there is no
 * ipc_sensorhub_is_up(), no ipc_service_is_up(), no ipc_lcd_is_up(). Painting
 * a green tick beside them would be a decoration, not a reading, so this
 * screen says which questions the API can answer and which it cannot.
 *******************************************************************************/
static lv_obj_t *card(lv_obj_t *parent, int32_t y, int32_t h, const char *title)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, lv_pct(96), h);
    lv_obj_set_pos(box, 8, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x141428), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x2A3550), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    return box;
}

static void line(lv_obj_t *box, const char *text, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
}

int example_ipc_core_bringup(lv_obj_t *parent)
{
    lv_obj_clean(parent);

    /* The one thing this core CAN check. ui_widget_mgr_needs_container()
     * answers true when no parent is bound — at boot, and again every time a
     * page unbinds on its way out. Every widget create returns -2 while it is
     * true, so it is the first thing to test when nothing appears. */
    const bool widgets_bound = !ui_widget_mgr_needs_container();
    lv_obj_t  *widget_parent = ui_widget_mgr_get_parent();

    lv_obj_t *box = card(parent, 8, 150, "What this core can observe");
    if (widgets_bound) {
        line(box, (widget_parent == parent)
                    ? "ipc_ui        container bound to THIS page"
                    : "ipc_ui        container bound to another page",
             0x7BE38B);
    } else {
        line(box, "ipc_ui        no container - every create returns -2",
             0xE8A33D);
    }
    line(box, "ipc_sensorhub no state probe in the API", 0x9AD1FF);
    line(box, "ipc_service   no state probe in the API", 0x9AD1FF);
    line(box, "ipc_lcd       no state probe; is_panel_visible() is about",
         0x9AD1FF);
    line(box, "              the PANEL, not the receiver", 0x9AD1FF);

    lv_obj_t *box2 = card(parent, 166, 190, "The order, once, at boot");
    line(box2, "1  cm55_ipc_communication_setup()   the pipe", 0xD8E0F0);
    line(box2, "2  ipc_sensorhub_init()             no display needed", 0xD8E0F0);
    line(box2, "3  ipc_service_init()               starts a task", 0xD8E0F0);
    line(box2, "4  ipc_lcd_init(console_parent)     blocks 50 ms", 0xD8E0F0);
    line(box2, "5  ipc_ui_init(ui_parent)           needs LVGL", 0xD8E0F0);
    line(box2, "   (5 calls ui_widget_mgr_init() for you)", 0x9AD1FF);

    sdk_example_logf("widget container: %s",
                     !widgets_bound ? "NULL" :
                     (widget_parent == parent ? "this page" : "another page"));
    sdk_example_logf("the other three receivers publish no state - not probed");
    sdk_example_logf("this example REPORTS; it does not re-init.");
    sdk_example_logf("copy tesaiot_ipc_core_bringup() into your GFX task.");

    /* Honest: the only thing checked is whether widgets have a home, and it
     * may legitimately be false on a board whose panel did not come up. */
    return widgets_bound ? SDK_EX_OK : SDK_EX_UNAVAILABLE;
}
