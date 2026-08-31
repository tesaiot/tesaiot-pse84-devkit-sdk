/******************************************************************************
* File Name:   subscriber_task.h
*
* Description: MQTT Subscriber Task for BENTO BentoClaw.
*              Subscribes to device command topics on TESAIoT Platform.
*
*              Phase 1: Basic subscribe + command dispatch
*              Phase 4 will add: Protected Update, CSR workflow
*
* Based on: pse84_tesaiot_client subscriber_task.h
******************************************************************************/

#ifndef SUBSCRIBER_TASK_H_
#define SUBSCRIBER_TASK_H_

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "cy_mqtt_api.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define SUBSCRIBER_TASK_PRIORITY           (2U)
/* Depth in WORDS. 2048 words = 8 KB.
 *
 * This was 768 words (3 KB), described as "subscribe + callback only". It
 * overflowed the moment it first had real work to do: FreeRTOS printed
 * "FATAL: Stack overflow in task 'Subscriber'" immediately after
 * "[MQTT] Connected to broker" on 2026-08-05, taking CM33_NS down with it —
 * the REPL stopped answering and nothing could be published.
 *
 * It went unnoticed because until that day no connection to the broker had ever
 * succeeded, so this task had never run past its first subscribe. cy_mqtt_subscribe()
 * and the message callback both run with a TLS record layer underneath them,
 * which is where the depth goes.
 *
 * Measured twice on hardware, and the second measurement is why this is 3072
 * rather than 2048:
 *   830 words  — connected and subscribed, nothing inbound (2026-08-05)
 *  1242 words  — installing an enrolled certificate: PEM parse, DER convert,
 *                OPTIGA write, then the challenge signature that proves the
 *                certificate belongs to the key (2026-08-06)
 * The first number is what this task does at rest; the second is what it does
 * when it has real work, and it is half again as deep. 768 words — what this
 * had originally — is below both, which is why it overflowed the first time a
 * connection ever succeeded.
 *
 * 3072 keeps 2.5x over the working peak. Re-measure before trimming, and
 * re-measure again when a deeper handler joins the dispatch. */
#define SUBSCRIBER_TASK_STACK_SIZE         (3072U)

/*******************************************************************************
* Types
*******************************************************************************/
typedef enum {
    SUBSCRIBE_TO_TOPIC,
    UNSUBSCRIBE_FROM_TOPIC,
    /* A single_bundle Protected Update arrived on commands/protected_update.
     * Payload is the whole JSON bundle; handled by tesaiot_pu_ingest.c when the
     * firmware is built with ENABLE_OPTIGA_CLM. */
    HANDLE_PROTECTED_UPDATE_BUNDLE,
    /* A signed certificate on commands/certificate. The CSR workflow answers
     * this way — the platform publishes the certificate directly rather than
     * as a Protected Update bundle. */
    HANDLE_DEVICE_CERTIFICATE,
    /* A platform status message on commands/status — csr_queued,
     * manifest_published, or a failure reason. Logged for visibility. */
    HANDLE_COMMAND_STATUS
} subscriber_cmd_t;

typedef struct {
    subscriber_cmd_t cmd;
    char *data;
    int data_size;
    bool need_free;
} subscriber_data_t;

/*******************************************************************************
* Extern Variables
*******************************************************************************/
extern TaskHandle_t subscriber_task_handle;
extern QueueHandle_t subscriber_task_q;

/*******************************************************************************
* Functions
*******************************************************************************/
void subscriber_task(void *pvParameters);
void mqtt_subscription_callback(cy_mqtt_publish_info_t *received_msg_info);

#endif /* SUBSCRIBER_TASK_H_ */
