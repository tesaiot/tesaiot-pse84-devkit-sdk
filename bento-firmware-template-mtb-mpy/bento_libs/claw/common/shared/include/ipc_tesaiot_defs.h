/*******************************************************************************
 * File Name: ipc_tesaiot_defs.h
 *
 * Description: TESAIoT connectivity IPC definitions — shared between CM33_NS
 *              and CM55 cores. Defines status struct, deferred flags, and
 *              shared memory layout for the TESAIoT Connectivity page.
 *
 * Author:      Wiroon Sriborrirux (BDH)
 *
 ******************************************************************************/

#ifndef IPC_TESAIOT_DEFS_H
#define IPC_TESAIOT_DEFS_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Connectivity Mode Enum (shared between CM33_NS config store and CM55 UI)
 ******************************************************************************/
typedef enum {
    TESAIOT_MODE_MTLS       = 0,   /* mTLS + OPTIGA Trust M hardware keys */
    TESAIOT_MODE_MTLS_SW    = 1,   /* mTLS with software PEM cert/key */
    TESAIOT_MODE_SERVER_TLS = 2,   /* serverTLS + API key (username/password) */
    TESAIOT_MODE_HTTPS_API  = 3,   /* HTTPS REST API (APISIX key-auth) */
    TESAIOT_MODE_COUNT
} tesaiot_mode_t;

/*******************************************************************************
 * Config Struct (~660 bytes) — shared via CY_SECTION_SHAREDMEM.
 * CM33_NS writes under mutex, CM55 reads directly (read-only).
 ******************************************************************************/
typedef struct {
    /* Identity */
    tesaiot_mode_t tls_mode;
    char device_id[48];         /* serverTLS / HTTPS device_id */
    char mtls_device_id[48];    /* mTLS + Trust M device_id */
    char factory_uid[56];
    char api_key[96];           /* serverTLS + HTTPs gateway key (tesa_gwk_...) */
    char apisix_api_key[68];    /* RESTful API key (tesa_ak_...) */

    /* MQTT credentials (mode 2: serverTLS + MQTTs) */
    char     mqtt_pass[68];         /* MQTT password (separate from api_key) */

    /* MQTT (modes 0-2) */
    char     broker[64];
    uint16_t port;
    char     sni_hostname[64];
    uint8_t  qos;
    uint16_t keepalive;
    uint32_t timeout_ms;
    uint8_t  max_retries;
    uint32_t retry_interval_ms;
    uint16_t network_buffer;

    /* HTTPS (mode 3) */
    char     api_host[64];
    uint16_t api_port;
    char     api_endpoint[64];
    char     api_key_header[32];
    uint16_t http_buf_size;
    uint32_t http_send_timeout_ms;
    uint32_t http_recv_timeout_ms;

    /* WiFi */
    char wifi_ssid[33];
    char wifi_pass[65];
    char wifi_security[16];

    /* SNTP */
    char   sntp_server[64];
    int8_t sntp_timezone;

    /* Debug */
    uint8_t debug_level;

    /* Internal */
    uint32_t _version;
    uint32_t _checksum;
} tesaiot_config_t;

/*******************************************************************************
 * Connection State Enum
 ******************************************************************************/
typedef enum {
    TESAIOT_CONN_OFF        = 0,
    TESAIOT_CONN_CONNECTING = 1,
    TESAIOT_CONN_CONNECTED  = 2,
    TESAIOT_CONN_FAILED     = 3,
} tesaiot_conn_state_t;

/*******************************************************************************
 * OPTIGA State Enum
 ******************************************************************************/
typedef enum {
    TESAIOT_OPTIGA_NA          = 0,   /* Not present / not applicable */
    TESAIOT_OPTIGA_DETECTED    = 1,   /* Chip detected, not provisioned */
    TESAIOT_OPTIGA_PROVISIONED = 2,   /* Credentials loaded */
    TESAIOT_OPTIGA_FAILED      = 3,   /* Communication error */
} tesaiot_optiga_state_t;

