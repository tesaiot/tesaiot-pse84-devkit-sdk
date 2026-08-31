/*******************************************************************************
 * File Name: wifi_init.c
 *
 * Description: WiFi SDIO + WCM initialization for CM33_NS.
 *              Adapted from PSoC-Edge-AI-TESAIoT-RTSP-Server/proj_cm33_ns/
 *              rtsp_server_task.c (app_sdio_init, lines 220-269).
 *
 *              SDHC0 is PPC-protected for CM33_NS only. This file provides
 *              lazy WiFi init — called from modwifi.c on first wifi use.
 *
 * Author: TESAIoT
 *
 ******************************************************************************/

#include "wifi_init.h"
#include "cybsp.h"
#include "cy_wcm.h"
#include "cy_sysint.h"
#include "mtb_hal_sdio.h"
#include "cy_sd_host.h"

#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Configuration
 ******************************************************************************/
#define WIFI_SDIO_INTERRUPT_PRIORITY      (7U)
#define WIFI_HOST_WAKE_INTERRUPT_PRIORITY  (2U)
#define WIFI_SDIO_FREQUENCY_HZ            (25000000U)

/*******************************************************************************
 * Private Variables
 ******************************************************************************/
static mtb_hal_sdio_t sdio_instance;
static cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;
static bool wifi_ready = false;

/*******************************************************************************
 * Interrupt Handlers
 ******************************************************************************/
static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

/*******************************************************************************
 * Function Name: app_wifi_init
 ******************************************************************************/
cy_rslt_t app_wifi_init(void)
{
    if (wifi_ready) {
        return CY_RSLT_SUCCESS;
    }

    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;

#ifdef BOOT_VERBOSE
    printf("[WiFi] Initializing SDIO...\r\n");
#endif

    /* SDIO interrupt */
    cy_stc_sysint_t sdio_intr_cfg = {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = WIFI_SDIO_INTERRUPT_PRIORITY,
    };

    /* Host wake interrupt */
    cy_stc_sysint_t host_wake_intr_cfg = {
        .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = WIFI_HOST_WAKE_INTERRUPT_PRIORITY,
    };

    /* Initialize SDIO interrupt */
    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&sdio_intr_cfg,
                                              sdio_interrupt_handler)) {
        printf("[WiFi] ERROR: SDIO interrupt init failed\r\n");
        return CY_RSLT_TYPE_ERROR;
    }
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    /* Setup SDIO HAL */
    result = mtb_hal_sdio_setup(&sdio_instance,
                                 &CYBSP_WIFI_SDIO_sdio_hal_config,
                                 NULL, &sdhc_host_context);
    if (CY_RSLT_SUCCESS != result) {
        printf("[WiFi] ERROR: SDIO setup failed (0x%08lX)\r\n",
               (unsigned long)result);
        return result;
    }

    /* Enable and initialize SD Host */
    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW,
                    CYBSP_WIFI_SDIO_sdio_hal_config.host_config,
                    &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);

    /* Configure SDIO frequency and block size */
    sdio_hal_cfg.frequencyhal_hz = WIFI_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size = 64U;  /* 64-byte SDIO block size */
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

    /* Setup WiFi GPIOs */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin,
                       CYBSP_WIFI_WL_REG_ON_PORT_NUM,
                       CYBSP_WIFI_WL_REG_ON_PIN);
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin,
                       CYBSP_WIFI_HOST_WAKE_PORT_NUM,
                       CYBSP_WIFI_HOST_WAKE_PIN);

    /* Initialize host wake interrupt */
    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&host_wake_intr_cfg,
                                              host_wake_interrupt_handler)) {
        printf("[WiFi] ERROR: Host wake interrupt init failed\r\n");
        return CY_RSLT_TYPE_ERROR;
    }
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);

#ifdef BOOT_VERBOSE
    printf("[WiFi] SDIO ready. Initializing WCM...\r\n");
#endif

    /* Configure WCM with SDIO instance */
    memset(&wcm_config, 0, sizeof(wcm_config));
    /* Re-setup GPIO after memset cleared them */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin,
                       CYBSP_WIFI_WL_REG_ON_PORT_NUM,
                       CYBSP_WIFI_WL_REG_ON_PIN);
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin,
                       CYBSP_WIFI_HOST_WAKE_PORT_NUM,
                       CYBSP_WIFI_HOST_WAKE_PIN);
    wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
    wcm_config.wifi_interface_instance = &sdio_instance;

    /* Initialize WiFi Connection Manager (creates WHD thread internally) */
    result = cy_wcm_init(&wcm_config);
    if (CY_RSLT_SUCCESS != result) {
        printf("[WiFi] ERROR: WCM init failed (0x%08lX)\r\n",
               (unsigned long)result);
        return result;
    }

    wifi_ready = true;
