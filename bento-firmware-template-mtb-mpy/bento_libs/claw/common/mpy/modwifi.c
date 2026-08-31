/*******************************************************************************
 * File Name: modwifi.c
 *
 * Description: MicroPython 'wifi' module for CM33_NS.
 *              Direct WCM calls (no IPC proxy — WiFi stack runs locally).
 *
 *              v3.0: Rewritten from IPC proxy to direct cy_wcm_* calls.
 *              SDHC0 is PPC-protected for CM33_NS, so WiFi runs here directly.
 *              Lazy init: SDIO + WCM initialized on first wifi use.
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objlist.h"
#include "py/mphal.h"
#include "cy_wcm.h"
#include "wifi_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "sensor_auto_task.h"
#include "lfs_wifi_creds.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * WiFi Scan Configuration
 *******************************************************************************/
#define WIFI_SCAN_MAX_RESULTS   (20)
#define WIFI_SCAN_TIMEOUT_MS    (10000)
/* How long scan() is willing to wait for the board's own boot scan to finish
 * before giving up. The boot scan covers 2.4 and 5 GHz and takes up to 8 s on
 * this board (the 5 GHz band has far more channels), so the ceiling has to sit
 * above that with room to spare. */
#define WIFI_SCAN_BUSY_WAIT_MS  (10000)
#define WIFI_SCAN_BUSY_POLL_MS  (200)

typedef struct {
    cy_wcm_scan_result_t results[WIFI_SCAN_MAX_RESULTS];
    uint16_t count;
    SemaphoreHandle_t done;
} scan_context_t;

/*******************************************************************************
 * Helper: Ensure WiFi is initialized (lazy init)
 *******************************************************************************/
static void ensure_wifi_initialized(void) {
    if (app_wifi_is_ready()) return;

//! [ble_radio_scheduler_single_rf_guard]
/* ...context: inside the modwifi lazy-init path ... */
#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1) \
    && !(defined(BENTO_HAS_DUAL_BAND) && (BENTO_HAS_DUAL_BAND == 1))
    /* BLE-only Bento Buddy variant: this build does not set up the
     * COEX coexistence arbiter, so starting WiFi while BLE is
     * advertising would cause RF contention, dropped notifications, and
     * eventually a HardFault inside WHD. Refuse the call from Python.
     *
     * The dual-band TinyPython variant defines BENTO_HAS_DUAL_BAND=1 and
     * relies on the on-die COEX block of CYW55513 to time-slice WiFi+BLE
     * — the guard is skipped there.
     *
     * Permitted modes: the radio scheduler must currently be in
     * WIFI_ACTIVE or SWITCHING_TO_WIFI (Desktop Buddy / boot-path
     * already drove it that way) — otherwise the BLE adv is live and we
     * must refuse. Direct REPL calls from a notebook are still rejected
     * with a clear message pointing the user at the proper switch flow. */
    #include "../ble_nus/radio_scheduler.h"
    radio_mode_t rm = radio_scheduler_get_mode();
    bool wifi_permitted = (rm == RADIO_MODE_WIFI_ACTIVE
                           || rm == RADIO_MODE_SWITCHING_TO_WIFI
                           || rm == RADIO_MODE_WIFI_FAILED  /* retry path */);
    if (!wifi_permitted) {
        mp_raise_msg(&mp_type_OSError,
            MP_ERROR_TEXT("WiFi unavailable: BLE is active. Use the "
                          "Desktop Buddy or call bento_buddy.stop() "
                          "to switch radio mode first."));
    }
#endif
//! [ble_radio_scheduler_single_rf_guard]

    cy_rslt_t r = app_wifi_init();
    if (CY_RSLT_SUCCESS != r) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("WiFi init failed (0x%08lX)"),
            (unsigned long)r);
    }
}

/*******************************************************************************
 * Scan callback (called by WCM from WHD thread context)
 *******************************************************************************/
