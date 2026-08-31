/*******************************************************************************
 * File: wifi_connect_native.h
 *
 * Description:
 *   Native WiFi Connect page that reuses AIC-EEC WiFi UI core (aic_wifi.*)
 *   and bridges operations to local wifi_manager backend.
 *
 ******************************************************************************/

#ifndef WIFI_CONNECT_NATIVE_H
#define WIFI_CONNECT_NATIVE_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

lv_obj_t *wifi_connect_native_create(void);
void      wifi_connect_native_render(sensorhub_snapshot_t *snap);
void      wifi_connect_native_destroy(void);

#endif /* WIFI_CONNECT_NATIVE_H */
