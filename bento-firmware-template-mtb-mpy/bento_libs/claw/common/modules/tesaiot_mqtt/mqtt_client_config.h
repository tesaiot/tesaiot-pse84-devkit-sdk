/******************************************************************************
* File Name:   mqtt_client_config.h
*
* Description: MQTT client configuration for TESAIoT Platform — BENTO BentoClaw
*
*              Unlike the reference (compile-time #define), BentoClaw uses
*              runtime configuration from tesaiot_config_store.c (LittleFS).
*              This header provides:
*              - Debug level system (compatible with reference)
*              - MQTT topic macros (built from runtime device_id)
*              - Global variable declarations for broker/security/connection
*              - Constants that don't change at runtime
*
* Based on: pse84_tesaiot_client mqtt_client_config.h
******************************************************************************/

#ifndef MQTT_CLIENT_CONFIG_H_
#define MQTT_CLIENT_CONFIG_H_

#ifndef CY_MQTT_MAX_OUTGOING_PUBLISHES
#define CY_MQTT_MAX_OUTGOING_PUBLISHES ( 2U )
#endif

#include "cy_mqtt_api.h"
#include "tesaiot_root_ca.h"

/*******************************************************************************
* Debug Level System — Conditional Compilation
*******************************************************************************/
#define TESAIOT_DEBUG_LEVEL_NONE    0
#define TESAIOT_DEBUG_LEVEL_ERROR   1
#define TESAIOT_DEBUG_LEVEL_WARNING 2
#define TESAIOT_DEBUG_LEVEL_INFO    3
#define TESAIOT_DEBUG_LEVEL_VERBOSE 4

#ifndef TESAIOT_DEBUG_LEVEL
#define TESAIOT_DEBUG_LEVEL TESAIOT_DEBUG_LEVEL_INFO
#endif

#define TESAIOT_DEBUG_MQTT_ENABLED       (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_INFO)
#define TESAIOT_DEBUG_TRUSTM_ENABLED     (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_VERBOSE)
#define TESAIOT_DEBUG_SUBSCRIBER_ENABLED (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_INFO)
#define TESAIOT_DEBUG_PUBLISHER_ENABLED  (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_INFO)
#define TESAIOT_DEBUG_ERROR_ENABLED      (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_ERROR)
#define TESAIOT_DEBUG_WARNING_ENABLED    (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_WARNING)
#define TESAIOT_DEBUG_INFO_ENABLED       (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_INFO)
#define TESAIOT_DEBUG_VERBOSE_ENABLED    (TESAIOT_DEBUG_LEVEL >= TESAIOT_DEBUG_LEVEL_VERBOSE)

/*******************************************************************************
* Constants (not runtime-configurable)
*******************************************************************************/

/* QoS for MQTT messages (0, 1, or 2) */
#define MQTT_MESSAGES_QOS                 ( 1 )

/* Last Will and Testament — disabled by default */
#define ENABLE_LWT_MESSAGE                ( 0 )

/* MQTT device on/off messages */
#define MQTT_DEVICE_ON_MESSAGE            "TURN ON"
#define MQTT_DEVICE_OFF_MESSAGE           "TURN OFF"

/* MQTT handle descriptor string */
#define MQTT_HANDLE_DESCRIPTOR            "MQTThandleID"

/* Unique client ID generation — disabled (device_id/factory_uid are unique) */
#define GENERATE_UNIQUE_CLIENT_ID         ( 0 )

/* Maximum client identifier length (Trust M UID = 54 hex chars) */
#define MQTT_CLIENT_IDENTIFIER_MAX_LEN    ( 64 )

/*******************************************************************************
* Runtime Configuration Accessors
*
* All device-specific values (broker, port, device_id, api_key, etc.)
* are read from tesaiot_config_store.c at runtime.
* See: tesaiot_config_store.h for tesaiot_config_t struct
*******************************************************************************/

/* Topic buffer sizes — topics are built at runtime from config.device_id */
#define MQTT_TOPIC_MAX_LEN                ( 128 )

/* Topic format strings — used with snprintf(buf, sizeof(buf), fmt, device_id) */
#define MQTT_TOPIC_FMT_TELEMETRY          "device/%s/telemetry"
#define MQTT_TOPIC_FMT_TELEMETRY_SENSOR   "device/%s/telemetry/sensor"
#define MQTT_TOPIC_FMT_COMMANDS           "device/%s/commands"
#define MQTT_TOPIC_FMT_COMMANDS_WILDCARD  "device/%s/commands/#"
#define MQTT_TOPIC_FMT_COMMAND_CSR        "device/%s/commands/csr"
#define MQTT_TOPIC_FMT_COMMAND_REQUEST    "device/%s/commands/request"
#define MQTT_TOPIC_FMT_COMMAND_STATUS     "device/%s/commands/status"
#define MQTT_TOPIC_FMT_COMMAND_ACK        "device/%s/commands/ack"

/******************************************************************************
* Identity shim for carried-over TESAIoT sources
*
* tesaiot_optiga_trust_m.c comes from official_pse84_trustm_mTLS_tesaiot, where
* the device identity and its topics are compile-time constants baked into the
* image. Here they are runtime configuration — a board is provisioned after it
* is built, and the same binary serves every unit. Rather than edit a file that
* is carried over verbatim, the names it expects resolve to the configured
* values.
*
* Every use site takes these through strlen/strcpy/%s, never sizeof, so a
* function returning const char * substitutes cleanly.
*
* Topics follow the convention already in use at subscriber_task.c: device_id,
* not the mTLS username.
******************************************************************************/
const char *tesaiot_mqtt_client_id(void);

/* Points at the configured device id; the carried-over sources build topics
 * from it. Call tesaiot_mqtt_refresh_device_id() after the configuration
 * changes. */
extern const char *mqtt_device_id;
void tesaiot_mqtt_refresh_device_id(void);
const char *tesaiot_mqtt_username(void);
const char *tesaiot_mqtt_topic_telemetry_base(void);
const char *tesaiot_mqtt_topic_command_status(void);

#ifndef MQTT_CLIENT_IDENTIFIER
#define MQTT_CLIENT_IDENTIFIER          tesaiot_mqtt_client_id()
#endif
#ifndef MQTT_USERNAME
#define MQTT_USERNAME                   tesaiot_mqtt_username()
#endif
#ifndef MQTT_TELEMETRY_TOPIC_BASE
#define MQTT_TELEMETRY_TOPIC_BASE       tesaiot_mqtt_topic_telemetry_base()
#endif
#ifndef MQTT_PUB_TOPIC_COMMAND_STATUS
#define MQTT_PUB_TOPIC_COMMAND_STATUS   tesaiot_mqtt_topic_command_status()
#endif

/*******************************************************************************
* Global Variable Declarations
*******************************************************************************/
extern cy_mqtt_broker_info_t broker_info;
extern cy_awsport_ssl_credentials_t *security_info;
extern cy_mqtt_connect_info_t connection_info;

/*******************************************************************************
* Function to populate config structs from tesaiot_config_store at runtime
*******************************************************************************/

/**
 * @brief Initialize MQTT configuration from tesaiot_config_store.
 *
 * Reads broker, port, device_id, api_key, etc. from the runtime config
 * and populates broker_info, security_info, and connection_info globals.
 * Must be called before mqtt_init().
 *
 * @return true on success, false if config store is not initialized
 */
bool mqtt_client_config_init(void);

#endif /* MQTT_CLIENT_CONFIG_H_ */