static void scan_callback(cy_wcm_scan_result_t *result,
                           void *user_data,
                           cy_wcm_scan_status_t status) {
    scan_context_t *ctx = (scan_context_t *)user_data;

    if (status == CY_WCM_SCAN_INCOMPLETE && ctx->count < WIFI_SCAN_MAX_RESULTS) {
        /* Filter out empty SSIDs */
        if (result->SSID[0] != '\0') {
            memcpy(&ctx->results[ctx->count], result,
                   sizeof(cy_wcm_scan_result_t));
            ctx->count++;
        }
    }

    if (status == CY_WCM_SCAN_COMPLETE) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(ctx->done, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/*******************************************************************************
 * wifi.scan() -> [(ssid, rssi, security, channel), ...]
 *******************************************************************************/
static mp_obj_t wifi_scan(void) {
    ensure_wifi_initialized();

    scan_context_t ctx;
    ctx.count = 0;
    ctx.done = xSemaphoreCreateBinary();
    if (ctx.done == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to create semaphore"));
    }

    /* Wait out a scan that is already running, rather than failing on it.
     *
     * The board scans on its own at boot to re-join a known network, and that
     * scan is still in flight when a freshly started program reaches its first
     * wifi.scan(). WCM answers CY_RSLT_WCM_SCAN_IN_PROGRESS and the call used to
     * raise 0x082E000E straight at the learner - a hex number that says nothing
     * and that they can do nothing about, on the first line of the lesson.
     * Measured 2026-08-14: four course files failed this way, every one of them
     * on its first scan after a hard reset.
     *
     * Only one scan can run at a time, so the honest thing is to wait for the
     * other one and then ask again. The wait is bounded; if the radio really is
     * stuck the error that comes out says so in words. */
    cy_rslt_t r = cy_wcm_start_scan(scan_callback, &ctx, NULL);
    uint32_t waited_ms = 0;
    while (r == CY_RSLT_WCM_SCAN_IN_PROGRESS && waited_ms < WIFI_SCAN_BUSY_WAIT_MS) {
        mp_hal_delay_ms(WIFI_SCAN_BUSY_POLL_MS);
        waited_ms += WIFI_SCAN_BUSY_POLL_MS;
        ctx.count = 0;
        r = cy_wcm_start_scan(scan_callback, &ctx, NULL);
    }
    if (CY_RSLT_SUCCESS != r) {
        vSemaphoreDelete(ctx.done);
        if (r == CY_RSLT_WCM_SCAN_IN_PROGRESS) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(
                "WiFi busy: another scan has been running for over 10 seconds"));
        }
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("WiFi scan start failed (0x%08lX)"),
            (unsigned long)r);
    }

    /* Block until scan completes or timeout */
    if (xSemaphoreTake(ctx.done, pdMS_TO_TICKS(WIFI_SCAN_TIMEOUT_MS)) != pdTRUE) {
        cy_wcm_stop_scan();
        vSemaphoreDelete(ctx.done);
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("WiFi scan timeout"));
    }
    vSemaphoreDelete(ctx.done);

    /* Build Python list of tuples */
    mp_obj_list_t *list = MP_OBJ_TO_PTR(mp_obj_new_list(0, NULL));
    for (uint16_t i = 0; i < ctx.count; i++) {
        mp_obj_t tuple[4] = {
            mp_obj_new_str((const char *)ctx.results[i].SSID,
                           strlen((const char *)ctx.results[i].SSID)),
            mp_obj_new_int(ctx.results[i].signal_strength),
            mp_obj_new_int(ctx.results[i].security),
            mp_obj_new_int(ctx.results[i].channel),
        };
        mp_obj_list_append(MP_OBJ_FROM_PTR(list), mp_obj_new_tuple(4, tuple));
    }

    return MP_OBJ_FROM_PTR(list);
}
static MP_DEFINE_CONST_FUN_OBJ_0(wifi_scan_obj, wifi_scan);

/* Robust WCM connect — rides out the marginal COLD first-join. Each
 * cy_wcm_connect_ap() is a fresh ~13s WHD attempt that tears its half-join down on
 * failure, so re-issuing is the only recovery. On a cold boot the CYW55500 RX
 * buffer pool (14 x ~2KB) is malloc'd from the ~81KB shared heap during the
 * association burst; a momentary buffer shortfall drops the WLC_E_SET_SSID frame
 * → JOIN_SSID_SET unset → WHD_JOIN_IN_PROGRESS (0x020003FF). disconnect + settle
 * between attempts clears the WHD FSM for a clean retry. Warm reconnect succeeds
 * first try. (Mirror of the helper in sensor_auto_task.c.) */
#define WIFI_CONNECT_MAX_ATTEMPTS   (6)
#define WIFI_CONNECT_SETTLE_MS      (1500)

