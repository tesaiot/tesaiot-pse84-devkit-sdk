/******************************************************************************
* File Name:   publisher_task.c
*
* Description: MQTT Publisher Task for BENTO BentoClaw.
*              Queue-based publish: other tasks enqueue messages,
*              this task publishes them to the MQTT broker.
*
*              BentoClaw differences:
*              - No button ISR (BentoClaw uses MicroPython for user interaction)
*              - Topics built at runtime from tesaiot_config device_id
*              - Publish initiated via queue from MicroPython or sensor tasks
*
* Based on: pse84_tesaiot_client publisher_task.c
******************************************************************************/

#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "cybsp.h"
#include "FreeRTOS.h"

#include "publisher_task.h"
#include "mqtt_task.h"
#include "mqtt_client_config.h"

#include "cy_mqtt_api.h"

#define printf(...) ((void)0)  /* UART contention fix — see mqtt_task.c */

/******************************************************************************
* Macros
******************************************************************************/
#define PUBLISH_RETRY_LIMIT        (10U)
#define PUBLISH_RETRY_MS           (1000U)
#define PUBLISHER_TASK_QUEUE_LENGTH (3U)

/******************************************************************************
* Global Variables
******************************************************************************/
TaskHandle_t publisher_task_handle;
QueueHandle_t publisher_task_q = NULL;

/* Publish info — topic set dynamically per message */
static cy_mqtt_publish_info_t publish_info = {
    .qos = (cy_mqtt_qos_t)MQTT_MESSAGES_QOS,
    .topic = NULL,
    .topic_len = 0,
    .retain = false,
    .dup = false
};

/******************************************************************************
* Publisher Task
******************************************************************************/

//! [j4_mqtt_publisher_drain]
void publisher_task(void *pvParameters)
{
    publisher_data_t publisher_q_data;
    (void)pvParameters;

    /* Create the publisher command queue */
    publisher_task_q = xQueueCreate(PUBLISHER_TASK_QUEUE_LENGTH,
                                    sizeof(publisher_data_t));

    printf("[Publisher] Task started\n");

    while (true) {


        /* See the note on PUBLISHER_TASK_STACK_SIZE. (printf) bypasses this file's


         * printf mute, if it has one. */


        (printf)("[PUB-TASK] stack_free=%u words\n",


                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (pdTRUE == xQueueReceive(publisher_task_q, &publisher_q_data,
                                     portMAX_DELAY)) {
            switch (publisher_q_data.cmd) {
                case PUBLISH_MQTT_MSG: {
                    /* Set topic and payload from queue data */
                    publish_info.topic = publisher_q_data.topic;
                    publish_info.topic_len = publisher_q_data.topic_len;
                    publish_info.payload = publisher_q_data.data;
                    publish_info.payload_len = publisher_q_data.payload_len;

                    /* Publish with retries */
                    cy_rslt_t result = ~CY_RSLT_SUCCESS;
                    uint8_t max_retries = publisher_q_data.max_attempts > 0 ?
                                          publisher_q_data.max_attempts :
                                          PUBLISH_RETRY_LIMIT;

                    for (uint8_t retry = 0; retry < max_retries; retry++) {
                        result = cy_mqtt_publish(mqtt_connection, &publish_info);
                        if (result == CY_RSLT_SUCCESS) {
                            #if TESAIOT_DEBUG_PUBLISHER_ENABLED
                            printf("[Publisher] Published to %.*s (%u bytes)\n",
                                   (int)publish_info.topic_len,
                                   publish_info.topic,
                                   (unsigned)publish_info.payload_len);
                                   //! [j4_mqtt_publisher_drain]
                            #endif
                            break;
                        }

                        printf("[Publisher] Publish failed: 0x%08X (retry %u/%u)\n",
                               (unsigned int)result, retry + 1, max_retries);
                        vTaskDelay(pdMS_TO_TICKS(PUBLISH_RETRY_MS));
                    }

                    /* Free payload if requested */
                    if (publisher_q_data.free_after_publish &&
                        publisher_q_data.data != NULL) {
                        vPortFree(publisher_q_data.data);
                    }
                    break;
                }

                case PUBLISHER_INIT:
                    printf("[Publisher] Initialized\n");
                    break;

                case PUBLISHER_DEINIT:
                    printf("[Publisher] Deinitialized\n");
                    break;
            }
        }
    }
}
