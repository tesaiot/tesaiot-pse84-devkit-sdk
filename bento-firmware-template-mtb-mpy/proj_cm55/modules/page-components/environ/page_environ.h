/*******************************************************************************
 * File Name: page_environ.h
 *
 * Description: Environment page — DPS368 + SHT40 arc gauges + derived metrics.
 *              AI Kit only (BSP_HAS_DPS368 || BSP_HAS_SHT40).
 *              Page-based replacement for tab_environ (create/render/destroy).
 *
 *******************************************************************************/

#ifndef PAGE_ENVIRON_H
#define PAGE_ENVIRON_H

#include "bsp_feature_flags.h"

#if BSP_HAS_DPS368 || BSP_HAS_SHT40

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_environ_create(void);
void      page_environ_render(sensorhub_snapshot_t *snap);
void      page_environ_destroy(void);

#endif /* BSP_HAS_DPS368 || BSP_HAS_SHT40 */
#endif /* PAGE_ENVIRON_H */
