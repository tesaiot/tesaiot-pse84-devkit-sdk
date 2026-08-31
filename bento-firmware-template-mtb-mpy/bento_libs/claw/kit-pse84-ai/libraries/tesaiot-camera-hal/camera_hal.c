/******************************************************************************
 * \file camera_hal.c
 *
 * \brief
 *     TESAIoT Camera HAL — Core implementation.
 *     Factory, switch, and dispatch functions for camera backends.
 *
 *******************************************************************************
 * SPDX-FileCopyrightText: 2025-2026 TESAIoT Foundation Platform
 *
 * \author TESA Developer Team
 *
 * Link: https://tesaiot.github.io/developer-hub/
 ******************************************************************************/

#include "camera_hal.h"
#include <stdio.h>

/*******************************************************************************
 * Private: Backend ops table (indexed by camera_source_t)
 ******************************************************************************/

/** Cached backend ops pointers — populated on first init */
static const camera_ops_t *backend_ops[CAM_SOURCE_COUNT] = { NULL, NULL };

/**
 * Ensure backend ops are loaded for a given source.
 */
static camera_error_t ensure_backend_ops(camera_source_t source)
{
    if (source >= CAM_SOURCE_COUNT) {
        return CAM_ERR_INVALID_ARG;
    }

    if (backend_ops[source] != NULL) {
        return CAM_OK;
    }

    switch (source) {
    case CAM_SOURCE_UVC:
        backend_ops[CAM_SOURCE_UVC] = camera_backend_uvc_get_ops();
        break;
    case CAM_SOURCE_DVP:
        backend_ops[CAM_SOURCE_DVP] = camera_backend_dvp_get_ops();
        break;
    default:
        return CAM_ERR_INVALID_ARG;
    }

    if (backend_ops[source] == NULL) {
        return CAM_ERR_NOT_SUPPORTED;
    }

    return CAM_OK;
}

/*******************************************************************************
 * Public API Implementation
 ******************************************************************************/

camera_error_t camera_hal_init(camera_device_t *dev, camera_source_t source)
{
    if (dev == NULL) {
        return CAM_ERR_INVALID_ARG;
    }

    if (source >= CAM_SOURCE_COUNT) {
        return CAM_ERR_INVALID_ARG;
    }

    /* Load backend ops for BOTH sources (both backends stay initialized) */
    camera_error_t err;

    err = ensure_backend_ops(CAM_SOURCE_UVC);
    if (err != CAM_OK) {
        printf("[CAM_HAL] Warning: UVC backend not available\r\n");
    }

    err = ensure_backend_ops(CAM_SOURCE_DVP);
    if (err != CAM_OK) {
        printf("[CAM_HAL] Warning: DVP backend not available\r\n");
    }

    /* Verify requested source is available */
    if (backend_ops[source] == NULL) {
        printf("[CAM_HAL] Error: requested source %d not available\r\n", source);
        return CAM_ERR_NOT_SUPPORTED;
    }

    /* Set active backend */
    dev->ops = backend_ops[source];
    dev->source = source;
    dev->initialized = true;

    printf("[CAM_HAL] Initialized with source %s\r\n",
           source == CAM_SOURCE_DVP ? "DVP" : "UVC");

    return CAM_OK;
}

camera_error_t camera_hal_get_frame(camera_device_t *dev, camera_frame_t *frame)
{
    if (dev == NULL || frame == NULL) {
        return CAM_ERR_INVALID_ARG;
    }
    if (!dev->initialized || dev->ops == NULL) {
        return CAM_ERR_NOT_INIT;
    }

    return dev->ops->get_frame(NULL, frame);
}

camera_error_t camera_hal_release_frame(camera_device_t *dev, camera_frame_t *frame)
{
    if (dev == NULL || frame == NULL) {
        return CAM_ERR_INVALID_ARG;
    }
    if (!dev->initialized || dev->ops == NULL) {
        return CAM_ERR_NOT_INIT;
    }

    return dev->ops->release_frame(NULL, frame);
}

camera_error_t camera_hal_get_status(camera_device_t *dev, camera_status_t *status)
{
    if (dev == NULL || status == NULL) {
        return CAM_ERR_INVALID_ARG;
    }
    if (!dev->initialized || dev->ops == NULL) {
        return CAM_ERR_NOT_INIT;
    }

    return dev->ops->get_status(NULL, status);
}

camera_error_t camera_hal_switch(camera_device_t *dev, camera_source_t new_source)
{
    if (dev == NULL) {
        return CAM_ERR_INVALID_ARG;
    }
    if (!dev->initialized) {
        return CAM_ERR_NOT_INIT;
    }
    if (new_source >= CAM_SOURCE_COUNT) {
        return CAM_ERR_INVALID_ARG;
    }

    /* Same source — no-op */
    if (dev->source == new_source) {
        return CAM_OK;
    }

    /* Verify target backend is available */
    if (backend_ops[new_source] == NULL) {
        printf("[CAM_HAL] Cannot switch: source %d not available\r\n", new_source);
        return CAM_ERR_NOT_SUPPORTED;
    }

    /* Switch: just change the active ops pointer.
     * Both backends stay initialized — no teardown needed. */
    dev->ops = backend_ops[new_source];
    dev->source = new_source;

    printf("[CAM_HAL] Switched to %s\r\n",
           new_source == CAM_SOURCE_DVP ? "DVP" : "UVC");

    return CAM_OK;
}

camera_source_t camera_hal_get_source(const camera_device_t *dev)
{
    if (dev == NULL || !dev->initialized) {
        return CAM_SOURCE_UVC; /* default */
    }
    return dev->source;
}

bool camera_hal_needs_mirror(const camera_device_t *dev)
{
    if (dev == NULL || !dev->initialized) {
        return false;
    }

    /* DVP (OV7675): hardware mirror via MVFP register — no SW mirror */
    if (dev->source == CAM_SOURCE_DVP) {
        return false;
    }

    /* UVC: most cameras need SW mirror.
     * Exception: HBVCAM 0.3MP (point3mp) is already mirrored.
     * The caller (lcd_task) checks point3mp_camera_enabled for this. */
    return true;
}