#ifdef BOOT_VERBOSE
    printf("[WiFi] Ready!\r\n");
#endif
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Function Name: app_wifi_deinit
 ******************************************************************************/
void app_wifi_deinit(void)
{
    if (wifi_ready) {
        cy_wcm_deinit();
        wifi_ready = false;
        printf("[WiFi] Deinitialized\r\n");
    }
}

/*******************************************************************************
 * Function Name: app_wifi_is_ready
 ******************************************************************************/
bool app_wifi_is_ready(void)
{
    return wifi_ready;
}

/* ============================================================================
 * radio_scheduler glue
 *
 * These wrappers let the shared radio_scheduler library drive cy_wcm + LFS
 * creds without including MTB headers itself. They live here because the
 * cy_wcm + littlefs includes are already paid for in this TU. modwifi.c
 * still keeps its own copy of the connect path for direct Python use; the
 * scheduler path is the desktop/LCD-driven alternative.
 * ============================================================================
 */
#include "wifi_creds_types.h"
#include "lfs_wifi_creds.h"

static cy_wcm_ip_address_t s_last_ip;

//! [ble_app_wifi_connect_direct_override]
int app_wifi_connect_direct(const char *ssid, const char *password, const char *security)
{
    cy_rslt_t r = app_wifi_init();
    if (CY_RSLT_SUCCESS != r) return (int)r;

    cy_wcm_connect_params_t params;
    memset(&params, 0, sizeof(params));
    memset(&s_last_ip, 0, sizeof(s_last_ip));

    size_t ssid_len = strlen(ssid);
    if (ssid_len > sizeof(params.ap_credentials.SSID) - 1)
        ssid_len = sizeof(params.ap_credentials.SSID) - 1;
    memcpy(params.ap_credentials.SSID, ssid, ssid_len);

    if (password != NULL) {
        size_t pass_len = strlen(password);
        if (pass_len > sizeof(params.ap_credentials.password) - 1)
            pass_len = sizeof(params.ap_credentials.password) - 1;
        memcpy(params.ap_credentials.password, password, pass_len);
    }

    /* Security mapping. Empty password → OPEN; explicit "WPA3" / "OPEN"
     * supported per Desktop Buddy's wifi.connect verb. Default WPA2. */
    if (security != NULL && strcmp(security, "WPA3") == 0) {
        params.ap_credentials.security = CY_WCM_SECURITY_WPA3_SAE;
    } else if ((security != NULL && strcmp(security, "OPEN") == 0)
               || params.ap_credentials.password[0] == '\0') {
        params.ap_credentials.security = CY_WCM_SECURITY_OPEN;
    } else {
        params.ap_credentials.security = CY_WCM_SECURITY_WPA2_AES_PSK;
    }

    //! [ble_wifi_glue_retry_observables]
    /* ...context: inside app_wifi_connect_direct() ... */
    printf("[wifi-glue] connecting SSID=%s sec=%d\r\n",
           params.ap_credentials.SSID, (int)params.ap_credentials.security);

    /* Retry 3x with a 2s backoff — matches the modwifi.c policy and gives
     * the AP time to ARM during a freshly-booted router. */
    for (int attempt = 1; attempt <= 3; attempt++) {
        r = cy_wcm_connect_ap(&params, &s_last_ip);
        if (CY_RSLT_SUCCESS == r) {
            uint32_t v4 = s_last_ip.ip.v4;
            printf("[wifi-glue] connected, IP=%lu.%lu.%lu.%lu\r\n",
                   (unsigned long)(v4 & 0xFF),
                   (unsigned long)((v4 >> 8) & 0xFF),
                   (unsigned long)((v4 >> 16) & 0xFF),
                   (unsigned long)((v4 >> 24) & 0xFF));
            return 0;
        }
        printf("[wifi-glue] attempt %d/3 failed (0x%08lX)\r\n",
               attempt, (unsigned long)r);
               //! [ble_wifi_glue_retry_observables]
        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return (int)r;
}
//! [ble_app_wifi_connect_direct_override]

//! [ble_app_wifi_disconnect_override]
int app_wifi_disconnect(void)
{
    if (!wifi_ready) return 0;
    cy_rslt_t r = cy_wcm_disconnect_ap();
    memset(&s_last_ip, 0, sizeof(s_last_ip));
    return (CY_RSLT_SUCCESS == r) ? 0 : (int)r;
}
//! [ble_app_wifi_disconnect_override]

//! [ble_app_wifi_get_ipv4_override]
unsigned int app_wifi_get_ipv4(void)
{
    return (unsigned int)s_last_ip.ip.v4;
}
//! [ble_app_wifi_get_ipv4_override]

//! [ble_lfs_save_wifi_creds_override]
int lfs_save_wifi_creds(const char *ssid, const char *password, const char *security)
{
    if (!lfs_wifi_creds_ready()) return -1;

    qspi_wifi_entry_t saved[QSPI_WIFI_CREDS_MAX];
    int count = lfs_wifi_creds_read(saved, QSPI_WIFI_CREDS_MAX);
    if (count < 0) count = 0;

    /* Upsert: find existing slot for this SSID, else append (LRU eviction
     * when full). */
    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (strncmp(saved[i].ssid, ssid, sizeof(saved[i].ssid)) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        idx = (count < QSPI_WIFI_CREDS_MAX) ? count : 0;
        if (count < QSPI_WIFI_CREDS_MAX) count++;
    }

    memset(&saved[idx], 0, sizeof(qspi_wifi_entry_t));
    strncpy(saved[idx].ssid, ssid, sizeof(saved[idx].ssid) - 1);
    if (password != NULL) {
        strncpy(saved[idx].password, password, sizeof(saved[idx].password) - 1);
    }
    /* security field is uint8_t — store the low byte of the cy_wcm enum so
     * we stay binary-compatible with modwifi.c's saved entries (which also
     * writes the truncated enum). The low byte distinguishes the common
     * cases: 0=OPEN, 4=WPA2_AES (also matches WPA3_SAE's low byte — WPA3
     * is treated as WPA2 on the load path for now; explicit WPA3 storage
     * requires bumping the struct format). */
    bool is_open = (security != NULL && strcmp(security, "OPEN") == 0)
                   || (password == NULL || password[0] == '\0');
    saved[idx].security = is_open
        ? (uint8_t)(CY_WCM_SECURITY_OPEN & 0xFFu)
        : (uint8_t)(CY_WCM_SECURITY_WPA2_AES_PSK & 0xFFu);
    saved[idx].flags = 0x01; /* auto_connect */

    return lfs_wifi_creds_write(saved, count) ? 0 : -2;
}
//! [ble_lfs_save_wifi_creds_override]

//! [ble_lfs_load_wifi_creds_override]
bool lfs_load_wifi_creds(char *ssid_out, size_t ssid_cap,
                         char *pass_out, size_t pass_cap,
                         char *sec_out,  size_t sec_cap)
{
    if (!lfs_wifi_creds_ready()) return false;
    qspi_wifi_entry_t entries[QSPI_WIFI_CREDS_MAX];
    int count = lfs_wifi_creds_read(entries, QSPI_WIFI_CREDS_MAX);
    if (count <= 0) return false;

    /* Pick the most recently saved (last entry) for now — future work
     * could try each in order until one connects. */
    const qspi_wifi_entry_t *pick = &entries[count - 1];
    if (pick->ssid[0] == '\0') return false;

    if (ssid_out && ssid_cap > 0) {
        strncpy(ssid_out, pick->ssid, ssid_cap - 1);
        ssid_out[ssid_cap - 1] = '\0';
    }
    if (pass_out && pass_cap > 0) {
        strncpy(pass_out, pick->password, pass_cap - 1);
        pass_out[pass_cap - 1] = '\0';
    }
    if (sec_out && sec_cap > 0) {
        /* security is the truncated low byte of the cy_wcm enum. 0=OPEN
         * is the only case we need to distinguish on load; everything
         * else maps to WPA2 (WPA3 storage isn't supported yet). */
        const char *s = (pick->security == (uint8_t)(CY_WCM_SECURITY_OPEN & 0xFFu))
                        ? "OPEN" : "WPA2";
        strncpy(sec_out, s, sec_cap - 1);
        sec_out[sec_cap - 1] = '\0';
    }
    return true;
}
//! [ble_lfs_load_wifi_creds_override]
