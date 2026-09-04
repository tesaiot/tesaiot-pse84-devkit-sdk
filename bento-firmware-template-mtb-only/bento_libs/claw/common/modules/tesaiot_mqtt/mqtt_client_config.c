/******************************************************************************
* File Name:   mqtt_client_config.c
*
* Description: Runtime MQTT client configuration for BENTO BentoClaw.
*              Reads all parameters from tesaiot_config_store (LittleFS).
*              Replaces compile-time #define approach from reference.
*
* Based on: pse84_tesaiot_client mqtt_client_config.c
******************************************************************************/

#include <stdio.h>
#include <string.h>

/* TEMP-DIAG: mute lifted to read the MQTT connect errors */
// #define printf(...) ((void)0)   /* UART contention fix - see mqtt_task.c */

#include "mqtt_client_config.h"
#include "tesaiot_config_store.h"
#include "tesaiot_root_ca.h"
/* Phase G: mTLS setup — weak link, only available when mqtt_mtls_setup.c is compiled */
extern bool mqtt_mtls_setup_optiga(void) __attribute__((weak));

/******************************************************************************
* Static Buffers — persist for lifetime of MQTT connection
******************************************************************************/

/* Broker hostname buffer (read from config at runtime) */
static char s_broker_hostname[64];

/* SNI hostname buffer */
static char s_sni_hostname[64];

/* Client identifier buffer */
static char s_client_id[MQTT_CLIENT_IDENTIFIER_MAX_LEN + 1];

/* Username buffer */
static char s_username[64];

/* Password/API key buffer */
static char s_password[128];

/******************************************************************************
* Global Variables
******************************************************************************/

/* MQTT Broker/Server details — populated by mqtt_client_config_init() */
cy_mqtt_broker_info_t broker_info = {0};

/* TLS credentials (always secure for TESAIoT) */
static cy_awsport_ssl_credentials_t credentials = {
    /* Root CA is static — same for all connections */
    .root_ca = (const char *)TESAIOT_ROOT_CA_CERTIFICATE,
    .root_ca_size = sizeof(TESAIOT_ROOT_CA_CERTIFICATE),

    /* Client cert: NULL for serverTLS mode, populated for mTLS */
    .client_cert = NULL,
    .client_cert_size = 0,

    /* Private key: NULL for serverTLS mode, populated for mTLS */
    .private_key = NULL,
    .private_key_size = 0,

    /* ALPN: not used */
    .alpnprotos = NULL,
    .alpnprotoslen = 0,

    /* SNI: populated at runtime */
    .sni_host_name = NULL,
    .sni_host_name_size = 0
};

cy_awsport_ssl_credentials_t *security_info = &credentials;

/* MQTT connection information — populated by mqtt_client_config_init() */
cy_mqtt_connect_info_t connection_info = {
    .client_id = NULL,
    .client_id_len = 0,
    .username = NULL,
    .username_len = 0,
    .password = NULL,
    .password_len = 0,
    .clean_session = true,
    .keep_alive_sec = 180,
    .will_info = NULL
};

/******************************************************************************
* Function Definitions
******************************************************************************/

