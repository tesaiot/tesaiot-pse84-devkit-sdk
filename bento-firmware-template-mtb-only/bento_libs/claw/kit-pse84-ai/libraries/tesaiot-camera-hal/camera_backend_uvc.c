/******************************************************************************
 * \file camera_backend_uvc.c
 *
 * \brief
 *     TESAIoT Camera HAL — USB UVC backend.
 *     Wraps usb_camera_task.c globals with camera_ops_t vtable.
 *
 *     Frame flow:
 *       USB _cbOnData → fills usb_yuv_frames[], sets BufReady + lastBuffer
 *       get_frame → checks BufReady, validates NumBytes, returns pointer
 *       release_frame → clears NumBytes + BufReady (allows USB to reuse buffer)
 *
 *******************************************************************************
 * SPDX-FileCopyrightText: 2025-2026 TESAIoT Foundation Platform
 *
 * \author TESA Developer Team
 *
 * Link: https://tesaiot.github.io/developer-hub/
 ******************************************************************************/

#include "camera_hal.h"
#include "lcd_task.h"          /* for NUM_IMAGE_BUFFERS, CAMERA_BUFFER_SIZE,
                                  CAMERA_WIDTH, CAMERA_HEIGHT, usb_yuv_frames */
#include "usb_camera_task.h"   /* for _ImageBuff, lastBuffer, _device_connected,
                                  camera_not_supported, point3mp_camera_enabled */
#include <string.h>

/*******************************************************************************
 * Extern references to usb_camera_task globals
 ******************************************************************************/
/* These are defined in usb_camera_task.c:
 *   extern VideoBuffer_t _ImageBuff[];
 *   extern volatile uint8_t lastBuffer;
 *   extern volatile uint8_t _device_connected;
 *   extern bool camera_not_supported;
 *   extern bool point3mp_camera_enabled;
 *
 * And in lcd_task.c:
 *   extern vg_lite_buffer_t usb_yuv_frames[];
 */
extern volatile uint8_t _device_connected;
extern bool camera_not_supported;

/*******************************************************************************
 * Backend Operations
 ******************************************************************************/

/**
 * UVC get_frame: non-blocking frame acquisition.
 *
 * Checks BufReady on the last-written buffer. If primary buffer not ready,
 * tries the alternate buffer (same logic as current lcd_task.c).
 *
 * Does NOT clear BufReady — that happens in release_frame().
 */
static camera_error_t uvc_get_frame(void *ctx, camera_frame_t *frame)
{
    (void)ctx;

    /* Check device connection */
    if (_device_connected == 0) {
        return CAM_ERR_DISCONNECTED;
    }

    if (camera_not_supported) {
        return CAM_ERR_NOT_SUPPORTED;
    }

    /* Read last buffer index with memory barrier */
    __DMB();
    uint8_t work_buf = lastBuffer;

    /* Validate buffer index */
    if (work_buf >= NUM_IMAGE_BUFFERS) {
        work_buf = 0;
    }

    uint8_t buf_ready = _ImageBuff[work_buf].BufReady;
    __DMB();

    /* If primary buffer not ready, try alternate */
    if (buf_ready == 0) {
        uint8_t alt_buf = (work_buf + 1) % NUM_IMAGE_BUFFERS;
        __DMB();
        if (_ImageBuff[alt_buf].BufReady == 1) {
            work_buf = alt_buf;
            buf_ready = 1;
        }
        __DMB();
    }

    if (buf_ready == 0) {
        return CAM_ERR_NO_FRAME;
    }

    /* Validate frame data size */
    __DMB();
    if (_ImageBuff[work_buf].NumBytes != CAMERA_BUFFER_SIZE) {
        /* Bad data — clear buffer to prevent stuck state */
        _ImageBuff[work_buf].NumBytes = 0;
        __DMB();
        _ImageBuff[work_buf].BufReady = 0;
        __DMB();
        return CAM_ERR_BAD_DATA;
    }

    /* Fill frame descriptor */
    frame->data          = usb_yuv_frames[work_buf].memory;
    frame->width         = CAMERA_WIDTH;
    frame->height        = CAMERA_HEIGHT;
    frame->stride_bytes  = usb_yuv_frames[work_buf].stride;
    frame->format        = CAM_FORMAT_YUYV422;
    frame->buffer_index  = work_buf;
    frame->_backend_buf  = &usb_yuv_frames[work_buf];

    return CAM_OK;
}

/**
 * UVC release_frame: release buffer back to USB callback.
 *
 * Clears NumBytes and BufReady so the USB _cbOnData() callback
 * can write new frame data to this buffer.
 */
static camera_error_t uvc_release_frame(void *ctx, camera_frame_t *frame)
{
    (void)ctx;

    if (frame == NULL) {
        return CAM_ERR_INVALID_ARG;
    }

    uint8_t idx = frame->buffer_index;
    if (idx >= NUM_IMAGE_BUFFERS) {
        return CAM_ERR_INVALID_ARG;
    }

    /* Clear buffer status — order matters:
     * NumBytes first, then BufReady (same as current lcd_task.c) */
    _ImageBuff[idx].NumBytes = 0;
    __DMB();
    _ImageBuff[idx].BufReady = 0;
    __DMB();

    return CAM_OK;
}

/**
 * UVC get_status: check USB device connection and support.
 */
static camera_error_t uvc_get_status(void *ctx, camera_status_t *status)
{
    (void)ctx;

    status->connected = (_device_connected != 0) && !camera_not_supported;
    status->streaming = (_device_connected != 0) && !camera_not_supported;

    return CAM_OK;
}

/*******************************************************************************
 * Vtable
 ******************************************************************************/

static const camera_ops_t uvc_ops = {
    .get_frame     = uvc_get_frame,
    .release_frame = uvc_release_frame,
    .get_status    = uvc_get_status,
};

const camera_ops_t *camera_backend_uvc_get_ops(void)
{
    return &uvc_ops;
}
