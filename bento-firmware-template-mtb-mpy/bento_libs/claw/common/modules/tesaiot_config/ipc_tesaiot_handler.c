/*******************************************************************************
 * File Name: ipc_tesaiot_handler.c
 *
 * Description: TESAIoT Config IPC handler — CM33_NS side.
 *              Receives config/mode commands from CM55, dispatches to
 *              tesaiot_config_store for file I/O (which requires
 *              MicroPython task context — NOT ISR).
 *
 *              Architecture: ISR callback → binary semaphore → FreeRTOS task
 *              (same proven pattern as ipc_hsm_handler.c)
 *
 * Author:      Wiroon Sriborrirux (BDH)
 *
 ******************************************************************************/

#include "ipc_tesaiot_handler.h"
#include "ipc_communication.h"
#include "tesaiot_config_store.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "optiga_util.h"
#include <string.h>
#include <stdio.h>

#define printf(...) ((void)0)  /* UART contention fix — see mqtt_task.c */

/* ARM memory barrier for cross-core shared memory coherency */
#ifndef __DMB
#define __DMB() __asm volatile("dmb 0xF" ::: "memory")
#endif

/*******************************************************************************
 * Shared Memory — status snapshot visible to CM55
 * ~170 bytes. Config display fields (device_id, broker_url, port, factory_uid)
 * are embedded directly in tesaiot_status_t to avoid a separate config_view.
 ******************************************************************************/
CY_SECTION_SHAREDMEM static tesaiot_status_t s_tesaiot_status;

/*******************************************************************************
 * Touch pause/resume IPC — shared I2C bus with CM55 FT5406 touch controller.
 * Must pause touch polling before any OPTIGA I2C access.
 ******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t s_optiga_touch_msg;

static void tesaiot_touch_send(uint32_t cmd)
{
    memset(&s_optiga_touch_msg, 0, sizeof(s_optiga_touch_msg));
    s_optiga_touch_msg.client_id = CM55_IPC_SENSOR_CLIENT_ID;
    s_optiga_touch_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_optiga_touch_msg.cmd       = cmd;
    for (int i = 0; i < 50; i++) {
        if (CY_IPC_PIPE_SUCCESS == Cy_IPC_Pipe_SendMessage(
                CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
                (void *)&s_optiga_touch_msg, NULL))
            return;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/*******************************************************************************
 * OPTIGA Trust M probe — detect chip + read UID (27 bytes, OID 0xE0C2)
 ******************************************************************************/
static volatile optiga_lib_status_t s_optiga_cb_status;

static void tesaiot_optiga_cb(void *ctx, optiga_lib_status_t status)
{
    (void)ctx;
    s_optiga_cb_status = status;
}

static optiga_lib_status_t tesaiot_optiga_wait(uint32_t timeout_ms)
{
    while (s_optiga_cb_status == OPTIGA_LIB_BUSY && timeout_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (timeout_ms >= 10) timeout_ms -= 10; else timeout_ms = 0;
    }
    return s_optiga_cb_status;
}

/* Probe OPTIGA: open, read UID, close. Returns true if chip detected.
 * uid_hex_out must be >= 56 chars. Writes full 27-byte UID as hex (54 chars). */