/*******************************************************************************
 * Certificate State Enum
 ******************************************************************************/
typedef enum {
    TESAIOT_CERT_NONE    = 0,
    TESAIOT_CERT_LOADED  = 1,
    TESAIOT_CERT_VALID   = 2,
    TESAIOT_CERT_EXPIRED = 3,
} tesaiot_cert_state_t;

/*******************************************************************************
 * Deferred Flag Bitmask
 * CM33_NS sets flags, CM55 polls and clears in render_cb (33ms).
 ******************************************************************************/
#define TESAIOT_FLAG_WIFI_CHANGED   (1u << 0)
#define TESAIOT_FLAG_MQTT_CHANGED   (1u << 1)
#define TESAIOT_FLAG_HTTPS_CHANGED  (1u << 2)
#define TESAIOT_FLAG_CONFIG_CHANGED (1u << 3)
#define TESAIOT_FLAG_ERROR          (1u << 4)
#define TESAIOT_FLAG_NTP_CHANGED    (1u << 5)
#define TESAIOT_FLAG_OPTIGA_CHANGED (1u << 6)
#define TESAIOT_FLAG_CERT_CHANGED   (1u << 7)

/*******************************************************************************
 * Status Struct — lives in CY_SECTION_SHAREDMEM on CM33_NS side.
 * CM55 reads directly (single writer, no mutex needed).
 * ~96 bytes total.
 ******************************************************************************/
typedef struct {
    volatile uint8_t  wifi_state;       /* tesaiot_conn_state_t */
    volatile uint8_t  mqtt_state;       /* tesaiot_conn_state_t */
    volatile uint8_t  https_state;      /* tesaiot_conn_state_t */
    volatile uint8_t  ntp_state;        /* tesaiot_conn_state_t */
    volatile uint8_t  optiga_state;     /* tesaiot_optiga_state_t */
    volatile uint8_t  cert_state;       /* tesaiot_cert_state_t */
    volatile uint8_t  active_mode;      /* tesaiot_mode_t */
    volatile uint8_t  switching;        /* 1 = mode switch in progress */
    volatile uint32_t last_error;       /* Error code (0 = no error) */
    volatile uint32_t uptime_s;         /* Connection uptime in seconds */
    char              ip_addr[16];      /* WiFi IP address (null-terminated) */
    char              broker_url[64];   /* Current broker/host (null-terminated) */
    volatile uint32_t flags;            /* Deferred flag bitmask */

    /* Config display fields — populated from config store at init and on change.
     * Replaces tesaiot_config_view_t (removed from shared memory budget). */
    char              device_id[48];    /* Device ID (null-terminated) */
    uint16_t          port;             /* MQTT port or API port */
    char              factory_uid[56];  /* Trust M UID from OPTIGA (27 bytes = 54 hex chars + null) */

    /* Debug: password length for LCD display (0=empty for mTLS cert auth) */
    uint8_t           dbg_pass_len;
} tesaiot_status_t;

/*******************************************************************************
 * Config Display View — lightweight subset for CM55 UI display.
 * Only the fields page_tesaiot_connect.c actually reads.
 * ~180 bytes (vs ~660 for full tesaiot_config_t).
 * Lives in CY_SECTION_SHAREDMEM alongside tesaiot_status_t.
 ******************************************************************************/
typedef struct {
    tesaiot_mode_t tls_mode;
    char device_id[48];
    char broker[64];
    uint16_t port;
    char api_host[64];
    uint16_t api_port;
} tesaiot_config_view_t;

/*******************************************************************************
 * CM33 IPC Client ID for TESAIoT Config handler
 * (receives commands from CM55: mode switch, config get/set)
 ******************************************************************************/
#define CM33_IPC_TESAIOT_CFG_CLIENT_ID  (1UL)

#endif /* IPC_TESAIOT_DEFS_H */
