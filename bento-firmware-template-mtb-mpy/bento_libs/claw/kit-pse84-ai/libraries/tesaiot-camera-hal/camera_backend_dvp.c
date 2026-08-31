/******************************************************************************
 * \file camera_backend_dvp.c
 *
 * \brief
 *     TESAIoT Camera HAL — DVP backend (OV7675).
 *     Wraps dvp_camera_task.c globals with camera_ops_t vtable.
 *
 *     Frame flow:
 *       VSYNC ISR → sets dvp_frame_ready, toggles dvp_active_frame
 *       get_frame → checks flag, clears it, returns pointer to ready buffer
 *       release_frame → no-op (flag already cleared, ISR writes to OTHER buffer)
 *
 *******************************************************************************
 * SPDX-FileCopyrightText: 2025-2026 TESAIoT Foundation Platform
 *
 * \author TESA Developer Team
 *
 * Link: https://tesaiot.github.io/developer-hub/
 ******************************************************************************/

#include "camera_hal.h"
#include "dvp_camera_task.h"
#include <string.h>

/*******************************************************************************
 * Extern references to dvp_camera_task globals
 ******************************************************************************/
/* These are defined in dvp_camera_task.c and dvp_camera_task.h:
 *   extern vg_lite_buffer_t dvp_image_frames[];
 *   extern volatile bool dvp_frame_ready;
 *   extern volatile bool dvp_active_frame;
 *   extern volatile uint8_t dvp_device_connected;
 */

/*******************************************************************************
 * Backend Operations
 ******************************************************************************/

/**
 * DVP get_frame: non-blocking frame acquisition.
 *
 * Checks dvp_frame_ready flag (set by VSYNC ISR).
 * If ready, clears the flag immediately and returns a pointer to the
 * completed frame buffer. This is safe because the ISR writes to the
 * OTHER buffer (double-buffering).
 */
static camera_error_t dvp_get_frame(void *ctx, camera_frame_t *frame)
{
    (void)ctx;

    /* Check if DVP has been initialized */
    if (!dvp_device_connected) {
        return CAM_ERR_NOT_INIT;
    }

    /* Check frame ready flag (set by VSYNC ISR) */
    __DMB();
    if (!dvp_frame_ready) {
        return CAM_ERR_NO_FRAME;
    }

    /* Consume the frame: clear flag before reading buffer.
     * Safe because ISR writes to the OTHER buffer. */
    dvp_frame_ready = false;
    __DMB();

    /* Ready buffer is the one ISR is NOT currently writing to.
     * dvp_active_frame == true means ISR is writing buffer[1],
     * so buffer[0] is the completed frame (and vice versa). */
    uint8_t ready_idx = dvp_active_frame ? 0 : 1;

    frame->data          = dvp_image_frames[ready_idx].memory;
    frame->width         = DVP_CAMERA_WIDTH;
    frame->height        = DVP_CAMERA_HEIGHT;
    frame->stride_bytes  = dvp_image_frames[ready_idx].stride;
    frame->format        = CAM_FORMAT_RGB565;
    frame->buffer_index  = ready_idx;
    frame->_backend_buf  = &dvp_image_frames[ready_idx];

    return CAM_OK;
}

/**
 * DVP release_frame: no-op.
 *
 * The dvp_frame_ready flag was already cleared in get_frame().
 * The ISR writes to the OTHER buffer, so no explicit release is needed.
 */
static camera_error_t dvp_release_frame(void *ctx, camera_frame_t *frame)
{
    (void)ctx;
    (void)frame;
    return CAM_OK;
}

/**
 * DVP get_status: check device connection.
 *
 * DVP is always "streaming" once initialized — DMA ISR captures continuously.
 */
static camera_error_t dvp_get_status(void *ctx, camera_status_t *status)
{
    (void)ctx;

    status->connected = (dvp_device_connected != 0);
    status->streaming = (dvp_device_connected != 0);

    return CAM_OK;
}

/*******************************************************************************
 * Vtable
 ******************************************************************************/

static const camera_ops_t dvp_ops = {
    .get_frame     = dvp_get_frame,
    .release_frame = dvp_release_frame,
    .get_status    = dvp_get_status,
};

const camera_ops_t *camera_backend_dvp_get_ops(void)
{
    return &dvp_ops;
}
