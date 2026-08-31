/******************************************************************************
* File Name:   subscriber_task.c
*
* Description: MQTT Subscriber Task for BENTO BentoClaw.
*              Subscribes to device command topics on TESAIoT Platform.
*
*              Phase 1: Basic subscribe + command dispatch via queue
*              - Subscribes to device/{device_id}/commands/# (wildcard)
*              - Copies incoming payload to heap (MQTT lib reuses buffer)
*              - Dispatches to subscriber queue for processing
*              - No OPTIGA, no Protected Update, no CSR (Phase 4)
*
*              CRITICAL: mqtt_subscription_callback() is called from
*              MQTT event thread — NO printf, NO blocking, NO mutex!
*
* Based on: pse84_tesaiot_client subscriber_task.c (3065 lines)
******************************************************************************/

#include "cybsp.h"
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "subscriber_task.h"
#include "mqtt_task.h"
#include "mqtt_client_config.h"
#include "tesaiot_config_store.h"

#include "cy_mqtt_api.h"

/* Weak: present only when the firmware is built with ENABLE_OPTIGA_CLM, which
 * is when tesaiot_pu_ingest.c is compiled. Everywhere else a Protected Update
 * bundle is logged and dropped. */
extern void tesaiot_pu_ingest_bundle(char *bundle_data, uint16_t bundle_size)
    __attribute__((weak));
extern void tesaiot_pu_ingest_certificate(char *cert_payload, uint16_t cert_size)
    __attribute__((weak));
/* Weak, same condition. Creates the event group the Enrol path waits on. Before
 * this existed the group was created only when a bundle arrived, and Enrol
 * refuses to run without it — so on a freshly power-cycled board the two waited
 * on each other and Enrol never worked for that whole boot. */
extern void tesaiot_pu_ingest_init(void) __attribute__((weak));

#define printf(...) ((void)0)  /* UART contention fix — see mqtt_task.c */

/******************************************************************************
* Macros
******************************************************************************/
#define MAX_SUBSCRIBE_RETRIES           (3U)
#define MQTT_SUBSCRIBE_RETRY_INTERVAL_MS (1000U)
#define SUBSCRIPTION_COUNT              (1U)
#define SUBSCRIBER_TASK_QUEUE_LENGTH    (10U)

/******************************************************************************
* Global Variables
******************************************************************************/
TaskHandle_t subscriber_task_handle;
QueueHandle_t subscriber_task_q = NULL;

/******************************************************************************
* Static Variables
******************************************************************************/

/* Topic buffer — built at runtime from config.device_id */
static char s_sub_topic[MQTT_TOPIC_MAX_LEN];

/* Subscribe info — topic set dynamically */
static cy_mqtt_subscribe_info_t subscribe_info = {
    .qos = (cy_mqtt_qos_t)MQTT_MESSAGES_QOS,
    .topic = NULL,
    .topic_len = 0
};

/******************************************************************************
* Static Functions
******************************************************************************/

/**
 * Subscribe to the device command topic with retries.
 * Called once after task creation (MQTT is already connected).
 */
static cy_rslt_t subscribe_to_topic(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Build topic from runtime config */
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);

    snprintf(s_sub_topic, sizeof(s_sub_topic),
             MQTT_TOPIC_FMT_COMMANDS_WILDCARD, cfg.device_id);

    subscribe_info.topic = s_sub_topic;
    subscribe_info.topic_len = strlen(s_sub_topic);

    printf("[Subscriber] Subscribing to: %s\n", s_sub_topic);

    for (uint32_t retry = 0; retry < MAX_SUBSCRIBE_RETRIES; retry++) {
        result = cy_mqtt_subscribe(mqtt_connection, &subscribe_info,
                                   SUBSCRIPTION_COUNT);
        if (result == CY_RSLT_SUCCESS) {
            printf("[Subscriber] Subscribed (QoS=%d)\n",
                   (int)subscribe_info.qos);
            return result;
        }

        printf("[Subscriber] Subscribe failed: 0x%08X (retry %lu/%u)\n",
               (unsigned int)result, (unsigned long)(retry + 1),
               MAX_SUBSCRIBE_RETRIES);

        vTaskDelay(pdMS_TO_TICKS(MQTT_SUBSCRIBE_RETRY_INTERVAL_MS));
    }

    printf("[Subscriber] Subscribe failed after %u retries\n",
           MAX_SUBSCRIBE_RETRIES);
    return result;
}

/******************************************************************************
* Subscriber Task
******************************************************************************/

