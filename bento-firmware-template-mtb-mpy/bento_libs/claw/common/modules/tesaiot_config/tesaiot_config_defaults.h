/*******************************************************************************
 * File Name: tesaiot_config_defaults.h
 *
 * Description: Default values for all TESAIoT connectivity config fields.
 *              Used by tesaiot_config_store.c when no config file exists
 *              or when resetting to factory defaults.
 *
 * Author:      Wiroon Sriborrirux (BDH)
 *
 ******************************************************************************/

#ifndef TESAIOT_CONFIG_DEFAULTS_H
#define TESAIOT_CONFIG_DEFAULTS_H

/*******************************************************************************
 * Identity Defaults
 ******************************************************************************/
#define TESAIOT_DEFAULT_TLS_MODE        TESAIOT_MODE_SERVER_TLS
#define TESAIOT_DEFAULT_DEVICE_ID       ""
#define TESAIOT_DEFAULT_MTLS_DEVICE_ID  ""
#define TESAIOT_DEFAULT_FACTORY_UID     ""
#define TESAIOT_DEFAULT_API_KEY         "REPLACE_WITH_YOUR_TESAIOT_GATEWAY_KEY"
#define TESAIOT_DEFAULT_APISIX_API_KEY  "REPLACE_WITH_YOUR_TESAIOT_API_KEY"
#define TESAIOT_DEFAULT_MQTT_PASS       "REPLACE_WITH_YOUR_MQTT_PASSWORD"

/*******************************************************************************
 * MQTT Defaults (Modes 0-2)
 ******************************************************************************/
/* mqtt.tesaiot.dev is different infrastructure from the old .com broker
 * (DigitalOcean vs an AWS NLB in ap-southeast-7), so the pinned trust anchor was
 * re-checked rather than assumed: the SHA-256 of the DER in tesaiot_root_ca.h is
 * e3ff5011703755b697227a17945837b7b43616a060d14b37995b62c24e0d7e98, identical to
 * the CA supplied for .dev. Same private PKI, so only the names change here.
 * Port stays mode-driven — mqtt_client_config.c picks 8883 for mTLS, 8884 for
 * server-TLS; this default only applies to the latter. */
#define TESAIOT_DEFAULT_BROKER          "mqtt.tesaiot.dev"
#define TESAIOT_DEFAULT_PORT            8884
#define TESAIOT_DEFAULT_SNI_HOSTNAME    "mqtt.tesaiot.dev"
#define TESAIOT_DEFAULT_QOS             1
#define TESAIOT_DEFAULT_KEEPALIVE       60
#define TESAIOT_DEFAULT_TIMEOUT_MS      10000
#define TESAIOT_DEFAULT_MAX_RETRIES     3
#define TESAIOT_DEFAULT_RETRY_INTERVAL  5000
#define TESAIOT_DEFAULT_NETWORK_BUFFER  4096

/*******************************************************************************
 * HTTPS Defaults (Mode 3)
 * Preferred: tesaiot.com:9444 (direct nginx, no SNI — best for MCU)
 * Fallback:  api.tesaiot.com:443 (via APISIX, needs SNI)
 * Endpoint:  /api/v1/devices/{device_id}/telemetry  (device-specific)
 *         or /api/v1/telemetry                       (generic alt)
 ******************************************************************************/
#define TESAIOT_DEFAULT_API_HOST        "api.tesaiot.com"
#define TESAIOT_DEFAULT_API_PORT        443
#define TESAIOT_DEFAULT_API_ENDPOINT    "/api/v1/telemetry"
#define TESAIOT_DEFAULT_API_KEY_HEADER  "X-API-Key"
#define TESAIOT_DEFAULT_HTTP_BUF_SIZE   2048
#define TESAIOT_DEFAULT_HTTP_SEND_TIMEOUT 10000
#define TESAIOT_DEFAULT_HTTP_RECV_TIMEOUT 10000

/*******************************************************************************
 * WiFi Defaults
 ******************************************************************************/
#define TESAIOT_DEFAULT_WIFI_SSID       ""
#define TESAIOT_DEFAULT_WIFI_PASS       ""
#define TESAIOT_DEFAULT_WIFI_SECURITY   "wpa2"

/*******************************************************************************
 * SNTP Defaults
 ******************************************************************************/
#define TESAIOT_DEFAULT_SNTP_SERVER     "pool.ntp.org"
#define TESAIOT_DEFAULT_SNTP_TIMEZONE   7   /* UTC+7 (Thailand) */

/*******************************************************************************
 * Debug
 ******************************************************************************/
#define TESAIOT_DEFAULT_DEBUG_LEVEL     1

/*******************************************************************************
 * Internal
 ******************************************************************************/
#define TESAIOT_CONFIG_VERSION          2
#define TESAIOT_CONFIG_FILE_PATH        "/.tesaiot_config"

#endif /* TESAIOT_CONFIG_DEFAULTS_H */