static cy_rslt_t wifi_connect_robust(cy_wcm_connect_params_t *params,
                                     cy_wcm_ip_address_t *ip) {
    cy_rslt_t r = CY_RSLT_TYPE_ERROR;
    for (int attempt = 1; attempt <= WIFI_CONNECT_MAX_ATTEMPTS; attempt++) {
        r = cy_wcm_connect_ap(params, ip);
        if (CY_RSLT_SUCCESS == r || cy_wcm_is_connected_to_ap()) {
            return CY_RSLT_SUCCESS;
        }
        printf("[WiFi] attempt %d/%d failed (0x%08lX)\r\n",
               attempt, WIFI_CONNECT_MAX_ATTEMPTS, (unsigned long)r);
        if (attempt < WIFI_CONNECT_MAX_ATTEMPTS) {
            cy_wcm_disconnect_ap();
            vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_SETTLE_MS));
        }
    }
    return r;
}

/*******************************************************************************
 * wifi.connect(ssid, password) -> True/False
 *******************************************************************************/
static mp_obj_t wifi_connect(mp_obj_t ssid_obj, mp_obj_t pass_obj) {
    ensure_wifi_initialized();

    size_t ssid_len, pass_len;
    const char *ssid = mp_obj_str_get_data(ssid_obj, &ssid_len);
    const char *pass = mp_obj_str_get_data(pass_obj, &pass_len);

    cy_wcm_connect_params_t params;
    cy_wcm_ip_address_t ip;
    memset(&params, 0, sizeof(params));
    memset(&ip, 0, sizeof(ip));

    if (ssid_len > sizeof(params.ap_credentials.SSID) - 1) {
        ssid_len = sizeof(params.ap_credentials.SSID) - 1;
    }
    if (pass_len > sizeof(params.ap_credentials.password) - 1) {
        pass_len = sizeof(params.ap_credentials.password) - 1;
    }
    memcpy(params.ap_credentials.SSID, ssid, ssid_len);
    memcpy(params.ap_credentials.password, pass, pass_len);
    /* WPA3_WPA2_PSK (SAE + PMF) — WPA2-only fails on a WPA3/WPA2-mixed AP that
     * requires PMF (join reaches SECURITY_COMPLETE but never SSID_SET → 0x020003FF);
     * backward-compatible with pure WPA2 APs. */
    params.ap_credentials.security = CY_WCM_SECURITY_WPA3_WPA2_PSK;

    printf("[WiFi] Connecting to '%s'...\r\n", params.ap_credentials.SSID);

    cy_rslt_t r = wifi_connect_robust(&params, &ip);
    if (CY_RSLT_SUCCESS != r) {
        printf("[WiFi] Connect failed after %d attempts (last 0x%08lX)\r\n",
               WIFI_CONNECT_MAX_ATTEMPTS, (unsigned long)r);
        return mp_const_false;
    }

    uint32_t v4 = ip.ip.v4;
    printf("[WiFi] Connected! IP=%lu.%lu.%lu.%lu\r\n",
           (unsigned long)(v4 & 0xFF),
           (unsigned long)((v4 >> 8) & 0xFF),
           (unsigned long)((v4 >> 16) & 0xFF),
           (unsigned long)((v4 >> 24) & 0xFF));

    /* Push WiFi state + NTP sync + time to CM55 topbar */
    sensor_auto_push_wifi_state(true);
    sensor_auto_ntp_and_push_time();

    /* Save credentials to QSPI for boot auto-connect */
    if (lfs_wifi_creds_ready()) {
        /* Read existing entries, check if already saved */
        qspi_wifi_entry_t saved[QSPI_WIFI_CREDS_MAX];
        int count = lfs_wifi_creds_read(saved, QSPI_WIFI_CREDS_MAX);
        bool found = false;
        for (int i = 0; i < count; i++) {
            if (strncmp(saved[i].ssid, (const char *)params.ap_credentials.SSID, 32) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            /* Add new entry */
            int idx = (count < QSPI_WIFI_CREDS_MAX) ? count : 0;
            memset(&saved[idx], 0, sizeof(qspi_wifi_entry_t));
            strncpy(saved[idx].ssid, (const char *)params.ap_credentials.SSID, 32);
            strncpy(saved[idx].password, (const char *)params.ap_credentials.password, 64);
            saved[idx].security = CY_WCM_SECURITY_WPA2_AES_PSK;
            saved[idx].flags = 0x01;  /* auto_connect */
            int new_count = (count < QSPI_WIFI_CREDS_MAX) ? count + 1 : QSPI_WIFI_CREDS_MAX;
            if (lfs_wifi_creds_write(saved, new_count)) {
                printf("[WiFi] Credentials saved to QSPI (%d entries)\r\n", new_count);
            }
        }
    }

    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_2(wifi_connect_obj, wifi_connect);

/*******************************************************************************
 * wifi.disconnect()
 *******************************************************************************/
static mp_obj_t wifi_disconnect(void) {
    if (!app_wifi_is_ready()) return mp_const_none;
    cy_wcm_disconnect_ap();
    sensor_auto_push_wifi_state(false);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(wifi_disconnect_obj, wifi_disconnect);

/*******************************************************************************
 * wifi.status() -> dict { 'mode', 'connected', 'ip', 'ssid', 'rssi' }
 *******************************************************************************/
static mp_obj_t wifi_status(void) {
    mp_obj_dict_t *result = MP_OBJ_TO_PTR(mp_obj_new_dict(5));

    bool ready = app_wifi_is_ready();
    bool connected = ready ? cy_wcm_is_connected_to_ap() : false;

    const char *mode_str = ready ? (connected ? "sta" : "idle") : "off";
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_mode),
        mp_obj_new_str(mode_str, strlen(mode_str)));

    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_connected),
        mp_obj_new_bool(connected));

    /* Get IP address */
    char ip_buf[16] = "0.0.0.0";
    if (connected) {
        cy_wcm_ip_address_t ip;
        if (CY_RSLT_SUCCESS == cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_STA, &ip)) {
            uint32_t v4 = ip.ip.v4;
            snprintf(ip_buf, sizeof(ip_buf), "%lu.%lu.%lu.%lu",
                     (unsigned long)(v4 & 0xFF),
                     (unsigned long)((v4 >> 8) & 0xFF),
                     (unsigned long)((v4 >> 16) & 0xFF),
                     (unsigned long)((v4 >> 24) & 0xFF));
        }
    }
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_ip),
        mp_obj_new_str(ip_buf, strlen(ip_buf)));

    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_ssid),
        mp_obj_new_str("", 0));

    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_rssi),
        mp_obj_new_int(0));

    return MP_OBJ_FROM_PTR(result);
}
static MP_DEFINE_CONST_FUN_OBJ_0(wifi_status_obj, wifi_status);

