/******************************************************************************
 * \file camera_hal.h
 *
 * \brief
 *     TESAIoT Camera HAL — Hardware Abstraction Layer for camera backends.
 *     Provides a unified vtable-based interface for DVP and USB UVC cameras.
 *
 *     Design: Zephyr Video API-inspired vtable pattern (camera_ops_t).
 *     Each backend implements get_frame/release_frame/get_status operations.
 *     The HAL dispatches calls to the active backend.
 *
 *     Buffer allocation is NOT managed by the HAL — it remains in the
 *     application (lcd_task.c) which allocates via vg_lite_allocate().
 *
 *******************************************************************************
 * SPDX-FileCopyrightText: 2025-2026 TESAIoT Foundation Platform
 *
 * \author TESA Developer Team
 *
 * Link: https://tesaiot.github.io/developer-hub/
 ******************************************************************************/

#ifndef CAMERA_HAL_H
#define CAMERA_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*******************************************************************************
 * Error Codes
 ******************************************************************************/
typedef enum {
    CAM_OK = 0,               /**< Success */
    CAM_ERR_NO_FRAME,         /**< No frame available (try again later) */
    CAM_ERR_NOT_INIT,         /**< Backend not initialized */
    CAM_ERR_DISCONNECTED,     /**< Camera device disconnected */
    CAM_ERR_INVALID_ARG,      /**< NULL pointer or invalid argument */
    CAM_ERR_BAD_DATA,         /**< Frame data validation failed */
    CAM_ERR_NOT_SUPPORTED,    /**< Camera not supported (unknown VID/PID) */
    CAM_ERR_INIT_FAILED,      /**< Backend initialization failed */
} camera_error_t;

/*******************************************************************************
 * Enumerations
 ******************************************************************************/

/** Camera source type */
typedef enum {
    CAM_SOURCE_UVC = 0,       /**< USB Video Class camera */
    CAM_SOURCE_DVP = 1,       /**< Digital Video Port camera (OV7675) */
    CAM_SOURCE_COUNT
} camera_source_t;

/** Pixel format of frame data */
typedef enum {
    CAM_FORMAT_YUYV422,       /**< YUYV 4:2:2 (USB camera native) */
    CAM_FORMAT_RGB565,        /**< RGB565 / BGR565 (DVP camera native) */
    CAM_FORMAT_RGB888,        /**< RGB888 (after conversion) */
} camera_pixel_format_t;

/*******************************************************************************
 * Data Structures
 ******************************************************************************/

/**
 * Camera frame descriptor.
 *
 * Returned by get_frame(), must be passed back to release_frame() when done.
 * The `data` pointer is valid until release_frame() is called.
 */
typedef struct {
    void                 *data;          /**< Pointer to raw pixel data */
    uint32_t              width;         /**< Frame width in pixels */
    uint32_t              height;        /**< Frame height in pixels */
    uint32_t              stride_bytes;  /**< Bytes per row (may include padding) */
    camera_pixel_format_t format;        /**< Pixel format */
    uint8_t               buffer_index;  /**< Double-buffer index (0 or 1) */
    void                 *_backend_buf;  /**< Backend-specific buffer (e.g. vg_lite_buffer_t*) */
} camera_frame_t;

/** Camera device status */
typedef struct {
    bool    connected;        /**< Device is physically connected */
    bool    streaming;        /**< Device is actively producing frames */
} camera_status_t;

/*******************************************************************************
 * Backend vtable (implemented by each camera backend)
 ******************************************************************************/

/**
 * Camera operations vtable.
 *
 * Each backend (DVP, UVC) provides an implementation of these operations.
 * The `ctx` parameter is backend-specific private data.
 */
