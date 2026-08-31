/******************************************************************************
* File Name:   tesaiot_mqtt.h
*
* Description: TESAIoT MQTT Client Wrapper API for BENTO BentoClaw.
*              Provides clean API around mqtt_task.c state machine.
*
* Based on: pse84_tesaiot_client tesaiot_mqtt.h
******************************************************************************/

#ifndef TESAIOT_MQTT_H
#define TESAIOT_MQTT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Connect to MQTT broker.
 *
 * Checks if already connected. If not, requests mqtt_task to start
 * and waits for connection (up to 30 seconds).
 *
 * @return true on success, false on timeout or error
 */
bool tesaiot_mqtt_connect(void);

/**
 * @brief Check if MQTT is currently connected.
 *
 * @return true if connected, false otherwise
 */
/**
 * @brief Disconnect from MQTT broker.
 *
 * Requests mqtt_task to stop and updates shared status bridge.
 *
 * @return true on success, false on error
 */
bool tesaiot_mqtt_disconnect(void);

bool tesaiot_mqtt_is_connected(void);

/**
 * @brief Publish a message to the MQTT broker.
 *
 * Enqueues the message to the publisher task queue.
 * Caller owns the topic/payload buffers — they are copied into the queue entry.
 * If topic is NULL, uses default telemetry topic: device/{device_id}/telemetry
 *
 * @param topic       MQTT topic string (NULL for default telemetry topic)
 * @param payload     Message payload (JSON string typically)
 * @param payload_len Length of payload in bytes
 * @return true if enqueued successfully, false on error
 */
bool tesaiot_mqtt_publish(const char *topic, const char *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* TESAIOT_MQTT_H */
