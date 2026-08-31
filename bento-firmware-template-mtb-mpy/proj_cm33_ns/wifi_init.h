/*******************************************************************************
 * File Name: wifi_init.h
 *
 * Description: WiFi SDIO + WCM initialization for CM33_NS.
 *              SDHC0 is PPC-protected for CM33_NS only.
 *              Lazy init: called on first wifi.scan() or wifi.connect().
 *
 * Author: TESAIoT
 *
 ******************************************************************************/

#ifndef WIFI_INIT_H
#define WIFI_INIT_H

#include "cy_result.h"
#include <stdbool.h>

/**
 * Initialize WiFi SDIO hardware and WiFi Connection Manager (WCM).
 * Includes: SDIO interrupt setup, SD Host init, GPIO setup (WL_REG_ON, HOST_WAKE),
 * and cy_wcm_init().
 *
 * Safe to call multiple times — returns immediately if already initialized.
 *
 * Must be called from a FreeRTOS task context (WCM creates internal threads).
 *
 * @return CY_RSLT_SUCCESS on success, error code on failure.
 */
cy_rslt_t app_wifi_init(void);

/**
 * Deinitialize WiFi (for soft reboot cleanup).
 * Calls cy_wcm_deinit().
 */
void app_wifi_deinit(void);

/**
 * Check if WiFi has been initialized.
 * @return true if app_wifi_init() has succeeded.
 */
bool app_wifi_is_ready(void);

/* --- radio_scheduler glue ------------------------------------------------
 * These wrappers expose cy_wcm + LFS creds to the shared radio_scheduler
 * library without forcing it to depend on the project's MTB headers.
 * They live here because wifi_init.c already pulls in cy_wcm headers.
 * ----------------------------------------------------------------------- */

/* Initialise WCM if needed, then call cy_wcm_connect_ap with 3 retries.
 * Returns 0 on success, non-zero (cy_rslt_t) on failure. */
int app_wifi_connect_direct(const char *ssid,
                            const char *password,
                            const char *security);

/* Tear down the active Wi-Fi link. Returns 0 on success. Safe to call
 * when not connected. */
int app_wifi_disconnect(void);

/* Return the current IPv4 in network byte order (0 if no link). */
unsigned int app_wifi_get_ipv4(void);

/* Save Wi-Fi credentials to LittleFS. Returns 0 on success, non-zero on
 * write failure. Existing creds for the same SSID are overwritten. */
int lfs_save_wifi_creds(const char *ssid,
                        const char *password,
                        const char *security);

/* Load saved credentials. Returns true if creds are present and non-empty,
 * filling the output buffers (NUL-terminated, truncated to caps - 1). */
#include <stddef.h>
bool lfs_load_wifi_creds(char *ssid_out, size_t ssid_cap,
                         char *pass_out, size_t pass_cap,
                         char *sec_out,  size_t sec_cap);

#endif /* WIFI_INIT_H */