/*******************************************************************************
 * wifi.is_connected() -> bool
 *******************************************************************************/
static mp_obj_t wifi_is_connected(void) {
    if (!app_wifi_is_ready()) return mp_const_false;
    return mp_obj_new_bool(cy_wcm_is_connected_to_ap());
}
static MP_DEFINE_CONST_FUN_OBJ_0(wifi_is_connected_obj, wifi_is_connected);

/*******************************************************************************
 * wifi.ip() -> str
 *******************************************************************************/
static mp_obj_t wifi_ip(void) {
    if (!app_wifi_is_ready()) {
        return mp_obj_new_str("0.0.0.0", 7);
    }

    cy_wcm_ip_address_t ip;
    cy_rslt_t r = cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_STA, &ip);
    if (CY_RSLT_SUCCESS != r) {
        r = cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_AP, &ip);
    }
    if (CY_RSLT_SUCCESS != r) {
        return mp_obj_new_str("0.0.0.0", 7);
    }

    char buf[16];
    uint32_t v4 = ip.ip.v4;
    snprintf(buf, sizeof(buf), "%lu.%lu.%lu.%lu",
             (unsigned long)(v4 & 0xFF),
             (unsigned long)((v4 >> 8) & 0xFF),
             (unsigned long)((v4 >> 16) & 0xFF),
             (unsigned long)((v4 >> 24) & 0xFF));

    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_0(wifi_ip_obj, wifi_ip);

/*******************************************************************************
 * wifi.ping(host, timeout=5000) -> int (ms) or -1 on failure
 *******************************************************************************/
static mp_obj_t wifi_ping(size_t n_args, const mp_obj_t *args) {
    ensure_wifi_initialized();

    if (!cy_wcm_is_connected_to_ap()) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("WiFi not connected"));
    }

    size_t host_len;
    const char *host = mp_obj_str_get_data(args[0], &host_len);

    uint32_t timeout_ms = 5000;
    if (n_args >= 2) {
        timeout_ms = (uint32_t)mp_obj_get_int(args[1]);
    }

    /* Parse IP address from dotted-decimal string */
    cy_wcm_ip_address_t ip_addr;
    memset(&ip_addr, 0, sizeof(ip_addr));
    ip_addr.version = CY_WCM_IP_VER_V4;

    unsigned int a, b, c, d;
    if (sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        ip_addr.ip.v4 = (uint32_t)a | ((uint32_t)b << 8) |
                         ((uint32_t)c << 16) | ((uint32_t)d << 24);
    } else {
        /* Hostname: try DNS resolution via cy_wcm internal */
        cy_rslt_t dns_r = cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_STA, &ip_addr);
        (void)dns_r;
        /* cy_wcm_ping requires a direct IP; for hostnames, resolve first.
         * cy_wcm does not expose DNS, so for now only IPs are supported. */
        mp_raise_msg(&mp_type_ValueError,
            MP_ERROR_TEXT("ping requires IP address (e.g. '8.8.8.8')"));
    }

    uint32_t elapsed_ms = 0;
    cy_rslt_t r = cy_wcm_ping(CY_WCM_INTERFACE_TYPE_STA, &ip_addr,
                                timeout_ms, &elapsed_ms);
    if (r != CY_RSLT_SUCCESS) {
        return mp_obj_new_int(-1);
    }

    return mp_obj_new_int((mp_int_t)elapsed_ms);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wifi_ping_obj, 1, 2, wifi_ping);

