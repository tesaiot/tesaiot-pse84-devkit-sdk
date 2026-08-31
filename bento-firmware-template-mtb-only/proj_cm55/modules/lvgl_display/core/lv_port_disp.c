/*******************************************************************************
 * Upstream provenance and licence — LVGL
 *
 * This file is derived from LVGL v9.5.0, upstream file:
 *     examples/porting/lv_port_disp_template.c
 *     https://github.com/lvgl/lvgl/tree/v9.5.0
 *
 * LVGL is distributed under the MIT licence, which requires its copyright
 * notice and permission notice to be retained in all copies and substantial
 * portions of the software. That notice is reproduced below in full.
 *
 * The Infineon notice that follows this block covers Infineon's modifications
 * to this file. Both notices stand together: neither supersedes the other, and
 * the Infineon terms do not narrow the MIT grant over the LVGL-authored code.
 *
 * ---------------------------------------------------------------------------
 * MIT licence
 * Copyright (c) 2025 LVGL Kft
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------
 *
 * The quotation marks above are ASCII; the upstream file uses typographic
 * quotes. The byte-exact upstream text is shipped as ../LICENSE-LVGL.txt
 ******************************************************************************/
/*******************************************************************************
* File Name        : lv_port_disp.c
*
* Description      : This file provides implementation of low level display
*                    device driver for LVGL.
*
* Related Document : See README.md
*
********************************************************************************
 * (c) 2025, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "lv_port_disp.h"
#include "lv_draw_sw.h"
#include <stdbool.h>
#include <string.h>
#include "cy_graphics.h"
#include "FreeRTOS.h"
#include "task.h"


#if LV_COLOR_DEPTH == 16
    #define BYTE_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565)) 
#elif LV_COLOR_DEPTH == 32
    #define BYTE_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_ARGB8888))
#endif


/*******************************************************************************
* Global Variables
*******************************************************************************/
CY_SECTION(".cy_gpu_buf") LV_ATTRIBUTE_MEM_ALIGN uint8_t disp_buf1[MY_DISP_HOR_RES *
                                               MY_DISP_VER_RES * BYTE_PER_PIXEL];
CY_SECTION(".cy_gpu_buf") LV_ATTRIBUTE_MEM_ALIGN uint8_t disp_buf2[MY_DISP_HOR_RES *
                                               MY_DISP_VER_RES * BYTE_PER_PIXEL];
/* Frame buffers used by GFXSS to render UI */
void *frame_buffer1 = &disp_buf1;
void *frame_buffer2 = &disp_buf2;

cy_stc_gfx_context_t gfx_context;

/* Display driver pointer, set by disp_flush(), used by DC ISR to signal
 * flush completion without blocking the GFX task. */
static volatile lv_display_t *s_flush_disp = NULL;

/* Flush timeout watchdog: detect missed DC interrupts.
 * Normal flush completes in <16ms (one VSYNC at 60Hz).
 * If DC interrupt is lost, LVGL permanently thinks it's still flushing
 * and never renders another frame — causing the display freeze.
 * After FLUSH_TIMEOUT_TICKS, force-complete the flush (costs 1 dropped frame). */
static volatile TickType_t s_flush_start_tick = 0;
static volatile uint32_t   s_flush_start_count = 0;
static volatile uint32_t   s_flush_ready_count = 0;
static volatile uint32_t   s_flush_timeout_count = 0;
#define FLUSH_TIMEOUT_TICKS  pdMS_TO_TICKS(500)


/*******************************************************************************
* Function Name: disp_flush
********************************************************************************
* Summary:
*  Flush the content of the internal buffer the specific area on the display.
*  You can use DMA or any hardware acceleration to do this operation in the
*  background but 'lv_disp_flush_ready()' has to be called when finished.
*
* Parameters:
*  *disp_drv: Pointer to the display driver structure to be registered by HAL.
*  *area: Pointer to the area of the screen (not used).
*  *color_p: Pointer to the frame buffer address.
*
* Return:
*  void
*
*******************************************************************************/
//! [j2_disp_flush_dc_handshake]
static void LV_ATTRIBUTE_FAST_MEM disp_flush(lv_display_t *disp_drv,
                                             const lv_area_t *area,
                                             uint8_t *color_p)
{
    CY_UNUSED_PARAMETER(area);

    /* Store display pointer so the DC ISR can call flush_ready.
     * Non-blocking: the GFX task remains free to run LVGL timers
     * (spinner animation, IPC UI commands) while waiting for the
     * DC hardware to finish the frame buffer swap. */
    s_flush_disp = disp_drv;
    s_flush_start_tick = xTaskGetTickCount();
    s_flush_start_count++;

    Cy_GFXSS_Set_FrameBuffer((GFXSS_Type*) GFXSS, (uint32_t*) color_p,
            &gfx_context);
}


