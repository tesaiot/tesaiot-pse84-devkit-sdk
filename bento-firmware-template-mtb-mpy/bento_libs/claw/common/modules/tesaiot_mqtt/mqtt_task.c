/******************************************************************************
* File Name:   mqtt_task.c
*
* Description: MQTT Client Task for BENTO BentoClaw.
*              Manages MQTT connection lifecycle to TESAIoT Platform.
*
*              BentoClaw architecture:
*              - WiFi/SDIO already initialized by wifi_init.c (CM33_NS)
*              - NTP already synced by ntp_sync.c
*              - Config read from tesaiot_config_store at runtime
*              - Phase 1: serverTLS only (no OPTIGA cert selection)
*
* Based on: pse84_tesaiot_client mqtt_task.c (1536 lines)
******************************************************************************/

#include "cybsp.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "mqtt_task.h"
#include "subscriber_task.h"
#include "publisher_task.h"
#include "mqtt_client_config.h"
#include "tesaiot_config_store.h"

#include "cy_retarget_io.h"
#include "cy_wcm.h"
#include "wifi_init.h"
#include "cy_mqtt_api.h"

#include "lwip/netif.h"

/* Logging disabled — UART mutex contention corrupts BentoClaw TACP binary
 * frames (printf from other tasks interleaves mid-frame).  Remove this
 * #define to re-enable debug logging. */
/* TEMP-DIAG: mute lifted to read the MQTT connect errors */
/* #define printf(...) ((void)0) */

/******************************************************************************
* Macros
******************************************************************************/
#define MQTT_TASK_QUEUE_LENGTH           (3u)
#define TASK_CREATION_DELAY_MS           (2000u)

/* Flag Masks for tracking cleanup */
#define WCM_INITIALIZED                  (1lu << 0)
#define WIFI_CONNECTED                   (1lu << 1)
#define LIBS_INITIALIZED                 (1lu << 2)
#define BUFFER_INITIALIZED               (1lu << 3)
#define MQTT_INSTANCE_CREATED            (1lu << 4)
#define MQTT_CONNECTION_SUCCESS          (1lu << 5)
#define MQTT_MSG_RECEIVED                (1lu << 6)
#define TIME_DIV_MS                      (60000u)

#define CHECK_RESULT(result, init_mask, error_message...)      \
                     do                                        \
                     {                                         \
                         if ((int)result == CY_RSLT_SUCCESS)   \
                         {                                     \
                             status_flag |= init_mask;         \
                         }                                     \
                         else                                  \
                         {                                     \
                             printf(error_message);            \
                             return result;                    \
                         }                                     \
                     } while(0)

#define MQTT_CTRL_START_BIT              (1U)

/******************************************************************************
* Global Variables
******************************************************************************/
cy_mqtt_t mqtt_connection;
QueueHandle_t mqtt_task_q;
volatile uint32_t status_flag;

/* Network buffer for MQTT send/receive — static (Phase 0 GC-safe design) */
static uint8_t mqtt_network_buf[3200];

static EventGroupHandle_t mqtt_control_events;
static volatile bool mqtt_start_requested = false;
static volatile bool mqtt_started = false;
static volatile bool mqtt_stop_requested = false;
static volatile bool mqtt_shutting_down = false;
static TaskHandle_t s_mqtt_task_handle = NULL;

/******************************************************************************
* Forward Declarations
******************************************************************************/
static void cleanup(void);
static cy_rslt_t mqtt_init(void);

/******************************************************************************
* Internal Functions
******************************************************************************/

/* Lazy task creation — called from tesaiot.connect() context.
 * Boot-time creation was removed because the 5KB stack competed with
 * cy_wcm_init() for FreeRTOS heap, preventing WiFi SDIO init. */
