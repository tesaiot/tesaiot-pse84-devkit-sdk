/******************************************************************************
* File Name:   core_mqtt_config.h
*
* Description: Configuration macros for the MQTT library.
*              Part of TESAIoT Secure Firmware for BENTO PSoC Edge E84.
*
* Based on: pse84_tesaiot_client reference implementation
******************************************************************************/

#ifndef CORE_MQTT_CONFIG_H_
#define CORE_MQTT_CONFIG_H_

/* Maximum number of MQTT PUBLISH messages pending acknowledgement */
#define MQTT_STATE_ARRAY_MAX_COUNT              ( 10U )

/* Retry count for reading CONNACK from the network */
#define MQTT_MAX_CONNACK_RECEIVE_RETRY_COUNT    ( 2U )

/* Milliseconds to wait for a ping response (keepalive mechanism) */
#define MQTT_PINGRESP_TIMEOUT_MS                ( 5000U )

#endif /* ifndef CORE_MQTT_CONFIG_H_ */