void subscriber_task(void *pvParameters)
{
    subscriber_data_t subscriber_q_data;
    (void)pvParameters;

    /* Delete old queue if exists (from previous MQTT session) */
    if (subscriber_task_q != NULL) {
        vQueueDelete(subscriber_task_q);
        subscriber_task_q = NULL;
    }

    /* Create the subscriber command queue */
    subscriber_task_q = xQueueCreate(SUBSCRIBER_TASK_QUEUE_LENGTH,
                                      sizeof(subscriber_data_t));
    if (subscriber_task_q == NULL) {
        printf("[Subscriber] Queue creation failed!\n");
        vTaskDelete(NULL);
        return;
    }

    printf("[Subscriber] Task started\n");

    /* Bring up the provisioning event group here, not on the first bundle.
     * See the note on the extern above. */
    if (tesaiot_pu_ingest_init) {
        tesaiot_pu_ingest_init();
    }

    /* Subscribe to device command topic */
    cy_rslt_t result = subscribe_to_topic();
    if (result != CY_RSLT_SUCCESS) {
        /* Notify mqtt_task to handle subscription failure */
        mqtt_task_cmd_t cmd = HANDLE_MQTT_SUBSCRIBE_FAILURE;
        xQueueSend(mqtt_task_q, &cmd, portMAX_DELAY);
    }

    /* Main loop — process incoming messages from queue */
    while (true) {
        /* Report how close this task came to its limit; see the note on the size.
         *
         * (printf) in parentheses, because this file mutes printf with a
         * function-like macro a few lines below its includes. Writing it plainly
         * compiles to nothing, which is exactly how the first attempt at this
         * measurement produced no output and no clue why. */
        (printf)("[SUB] stack_free=%u words\n",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (pdTRUE == xQueueReceive(subscriber_task_q, &subscriber_q_data,
                                     portMAX_DELAY)) {
            switch (subscriber_q_data.cmd) {
                case SUBSCRIBE_TO_TOPIC:
                    /* Anything on commands/# that is not a bundle or a status.
                     *
                     * (printf) bypasses this file's mute deliberately. This was
                     * silent, and during the first enrolment attempt that meant
                     * there was no way to tell whether the platform had replied
                     * at all — the only evidence a message had even arrived was
                     * the stack-usage line printed next to it. Enrolment traffic
                     * is rare and operator-driven; seeing it is worth the UART.
                     *
                     * Truncated: a single_bundle Protected Update is kilobytes,
                     * and dumping it here would stall the task on the UART. */
                    (printf)("[Subscriber] %.*s%s\n",
                             subscriber_q_data.data_size > 240
                                 ? 240 : subscriber_q_data.data_size,
                             subscriber_q_data.data ? subscriber_q_data.data : "(null)",
                             subscriber_q_data.data_size > 240 ? " ...(truncated)" : "");
                    break;

                case UNSUBSCRIBE_FROM_TOPIC:
                    printf("[Subscriber] Unsubscribe requested\n");
                    if (subscribe_info.topic != NULL) {
                        cy_mqtt_unsubscribe(mqtt_connection,
                                            &subscribe_info,
                                            SUBSCRIPTION_COUNT);
                    }
                    break;

                case HANDLE_PROTECTED_UPDATE_BUNDLE:
                    /* (printf) bypasses this file's printf mute — enrolment is
                     * rare and operator-driven; its progress should be seen. */
                    (printf)("[Subscriber] Protected Update bundle (%d bytes)\n",
                             subscriber_q_data.data_size);
                    if (tesaiot_pu_ingest_bundle && subscriber_q_data.data) {
                        tesaiot_pu_ingest_bundle(subscriber_q_data.data,
                                                 (uint16_t)subscriber_q_data.data_size);
                    } else {
                        (printf)("[Subscriber] No ingest in this build "
                                 "(ENABLE_OPTIGA_CLM=0) — bundle dropped\n");
                    }
                    break;

                case HANDLE_DEVICE_CERTIFICATE:
                    (printf)("[Subscriber] Certificate from platform (%d bytes)\n",
                             subscriber_q_data.data_size);
                    if (tesaiot_pu_ingest_certificate && subscriber_q_data.data) {
                        tesaiot_pu_ingest_certificate(subscriber_q_data.data,
                                                      (uint16_t)subscriber_q_data.data_size);
                    } else {
                        (printf)("[Subscriber] No installer in this build "
                                 "(ENABLE_OPTIGA_CLM=0) — certificate dropped\n");
                    }
                    break;

                case HANDLE_COMMAND_STATUS:
                    (printf)("[Subscriber] Platform status: %.*s\n",
                             subscriber_q_data.data_size,
                             subscriber_q_data.data ? subscriber_q_data.data : "(null)");
                    break;
            }

            /* Free payload if allocated by callback */
            if (subscriber_q_data.need_free && subscriber_q_data.data != NULL) {
                vPortFree(subscriber_q_data.data);
            }
        }
    }
}

/******************************************************************************
* MQTT Subscription Callback
*
* CRITICAL: Called from MQTT event thread context!
*   - NO printf (UART buffer overflow → callback blocks → MQTT stalls)
*   - NO mutex/semaphore take (deadlock with MQTT library internal lock)
*   - NO blocking calls of any kind
*   - Must copy payload (MQTT library reuses the receive buffer)
*   - xQueueSend with timeout=0 (non-blocking)
******************************************************************************/

void mqtt_subscription_callback(cy_mqtt_publish_info_t *received_msg_info)
{
    if (subscriber_task_q == NULL) {
        return;
    }

    const char *payload = received_msg_info->payload;
    int payload_len = received_msg_info->payload_len;

    /* Allocate heap copy of payload (MQTT lib reuses receive buffer) */
    char *data_copy = (char *)pvPortMalloc(payload_len + 1);
    if (data_copy == NULL) {
        return; /* Drop message — no heap available */
    }

    memcpy(data_copy, payload, payload_len);
    data_copy[payload_len] = '\0';

    /* Classify by topic suffix. strncmp only — this runs on the MQTT event
     * thread, where blocking or printing is forbidden. */
    subscriber_cmd_t cmd = SUBSCRIBE_TO_TOPIC;
    {
        const char *topic = received_msg_info->topic;
        uint16_t tlen = received_msg_info->topic_len;
        /* Our own outbound requests come straight back: the device publishes to
         * commands/request and subscribes to commands/#, so every request is
         * echoed to us. Harmless while nothing handles that topic, and a
         * double-processed request the moment something does. Drop it here,
         * where the topic is still available. */
        static const char REQ_SUFFIX[] = "/commands/request";
        static const char CSR_SUFFIX[] = "/commands/csr";
        static const char PU_SUFFIX[] = "/commands/protected_update";
        static const char ST_SUFFIX[] = "/commands/status";
        static const char CT_SUFFIX[] = "/commands/certificate";
        if (topic != NULL) {
            if ((tlen >= sizeof(REQ_SUFFIX) - 1 &&
                 0 == strncmp(topic + tlen - (sizeof(REQ_SUFFIX) - 1),
                              REQ_SUFFIX, sizeof(REQ_SUFFIX) - 1)) ||
                (tlen >= sizeof(CSR_SUFFIX) - 1 &&
                 0 == strncmp(topic + tlen - (sizeof(CSR_SUFFIX) - 1),
                              CSR_SUFFIX, sizeof(CSR_SUFFIX) - 1))) {
                vPortFree(data_copy);   /* our own echo — nothing to do */
                return;
            }
            if (tlen >= sizeof(PU_SUFFIX) - 1 &&
                0 == strncmp(topic + tlen - (sizeof(PU_SUFFIX) - 1),
                             PU_SUFFIX, sizeof(PU_SUFFIX) - 1)) {
                cmd = HANDLE_PROTECTED_UPDATE_BUNDLE;
            } else if (tlen >= sizeof(CT_SUFFIX) - 1 &&
                       0 == strncmp(topic + tlen - (sizeof(CT_SUFFIX) - 1),
                                    CT_SUFFIX, sizeof(CT_SUFFIX) - 1)) {
                cmd = HANDLE_DEVICE_CERTIFICATE;
            } else if (tlen >= sizeof(ST_SUFFIX) - 1 &&
                       0 == strncmp(topic + tlen - (sizeof(ST_SUFFIX) - 1),
                                    ST_SUFFIX, sizeof(ST_SUFFIX) - 1)) {
                cmd = HANDLE_COMMAND_STATUS;
            }
        }
    }

    /* Build queue message */
    subscriber_data_t msg = {
        .cmd = cmd,
        .data = data_copy,
        .data_size = payload_len,
        .need_free = true
    };

    /* Non-blocking send — drop if queue full (better than blocking callback) */
    if (xQueueSend(subscriber_task_q, &msg, 0) != pdPASS) {
        vPortFree(data_copy); /* Queue full — free the copy */
    }
}