//! [j4_mqtt_lazy_task_start]
bool mqtt_request_start(void)
{
    /* Create MQTT task on first request (lazy — saves 5KB heap during boot) */
    if (s_mqtt_task_handle == NULL) {
        BaseType_t r = xTaskCreate(
            mqtt_client_task, "MQTT",
            /* Depth is in WORDS, and MQTT_CLIENT_TASK_STACK_SIZE already is —
             * its comment says "4096 words", and the Subscriber and Publisher
             * tasks below pass their own word counts straight through. Dividing
             * by sizeof(StackType_t) here handed FreeRTOS 1024 words, so the
             * task ran on 4 KB instead of the intended 16 KB.
             *
             * read_certificate_from_optiga() alone declares 4,096 bytes of
             * locals (two 2 KB buffers), so it overflowed on entry — before
             * reaching any of the OPTIGA wait loops, which is why adding
             * timeouts to those never produced a message. The overflow also
             * beat configCHECK_FOR_STACK_OVERFLOW, which only samples on a
             * context switch: the frame was gone before one happened, so
             * vApplicationStackOverflowHook never fired and the core just
             * stopped. Diagnosed 2026-08-04 bringing up mTLS on the Dev Kit. */
            MQTT_CLIENT_TASK_STACK_SIZE,
            NULL, MQTT_CLIENT_TASK_PRIORITY, &s_mqtt_task_handle);
        if (r != pdPASS) {
            printf("[MQTT] Task creation failed\n");
            return false;
        }
        /* Wait for task to create event group (poll — 200ms was racy) */
        for (int i = 0; i < 20 && mqtt_control_events == NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    if (mqtt_control_events == NULL) return false;
    if (mqtt_start_requested) return true;

    mqtt_start_requested = true;
    (void)xEventGroupSetBits(mqtt_control_events, MQTT_CTRL_START_BIT);
    return true;
}
//! [j4_mqtt_lazy_task_start]

bool mqtt_is_started(void)
{
    return mqtt_started;
}

bool mqtt_is_connected(void)
{
    return (status_flag & MQTT_CONNECTION_SUCCESS) != 0U;
}

bool mqtt_force_reconnect(void)
{
    printf("[MQTT] Forced reconnect...\n");

    /* Full teardown — destroy stale TLS/network state */
    cleanup();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Fresh init + connect */
    if (CY_RSLT_SUCCESS != mqtt_init()) {
        printf("[MQTT] Re-init failed\n");
        return false;
    }

    cy_rslt_t result = cy_mqtt_connect(mqtt_connection, &connection_info);
    if (result != CY_RSLT_SUCCESS) {
        printf("[MQTT] Reconnect failed: 0x%08X\n", (unsigned int)result);
        return false;
    }

    status_flag |= MQTT_CONNECTION_SUCCESS;
    printf("[MQTT] Reconnected\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    return true;
}

bool mqtt_request_stop(void)
{
    if (!mqtt_started) return true;
    mqtt_stop_requested = true;
    return true;
}

/******************************************************************************
* Cleanup — full MQTT teardown for clean reconnect
*
* cy_mqtt_connect() on the SAME mqtt_obj reuses stale internal state
* (network_context, retry counters, transport buffers) that causes TLS
* handshake failure on the second connection ("Unknown CA" / 0x08060009).
*
* Fix: destroy the entire MQTT instance (disconnect → delete → deinit)
* and let mqtt_init() recreate from scratch.  cy_socket_deinit ref-counting
* keeps the WiFi socket layer alive — only the MQTT layer is torn down.
******************************************************************************/

static void cleanup(void)
{
    /* Release mTLS resources (PSA key, SE slots) if active — Phase G */
    extern void mqtt_mtls_cleanup(void) __attribute__((weak));
    if (mqtt_mtls_cleanup) mqtt_mtls_cleanup();

    /* Step 1: Disconnect (closes TCP + frees TLS context + root CA) */
    if (status_flag & MQTT_CONNECTION_SUCCESS) {
        printf("[MQTT] Disconnecting...\n");
        cy_mqtt_disconnect(mqtt_connection);
        status_flag &= ~MQTT_CONNECTION_SUCCESS;
        printf("[MQTT] Disconnected\n");
        /* cy_mqtt_disconnect() releases process_mutex, but the MQTT event
         * thread may still be processing the disconnect event (it acquires
         * process_mutex to fire callbacks).  Wait for event thread to
         * finish before cy_mqtt_delete() tries to acquire the same mutex. */
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    /* Step 2: Delete MQTT instance (frees mqtt_obj, timers, mutex) */
    if (status_flag & MQTT_INSTANCE_CREATED) {
        printf("[MQTT] Deleting instance...\n");
        cy_mqtt_delete(mqtt_connection);
        mqtt_connection = NULL;
        status_flag &= ~MQTT_INSTANCE_CREATED;
    }

    /* Step 3: Deinit MQTT library (decrements cy_socket ref count).
     * WiFi (WCM) holds its own ref — socket layer stays alive. */
    if (status_flag & LIBS_INITIALIZED) {
        cy_mqtt_deinit();
        status_flag &= ~LIBS_INITIALIZED;
    }

    printf("[MQTT] Cleanup complete (flags=0x%lX)\n", (unsigned long)status_flag);
}

/******************************************************************************
* MQTT Event Callback
******************************************************************************/

static void mqtt_event_callback(cy_mqtt_t mqtt_handle, cy_mqtt_event_t event,
                                void *user_data)
{
    mqtt_task_cmd_t mqtt_task_cmd;
    CY_UNUSED_PARAMETER(mqtt_handle);
    CY_UNUSED_PARAMETER(user_data);

    switch (event.type) {
        case CY_MQTT_EVENT_TYPE_DISCONNECT:
            status_flag &= ~(MQTT_CONNECTION_SUCCESS);
            printf("[MQTT] Disconnected (reason=%d)\n", (int)event.data.reason);
            /* Skip queueing during intentional shutdown — prevents deadlock:
             * callback runs in event thread holding process_mutex, blocking
             * xQueueSend here would prevent cy_mqtt_delete() from acquiring
             * the same mutex. Use timeout=0 as extra safety. */
            if (!mqtt_shutting_down) {
                mqtt_task_cmd = HANDLE_DISCONNECTION;
                xQueueSend(mqtt_task_q, &mqtt_task_cmd, 0);
            }
            break;

        case CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE:
            status_flag |= MQTT_MSG_RECEIVED;
            mqtt_subscription_callback(&(event.data.pub_msg.received_message));
            break;

        default:
            printf("[MQTT] Unknown event type\n");
            break;
    }
}

/******************************************************************************
* MQTT Init — creates MQTT client instance
******************************************************************************/

static cy_rslt_t mqtt_init(void)
{
    cy_rslt_t result;

    /* Initialize config from runtime store.
     *
     * This also runs the mTLS/OPTIGA bring-up, and its result was being
     * discarded. When the secure element could not be opened, setup reported
     * "[MQTT-Config] mTLS OPTIGA setup failed" and the connection was attempted
     * anyway — with no client certificate and no signing key, so the broker saw
     * an anonymous client and refused it. The visible symptom was a connect
     * failure that looked like a server-side rejection rather than a local one.
     * Observed 2026-08-04. */
    if (!mqtt_client_config_init()) {
        printf("[MQTT] Configuration failed — not attempting to connect\n");
        return CY_RSLT_MODULE_MQTT_ERROR;
    }

    /* Initialize MQTT library (first time only) */
    if (!(status_flag & LIBS_INITIALIZED)) {
        result = cy_mqtt_init();
        CHECK_RESULT(result, LIBS_INITIALIZED, "\n[MQTT] Library init failed!\n");
    }

    /* Create MQTT client instance (fresh each time — cleanup() destroys it) */
    if (!(status_flag & MQTT_INSTANCE_CREATED)) {
        result = cy_mqtt_create(mqtt_network_buf, sizeof(mqtt_network_buf),
                                security_info, &broker_info,
                                MQTT_HANDLE_DESCRIPTOR,
                                &mqtt_connection);
        CHECK_RESULT(result, MQTT_INSTANCE_CREATED, "\n[MQTT] Instance creation failed!\n");

        /* Register event callback */
        result = cy_mqtt_register_event_callback(mqtt_connection,
                    (cy_mqtt_callback_t)mqtt_event_callback, NULL);
        if (result == CY_RSLT_SUCCESS) {
            printf("[MQTT] Instance created\n");
        }
    }

    return result;
}

/******************************************************************************
* MQTT Connect — connects to broker with retries
******************************************************************************/

static cy_rslt_t mqtt_connect(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);

    //! [j4_mqtt_connect_observables]
    /* ...context: inside the MQTT connect helper ... */
    printf("[MQTT] Connecting to '%s:%u' as '%s'...\n",
           broker_info.hostname, broker_info.port,
           connection_info.client_id);
    printf("[MQTT] root_ca=%p size=%u flags=0x%lX\n",
           security_info ? security_info->root_ca : NULL,
           security_info ? (unsigned)security_info->root_ca_size : 0,
           (unsigned long)status_flag);

    for (uint32_t retry = 0; retry < cfg.max_retries; retry++) {
        /* Verify WiFi is still connected.
         * MUST use app_wifi_is_ready() — NOT cy_wcm_is_connected_to_ap().
         * BentoClaw uses lazy WiFi init: WCM is uninitialized until first
         * wifi.scan()/connect(). Calling cy_wcm API before init = HardFault. */
        if (!app_wifi_is_ready()) {
            printf("[MQTT] WiFi disconnected — waiting for reconnect...\n");
            for (int w = 0; w < 100 && !app_wifi_is_ready(); w++) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            if (!app_wifi_is_ready()) {
                printf("[MQTT] WiFi reconnect timeout\n");
                return ~CY_RSLT_SUCCESS;
            }
        }

        printf("[MQTT] cy_mqtt_connect attempt %lu...\n", (unsigned long)(retry + 1));
        result = cy_mqtt_connect(mqtt_connection, &connection_info);
        printf("[MQTT] cy_mqtt_connect returned: 0x%08X\n", (unsigned int)result);
        if (result == CY_RSLT_SUCCESS) {
            printf("[MQTT] Connected to broker\n");
            //! [j4_mqtt_connect_observables]
            status_flag |= MQTT_CONNECTION_SUCCESS;
            /* Bridge status directly — don't rely on cfg-task polling
             * (cfg task priority 1 < MQTT priority 2 → may not get CPU) */
            { extern void tesaiot_bridge_mqtt_connected(void);
              tesaiot_bridge_mqtt_connected(); }
            return result;
        }

        printf("[MQTT] Connect failed: 0x%08X (retry %lu/%u)\n",
               (unsigned int)result, (unsigned long)(retry + 1), cfg.max_retries);
        vTaskDelay(pdMS_TO_TICKS(cfg.retry_interval_ms));
    }

    printf("[MQTT] Exceeded max retries (%u)\n", cfg.max_retries);
    return result;
}

/******************************************************************************
* MQTT Client Task — main state machine
******************************************************************************/

void mqtt_client_task(void *pvParameters)
{
    mqtt_task_cmd_t mqtt_status;
    (void)pvParameters;

    /* Create message queue */
    mqtt_task_q = xQueueCreate(MQTT_TASK_QUEUE_LENGTH, sizeof(mqtt_task_cmd_t));

    /* Create control event group */
    if (mqtt_control_events == NULL) {
        mqtt_control_events = xEventGroupCreate();
        if (mqtt_control_events == NULL) {
            printf("[MQTT] Failed to create event group\n");
            s_mqtt_task_handle = NULL;
            vTaskDelete(NULL);
        }
    }

    /* BentoClaw lazy WiFi: WCM not initialized until first wifi.scan()/connect().
     * MUST use app_wifi_is_ready() — cy_wcm_is_connected_to_ap() crashes
     * when called before WCM init (HardFault / board hang).
     * Timeout after 60s — don't block forever if WiFi never connects. */
    printf("[MQTT] Waiting for WiFi...\n");
    for (int w = 0; w < 120 && !app_wifi_is_ready(); w++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!app_wifi_is_ready()) {
        printf("[MQTT] WiFi not ready after 60s — aborting\n");
        s_mqtt_task_handle = NULL;
        vTaskDelete(NULL);
        return;  /* unreachable but satisfies compiler */
    }
    status_flag |= WIFI_CONNECTED;
    printf("[MQTT] WiFi connected\n");

    /* Wait for MQTT start request */
mqtt_wait_for_start:
    printf("[MQTT] Waiting for start request...\n");
    (void)xEventGroupWaitBits(mqtt_control_events,
                              MQTT_CTRL_START_BIT,
                              pdTRUE, pdFALSE,
                              portMAX_DELAY);
    mqtt_started = true;
    printf("[MQTT] Start request received\n");

    /* Initialize and connect */
    if ((CY_RSLT_SUCCESS == mqtt_init()) && (CY_RSLT_SUCCESS == mqtt_connect())) {
        /* Create subscriber task */
        BaseType_t sub_result = xTaskCreate(subscriber_task, "Subscriber",
                                            SUBSCRIBER_TASK_STACK_SIZE, NULL,
                                            SUBSCRIBER_TASK_PRIORITY,
                                            &subscriber_task_handle);

        if (sub_result == pdPASS) {
            vTaskDelay(pdMS_TO_TICKS(TASK_CREATION_DELAY_MS));

            /* Create publisher task */
            BaseType_t pub_result = xTaskCreate(publisher_task, "Publisher",
                                                PUBLISHER_TASK_STACK_SIZE, NULL,
                                                PUBLISHER_TASK_PRIORITY,
                                                &publisher_task_handle);

            if (pub_result != pdPASS) {
                printf("[MQTT] Publisher task creation failed\n");
                cleanup();
                goto mqtt_wait_for_start;
            }
        } else {
            printf("[MQTT] Subscriber task creation failed\n");
            cleanup();
            goto mqtt_wait_for_start;
        }
    } else {
        printf("[MQTT] Init/Connect failed — will retry on next start request\n");
        cleanup();
        mqtt_started = false;
        mqtt_start_requested = false;
        goto mqtt_wait_for_start;
    }

    /* Main event loop — handle disconnections */
    while (true) {
        if (mqtt_stop_requested) {
            printf("[MQTT] Stop requested\n");
            mqtt_shutting_down = true;

            /* Delete subscriber/publisher FIRST — both are blocked on their
             * own xQueueReceive (not holding MQTT internal mutex), so
             * vTaskDelete is safe here.  Null out their queues to prevent
             * the MQTT event-thread callback from touching dead queues. */
            if (subscriber_task_handle) { vTaskDelete(subscriber_task_handle); subscriber_task_handle = NULL; }
            subscriber_task_q = NULL;
            if (publisher_task_handle) { vTaskDelete(publisher_task_handle); publisher_task_handle = NULL; }
            publisher_task_q = NULL;

            /* Full teardown: cleanup() does disconnect → 300ms wait →
             * delete instance → deinit.  The 300ms gap lets the MQTT
             * event thread release process_mutex before cy_mqtt_delete. */
            cleanup();

            /* Flush stale HANDLE_DISCONNECTION from queue (fired by
             * cy_mqtt_disconnect's async callback before we set the
             * shutting_down guard, or queued with timeout=0). */
            { mqtt_task_cmd_t stale;
              while (xQueueReceive(mqtt_task_q, &stale, 0) == pdTRUE) {} }

            mqtt_shutting_down = false;
            mqtt_stop_requested = false;
            mqtt_started = false;
            mqtt_start_requested = false;
            goto mqtt_wait_for_start;
        }

        if (pdTRUE == xQueueReceive(mqtt_task_q, &mqtt_status, pdMS_TO_TICKS(500))) {
            switch (mqtt_status) {
                case HANDLE_DISCONNECTION:
                    printf("[MQTT] Handling disconnection — reconnecting...\n");
                    mqtt_shutting_down = true;
                    /* Delete sub/pub tasks first (blocked on own queues, safe) */
                    if (subscriber_task_handle) { vTaskDelete(subscriber_task_handle); subscriber_task_handle = NULL; }
                    subscriber_task_q = NULL;
                    if (publisher_task_handle) { vTaskDelete(publisher_task_handle); publisher_task_handle = NULL; }
                    publisher_task_q = NULL;
                    /* cleanup() handles disconnect → 300ms → delete → deinit */
                    cleanup();
                    mqtt_shutting_down = false;

                    /* Retry connection */
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    if ((CY_RSLT_SUCCESS == mqtt_init()) && (CY_RSLT_SUCCESS == mqtt_connect())) {
                        /* Recreate tasks */
                        xTaskCreate(subscriber_task, "Subscriber",
                                    SUBSCRIBER_TASK_STACK_SIZE, NULL,
                                    SUBSCRIBER_TASK_PRIORITY, &subscriber_task_handle);
                        vTaskDelay(pdMS_TO_TICKS(TASK_CREATION_DELAY_MS));
                        xTaskCreate(publisher_task, "Publisher",
                                    PUBLISHER_TASK_STACK_SIZE, NULL,
                                    PUBLISHER_TASK_PRIORITY, &publisher_task_handle);
                    } else {
                        printf("[MQTT] Reconnect failed\n");
                        mqtt_started = false;
                        mqtt_start_requested = false;
                        goto mqtt_wait_for_start;
                    }
                    break;

                case HANDLE_MQTT_SUBSCRIBE_FAILURE:
                    printf("[MQTT] Subscribe failure — reconnecting...\n");
                    mqtt_force_reconnect();
                    break;

                case HANDLE_MQTT_PUBLISH_FAILURE:
                    printf("[MQTT] Publish failure — reconnecting...\n");
                    mqtt_force_reconnect();
                    break;
            }
        }
    }
}
