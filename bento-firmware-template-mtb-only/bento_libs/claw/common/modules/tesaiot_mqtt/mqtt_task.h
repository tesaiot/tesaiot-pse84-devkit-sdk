/******************************************************************************
* File Name:   mqtt_task.h
*
* Description: MQTT Client Task for BENTO BentoClaw.
*              Manages MQTT connection lifecycle to TESAIoT Platform.
*
*              BentoClaw differences from reference:
*              - WiFi/SDIO handled externally by wifi_init.c
*              - NTP handled externally by ntp_sync.c
*              - Runtime config from tesaiot_config_store (not #define)
*              - Phase 1: serverTLS only (OPTIGA deferred to Phase 3)
*
* Based on: pse84_tesaiot_client mqtt_task.h
******************************************************************************/

#ifndef MQTT_TASK_H_
#define MQTT_TASK_H_

#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "cy_mqtt_api.h"

/*******************************************************************************
* Macros
*******************************************************************************/
/* All tasks at priority 2 — prevents priority inversion deadlock (proven) */
#define MQTT_CLIENT_TASK_PRIORITY  (2U)
/* Depth in WORDS, as xTaskCreate() takes it — 8192 words = 32 KB.
 *
 * mTLS reaches deeper than a plain TLS handshake: mbedTLS calls psa_sign_hash()
 * for CertificateVerify, which reaches optiga_psa_sign() and trustm_ecdsa_sign(),
 * each with its own signature buffers.
 * Measured with uxTaskGetStackHighWaterMark() at the deepest point reached —
 * inside optiga_psa_sign(), called from mbedTLS CertificateVerify underneath the
 * whole handshake frame — the peak was 1602 words (6.4 KB) on 2026-08-04. 4096
 * words leaves better than 2x headroom, and the 16 KB it does not take is worth
 * more to the C heap, which the TLS record buffers draw on during a handshake. */
#define MQTT_CLIENT_TASK_STACK_SIZE (1024U * 4U)

/*******************************************************************************
* Types
*******************************************************************************/
typedef enum {
    HANDLE_MQTT_SUBSCRIBE_FAILURE,
    HANDLE_MQTT_PUBLISH_FAILURE,
    HANDLE_DISCONNECTION
} mqtt_task_cmd_t;

/*******************************************************************************
* Extern Variables
*******************************************************************************/
extern cy_mqtt_t mqtt_connection;
extern QueueHandle_t mqtt_task_q;

/*******************************************************************************
* Public Functions
*******************************************************************************/

/** MQTT client task entry point (FreeRTOS task function) */
void mqtt_client_task(void *pvParameters);

/** Request MQTT task to start and connect */
bool mqtt_request_start(void);

/** Check if MQTT task has started */
bool mqtt_is_started(void);

/** Check if MQTT is connected to broker */
bool mqtt_is_connected(void);

/** Force MQTT reconnection (clears publish slots) */
bool mqtt_force_reconnect(void);

/** Request MQTT task to stop and cleanup */
bool mqtt_request_stop(void);

#endif /* MQTT_TASK_H_ */
