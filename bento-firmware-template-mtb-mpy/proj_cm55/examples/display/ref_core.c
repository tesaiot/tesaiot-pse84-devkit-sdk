/* sdk-example: core=cm55 variant=both group=display
 * id:      cm55/display/ref_core
 * title:   Reference: the three symbols with no header
 * teaches: the archive exports three symbols that no shipped header declares -- what they are, the correct extern for each, and when you would want them
 * apis:    calculate_idle_percentage, tesaiot_display_ready, disp_touch_i2c_controller_context, g_tesaiot_display_diag, rtos_cm55_gfx_task_handle
 * entry:   example_cm55_reference
 */
/*
 * THIS IS A REFERENCE LIST, NOT A WORKING JOB.
 *
 * lib/cm55_core/api.txt names three symbols that no shipped header declares.
 * dist/cm55_core/include/bento_secure_undeclared.h records the gap and says
 * "declarations recovered: 0" -- it found no prototype to copy, because in the
 * firmware tree each of these is declared at its point of use rather than in a
 * header. They are exported all the same, so they link; you simply have to
 * write the extern yourself, and it has to be the RIGHT one. A wrong type here
 * is not a compile error, it is a wrong address or a wrong width at run time.
 *
 * Each extern below is the declaration the firmware itself uses, taken from
 * the file that already declares it. The source of each is named so you can
 * check it rather than trust this comment.
 *
 * Nothing here starts anything. Every call is a read.
 */

#include <stdbool.h>
#include <stdint.h>

#include "../sdk_examples.h"
#include "tesaiot_display.h"   /* also brings in the PDL and the I2C base */

/*******************************************************************************
 * The three undeclared exports
 ******************************************************************************/

/** LVGL's idle hook. lv_conf.h sets LV_SYSMON_GET_IDLE to this and declares it
 *  there -- `extern uint32_t calculate_idle_percentage(void);`
 *  (proj_cm55/modules/lvgl_display/core/lv_conf.h).
 *
 *  WHEN: you want CPU headroom as a number, without the sysmon overlay.
 *  RETURNS: percent of time the idle task ran, 0..100. A reading near 0 means
 *  something below the GFX task is starving -- which in this tree has been the
 *  root of a model-link wedge and a USB HID stall. */
extern uint32_t calculate_idle_percentage(void);

/** Set by the display controller as bring-up completes. main.c declares it
 *  inline: `extern volatile uint8_t tesaiot_display_ready;`
 *
 *  volatile and uint8_t both matter: it is written by the GFX task and read by
 *  every other task in a spin-with-delay, and declaring it uint32_t reads three
 *  neighbouring bytes as part of the value.
 *
 *  WHEN: any task that must not touch LVGL or IPC before the display exists.
 *  main.c's app_task and the radar task both wait on it, with a bound -- 150
 *  x 100 ms in app_task -- rather than forever.
 *  RETURNS: 0 = still coming up, 1 = display and IPC are live,
 *           2 = IPC only (headless: the panel failed, the rest works). */
extern volatile uint8_t tesaiot_display_ready;

/** The PDL context for the SCB the display's touch controller sits on.
 *  Declared by lv_port_indev.h and re-declared at each point of use, e.g.
 *  `extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;`
 *  (proj_cm55/modules/cm55_sensor_poll/cm55_sensor_poll.c).
 *
 *  WHEN: you need to talk to anything else on that bus. It is SHARED -- the
 *  touch controller, the on-board sensors, the audio codec and the RGB matrix
 *  all drive it through this one context, paired with
 *  DISPLAY_I2C_CONTROLLER_HW. Never allocate a second context for the same
 *  SCB: the driver keeps its transfer state here, and two contexts means two
 *  drivers each certain they own the bus.
 *
 *  Its fields are internal to the PDL and firmware must not read them; the
 *  supported way in is to pass its ADDRESS to a Cy_SCB_I2C_* call, which is
 *  what this file does below.
 *
 *  Bound every transfer you make on it. This bus is driven from the GFX task
 *  at MAX-1: an unbounded wait behind a clock-stretching device parks the
 *  display and starves everything below it. */
extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

int example_cm55_reference(lv_obj_t *parent)
{
    (void)parent;   /* a reference list writes to the log, not the screen */

    /* --- 1. bring-up state ------------------------------------------- */
    const uint8_t ready = tesaiot_display_ready;
    sdk_example_logf("tesaiot_display_ready = %u (%s)", (unsigned)ready,
                     (ready == 0u) ? "still coming up"
                   : (ready == 1u) ? "display + IPC live"
                   : (ready == 2u) ? "IPC only -- headless, the panel failed"
                                   : "unexpected value");

    /* rtos_cm55_gfx_task_handle IS declared (tesaiot_display.h). Paired with
     * the flag above it distinguishes "the task exists" from "the task has
     * finished bring-up" -- two different questions with two different
     * answers during the first second after reset. */
    sdk_example_logf("GFX task handle %s",
                     (rtos_cm55_gfx_task_handle != NULL) ? "created" : "NULL");

    /* --- 2. CPU headroom --------------------------------------------- */
    const uint32_t idle = calculate_idle_percentage();
    sdk_example_logf("CPU idle %lu%%", (unsigned long)idle);
    if (idle < 10u) {
        sdk_example_logf("  under 10%%: something at or below the GFX task is"
                         " burning the core. Starvation here has wedged the"
                         " model link and stalled the USB HID stream before");
    }

    /* --- 3. the shared display/touch I2C ------------------------------ */
    /* Read-only, and the context is passed to the driver rather than read
     * directly -- its fields are documented as internal. */
    const uint32_t i2c_status =
        Cy_SCB_I2C_MasterGetStatus(DISPLAY_I2C_CONTROLLER_HW,
                                   &disp_touch_i2c_controller_context);
    sdk_example_logf("display/touch I2C: ctx 0x%08lX, master status 0x%08lX, %s",
                     (unsigned long)(uintptr_t)&disp_touch_i2c_controller_context,
                     (unsigned long)i2c_status,
                     Cy_SCB_I2C_IsBusBusy(DISPLAY_I2C_CONTROLLER_HW)
                         ? "BUSY" : "idle");
    sdk_example_logf("  pass that context, and DISPLAY_I2C_CONTROLLER_HW, to"
                     " every Cy_SCB_I2C_* call on this bus");

    /* --- 4. the display counters, for completeness -------------------- */
    /* Declared in tesaiot_display.h; read here so a single tap gives the whole
     * cm55_core picture. cm55_core/01 explains how to interpret them. */
    sdk_example_logf("display: %lu frames flushed, %lu timeouts, %lu GPU recoveries",
                     (unsigned long)g_tesaiot_display_diag.flush_ready_count,
                     (unsigned long)g_tesaiot_display_diag.flush_timeout_count,
                     (unsigned long)g_tesaiot_display_diag.gpu_recovery_count);
    sdk_example_logf("GFX stack min free: %lu words",
                     (unsigned long)g_tesaiot_display_diag.gfx_task_stack_min_words);

    return SDK_EX_OK;
}