static bool tesaiot_optiga_probe(char *uid_hex_out, size_t hex_len)
{
    optiga_util_t *util = optiga_util_create(0, tesaiot_optiga_cb, NULL);
    if (!util) return false;

    /* Open application */
    s_optiga_cb_status = OPTIGA_LIB_BUSY;
    optiga_util_open_application(util, 0);
    if (tesaiot_optiga_wait(2000) != OPTIGA_LIB_SUCCESS) {
        printf("[TESAIOT] OPTIGA not responding\r\n");
        optiga_util_destroy(util);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Read UID (OID 0xE0C2, 27 bytes) */
    bool detected = false;
    uint8_t uid_raw[27];
    uint16_t uid_len = sizeof(uid_raw);
    s_optiga_cb_status = OPTIGA_LIB_BUSY;
    optiga_lib_status_t rc = optiga_util_read_data(util, 0xE0C2, 0, uid_raw, &uid_len);
    if (rc == OPTIGA_LIB_SUCCESS && tesaiot_optiga_wait(2000) == OPTIGA_LIB_SUCCESS && uid_len > 0) {
        detected = true;
        /* Convert ALL bytes to hex string (27 bytes = 54 hex chars + null) */
        for (int i = 0; i < uid_len && (size_t)(i * 2 + 2) < hex_len; i++) {
            snprintf(&uid_hex_out[i * 2], 3, "%02X", uid_raw[i]);
        }
        uid_hex_out[(uid_len * 2 < (int)hex_len) ? uid_len * 2 : (int)hex_len - 1] = '\0';
        printf("[TESAIOT] OPTIGA UID: %s (%u bytes)\r\n", uid_hex_out, uid_len);
    }

    /* Close application */
    s_optiga_cb_status = OPTIGA_LIB_BUSY;
    optiga_util_close_application(util, 0);
    tesaiot_optiga_wait(2000);
    optiga_util_destroy(util);

    return detected;
}

/*******************************************************************************
 * Static task + semaphore (all statically allocated — no heap)
 ******************************************************************************/
#define TESAIOT_CFG_TASK_STACK_WORDS  1024  /* 4KB — enough for config save */
#define TESAIOT_CFG_TASK_PRIORITY     3  /* Same as MicroPython REPL — must respond to user IPC commands promptly */

static StackType_t        s_task_stack[TESAIOT_CFG_TASK_STACK_WORDS];
static StaticTask_t       s_task_tcb;
static SemaphoreHandle_t  s_sem;
static StaticSemaphore_t  s_sem_buf;

/*******************************************************************************
 * Pending command buffer (ISR writes, task reads)
 ******************************************************************************/
typedef struct {
    uint32_t cmd;
    char     data[IPC_DATA_MAX_LEN];
    uint32_t value;
} tesaiot_pending_cmd_t;

static volatile tesaiot_pending_cmd_t s_pending;

/*******************************************************************************
 * Sync config display fields into status struct (for CM55 UI)
 ******************************************************************************/
static void sync_config_to_status(void)
{
    const tesaiot_config_t *cfg = tesaiot_config_get_ptr();
    if (!cfg) return;

    /* Device ID: mTLS mode uses mtls_device_id, others use device_id */
    const char *active_device_id = (cfg->tls_mode == TESAIOT_MODE_MTLS)
        ? cfg->mtls_device_id : cfg->device_id;
    strncpy((char *)s_tesaiot_status.device_id, active_device_id,
            sizeof(s_tesaiot_status.device_id) - 1);
    s_tesaiot_status.device_id[sizeof(s_tesaiot_status.device_id) - 1] = '\0';

    strncpy((char *)s_tesaiot_status.factory_uid, cfg->factory_uid,
            sizeof(s_tesaiot_status.factory_uid) - 1);
    s_tesaiot_status.factory_uid[sizeof(s_tesaiot_status.factory_uid) - 1] = '\0';

    /* Broker URL + port — derive from active mode.
     * Port is mode-dependent (config stores only one port field):
     *   Mode 0 (mTLS):       broker:8883
     *   Mode 1 (serverTLS+HTTPs): broker:8883
     *   Mode 2 (serverTLS+MQTTs): broker:8884
     *   Mode 3 (REST API):   api_host:api_port */
    if (cfg->tls_mode == TESAIOT_MODE_HTTPS_API) {
        strncpy((char *)s_tesaiot_status.broker_url, cfg->api_host,
                sizeof(s_tesaiot_status.broker_url) - 1);
        s_tesaiot_status.port = cfg->api_port;
    } else {
        strncpy((char *)s_tesaiot_status.broker_url, cfg->broker,
                sizeof(s_tesaiot_status.broker_url) - 1);
        switch (cfg->tls_mode) {
        case TESAIOT_MODE_MTLS:
        case TESAIOT_MODE_MTLS_SW:
            s_tesaiot_status.port = 8883;
            break;
        case TESAIOT_MODE_SERVER_TLS:
        default:
            s_tesaiot_status.port = 8884;
            break;
        }
    }
    s_tesaiot_status.broker_url[sizeof(s_tesaiot_status.broker_url) - 1] = '\0';

    /* Debug: password length per mode (device_id already in status for User/Client display) */
    switch (cfg->tls_mode) {
    case TESAIOT_MODE_MTLS:
    case TESAIOT_MODE_MTLS_SW:
        s_tesaiot_status.dbg_pass_len = 0;  /* mTLS: no password (cert auth) */
        break;
    case TESAIOT_MODE_SERVER_TLS:
    default:
        s_tesaiot_status.dbg_pass_len = (uint8_t)strlen(cfg->mqtt_pass);
        break;
    }

    __DMB();
}

/*******************************************************************************
 * Push status update (set flag + memory barrier)
 ******************************************************************************/
static void push_status_flag(uint32_t flag)
{
    s_tesaiot_status.flags |= flag;
    __DMB();
}

/*******************************************************************************
 * Task: Process config commands (runs in FreeRTOS task context)
 ******************************************************************************/
static void tesaiot_cfg_task_func(void *arg)
{
    (void)arg;

    for (;;) {
        /*
         * CRITICAL: NO printf in this loop.
         *
         * retarget-io's printf acquires a FreeRTOS mutex with CY_RTOS_NEVER_TIMEOUT
         * and NO priority inheritance.  After MQTT connects, the MQTT/TCPIP/WHD tasks
         * (priorities 2-5) all use printf for logging.  If this task (priority 3)
         * calls printf while a lower-priority task holds the UART mutex, classic
         * priority inversion occurs: the lower-priority task is preempted by
         * higher-priority tasks and never releases the mutex → this task blocks
         * forever → DISCONNECT IPC never processed.
         *
         * All status reporting uses shared memory (s_tesaiot_status) instead.
         */

        if (xSemaphoreTake(s_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint32_t cmd = s_pending.cmd;

        switch (cmd) {
        case IPC_CMD_TESAIOT_MODE_SWITCH: {
            /* data[0] = new mode value (0-3) */
            uint8_t new_mode = (uint8_t)s_pending.data[0];
            if (new_mode < TESAIOT_MODE_COUNT) {
                char mode_str[4];
                snprintf(mode_str, sizeof(mode_str), "%u", new_mode);

                s_tesaiot_status.switching = 1;
                push_status_flag(TESAIOT_FLAG_CONFIG_CHANGED);

                if (tesaiot_config_set_field_nosave("tls_mode", mode_str)) {
                    s_tesaiot_status.active_mode = new_mode;
                    sync_config_to_status();
                    printf("[TESAIOT_IPC] mode switched to %u\r\n", new_mode);
                } else {
                    printf("[TESAIOT_IPC] mode switch failed\r\n");
                    push_status_flag(TESAIOT_FLAG_ERROR);
                }

                s_tesaiot_status.switching = 0;
                push_status_flag(TESAIOT_FLAG_CONFIG_CHANGED);
            }
            break;
        }

        case IPC_CMD_TESAIOT_CONFIG_SET: {
            /* data[] = "key=value\0" */
            char *eq = strchr((char *)s_pending.data, '=');
            if (eq) {
                *eq = '\0';
                const char *key = (char *)s_pending.data;
                const char *val = eq + 1;

                if (tesaiot_config_set_field_nosave(key, val)) {
                    sync_config_to_status();
                    push_status_flag(TESAIOT_FLAG_CONFIG_CHANGED);
                    printf("[TESAIOT_IPC] config set: %s=%s\r\n", key, val);
                } else {
                    printf("[TESAIOT_IPC] config set failed: %s\r\n", key);
                }
            }
            break;
        }

        case IPC_CMD_TESAIOT_CONFIG_GET: {
            /* Refresh config fields — config store may have loaded after init */
            sync_config_to_status();

            /* Re-evaluate OPTIGA state from factory_uid */
            if (s_tesaiot_status.factory_uid[0] != '\0' &&
                s_tesaiot_status.optiga_state == TESAIOT_OPTIGA_NA) {
                s_tesaiot_status.optiga_state = TESAIOT_OPTIGA_DETECTED;
            }

            /* Status pointer already written in-place by ISR callback */
            push_status_flag(TESAIOT_FLAG_CONFIG_CHANGED);
            printf("[TESAIOT_IPC] status refreshed for CM55\r\n");
            break;
        }

        case IPC_CMD_TESAIOT_CONNECT: {
            extern bool tesaiot_mqtt_connect(void);
            printf("[TESAIOT_IPC] connect requested\r\n");
            tesaiot_mqtt_connect();  /* non-blocking: starts MQTT task, returns immediately */
            break;
        }

        case IPC_CMD_TESAIOT_DISCONNECT: {
            extern bool tesaiot_mqtt_disconnect(void);
            printf("[TESAIOT_IPC] disconnect requested\r\n");
            tesaiot_mqtt_disconnect();
            break;
        }

        default:
            printf("[TESAIOT_IPC] unknown cmd 0x%02lX\r\n", (unsigned long)cmd);
            break;
        }

        /* No printf here — see priority inversion comment above */
    }
}

/*******************************************************************************
 * ISR Callback — registered with IPC pipe
 ******************************************************************************/
static void tesaiot_cfg_ipc_callback(uint32_t *msg_data)
{
    if (!msg_data) return;
    ipc_msg_t *msg = (ipc_msg_t *)msg_data;

    /* Accept TESAIoT config commands only */
    if (msg->cmd != IPC_CMD_TESAIOT_MODE_SWITCH &&
        msg->cmd != IPC_CMD_TESAIOT_CONFIG_SET &&
        msg->cmd != IPC_CMD_TESAIOT_CONFIG_GET &&
        msg->cmd != IPC_CMD_TESAIOT_CONNECT &&
        msg->cmd != IPC_CMD_TESAIOT_DISCONNECT) {
        return;
    }

    /*
     * CONFIG_GET: Write shared memory addresses IN-PLACE into the sender's
     * CY_SECTION_SHAREDMEM ipc_msg_t buffer (CM55's s_cmd_msg).
     * Zero additional shared memory cost — reuses sender's buffer.
     * ISR context is safe: IPC pipe hasn't released yet, so buffer is ours.
     */
    if (msg->cmd == IPC_CMD_TESAIOT_CONFIG_GET) {
        msg->cmd   = IPC_CMD_TESAIOT_STATUS_PUSH;
        msg->value = (uint32_t)(uintptr_t)&s_tesaiot_status;
        __DMB();
    }

    /* Copy to pending buffer (ISR-safe: memcpy is interrupt-safe for fixed-size) */
    s_pending.cmd   = msg->cmd;
    s_pending.value = msg->value;
    memcpy((void *)s_pending.data, msg->data, IPC_DATA_MAX_LEN);

    /* Wake task */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

void ipc_tesaiot_handler_init(void)
{
    /* Initialize shared status */
    memset(&s_tesaiot_status, 0, sizeof(s_tesaiot_status));
    s_tesaiot_status.active_mode = tesaiot_config_get_ptr()->tls_mode;

    /* Populate config display fields for CM55 UI */
    sync_config_to_status();

    /* Root CA is embedded at compile time — cert always available */
    s_tesaiot_status.cert_state = TESAIOT_CERT_LOADED;

    /* If factory_uid is populated, OPTIGA has been detected previously */
    if (s_tesaiot_status.factory_uid[0] != '\0') {
        s_tesaiot_status.optiga_state = TESAIOT_OPTIGA_DETECTED;
    }

    /* Create semaphore + task */
    s_sem = xSemaphoreCreateBinaryStatic(&s_sem_buf);

    xTaskCreateStatic(
        tesaiot_cfg_task_func,
        "TESAIOT_CFG",
        TESAIOT_CFG_TASK_STACK_WORDS,
        NULL,
        TESAIOT_CFG_TASK_PRIORITY,
        s_task_stack,
        &s_task_tcb);

    /* Register IPC callback for config commands from CM55 */
    (void)Cy_IPC_Pipe_RegisterCallback(
        CM33_IPC_PIPE_EP_ADDR,
        tesaiot_cfg_ipc_callback,
        (uint32_t)CM33_IPC_TESAIOT_CFG_CLIENT_ID);

    printf("[TESAIOT_IPC] handler initialized\r\n");
}

tesaiot_status_t *ipc_tesaiot_get_status(void)
{
    return &s_tesaiot_status;
}

void ipc_tesaiot_refresh_status(void)
{
    sync_config_to_status();

    /* Root CA always embedded */
    s_tesaiot_status.cert_state = TESAIOT_CERT_LOADED;

    /* OPTIGA hardware detection — probe chip if UID missing or truncated.
     * Full UID = 27 bytes = 54 hex chars. Re-probe if shorter (legacy save). */
    if (s_tesaiot_status.factory_uid[0] == '\0' ||
        strlen((const char *)s_tesaiot_status.factory_uid) < 54) {
        /* Pause CM55 touch (shared I2C bus) */
        tesaiot_touch_send(IPC_CMD_TOUCH_PAUSE);
        vTaskDelay(pdMS_TO_TICKS(50));

        char uid_hex[56] = {0};
        if (tesaiot_optiga_probe(uid_hex, sizeof(uid_hex))) {
            s_tesaiot_status.optiga_state = TESAIOT_OPTIGA_DETECTED;
            strncpy((char *)s_tesaiot_status.factory_uid, uid_hex,
                    sizeof(s_tesaiot_status.factory_uid) - 1);
            /* Save UID to config store for next boot */
            tesaiot_config_set_field("factory_uid", uid_hex);
        } else {
            s_tesaiot_status.optiga_state = TESAIOT_OPTIGA_FAILED;
        }

        tesaiot_touch_send(IPC_CMD_TOUCH_RESUME);
    } else {
        s_tesaiot_status.optiga_state = TESAIOT_OPTIGA_DETECTED;
    }

    push_status_flag(TESAIOT_FLAG_CONFIG_CHANGED | TESAIOT_FLAG_OPTIGA_CHANGED);
}
