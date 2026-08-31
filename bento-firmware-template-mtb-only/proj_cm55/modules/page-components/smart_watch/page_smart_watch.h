/*******************************************************************************
 * File Name: page_smart_watch.h
 *
 * Description: Smart Watch page — Smartwatch Demo embedded in page manager.
 *
 *******************************************************************************/

#ifndef PAGE_SMART_WATCH_H
#define PAGE_SMART_WATCH_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_smart_watch_create(void);
void      page_smart_watch_render(sensorhub_snapshot_t *snap);
void      page_smart_watch_destroy(void);

#endif /* PAGE_SMART_WATCH_H */
