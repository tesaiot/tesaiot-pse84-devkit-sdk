/******************************************************************************
* File Name:   tesaiot_mqtt.c
*
* Description: TESAIoT MQTT Client Wrapper Implementation for BENTO BentoClaw.
*              Delegates to mqtt_task.c state machine functions.
*
* Based on: pse84_tesaiot_client tesaiot_mqtt.c
******************************************************************************/

#include "tesaiot_mqtt.h"
#include "ipc_tesaiot_handler.h"
#include "tesaiot_config_store.h"
#include "publisher_task.h"
#include <stdio.h>
#include <string.h>

#define printf(...) ((void)0)  /* UART contention fix — see mqtt_task.c */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Topic format — matches mqtt_client_config.h (avoid pulling in cy_mqtt_api.h) */
#define TESAIOT_TOPIC_FMT_TELEMETRY  "device/%s/telemetry"

/*---------------------------------------------------------------------------
 * External Functions (from mqtt_task.c)
 *--------------------------------------------------------------------------*/
extern bool mqtt_request_start(void);
extern bool mqtt_is_connected(void);

/* ARM memory barrier */
#ifndef __DMB
#define __DMB() __asm volatile("dmb 0xF" ::: "memory")
#endif

/*---------------------------------------------------------------------------
 * Bridge: update TESAIoT shared status when MQTT state changes
 *--------------------------------------------------------------------------*/
static void tesaiot_bridge_mqtt(uint8_t state)
{
    tesaiot_status_t *ts = ipc_tesaiot_get_status();
    if (!ts) return;
    ts->mqtt_state = state;

    /* On connect: also write broker_url from config */
    if (state == TESAIOT_CONN_CONNECTED) {
        const tesaiot_config_t *cfg = tesaiot_config_get_ptr();
        if (cfg) {
            if (cfg->tls_mode == TESAIOT_MODE_HTTPS_API) {
                strncpy((char *)ts->broker_url, cfg->api_host,
                        sizeof(ts->broker_url) - 1);
            } else {
                strncpy((char *)ts->broker_url, cfg->broker,
                        sizeof(ts->broker_url) - 1);
            }
            ((char *)ts->broker_url)[sizeof(ts->broker_url) - 1] = '\0';

            /* mTLS mode: mark OPTIGA and cert as provisioned/loaded */
            if (cfg->tls_mode == TESAIOT_MODE_MTLS) {
                ts->optiga_state = TESAIOT_OPTIGA_PROVISIONED;
                ts->cert_state   = TESAIOT_CERT_LOADED;
                ts->flags |= TESAIOT_FLAG_OPTIGA_CHANGED | TESAIOT_FLAG_CERT_CHANGED;
            }
        }
    }

    ts->flags |= TESAIOT_FLAG_MQTT_CHANGED;
    __DMB();
}

/* Called directly from mqtt_task.c when cy_mqtt_connect succeeds.
 * This bypasses the cfg-task polling loop (priority inversion issue). */
void tesaiot_bridge_mqtt_connected(void)
{
    tesaiot_bridge_mqtt(TESAIOT_CONN_CONNECTED);
}

/*---------------------------------------------------------------------------
 * Public API Implementation
 *--------------------------------------------------------------------------*/

//! [j4_mqtt_publish_queue]
bool tesaiot_mqtt_connect(void)
{
    if (mqtt_is_connected()) {
        return true;
    }

    printf("[TESAIoT-MQTT] Requesting connection...\n");
    tesaiot_bridge_mqtt(TESAIOT_CONN_CONNECTING);

    if (!mqtt_request_start()) {
        printf("[TESAIoT-MQTT] Failed to request start\n");
        tesaiot_bridge_mqtt(TESAIOT_CONN_FAILED);
        return false;
    }

    /* Non-blocking: MQTT task runs at priority 2 and calls
     * tesaiot_bridge_mqtt_connected() directly when connected.
     * The old 30-second polling loop blocked this cfg task (priority 1),
     * preventing it from ever processing DISCONNECT commands. */
    printf("[TESAIoT-MQTT] MQTT task started (async)\n");
    return true;
}

bool tesaiot_mqtt_disconnect(void)
{
    extern bool mqtt_request_stop(void);

    if (!mqtt_is_connected()) {
        return true;
    }

    printf("[TESAIoT-MQTT] Disconnecting...\n");
    bool ok = mqtt_request_stop();

    if (ok) {
        tesaiot_bridge_mqtt(TESAIOT_CONN_OFF);
        printf("[TESAIoT-MQTT] Disconnected\n");
    } else {
        printf("[TESAIoT-MQTT] Disconnect failed\n");
    }

    return ok;
}

bool tesaiot_mqtt_publish(const char *topic, const char *payload, size_t payload_len)
{
    extern QueueHandle_t publisher_task_q;

    if (!mqtt_is_connected()) {
        printf("[TESAIoT-MQTT] Cannot publish: not connected\n");
        return false;
    }

    if (publisher_task_q == NULL) {
        printf("[TESAIoT-MQTT] Cannot publish: publisher queue not ready\n");
        return false;
    }

    /* Build topic: caller-provided or default telemetry topic */
    publisher_data_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.cmd = PUBLISH_MQTT_MSG;

    if (topic != NULL && topic[0] != '\0') {
        size_t tlen = strlen(topic);
        if (tlen >= sizeof(msg.topic)) {
            printf("[TESAIoT-MQTT] Topic too long (%u >= %u)\n",
                   (unsigned)tlen, (unsigned)sizeof(msg.topic));
            return false;
        }
        memcpy(msg.topic, topic, tlen + 1);
        msg.topic_len = tlen;
    } else {
        /* Default: device/{device_id}/telemetry */
        const tesaiot_config_t *cfg = tesaiot_config_get_ptr();
        if (!cfg || cfg->device_id[0] == '\0') {
            printf("[TESAIoT-MQTT] No device_id configured\n");
            return false;
        }
        int n = snprintf(msg.topic, sizeof(msg.topic),
                         TESAIOT_TOPIC_FMT_TELEMETRY, cfg->device_id);
        if (n < 0 || (size_t)n >= sizeof(msg.topic)) {
            printf("[TESAIoT-MQTT] Topic build failed\n");
            return false;
        }
        msg.topic_len = (size_t)n;
    }

    /* Allocate payload copy on C heap (freed by publisher_task after publish) */
    if (payload != NULL && payload_len > 0) {
        char *payload_copy = pvPortMalloc(payload_len);
        if (payload_copy == NULL) {
            printf("[TESAIoT-MQTT] Payload alloc failed (%u bytes)\n",
                   (unsigned)payload_len);
            return false;
        }
        memcpy(payload_copy, payload, payload_len);
        msg.data = payload_copy;
        msg.payload_len = payload_len;
        msg.free_after_publish = true;
    } else {
        msg.data = NULL;
        msg.payload_len = 0;
        msg.free_after_publish = false;
    }

    msg.max_attempts = 3;
    msg.retry_delay_ticks = pdMS_TO_TICKS(1000);

    /* Enqueue — non-blocking (0 timeout) to avoid deadlock from MicroPython task */
    if (xQueueSend(publisher_task_q, &msg, 0) != pdTRUE) {
    //! [j4_mqtt_publish_queue]
        printf("[TESAIoT-MQTT] Publish queue full\n");
        if (msg.free_after_publish && msg.data) {
            vPortFree(msg.data);
        }
        return false;
    }

    return true;
}

bool tesaiot_mqtt_is_connected(void)
{
    return mqtt_is_connected();
}
