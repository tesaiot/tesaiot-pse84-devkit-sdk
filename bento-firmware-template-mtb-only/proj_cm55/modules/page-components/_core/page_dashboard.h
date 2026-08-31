/*******************************************************************************
 * File Name: page_dashboard.h
 *
 * Description: Dashboard page — all-sensors overview in card grid layout.
 *              Page-based replacement for tab_dashboard (create/render/destroy).
 *
 *******************************************************************************/

#ifndef PAGE_DASHBOARD_H
#define PAGE_DASHBOARD_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_dashboard_create(void);
void      page_dashboard_render(sensorhub_snapshot_t *snap);
void      page_dashboard_destroy(void);

#endif /* PAGE_DASHBOARD_H */
