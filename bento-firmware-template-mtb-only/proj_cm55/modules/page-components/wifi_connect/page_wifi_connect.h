/*******************************************************************************
 * File Name: page_wifi_connect.h
 *
 * Description: WiFi Connect page — native demo wrapper.
 *
 *******************************************************************************/

#ifndef PAGE_WIFI_CONNECT_H
#define PAGE_WIFI_CONNECT_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *page_wifi_connect_create(void);
void      page_wifi_connect_render(sensorhub_snapshot_t *snap);
void      page_wifi_connect_destroy(void);

#endif /* PAGE_WIFI_CONNECT_H */