typedef struct camera_ops {
    /**
     * Try to acquire a frame from the camera.
     *
     * Non-blocking: returns CAM_ERR_NO_FRAME if no frame is ready.
     * On success, caller MUST call release_frame() when done processing.
     *
     * DVP: clears dvp_frame_ready flag immediately (ISR writes to other buffer)
     * UVC: does NOT clear BufReady — release_frame() does that
     */
    camera_error_t (*get_frame)(void *ctx, camera_frame_t *frame);

    /**
     * Release a previously acquired frame.
     *
     * Must be called after get_frame() succeeds and processing is complete.
     * DVP: no-op (flag already cleared in get_frame)
     * UVC: clears BufReady so USB callback can reuse the buffer
     */
    camera_error_t (*release_frame)(void *ctx, camera_frame_t *frame);

    /**
     * Get current camera status (connected, streaming).
     */
    camera_error_t (*get_status)(void *ctx, camera_status_t *status);
} camera_ops_t;

/*******************************************************************************
 * Camera Device
 ******************************************************************************/

/** Camera device instance — holds active backend and source type */
typedef struct {
    const camera_ops_t *ops;          /**< Active backend vtable */
    camera_source_t     source;       /**< Current camera source */
    bool                initialized;  /**< HAL has been initialized */
} camera_device_t;

/*******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * Initialize the camera HAL with a specific source.
 *
 * For DVP: calls dvp_camera_init() internally.
 * For UVC: no-op (USB task already running from main.c).
 *
 * IMPORTANT: VGLite buffers (dvp_image_frames[], usb_yuv_frames[])
 * must be allocated BEFORE calling this function.
 *
 * @param dev     Device instance to initialize (caller-allocated)
 * @param source  Camera source to activate
 * @return CAM_OK on success
 */
camera_error_t camera_hal_init(camera_device_t *dev, camera_source_t source);

/**
 * Try to acquire a frame from the active camera.
 *
 * Non-blocking. Returns CAM_ERR_NO_FRAME if no frame available.
 * On success, caller MUST call camera_hal_release_frame() when done.
 *
 * @param dev    Initialized device
 * @param frame  Output: filled with frame data on success
 * @return CAM_OK on success, CAM_ERR_NO_FRAME if not ready
 */
camera_error_t camera_hal_get_frame(camera_device_t *dev, camera_frame_t *frame);

/**
 * Release a previously acquired frame.
 *
 * @param dev    Initialized device
 * @param frame  Frame to release (from get_frame)
 * @return CAM_OK on success
 */
camera_error_t camera_hal_release_frame(camera_device_t *dev, camera_frame_t *frame);

/**
 * Get current camera status.
 *
 * @param dev     Initialized device
 * @param status  Output: filled with status
 * @return CAM_OK on success
 */
camera_error_t camera_hal_get_status(camera_device_t *dev, camera_status_t *status);

/**
 * Switch to a different camera source.
 *
 * Both backends stay initialized — switching just changes which
 * backend's get_frame() is called. Instant, no re-initialization needed.
 *
 * @param dev         Initialized device
 * @param new_source  Source to switch to
 * @return CAM_OK on success
 */
camera_error_t camera_hal_switch(camera_device_t *dev, camera_source_t new_source);

/**
 * Get the currently active camera source.
 *
 * @param dev  Initialized device
 * @return Current camera_source_t
 */
camera_source_t camera_hal_get_source(const camera_device_t *dev);

/**
 * Check if the active camera requires software mirror.
 *
 * USB: most cameras need SW mirror (except HBVCAM 0.3MP)
 * DVP: OV7675 mirrors via MVFP register, no SW mirror needed
 *
 * @param dev  Initialized device
 * @return true if software mirror is needed
 */
bool camera_hal_needs_mirror(const camera_device_t *dev);

/*******************************************************************************
 * Backend Registration (called by camera_hal.c, implemented by backends)
 ******************************************************************************/

/** Get DVP backend operations vtable */
const camera_ops_t *camera_backend_dvp_get_ops(void);

/** Get UVC backend operations vtable */
const camera_ops_t *camera_backend_uvc_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_HAL_H */
