/*******************************************************************************
 * File Name: page_wifi_connect.c
 *
 * Description: WiFi Connect page — native import from AIC-EEC part5 aic_wifi.
 *              Delegates to wifi_connect_native demo implementation.
 *
 *******************************************************************************/

#include "page_wifi_connect.h"
#include "demo/wifi_connect_native/wifi_connect_native.h"

lv_obj_t *page_wifi_connect_create(void)
{
    return wifi_connect_native_create();
}

void page_wifi_connect_render(sensorhub_snapshot_t *snap)
{
    wifi_connect_native_render(snap);
}

void page_wifi_connect_destroy(void)
{
    wifi_connect_native_destroy();
}