bool mqtt_client_config_init(void)
{
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);

    /* Keep the topic device id in step with the configuration. */
    tesaiot_mqtt_refresh_device_id();

    /* Broker hostname */
    strncpy(s_broker_hostname, cfg.broker, sizeof(s_broker_hostname) - 1);
    s_broker_hostname[sizeof(s_broker_hostname) - 1] = '\0';
    broker_info.hostname = s_broker_hostname;
    broker_info.hostname_len = strlen(s_broker_hostname);

    //! [j4_mqtt_port_from_tls_mode]
    /* ...context: inside mqtt_client_config_init() ... */
    /* Port derived from mode (config stores only one port field):
     *   Mode 0/1 (mTLS): 8883
     *   Mode 2 (serverTLS+MQTTs): 8884
     *   Mode 3 (REST API): api_port */
    switch (cfg.tls_mode) {
        case TESAIOT_MODE_MTLS:
        case TESAIOT_MODE_MTLS_SW:
            broker_info.port = 8883;
            break;
        case TESAIOT_MODE_SERVER_TLS:
        default:
            broker_info.port = 8884;
            break;
    }
    //! [j4_mqtt_port_from_tls_mode]

    /* SNI hostname */
    if (cfg.sni_hostname[0] != '\0') {
        strncpy(s_sni_hostname, cfg.sni_hostname, sizeof(s_sni_hostname) - 1);
        s_sni_hostname[sizeof(s_sni_hostname) - 1] = '\0';
    } else {
        /* Default: same as broker */
        strncpy(s_sni_hostname, s_broker_hostname, sizeof(s_sni_hostname) - 1);
        s_sni_hostname[sizeof(s_sni_hostname) - 1] = '\0';
    }
    credentials.sni_host_name = s_sni_hostname;
    credentials.sni_host_name_size = strlen(s_sni_hostname) + 1;

    /* Client identifier:
     *   mTLS modes: factory_uid (Trust M hardware UID — globally unique)
     *   serverTLS:  device_id (UUID from TESAIoT Platform)
     *   HTTPS mode: not used (no MQTT)
     */
    switch (cfg.tls_mode) {
        case TESAIOT_MODE_MTLS:
        case TESAIOT_MODE_MTLS_SW:
            if (cfg.factory_uid[0] != '\0') {
                strncpy(s_client_id, cfg.factory_uid, sizeof(s_client_id) - 1);
            } else {
                strncpy(s_client_id, cfg.device_id, sizeof(s_client_id) - 1);
            }
            break;
        case TESAIOT_MODE_SERVER_TLS:
        default:
            strncpy(s_client_id, cfg.device_id, sizeof(s_client_id) - 1);
            break;
    }
    s_client_id[sizeof(s_client_id) - 1] = '\0';
    connection_info.client_id = s_client_id;
    connection_info.client_id_len = strlen(s_client_id);

    /* Username and password based on mode.
     * Reference: mqtt_client_config.h from pse84_tesaiot_client:
     *   mTLS:      USERNAME=DEVICE_ID (mTLS device), PASSWORD="" (cert auth)
     *   serverTLS: USERNAME=DEVICE_ID,               PASSWORD=API_KEY
     *
     * BentoClaw multi-device mapping:
     *   Mode 0 mTLS:      username=mtls_device_id, password="" (cert)
     *   Mode 2 serverTLS:  username=device_id,      password=mqtt_pass
     */
    switch (cfg.tls_mode) {
        case TESAIOT_MODE_MTLS:
        case TESAIOT_MODE_MTLS_SW:
            /* mTLS: username = mTLS device_id, password = empty (cert-based) */
            strncpy(s_username, cfg.mtls_device_id, sizeof(s_username) - 1);
            s_password[0] = '\0';
            connection_info.password = s_password;
            connection_info.password_len = 0;
            break;
        case TESAIOT_MODE_SERVER_TLS:
        default:
            /* serverTLS + MQTTs: username = device_id, password = mqtt_pass */
            strncpy(s_username, cfg.device_id, sizeof(s_username) - 1);
            strncpy(s_password, cfg.mqtt_pass, sizeof(s_password) - 1);
            s_password[sizeof(s_password) - 1] = '\0';
            connection_info.password = s_password;
            connection_info.password_len = strlen(s_password);
            break;
    }
    s_username[sizeof(s_username) - 1] = '\0';
    connection_info.username = s_username;
    connection_info.username_len = strlen(s_username);

    /* Keep-alive and timeout from config */
    connection_info.keep_alive_sec = cfg.keepalive;
    connection_info.clean_session = true;

    printf("[MQTT-Config] Mode=%d, Broker=%s:%u, Client=%s, User=%s, PassLen=%u\n",
           (int)cfg.tls_mode, s_broker_hostname, broker_info.port,
           s_client_id, s_username, (unsigned)connection_info.password_len);

    /* mTLS + OPTIGA: read cert, register PSA SE, bind hardware key (Phase G) */
    if (cfg.tls_mode == TESAIOT_MODE_MTLS) {
        if (mqtt_mtls_setup_optiga) {
            if (!mqtt_mtls_setup_optiga()) {
                printf("[MQTT-Config] mTLS OPTIGA setup failed\n");
                return false;
            }
        } else {
            printf("[MQTT-Config] mTLS not available (Phase G not compiled)\n");
            return false;
        }
    }

    return true;
}

/******************************************************************************
* Identity shim — see the note in mqtt_client_config.h
******************************************************************************/

/* tesaiot_optiga_trust_m.c builds its topics from this pointer. In the
 * reference firmware it is a compile-time constant; here it points at the
 * configured device id, refreshed whenever the configuration is read. It
 * resolved to nothing until now only because every function that used it was
 * dead-stripped for want of a caller. */
static char s_device_id_for_topics[48];
const char *mqtt_device_id = s_device_id_for_topics;

void tesaiot_mqtt_refresh_device_id(void)
{
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);
    strncpy(s_device_id_for_topics, cfg.device_id,
            sizeof(s_device_id_for_topics) - 1);
    s_device_id_for_topics[sizeof(s_device_id_for_topics) - 1] = '\0';
}
const char *tesaiot_mqtt_client_id(void)
{
    return s_client_id;
}

const char *tesaiot_mqtt_username(void)
{
    return s_username;
}

const char *tesaiot_mqtt_topic_telemetry_base(void)
{
    static char topic[MQTT_TOPIC_MAX_LEN];
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);
    snprintf(topic, sizeof(topic), MQTT_TOPIC_FMT_TELEMETRY, cfg.device_id);
    return topic;
}

const char *tesaiot_mqtt_topic_command_status(void)
{
    static char topic[MQTT_TOPIC_MAX_LEN];
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);
    snprintf(topic, sizeof(topic), MQTT_TOPIC_FMT_COMMAND_STATUS, cfg.device_id);
    return topic;
}
