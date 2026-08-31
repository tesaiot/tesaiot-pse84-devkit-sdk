/*******************************************************************************
 * File Name: page_home.h
 *
 * Description: Home screen — card grid menu for page navigation.
 *              Shows one card per page with sensor color, title, and live
 *              preview data. Tap a card to navigate to that page.
 *
 *******************************************************************************/

#ifndef PAGE_HOME_H
#define PAGE_HOME_H

#include "lvgl.h"
#include "ipc_sensorhub.h"

/** Create the Home screen with card grid. */
lv_obj_t *page_home_create(void);

/** Update preview labels with live sensor data. */
void page_home_render(sensorhub_snapshot_t *snap);

/** Cleanup (no-op for Home, but keeps interface consistent). */
void page_home_destroy(void);

#endif /* PAGE_HOME_H */
