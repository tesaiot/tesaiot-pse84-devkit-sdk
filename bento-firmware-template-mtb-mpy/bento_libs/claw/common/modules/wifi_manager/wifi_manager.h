/*******************************************************************************
 * File Name: wifi_manager.h
 *
 * Description: WiFi manager for CM55. Handles SoftAP and STA modes
 *              using Cypress WiFi Connection Manager (WCM).
 *              Runs as a FreeRTOS task.
 *
 *******************************************************************************/

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "cy_result.h"

/* Max entries per scan response (bounded by IPC payload budget). */
#define WIFI_MGR_SCAN_MAX_ENTRIES  (6)

/* WiFi scan entry (portable copy of IPC payload schema). */
typedef struct {
    char ssid[33];              /* SSID (null-terminated, max 32 chars) */
    int8_t rssi;                /* Signal strength dBm */
    uint8_t security;           /* 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3 */
    uint8_t channel;            /* WiFi channel */
} wifi_mgr_scan_entry_t;

/* WiFi operating modes */
typedef enum {
    WIFI_MGR_MODE_NONE = 0,
    WIFI_MGR_MODE_SOFTAP,
    WIFI_MGR_MODE_STA,
} wifi_mgr_mode_t;

/* WiFi status */
typedef struct {
    wifi_mgr_mode_t mode;
    bool connected;
    char ip_addr[16];
    char ssid[33];
    int8_t rssi;
    uint8_t mac_addr[6];
} wifi_mgr_status_t;

/**
 * Initialize WiFi manager. Creates the WiFi task.
 * @return true on success.
 */
bool wifi_manager_init(void);

/**
 * Start WiFi SoftAP with default SSID/password from wifi_config.h.
 * @return true on success.
 */
bool wifi_manager_start_softap(void);

/**
 * Connect to a WiFi network in STA mode.
 * @param ssid     Network SSID (null-terminated).
 * @param password Network password (null-terminated).
 * @return true on successful connection.
 */
bool wifi_manager_connect(const char *ssid, const char *password);

/**
 * Perform a scan and return up to max_entries networks (BLOCKING).
 * @param out         Output array (may be NULL if max_entries is 0).
 * @param max_entries Capacity of out[].
 * @return number of copied entries on success, -1 on error.
 */
int wifi_manager_scan(wifi_mgr_scan_entry_t *out, size_t max_entries);

/**
 * Non-blocking scan API — use from LVGL callbacks to avoid blocking GFX task.
 *
 * Usage pattern:
 *   1. wifi_manager_scan_start()   → send IPC, return immediately
 *   2. wifi_manager_scan_ready()   → poll periodically (100ms timer)
 *   3. wifi_manager_scan_result()  → get results after ready==true
 */
bool wifi_manager_scan_start(void);
bool wifi_manager_scan_ready(void);
int  wifi_manager_scan_result(wifi_mgr_scan_entry_t *out, size_t max_entries);

/**
 * Non-blocking status API — use from LVGL callbacks to avoid blocking GFX task.
 *
 * Usage pattern:
 *   1. wifi_manager_status_start()   → send IPC, return immediately
 *   2. wifi_manager_status_ready()   → poll periodically (100ms timer)
 *   3. wifi_manager_status_result()  → get results after ready==true
 */
bool wifi_manager_status_start(void);
bool wifi_manager_status_ready(void);
bool wifi_manager_status_result(wifi_mgr_status_t *status);

/**
 * Disconnect from current WiFi network.
 */
void wifi_manager_disconnect(void);

/**
 * Get current WiFi status.
 * @param status  Output status struct.
 */
void wifi_manager_get_status(wifi_mgr_status_t *status);

/**
 * Check if WiFi is connected (SoftAP running or STA connected).
 */
bool wifi_manager_is_connected(void);

/**
 * Get the current IP address string.
 * @return IP address string or "0.0.0.0" if not connected.
 */
const char *wifi_manager_get_ip(void);

/**
 * Get the last cy_rslt_t error code from wifi_manager operations.
 */
cy_rslt_t wifi_manager_last_error(void);

#endif /* WIFI_MANAGER_H */
