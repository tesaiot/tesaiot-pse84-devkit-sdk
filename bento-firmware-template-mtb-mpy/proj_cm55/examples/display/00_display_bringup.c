/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/00_display_bringup
 * title:   Bring the display up (and prove it came up)
 * teaches: tesaiot_display_init() owns its own task -- never create one yourself -- and g_tesaiot_display_diag is how you tell a working panel from a silent one
 * apis:    tesaiot_display_init, tesaiot_display_task, rtos_cm55_gfx_task_handle, g_tesaiot_display_diag
 * entry:   example_cm55_display_bringup
 */
/*
 * THE WHOLE BRING-UP IS ONE CALL, from main(), after cybsp_init() and
 * __enable_irq():
 *
 *     BaseType_t rc = tesaiot_display_init();
 *     if (pdPASS != rc) { ... }
 *
 * That call configures GFXSS, vg_lite and LVGL, brings up the display and
 * input ports, and CREATES THE GRAPHICS TASK ITSELF -- tesaiot_display_task at
 * GFX_TASK_PRIORITY with a GFX_TASK_STACK_SIZE stack, publishing its handle as
 * rtos_cm55_gfx_task_handle. Do not call xTaskCreate(tesaiot_display_task, ...)
 * yourself: a second graphics task means two owners for one framebuffer and
 * one vg_lite context, and LVGL is explicitly not thread safe.
 *
 * The entry point is exported all the same, because a port that must own its
 * own task creation needs to name it. Product code in this tree does not.
 *
 * TWO RULES THAT FOLLOW FROM IT
 *
 *  - ALL LVGL work happens in that task. A widget created from any other task
 *    is the single most common HardFault in this project. run() is called from
 *    inside it, which is exactly why examples may draw.
 *  - Nothing else may run before it is up. main() spawns app_task, which waits
 *    on `tesaiot_display_ready` (see cm55_core/ref_cm55_core) rather than
 *    assuming; the radar task does the same.
 *
 * WHAT THIS EXAMPLE DOES. Run from the Examples page, the display is already
 * up, so it VERIFIES the bring-up instead of repeating it: it confirms it is
 * on the GFX task and reads the diagnostic counters that separate "the panel
 * is working" from "the panel is dark and nobody noticed". Copied into main()
 * before bring-up, the same function takes the cold path and performs it.
 */

#include <stdbool.h>
#include <stdint.h>

#include "../sdk_examples.h"
#include "tesaiot_display.h"

int example_cm55_display_bringup(lv_obj_t *parent)
{
    /* The GFX task handle is the one honest test for "has bring-up run". */
    if (rtos_cm55_gfx_task_handle == NULL) {
        /* COLD PATH. Reachable when this function is called from main()
         * before the display exists -- which is how you would copy this file
         * into a project of your own. It is NOT the path the Examples page
         * takes, because that page is drawn by the task this creates. */
        const BaseType_t rc = tesaiot_display_init();
        if (pdPASS != rc) {
            sdk_example_logf("tesaiot_display_init() returned %ld -- no GFX task",
                             (long)rc);
            return SDK_EX_UNAVAILABLE;
        }
        sdk_example_logf("display brought up; GFX task created by the SDK");
        return SDK_EX_OK;
    }

    /* --- already up: verify, do not repeat ----------------------------- */

    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    const bool on_gfx = (self == rtos_cm55_gfx_task_handle);

    sdk_example_logf("GFX task %s -- LVGL calls here are %s",
                     on_gfx ? "IS this task" : "is a DIFFERENT task",
                     on_gfx ? "legal" : "A BUG");
    if (!on_gfx) {
        /* Should be impossible from the Examples page. If it ever happens,
         * drawing would be the fault, so refuse rather than draw. */
        return SDK_EX_REFUSED;
    }

    /* The entry point tesaiot_display_init() handed to the scheduler. Printed
     * so a map-file cross-check is possible; never passed to xTaskCreate. */
    sdk_example_logf("entry 0x%08lX \"%s\" prio %d, stack %lu words",
                     (unsigned long)(uintptr_t)&tesaiot_display_task,
                     GFX_TASK_NAME, (int)GFX_TASK_PRIORITY,
                     (unsigned long)GFX_TASK_STACK_SIZE);
    sdk_example_logf("stack never used: %lu words (all-time low)",
                     (unsigned long)uxTaskGetStackHighWaterMark(self));

    /* Counters the display controller keeps. Cumulative for the boot, and the
     * only evidence available on a core with no console. */
    sdk_example_logf("frames: flush_start %lu, flush_ready %lu, timeouts %lu",
                     (unsigned long)g_tesaiot_display_diag.flush_start_count,
                     (unsigned long)g_tesaiot_display_diag.flush_ready_count,
                     (unsigned long)g_tesaiot_display_diag.flush_timeout_count);
    sdk_example_logf("DC: disp0 %lu, underflow %lu, bus errors %lu, last irq 0x%08lX",
                     (unsigned long)g_tesaiot_display_diag.dc_disp0_count,
                     (unsigned long)g_tesaiot_display_diag.dc_underflow_count,
                     (unsigned long)g_tesaiot_display_diag.dc_bus_error_count,
                     (unsigned long)g_tesaiot_display_diag.last_dc_irq_status);
    sdk_example_logf("GPU recoveries: %lu",
                     (unsigned long)g_tesaiot_display_diag.gpu_recovery_count);

    /* How to read them, so the numbers are not just numbers. */
    if (g_tesaiot_display_diag.flush_ready_count == 0u) {
        sdk_example_logf("-> no flush ever completed. The panel is not being"
                         " driven; suspect the DSI link, not LVGL");
    } else if (g_tesaiot_display_diag.flush_timeout_count > 0u) {
        sdk_example_logf("-> %lu flush timeout(s): a frame was abandoned waiting"
                         " for the controller",
                         (unsigned long)g_tesaiot_display_diag.flush_timeout_count);
    }
    if (g_tesaiot_display_diag.dc_underflow_count > 0u) {
        sdk_example_logf("-> display-controller underflow: the fetch could not"
                         " keep up. Usually SMIF contention, not the GPU");
    }
    if (g_tesaiot_display_diag.dc_bus_error_count > 0u) {
        sdk_example_logf("-> DC BUS ERROR: a framebuffer address this core is not"
                         " granted. Check the MPC regions before anything else");
    }

    /* Proof the LVGL half is live too: a widget, created in the only task
     * allowed to create one. */
    lv_obj_t *note = lv_label_create(parent);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_text_fmt(note,
        "display up: %lu frames flushed, %lu GPU recoveries\n"
        "this label was created from the GFX task -- the only task that may",
        (unsigned long)g_tesaiot_display_diag.flush_ready_count,
        (unsigned long)g_tesaiot_display_diag.gpu_recovery_count);

    return SDK_EX_OK;
}
