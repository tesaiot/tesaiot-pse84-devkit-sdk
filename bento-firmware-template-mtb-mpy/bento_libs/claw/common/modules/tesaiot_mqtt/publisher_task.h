/******************************************************************************
* File Name:   publisher_task.h
*
* Description: MQTT Publisher Task for BENTO BentoClaw.
*              Publishes telemetry data to TESAIoT Platform.
*
* Based on: pse84_tesaiot_client publisher_task.h
******************************************************************************/

#ifndef PUBLISHER_TASK_H_
#define PUBLISHER_TASK_H_

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************************************
* Macros
*******************************************************************************/
#define PUBLISHER_TASK_PRIORITY           (2U)
/* Depth in WORDS. 2048 words = 8 KB.
 *
 * 512 words (2 KB) for "dequeue + cy_mqtt_publish only" was the same
 * underestimate that overflowed the subscriber task — cy_mqtt_publish() writes
 * through the TLS record layer. This task has never been exercised either, for
 * the same reason: no successful broker connection until 2026-08-05. */
#define PUBLISHER_TASK_STACK_SIZE         (2048U)

/*******************************************************************************
* Types
*******************************************************************************/
typedef enum {
    PUBLISHER_INIT,
    PUBLISHER_DEINIT,
    PUBLISH_MQTT_MSG
} publisher_cmd_t;

typedef struct {
    publisher_cmd_t cmd;
    char *data;
    size_t payload_len;
    char topic[96];
    size_t topic_len;
    bool free_after_publish;
    uint8_t attempt;
    uint8_t max_attempts;
    TickType_t retry_delay_ticks;
} publisher_data_t;

/*******************************************************************************
* Extern Variables
*******************************************************************************/
extern TaskHandle_t publisher_task_handle;
extern QueueHandle_t publisher_task_q;

/*******************************************************************************
* Functions
*******************************************************************************/
void publisher_task(void *pvParameters);

#endif /* PUBLISHER_TASK_H_ */