/*******************************************************************************
 * wifi.softap(ssid, password) -> bool
 *******************************************************************************/
static mp_obj_t wifi_softap(size_t n_args, const mp_obj_t *args) {
    ensure_wifi_initialized();

    cy_wcm_ap_config_t ap_conf;
    memset(&ap_conf, 0, sizeof(ap_conf));

    /* Default SSID */
    const char *ssid = "PSoC-Edge-MPY";
    size_t ssid_len = strlen(ssid);
    const char *pass = "micropython";
    size_t pass_len = strlen(pass);

    if (n_args >= 1) {
        ssid = mp_obj_str_get_data(args[0], &ssid_len);
    }
    if (n_args >= 2) {
        pass = mp_obj_str_get_data(args[1], &pass_len);
    }

    if (ssid_len > sizeof(ap_conf.ap_credentials.SSID) - 1) {
        ssid_len = sizeof(ap_conf.ap_credentials.SSID) - 1;
    }
    if (pass_len > sizeof(ap_conf.ap_credentials.password) - 1) {
        pass_len = sizeof(ap_conf.ap_credentials.password) - 1;
    }

    memcpy(ap_conf.ap_credentials.SSID, ssid, ssid_len);
    memcpy(ap_conf.ap_credentials.password, pass, pass_len);
    ap_conf.ap_credentials.security = CY_WCM_SECURITY_WPA2_AES_PSK;
    ap_conf.channel = 1;

    /* SoftAP IP settings */
    ap_conf.ip_settings.ip_address.version = CY_WCM_IP_VER_V4;
    ap_conf.ip_settings.ip_address.ip.v4 = 0x0104A8C0;  /* 192.168.4.1 */
    ap_conf.ip_settings.netmask.version = CY_WCM_IP_VER_V4;
    ap_conf.ip_settings.netmask.ip.v4 = 0x00FFFFFF;      /* 255.255.255.0 */
    ap_conf.ip_settings.gateway.version = CY_WCM_IP_VER_V4;
    ap_conf.ip_settings.gateway.ip.v4 = 0x0104A8C0;      /* 192.168.4.1 */

    printf("[WiFi] Starting SoftAP '%s'...\r\n", ap_conf.ap_credentials.SSID);

    cy_rslt_t r = cy_wcm_start_ap(&ap_conf);
    if (CY_RSLT_SUCCESS != r) {
        printf("[WiFi] SoftAP failed (0x%08lX)\r\n", (unsigned long)r);
        return mp_const_false;
    }

    printf("[WiFi] SoftAP ready @ 192.168.4.1\r\n");
    sensor_auto_push_wifi_state(true);
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wifi_softap_obj, 0, 2, wifi_softap);

/*******************************************************************************
 * Module Definition
 *******************************************************************************/
static const mp_rom_map_elem_t wifi_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_wifi) },
    { MP_ROM_QSTR(MP_QSTR_scan),           MP_ROM_PTR(&wifi_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect),        MP_ROM_PTR(&wifi_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_disconnect),     MP_ROM_PTR(&wifi_disconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_status),         MP_ROM_PTR(&wifi_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_connected),   MP_ROM_PTR(&wifi_is_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_ip),             MP_ROM_PTR(&wifi_ip_obj) },
    { MP_ROM_QSTR(MP_QSTR_ping),           MP_ROM_PTR(&wifi_ping_obj) },
    { MP_ROM_QSTR(MP_QSTR_softap),         MP_ROM_PTR(&wifi_softap_obj) },
};
static MP_DEFINE_CONST_DICT(wifi_module_globals, wifi_module_globals_table);

const mp_obj_module_t mp_module_wifi = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&wifi_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_wifi, mp_module_wifi);