/*******************************************************************************
* Function Name: lv_port_disp_flush_ready
********************************************************************************
* Summary:
*  Called from the DC interrupt handler to signal LVGL that the frame buffer
*  swap is complete.  This is ISR-safe — lv_display_flush_ready() only sets
*  internal flags (no mutex, no allocation).
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void lv_port_disp_flush_ready(void)
{
    /* Clear pointer FIRST to prevent double-call if watchdog fires
     * simultaneously.  32-bit pointer write is atomic on ARM Cortex-M. */
    lv_display_t *d = (lv_display_t *)s_flush_disp;
    s_flush_disp = NULL;
    if (d != NULL) {
        s_flush_ready_count++;
        lv_display_flush_ready(d);
        //! [j2_disp_flush_dc_handshake]
    }
}


//! [j2_flush_timeout_recovery]
/* ...context: lv_port_disp_check_flush_timeout() and its rationale ... */
/*******************************************************************************
* Function Name: lv_port_disp_check_flush_timeout
********************************************************************************
* Summary:
*  Called from the GFX task main loop to detect a stuck display flush.
*  If disp_flush() was called but the DC interrupt hasn't fired within
*  FLUSH_TIMEOUT_TICKS (500ms), force-complete the flush.
*
*  This handles missed DC interrupts — the primary suspected cause of
*  the stochastic display freeze (both GPU and SW rendering affected).
*
*  Cost: one dropped/stale frame (imperceptible at 30fps).
*  The display immediately resumes rendering on the next loop iteration.
*
*******************************************************************************/
void lv_port_disp_check_flush_timeout(void)
{
    lv_display_t *d = (lv_display_t *)s_flush_disp;
    if (d != NULL) {
        TickType_t elapsed = xTaskGetTickCount() - s_flush_start_tick;
        if (elapsed >= FLUSH_TIMEOUT_TICKS) {
            s_flush_disp = NULL;
            lv_display_flush_ready(d);
            //! [j2_flush_timeout_recovery]
            s_flush_timeout_count++;
        }
    }
}

uint32_t lv_port_disp_get_flush_start_count(void)
{
    return s_flush_start_count;
}

uint32_t lv_port_disp_get_flush_ready_count(void)
{
    return s_flush_ready_count;
}

uint32_t lv_port_disp_get_flush_timeout_count(void)
{
    return s_flush_timeout_count;
}


/*******************************************************************************
* Function Name: lv_port_disp_init
********************************************************************************
* Summary:
*  Initialization function for display devices supported by LittelvGL.
*   LVGL requires a buffer where it internally draws the widgets.
*   Later this buffer will passed to your display driver's `flush_cb` to copy
*   its content to your display.
*   The buffer has to be greater than 1 display row
*
*   There are 3 buffering configurations:
*   1. Create ONE buffer:
*      LVGL will draw the display's content here and writes it to your display
*
*   2. Create TWO buffer:
*      LVGL will draw the display's content to a buffer and writes it your
*      display.
*      You should use DMA to write the buffer's content to the display.
*      It will enable LVGL to draw the next part of the screen to the other
*      buffer while the data is being sent form the first buffer.
*      It makes rendering and flushing parallel.
*
*   3. Double buffering
*      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
*      This way LVGL will always provide the whole rendered screen in `flush_cb`
*      and you only need to change the frame buffer's address.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void lv_port_disp_init(void)
{
    memset(disp_buf1, 0, sizeof(disp_buf1));
    memset(disp_buf2, 0, sizeof(disp_buf2));

    lv_display_t * disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);

    lv_display_set_flush_cb(disp, disp_flush);

    lv_tick_set_cb(xTaskGetTickCount);

    /* LVGL 9.4 fix: use explicit stride to prevent auto-recalculation.
     * Without this, LVGL 9.4 recalculates stride from ACTUAL_DISP_HOR_RES (800)
     * instead of using the buffer's true stride based on MY_DISP_HOR_RES (832).
     * This causes a stride mismatch (1600 vs 1664 bytes) → garbled display. */
    lv_display_set_buffers_with_stride(disp, disp_buf1, disp_buf2, sizeof(disp_buf1),
                                       MY_DISP_HOR_RES * LV_COLOR_DEPTH / 8,
                                       LV_DISPLAY_RENDER_MODE_FULL);

    /* Set the display resolution to match the physical resolution so that
     * LVGL renders the UI within the limits of the actual display resolution.
     */
    lv_display_set_resolution(disp, ACTUAL_DISP_HOR_RES, ACTUAL_DISP_VER_RES);

    Cy_GFXSS_Clear_DC_Interrupt((GFXSS_Type*) GFXSS, &gfx_context);
}


/* [] END OF FILE */
