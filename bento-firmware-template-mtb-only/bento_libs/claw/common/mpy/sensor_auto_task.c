/*******************************************************************************
 * File Name: sensor_auto_task.c
 *
 * Description: Auto-sensor background FreeRTOS task for CM33_NS.
 *              Initializes all sensors, then periodically reads and pushes
 *              data via IPC to CM55 for LVGL dashboard display.
 *
 *              - Starts automatically on boot (no MicroPython interaction)
 *              - Uses separate IPC message buffer from modsensors.c
 *              - Acquires I2C mutex for thread-safe bus access
 *              - MicroPython can pause/resume via sensors.auto() API
 *
 *******************************************************************************/

#include "sensor_auto_task.h"
#include "sensor_i2c.h"
#include "sensor_bmi270.h"
#include "bsp_feature_flags.h"
#include "ipc_communication.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "wifi_init.h"
#include "cy_wcm.h"
#include "ntp_sync.h"
#include "cy_rtc.h"
#include "lfs_wifi_creds.h"
#include "ipc_tesaiot_handler.h"
#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1)
#include "ble_nus.h"
#endif
#include <string.h>
#include <stdio.h>

#define printf(...) ((void)0)  /* UART contention fix — see mqtt_task.c */

#if BSP_HAS_DPS368
#include "sensor_dps368.h"
#endif
#if BSP_HAS_SHT40
#include "sensor_sht40.h"
#endif
#if BSP_HAS_BMM350
#include "sensor_bmm350.h"
#endif
#if BSP_HAS_CAPSENSE
#include "sensor_capsense.h"
#endif
#if BSP_HAS_POTENTIOMETER
#include "sensor_potentiometer.h"
#endif

/*******************************************************************************
 * Configuration
 *******************************************************************************/
#define AUTO_TASK_STACK_SIZE  (4096 / sizeof(StackType_t))
#define AUTO_TASK_PRIORITY   (2)         /* Below MicroPython(3) */
#define AUTO_TASK_NAME       "SensorAuto"

#define AUTO_IPC_SEND_RETRIES    (20)
#define AUTO_IPC_RETRY_DELAY_US  (100)
#define AUTO_IPC_GAP_US          (200)   /* Gap between IPC messages */
#define AUTO_I2C_LOCK_TIMEOUT_MS (50)
#define AUTO_WIFI_REQ_QUEUE_LEN  (8)
#define AUTO_WIFI_IPC_STACK_WORDS  (4096 / sizeof(StackType_t))
#define AUTO_WIFI_IPC_TASK_NAME    "WiFiIPC"
#define AUTO_WIFI_IPC_TASK_PRIORITY  AUTO_TASK_PRIORITY  /* = 2, MUST be < TCPIP(4)/WCM(4) */
#define AUTO_WIFI_SCAN_TIMEOUT_MS (10000)
#define AUTO_WIFI_STATUS_DATA_LEN WIFI_STATUS_LEN_V2  /* 53 original + 6 bytes MAC at offset 53 */
#define WIFI_SCAN_COLLECT_MAX     (20)  /* Collect up to 20, sort by RSSI, send top 6 */

/* Boot auto-connect retry config */
#define BOOT_WIFI_MAX_RETRIES       (5)
#define BOOT_WIFI_RETRY_INTERVAL_MS (2000)

/* Boot WiFi credentials — loaded by mpy_main.c from LFS at boot, then
 * mutated by three runtime writers on three different tasks:
 *
 *   1. The legacy WiFi-IPC worker handling IPC_CMD_WIFI_CONNECT (this
 *      file, sensor_auto_task() context).
 *   2. The dual-band BLE worker handling bento.wifi.connect (the
 *      KIT_PSE84_AI-DualBand-TinyPython-BentoClaw firmware's
 *      dualband_wifi_connect_worker on its own FreeRTOS task).
 *   3. The MicroPython REPL idle handler that drains
 *      g_boot_wifi_creds_dirty into LFS (mpy_main.c).
 *
 * The struct is ~100 B; on Cortex-M33 a torn-write window is real.
 * Wrap every read + write through wifi_creds_lock/unlock helpers
 * (forward-declared in sensor_auto_task.h). The lock is a recursive
 * FreeRTOS mutex — boot-time read paths take it once outside any
 * task context (scheduler not yet started → take is a no-op),
 * runtime paths take it freely. */
qspi_wifi_entry_t g_boot_wifi_creds[QSPI_WIFI_CREDS_MAX];
volatile int g_boot_wifi_creds_count = 0;

/* Deferred QSPI save flag.  WiFi IPC worker sets this after a successful
 * IPC_CMD_WIFI_CONNECT (cannot call lfs_wifi_creds_write directly because
 * that requires MicroPython task context).  mpy_main.c flushes the dirty
 * credentials to QSPI at the next safe opportunity (soft reset, REPL idle). */
volatile bool g_boot_wifi_creds_dirty = false;

/* Mutex protecting the three globals above. Recursive so a critical
 * section that needs to read-then-write doesn't deadlock against
 * itself. Created lazily on first wifi_creds_lock() call so the
 * helper is safe to invoke from boot-time code paths that may run
 * before the scheduler starts. */
static SemaphoreHandle_t s_wifi_creds_mutex = NULL;
static StaticSemaphore_t s_wifi_creds_mutex_buf;

void wifi_creds_lock(void)
{
    if (s_wifi_creds_mutex == NULL) {
        s_wifi_creds_mutex =
            xSemaphoreCreateRecursiveMutexStatic(&s_wifi_creds_mutex_buf);
    }
    /* Pre-scheduler call sites (mpy_main boot path) reach here before
     * vTaskStartScheduler — xSemaphoreTakeRecursive returns errQUEUE_EMPTY
     * but execution is single-threaded by definition, so a no-op lock
     * is correct. Same shape FreeRTOS itself uses for its own lazy
     * primitives. */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        (void)xSemaphoreTakeRecursive(s_wifi_creds_mutex, portMAX_DELAY);
    }
}

void wifi_creds_unlock(void)
{
    if (s_wifi_creds_mutex == NULL) return;
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        (void)xSemaphoreGiveRecursive(s_wifi_creds_mutex);
    }
}

/*******************************************************************************
 * Static Data
 *******************************************************************************/

/* Own IPC buffer — must NOT share with modsensors.c static sensor_ipc_msg */
CY_SECTION_SHAREDMEM static ipc_msg_t s_auto_ipc_msg;

static TaskHandle_t s_auto_task_handle = NULL;
static volatile bool s_running = false;
static volatile bool s_paused = false;
static volatile uint32_t s_interval_ms = 100;    /* Default 10Hz */

/* Below this interval the loop enters IMU-fast mode: only the accelerometer is
 * read, because the magnetometer, CapSense, potentiometer and environmental
 * sensors each cost milliseconds of I2C and would stretch the loop far past the
 * requested period. The CM55 Edge AI engine asks for 20 ms (50 Hz) because its
 * motion model was trained at that rate; at 10 Hz the same gesture arrives
 * five times too slow and the model, correctly, sees nothing it recognises. */
#define SENSOR_AUTO_FAST_THRESHOLD_MS  (50u)
static volatile uint32_t s_enabled_mask = SENSOR_AUTO_ALL;
static volatile uint32_t s_push_count = 0;
static volatile uint16_t s_auto_seq = 0;
static bool s_ctrl_cb_registered = false;
/* This file serves both variants. mtb-mpy compiles it from Makefile.micropython
 * with no flag, so the default keeps that path byte-identical. mtb-only
 * compiles it from the project Makefile with BENTO_HAS_MPY=0. */
#ifndef BENTO_HAS_MPY
#define BENTO_HAS_MPY 1
#endif

#if BENTO_HAS_MPY
extern void mpy_request_safe_boot_once(void);
/* ISR-safe Delete-main.py trigger (tacp.c) — breaks a looping script + soft
 * reset so the boot path removes /main.py even when tacp_poll_uart() is starved. */
extern void tacp_request_delete_main_from_isr(void);
#endif

/* IPC initialized flag (separate from modsensors.c) */
static bool s_ipc_initialized = false;

/* Delete main.py request: set from ISR, polled from task context */
static volatile bool s_delete_pending = false;

/* Restart script request: set from ISR, polled from task context */
static volatile bool s_restart_pending = false;

/* BMI270 cache — written by auto_push_bmi270(), read by BentoClaw */
static volatile sensor_auto_bmi270_cache_t s_bmi270_cache = { 0 };

/* WiFi IPC request queue (CM55 -> CM33) */
typedef struct {
    uint32_t cmd;
    uint32_t value;
    uint8_t data[IPC_DATA_MAX_LEN];
    ipc_response_t *resp;
} wifi_ipc_req_t;

typedef struct {
    ipc_wifi_scan_entry_t entries[WIFI_SCAN_COLLECT_MAX];
    uint8_t count;
    SemaphoreHandle_t done;
} wifi_scan_ctx_t;

static QueueHandle_t s_wifi_req_queue = NULL;
static TaskHandle_t s_wifi_ipc_task_handle = NULL;

#define AUTO_WIFI_MODE_NONE    (0U)
#define AUTO_WIFI_MODE_SOFTAP  (1U)
#define AUTO_WIFI_MODE_STA     (2U)

static struct {
    uint8_t mode;
    bool connected;
    char ssid[33];
    char ip[16];
    int8_t rssi;
} s_wifi_state = {
    .mode = AUTO_WIFI_MODE_NONE,
    .connected = false,
    .ssid = "",
    .ip = "0.0.0.0",
    .rssi = 0,
};

static cy_rslt_t s_wifi_last_error = CY_RSLT_SUCCESS;

#define AUTO_WIFI_ERR_QUEUE_FULL   (0xDEAD6601UL)
#define AUTO_WIFI_ERR_TIMEOUT      (0xDEAD6602UL)
#define AUTO_WIFI_ERR_BAD_ARG      (0xDEAD6603UL)

static void wifi_ipc_handle_request(const wifi_ipc_req_t *req);
static void wifi_ipc_worker_task(void *arg);
static void wifi_ipc_reply_error_isr(ipc_response_t *resp, uint32_t cmd, uint32_t err);

/*******************************************************************************
 * IPC callback (CM55 -> CM33): pause/resume background auto-sensor task
 *
 * Command: IPC_CMD_SENSOR_AUTO_CTRL
 *   data[0] == 0: stop/pause auto push
 *   data[0] == 1: start/resume auto push
 *******************************************************************************/
static void sensor_auto_ctrl_callback(uint32_t *msg_data)
{
    if (msg_data == NULL) {
        return;
    }

    const ipc_msg_t *msg = (const ipc_msg_t *)msg_data;
    if (msg->cmd == IPC_CMD_SYSTEM_SAFE_REBOOT) {
#if BENTO_HAS_MPY
        /* Request one-shot safe boot on next reset so /main.py is skipped. */
        mpy_request_safe_boot_once();
#endif /* no VM: nothing to skip — acknowledge and ignore */
        return;
    }
    //! [mpy_tacp_delete_main_isr_escape]
    /* ...context: inside the sensor-ctrl IPC ISR callback ... */
    if (msg->cmd == IPC_CMD_DELETE_MAIN_PY) {
        /* Graceful path: polled by tacp_poll_uart() in task context (works when
         * the MP loop is alive, e.g. at the REPL). */
        s_delete_pending = true;
        /* Robust path: break the script + force a MicroPython soft reset DIRECTLY
         * from this IPC ISR, so the Delete button works even when a runaway
         * /main.py loop monopolises the CPU and starves tacp_poll_uart(). The
         * soft reset keeps SRAM (s_delete_main_flag) intact -> boot removes
         * /main.py. ISR-safe (no allocation, no hard reset). Ported from
         * Game-libraries 8a0689a (HW-verified on Eva). */
#if BENTO_HAS_MPY
        tacp_request_delete_main_from_isr();
#endif /* no VM: there is no /main.py to delete */
//! [mpy_tacp_delete_main_isr_escape]
        return;
    }
    if (msg->cmd == IPC_CMD_RESTART_SCRIPT) {
        /* Soft-reset to re-run main.py without deletion or hard reboot. */
        s_restart_pending = true;
        return;
    }

    //! [j3_wifi_ipc_isr_enqueue]
    /* ...context: inside the IPC ISR callback - ISR context, no printf ... */
    if (msg->cmd >= IPC_CMD_WIFI_SCAN && msg->cmd <= IPC_CMD_WIFI_SOFTAP) {
        /* NOTE: No printf here — this is ISR context, printf is NOT safe */
        ipc_response_t *resp = (ipc_response_t *)msg->value;
        if (s_wifi_req_queue == NULL) {
            wifi_ipc_reply_error_isr(resp, msg->cmd, AUTO_WIFI_ERR_QUEUE_FULL);
            return;
        }

        wifi_ipc_req_t req;
        memset(&req, 0, sizeof(req));
        req.cmd = msg->cmd;
        req.value = msg->value;
        req.resp = resp;
        memcpy(req.data, msg->data, sizeof(req.data));

        BaseType_t hpw = pdFALSE;
        if (xQueueSendFromISR(s_wifi_req_queue, &req, &hpw) != pdTRUE) {
            wifi_ipc_reply_error_isr(resp, msg->cmd, AUTO_WIFI_ERR_QUEUE_FULL);
        }
        /* WiFiIPC worker wakes automatically via xQueueReceive */
        portYIELD_FROM_ISR(hpw);
        return;
    }
    //! [j3_wifi_ipc_isr_enqueue]

    if (msg->cmd != IPC_CMD_SENSOR_AUTO_CTRL) return;

    uint8_t op = (uint8_t)msg->data[0];
    if (op == 0U) {
        s_paused = true;
        return;
    }

    /* op == 2: set the push interval, data[1..2] = interval in ms (LE).
     * Used by the CM55 Edge AI engine, whose models are trained at a fixed
     * sample rate (e.g. 50 Hz) that the 10 Hz dashboard default cannot feed.
     * sensor_auto_set_rate clamps to the safe 20..5000 ms range; anything
     * below SENSOR_AUTO_FAST_THRESHOLD_MS also switches the loop to reading
     * the accelerometer alone so the period is actually achievable. */
    if (op == 2U) {
        uint32_t ms = (uint32_t)((uint8_t)msg->data[1]) |
                      ((uint32_t)((uint8_t)msg->data[2]) << 8);
        if (ms != 0U) {
            sensor_auto_set_rate(ms);
        }
        /* Setting the model sample rate also means "I want the push task
         * running" -- the Edge AI engine is the only sender of op=2, and if a
         * prior MicroPython/Playground session left the task suspended, the rate
         * alone would never restart it. Resume here so the model start needs
         * only ONE message (no second op=1 that would collide on the shared IPC
         * buffer and drop the rate). ISR-safe. */
        s_paused = false;
        if (s_auto_task_handle != NULL && !s_running) {
            (void)xTaskResumeFromISR(s_auto_task_handle);
        }
        return;
    }

    /* Resume is ISR-safe. If task is already running this is harmless. */
    s_paused = false;
    if (s_auto_task_handle != NULL && !s_running) {
        (void)xTaskResumeFromISR(s_auto_task_handle);
    }
}

/*******************************************************************************
 * IPC Send (own buffer, same pattern as modsensors.c sensor_ipc_send)
 *******************************************************************************/
static bool auto_ipc_send(uint32_t cmd, const void *data, size_t len) {
    if (len > IPC_DATA_MAX_LEN) return false;

    if (!s_ipc_initialized) {
        cm33_ipc_communication_setup();
        Cy_SysLib_Delay(50);
        s_ipc_initialized = true;
    }

    memset(&s_auto_ipc_msg, 0, sizeof(s_auto_ipc_msg));
    s_auto_ipc_msg.client_id = CM55_IPC_SENSOR_CLIENT_ID;
    s_auto_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_auto_ipc_msg.cmd = cmd;
    s_auto_ipc_msg.value = (uint32_t)len;
    memcpy(s_auto_ipc_msg.data, data, len);

    int retries = AUTO_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t status;
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&s_auto_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) return true;
        Cy_SysLib_DelayUs(AUTO_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    return false;
}

/*******************************************************************************
 * WiFi IPC bridge (CM55 -> CM33)
 *******************************************************************************/
//! [j3_wifi_ipc_reply_dmb_barrier]
static void wifi_ipc_reply(ipc_response_t *resp, uint32_t cmd, uint8_t status,
                           const void *data, uint16_t data_len)
{
    if (resp == NULL) {
        return;
    }

    memset((void *)resp, 0, sizeof(*resp));
    resp->cmd = (uint8_t)cmd;
    resp->status = status;
    if (data != NULL && data_len > 0) {
        if (data_len > IPC_RESPONSE_DATA_MAX) {
            data_len = IPC_RESPONSE_DATA_MAX;
        }
        memcpy(resp->data, data, data_len);
        resp->data_len = data_len;
    }

    __DMB();
    resp->ready = 1;
}
//! [j3_wifi_ipc_reply_dmb_barrier]

static void wifi_ipc_reply_error_isr(ipc_response_t *resp, uint32_t cmd, uint32_t err)
{
    if (resp == NULL) {
        return;
    }

    resp->cmd = (uint8_t)cmd;
    resp->status = 1;
    resp->reserved = 0;
    resp->reserved2 = 0;
    resp->data_len = 4;
    memcpy(resp->data, &err, 4);
    __DMB();
    resp->ready = 1;
}

static void wifi_update_ip_from_wcm(void)
{
    cy_wcm_ip_address_t ip;
    memset(&ip, 0, sizeof(ip));

    s_wifi_state.ip[0] = '\0';
    if (CY_RSLT_SUCCESS != cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_STA, &ip)) {
        (void)cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_AP, &ip);
    }

    if (ip.version == CY_WCM_IP_VER_V4) {
        uint32_t v4 = ip.ip.v4;
        snprintf(s_wifi_state.ip, sizeof(s_wifi_state.ip), "%lu.%lu.%lu.%lu",
                 (unsigned long)(v4 & 0xFFU),
                 (unsigned long)((v4 >> 8) & 0xFFU),
                 (unsigned long)((v4 >> 16) & 0xFFU),
                 (unsigned long)((v4 >> 24) & 0xFFU));
    }

    if (s_wifi_state.ip[0] == '\0') {
        strncpy(s_wifi_state.ip, "0.0.0.0", sizeof(s_wifi_state.ip) - 1);
    }
}

//! [j3_wifi_connect_robust]
static bool wifi_ensure_ready(void)
{
    if (app_wifi_is_ready()) {
        return true;
    }

    cy_rslt_t r = app_wifi_init();
    if (r != CY_RSLT_SUCCESS) {
        s_wifi_last_error = r;
        return false;
    }

    s_wifi_last_error = CY_RSLT_SUCCESS;
    return true;
}

/*******************************************************************************
 * Robust WCM connect — rides out the marginal COLD first-join after boot.
 * See BENTO-TESAIoT-Game-libraries/docs/WIFI_NTP_COLD_JOIN_KNOWHOW.md for the full
 * root-cause writeup. In short: on a cold boot the CYW55500 RX buffer pool
 * (WHD_DEF_MAX_RXBUFPOST=14 x ~2KB) is malloc'd fresh from the ~81KB shared newlib
 * heap during the association burst; a momentary buffer shortfall drops the frame
 * that sets JOIN_SSID_SET (WLC_E_SET_SSID) and the join returns WHD_JOIN_IN_PROGRESS
 * (0x020003FF). Each cy_wcm_connect_ap() tears its half-join down on failure, so
 * re-issuing (with disconnect + settle between) is the only recovery. Warm reconnect
 * succeeds on the first try. Applies to every STA connect path.
 ******************************************************************************/
#define WIFI_CONNECT_MAX_ATTEMPTS   (6)
#define WIFI_CONNECT_SETTLE_MS      (1500)

static cy_rslt_t wifi_connect_robust(cy_wcm_connect_params_t *params,
                                     cy_wcm_ip_address_t *ip, const char *tag)
{
    cy_rslt_t r = CY_RSLT_TYPE_ERROR;
    for (int attempt = 1; attempt <= WIFI_CONNECT_MAX_ATTEMPTS; attempt++) {
        r = cy_wcm_connect_ap(params, ip);
        if (CY_RSLT_SUCCESS == r || cy_wcm_is_connected_to_ap()) {
            return CY_RSLT_SUCCESS;
        }
        printf("[%s] attempt %d/%d failed (0x%08lX)\r\n",
               tag, attempt, WIFI_CONNECT_MAX_ATTEMPTS, (unsigned long)r);
        if (attempt < WIFI_CONNECT_MAX_ATTEMPTS) {
            cy_wcm_disconnect_ap();               /* clear WHD join FSM for a clean retry */
            vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_SETTLE_MS));
        }
    }
    return r;
}
//! [j3_wifi_connect_robust]

/* NTP sync guard — ntp_sync_rtc() is in ntp_sync.c (MTB-compiled) */
static bool s_ntp_synced = false;

/*******************************************************************************
 * WiFi State + Time Push to CM55 (non-blocking, fire-and-forget)
 *
 * CM55 topbar reads these via volatile flags — no I2C, no blocking.
 * Uses auto_ipc_send() which targets CM55_IPC_SENSOR_CLIENT_ID (6).
 ******************************************************************************/
static void push_wifi_state_to_cm55(bool connected)
{
    uint8_t state = connected ? 1U : 0U;
    auto_ipc_send(IPC_CMD_WIFI_STATE_PUSH, &state, 1);

    /* Bridge: update TESAIoT shared status for Connectivity page */
    tesaiot_status_t *ts = ipc_tesaiot_get_status();
    if (ts) {
        ts->wifi_state = connected ? TESAIOT_CONN_CONNECTED : TESAIOT_CONN_OFF;
        strncpy((char *)ts->ip_addr, s_wifi_state.ip, sizeof(ts->ip_addr) - 1);
        ((char *)ts->ip_addr)[sizeof(ts->ip_addr) - 1] = '\0';
        ts->flags |= TESAIOT_FLAG_WIFI_CHANGED;
        __DMB();
    }
}

static void push_time_to_cm55(void)
{
    /* Read RTC (on CM33_NS where it's accessible) and format for CM55 display */
    cy_stc_rtc_config_t rtc;
    Cy_RTC_GetDateAndTime(&rtc);

    if (rtc.year < 25 || rtc.month < 1 || rtc.month > 12 ||
        rtc.dayOfWeek < 1 || rtc.dayOfWeek > 7) {
        return;  /* RTC not valid yet */
    }

    static const char * const dow[] = {
        "", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char * const mon[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d %s %02d:%02d",
             dow[rtc.dayOfWeek], (int)rtc.date,
             mon[rtc.month], (int)rtc.hour, (int)rtc.min);

    auto_ipc_send(IPC_CMD_TIME_PUSH, buf, strnlen(buf, sizeof(buf)) + 1);
}

/* Bridge: mark NTP synced in TESAIoT shared status */
static void tesaiot_bridge_ntp_synced(void)
{
    tesaiot_status_t *ts = ipc_tesaiot_get_status();
    if (ts) {
        ts->ntp_state = TESAIOT_CONN_CONNECTED;
        ts->flags |= TESAIOT_FLAG_NTP_CHANGED;
        __DMB();
    }
}

static void wifi_scan_callback(cy_wcm_scan_result_t *result,
                               void *user_data,
                               cy_wcm_scan_status_t status)
{
    wifi_scan_ctx_t *ctx = (wifi_scan_ctx_t *)user_data;
    if (ctx == NULL) {
        return;
    }

    if (status == CY_WCM_SCAN_INCOMPLETE) {
        if (result != NULL && ctx->count < WIFI_SCAN_COLLECT_MAX && result->SSID[0] != '\0') {
            ipc_wifi_scan_entry_t *e = &ctx->entries[ctx->count];
            memset(e, 0, sizeof(*e));
            strncpy(e->ssid, (const char *)result->SSID, sizeof(e->ssid) - 1);
            e->rssi = result->signal_strength;
            e->channel = result->channel;

            if (result->security == CY_WCM_SECURITY_OPEN) {
                e->security = 0;
            } else if (result->security <= CY_WCM_SECURITY_WEP_PSK) {
                e->security = 1;
            } else if (result->security <= CY_WCM_SECURITY_WPA_MIXED_PSK) {
                e->security = 2;
            } else if (result->security <= CY_WCM_SECURITY_WPA2_FBT_PSK) {
                e->security = 3;
            } else {
                e->security = 4;
            }

            ctx->count++;
        }
    } else if (status == CY_WCM_SCAN_COMPLETE) {
        if (__get_IPSR() != 0U) {
            BaseType_t hpw = pdFALSE;
            xSemaphoreGiveFromISR(ctx->done, &hpw);
            portYIELD_FROM_ISR(hpw);
        } else {
            xSemaphoreGive(ctx->done);
        }
    }
}

static void wifi_ipc_fill_status(uint8_t *buf)
{
    memset(buf, 0, AUTO_WIFI_STATUS_DATA_LEN);
    buf[WIFI_STATUS_OFF_MODE] = s_wifi_state.mode;
    buf[WIFI_STATUS_OFF_CONNECTED] = s_wifi_state.connected ? 1U : 0U;
    buf[WIFI_STATUS_OFF_RSSI] = (uint8_t)s_wifi_state.rssi;
    strncpy((char *)&buf[WIFI_STATUS_OFF_IP], s_wifi_state.ip, WIFI_STATUS_IP_MAXLEN);
    strncpy((char *)&buf[WIFI_STATUS_OFF_SSID], s_wifi_state.ssid, 32);

    /* MAC address at offset 53 (6 bytes) */
    if (app_wifi_is_ready()) {
        cy_wcm_mac_t mac;
        if (cy_wcm_get_mac_addr(CY_WCM_INTERFACE_TYPE_STA, &mac) == CY_RSLT_SUCCESS) {
            memcpy(&buf[WIFI_STATUS_OFF_MAC], mac, WIFI_MAC_ADDR_LEN);
        }
    }
}

static void wifi_ipc_refresh_state(void)
{
    if (!app_wifi_is_ready()) {
        s_wifi_state.mode = AUTO_WIFI_MODE_NONE;
        s_wifi_state.connected = false;
        s_wifi_state.rssi = 0;
        strncpy(s_wifi_state.ip, "0.0.0.0", sizeof(s_wifi_state.ip) - 1);
        if (s_wifi_state.ssid[0] == '\0') {
            strncpy(s_wifi_state.ssid, "", sizeof(s_wifi_state.ssid) - 1);
        }
        return;
    }

    s_wifi_state.connected = cy_wcm_is_connected_to_ap();
    if (s_wifi_state.connected) {
        s_wifi_state.mode = AUTO_WIFI_MODE_STA;
    } else if (s_wifi_state.mode != AUTO_WIFI_MODE_SOFTAP) {
        s_wifi_state.mode = AUTO_WIFI_MODE_NONE;
        s_wifi_state.ssid[0] = '\0';
    }
    wifi_update_ip_from_wcm();
}

//! [j3_wifi_ipc_scan_request]
static void wifi_ipc_handle_request(const wifi_ipc_req_t *req)
{
    uint8_t status_blob[AUTO_WIFI_STATUS_DATA_LEN];
    uint32_t err = 0;

    if (req == NULL || req->resp == NULL) {
        return;
    }

    switch (req->cmd) {
    case IPC_CMD_WIFI_SCAN: {
        printf("[WiFiIPC] SCAN cmd received\r\n");
        if (!wifi_ensure_ready()) {
            err = (uint32_t)s_wifi_last_error;
            printf("[WiFiIPC] SCAN: wifi not ready, err=0x%08lX\r\n", (unsigned long)err);
            wifi_ipc_reply(req->resp, req->cmd, 1, &err, 4);
            break;
        }
        printf("[WiFiIPC] SCAN: wifi ready, starting scan...\r\n");

        static wifi_scan_ctx_t scan_ctx;  /* static: safe, worker is single-threaded */
        memset(&scan_ctx, 0, sizeof(scan_ctx));
        scan_ctx.done = xSemaphoreCreateBinary();
        if (scan_ctx.done == NULL) {
            err = AUTO_WIFI_ERR_BAD_ARG;
            wifi_ipc_reply(req->resp, req->cmd, 2, &err, 4);
            break;
        }

        cy_wcm_scan_filter_t filter;
        memset(&filter, 0, sizeof(filter));
        filter.mode = CY_WCM_SCAN_FILTER_TYPE_RSSI;
        filter.param.rssi_range = -90;

        cy_rslt_t r = cy_wcm_start_scan(wifi_scan_callback, &scan_ctx, &filter);
        if (r != CY_RSLT_SUCCESS) {
            s_wifi_last_error = r;
            err = (uint32_t)r;
            printf("[WiFiIPC] SCAN: start_scan failed, err=0x%08lX\r\n", (unsigned long)err);
            vSemaphoreDelete(scan_ctx.done);
            wifi_ipc_reply(req->resp, req->cmd, 3, &err, 4);
            break;
        }

        if (xSemaphoreTake(scan_ctx.done, pdMS_TO_TICKS(AUTO_WIFI_SCAN_TIMEOUT_MS)) != pdTRUE) {
            (void)cy_wcm_stop_scan();
            err = AUTO_WIFI_ERR_TIMEOUT;
            printf("[WiFiIPC] SCAN: semaphore timeout (%d ms)\r\n", AUTO_WIFI_SCAN_TIMEOUT_MS);
            vSemaphoreDelete(scan_ctx.done);
            wifi_ipc_reply(req->resp, req->cmd, 4, &err, 4);
            break;
        }
        vSemaphoreDelete(scan_ctx.done);

        printf("[WiFiIPC] SCAN: collected %u raw networks\r\n", (unsigned)scan_ctx.count);

        /* --- Deduplicate: keep strongest RSSI per SSID --- */
        for (uint8_t i = 0; i < scan_ctx.count; i++) {
            for (uint8_t j = i + 1; j < scan_ctx.count; ) {
                if (strncmp(scan_ctx.entries[i].ssid, scan_ctx.entries[j].ssid, 32) == 0) {
                    if (scan_ctx.entries[j].rssi > scan_ctx.entries[i].rssi) {
                        scan_ctx.entries[i] = scan_ctx.entries[j];
                    }
                    scan_ctx.entries[j] = scan_ctx.entries[scan_ctx.count - 1];
                    scan_ctx.count--;
                } else {
                    j++;
                }
            }
        }

        /* --- Sort by RSSI descending (insertion sort) --- */
        for (uint8_t i = 1; i < scan_ctx.count; i++) {
            ipc_wifi_scan_entry_t tmp = scan_ctx.entries[i];
            uint8_t j = i;
            while (j > 0 && scan_ctx.entries[j - 1].rssi < tmp.rssi) {
                scan_ctx.entries[j] = scan_ctx.entries[j - 1];
                j--;
            }
            scan_ctx.entries[j] = tmp;
        }

        /* --- Truncate to IPC limit (top 6 strongest) --- */
        uint8_t send_count = scan_ctx.count;
        if (send_count > IPC_WIFI_SCAN_MAX_ENTRIES) {
            send_count = IPC_WIFI_SCAN_MAX_ENTRIES;
        }

        printf("[WiFiIPC] SCAN: sending top %u of %u unique networks\r\n",
               (unsigned)send_count, (unsigned)scan_ctx.count);
        uint16_t data_len = (uint16_t)(send_count * sizeof(ipc_wifi_scan_entry_t));
        wifi_ipc_reply(req->resp, req->cmd, 0, scan_ctx.entries, data_len);
        break;
        //! [j3_wifi_ipc_scan_request]
    }

    //! [j3_wifi_ipc_connect_case]
    /* ...context: inside wifi_ipc_handle_request() ... */
    case IPC_CMD_WIFI_CONNECT: {
        printf("[WiFiIPC] CONNECT cmd received\r\n");
        const char *ssid = (const char *)req->data;
        size_t ssid_len = strnlen(ssid, 32);
        const char *pass = (const char *)&req->data[ssid_len + 1];
        size_t pass_len = strnlen(pass, 63);

        if (ssid_len == 0U) {
            err = AUTO_WIFI_ERR_BAD_ARG;
            wifi_ipc_reply(req->resp, req->cmd, 1, &err, 4);
            break;
        }

        if (!wifi_ensure_ready()) {
            err = (uint32_t)s_wifi_last_error;
            wifi_ipc_reply(req->resp, req->cmd, 2, &err, 4);
            break;
        }

        cy_wcm_connect_params_t params;
        cy_wcm_ip_address_t ip;
        memset(&params, 0, sizeof(params));
        memset(&ip, 0, sizeof(ip));

        memcpy(params.ap_credentials.SSID, ssid, ssid_len);
        memcpy(params.ap_credentials.password, pass, pass_len);
        params.ap_credentials.security = (pass_len == 0U)
            ? CY_WCM_SECURITY_OPEN
            : CY_WCM_SECURITY_WPA3_WPA2_PSK;  /* SAE + PMF; backward-compatible w/ WPA2 */

        /* Robust multi-attempt connect — the WiFi Menu drives this IPC path and
         * previously made a SINGLE cy_wcm_connect_ap() call, so a marginal cold
         * join surfaced as "Try Again". */
        cy_rslt_t r = wifi_connect_robust(&params, &ip, "WiFiIPC");
        if (r != CY_RSLT_SUCCESS) {
            s_wifi_last_error = r;
            err = (uint32_t)r;
            wifi_ipc_reply(req->resp, req->cmd, 3, &err, 4);
            break;
        }

        s_wifi_state.mode = AUTO_WIFI_MODE_STA;
        s_wifi_state.connected = true;
        s_wifi_state.rssi = 0;
        memset(s_wifi_state.ssid, 0, sizeof(s_wifi_state.ssid));
        strncpy(s_wifi_state.ssid, ssid, sizeof(s_wifi_state.ssid) - 1);
        wifi_update_ip_from_wcm();
        s_wifi_last_error = CY_RSLT_SUCCESS;
        wifi_ipc_reply(req->resp, req->cmd, 0, NULL, 0);

        /* Push WiFi connected state to CM55 topbar */
        push_wifi_state_to_cm55(true);

        /* NTP → RTC sync (one-shot after first successful connect) */
        if (!s_ntp_synced) {
            if (ntp_sync_rtc()) {
                s_ntp_synced = true;
                push_time_to_cm55();
                tesaiot_bridge_ntp_synced();
            }
        }

        /* Stage credential for deferred QSPI save.
         * lfs_wifi_creds_write() requires MicroPython task context, so we
         * update the shared globals here and let mpy_main.c flush to QSPI
         * at the next soft reset or REPL idle.  This also makes the
         * credential immediately available for boot auto-connect if the
         * board soft-resets (Ctrl+D).
         *
         * The dual-band BLE worker writes the same array on its own
         * task; lock around the entire mutate-and-set-dirty sequence so
         * a concurrent writer never sees a half-updated entry or a
         * count that races the entry write. */
        wifi_creds_lock();
        {
            bool already_saved = false;
            for (int i = 0; i < g_boot_wifi_creds_count; i++) {
                if (strncmp(g_boot_wifi_creds[i].ssid, ssid, 32) == 0) {
                    /* Update password in case it changed */
                    memset(g_boot_wifi_creds[i].password, 0, 65);
                    strncpy(g_boot_wifi_creds[i].password, pass, 64);
                    g_boot_wifi_creds[i].flags = 0x01;
                    already_saved = true;
                    break;
                }
            }
            if (!already_saved) {
                int idx;
                if (g_boot_wifi_creds_count < QSPI_WIFI_CREDS_MAX) {
                    idx = g_boot_wifi_creds_count;
                    g_boot_wifi_creds_count = idx + 1;
                } else {
                    idx = 0;  /* overwrite oldest slot */
                }
                memset(&g_boot_wifi_creds[idx], 0, sizeof(qspi_wifi_entry_t));
                strncpy(g_boot_wifi_creds[idx].ssid, ssid, 32);
                strncpy(g_boot_wifi_creds[idx].password, pass, 64);
                g_boot_wifi_creds[idx].security = CY_WCM_SECURITY_WPA2_AES_PSK;
                g_boot_wifi_creds[idx].flags = 0x01;
            }
            g_boot_wifi_creds_dirty = true;
            printf("[WiFiIPC] Credential staged for QSPI save (%d entries)\r\n",
                   (int)g_boot_wifi_creds_count);
        }
        wifi_creds_unlock();

//! [j3_creds_save_mtb_only_immediate]
/* ...context: inside the IPC_CMD_WIFI_CONNECT success path ... */
#if !BENTO_HAS_MPY
        /* No VM means no mpy_main.c and therefore no deferred flusher — the
         * "requires MicroPython task context" above is about the Python-VFS
         * store, not this one. This runs in the WiFi worker task, and the C
         * store is plain lfs2 over serial memory, so write it out now.
         * Snapshot under the lock, write outside it: the flash write takes
         * milliseconds and the BLE worker shares these globals. */
        {
            qspi_wifi_entry_t snap[QSPI_WIFI_CREDS_MAX];
            int n;
            wifi_creds_lock();
            n = (int)g_boot_wifi_creds_count;
            if (n > QSPI_WIFI_CREDS_MAX) n = QSPI_WIFI_CREDS_MAX;
            memcpy(snap, g_boot_wifi_creds, (size_t)n * sizeof(qspi_wifi_entry_t));
            wifi_creds_unlock();

            extern bool lfs_wifi_creds_write(const qspi_wifi_entry_t *entries, int count);
            if (lfs_wifi_creds_write(snap, n)) {
                wifi_creds_lock();
                g_boot_wifi_creds_dirty = false;
                wifi_creds_unlock();
                printf("[WiFiIPC] Credentials persisted (%d entries)\r\n", n);
            } else {
                printf("[WiFiIPC] ERROR: credential save failed — kept dirty\r\n");
            }
        }
#endif
//! [j3_creds_save_mtb_only_immediate]
        break;
        //! [j3_wifi_ipc_connect_case]
    }

    case IPC_CMD_WIFI_DISCONNECT:
        if (app_wifi_is_ready()) {
            (void)cy_wcm_disconnect_ap();
        }
        s_wifi_state.mode = AUTO_WIFI_MODE_NONE;
        s_wifi_state.connected = false;
        s_wifi_state.rssi = 0;
        s_wifi_state.ssid[0] = '\0';
        strncpy(s_wifi_state.ip, "0.0.0.0", sizeof(s_wifi_state.ip) - 1);
        wifi_ipc_reply(req->resp, req->cmd, 0, NULL, 0);

        /* Push WiFi disconnected state to CM55 topbar */
        push_wifi_state_to_cm55(false);
        break;

    case IPC_CMD_WIFI_STATUS:
        wifi_ipc_refresh_state();
        wifi_ipc_fill_status(status_blob);
        wifi_ipc_reply(req->resp, req->cmd, 0, status_blob, AUTO_WIFI_STATUS_DATA_LEN);
        break;

    case IPC_CMD_WIFI_IP:
        wifi_ipc_refresh_state();
        wifi_ipc_reply(req->resp, req->cmd, 0, s_wifi_state.ip,
                       (uint16_t)(strnlen(s_wifi_state.ip, sizeof(s_wifi_state.ip)) + 1U));
        break;

    case IPC_CMD_WIFI_SOFTAP: {
        const char *ssid = (const char *)req->data;
        const char *pass = (const char *)&req->data[33];
        if (!wifi_ensure_ready()) {
            err = (uint32_t)s_wifi_last_error;
            wifi_ipc_reply(req->resp, req->cmd, 1, &err, 4);
            break;
        }

        if (ssid[0] == '\0') ssid = "PSoC-Edge-MPY";
        if (pass[0] == '\0') pass = "micropython";

        cy_wcm_ap_config_t ap_conf;
        memset(&ap_conf, 0, sizeof(ap_conf));
        strncpy((char *)ap_conf.ap_credentials.SSID, ssid, sizeof(ap_conf.ap_credentials.SSID) - 1);
        strncpy((char *)ap_conf.ap_credentials.password, pass, sizeof(ap_conf.ap_credentials.password) - 1);
        ap_conf.ap_credentials.security = CY_WCM_SECURITY_WPA2_AES_PSK;
        ap_conf.channel = 1;

        ap_conf.ip_settings.ip_address.version = CY_WCM_IP_VER_V4;
        ap_conf.ip_settings.ip_address.ip.v4 = 0x0104A8C0;  /* 192.168.4.1 */
        ap_conf.ip_settings.netmask.version = CY_WCM_IP_VER_V4;
        ap_conf.ip_settings.netmask.ip.v4 = 0x00FFFFFF;
        ap_conf.ip_settings.gateway.version = CY_WCM_IP_VER_V4;
        ap_conf.ip_settings.gateway.ip.v4 = 0x0104A8C0;

        cy_rslt_t r = cy_wcm_start_ap(&ap_conf);
        if (r != CY_RSLT_SUCCESS) {
            s_wifi_last_error = r;
            err = (uint32_t)r;
            wifi_ipc_reply(req->resp, req->cmd, 2, &err, 4);
            break;
        }

        s_wifi_state.mode = AUTO_WIFI_MODE_SOFTAP;
        s_wifi_state.connected = true;
        s_wifi_state.rssi = 0;
        memset(s_wifi_state.ssid, 0, sizeof(s_wifi_state.ssid));
        strncpy(s_wifi_state.ssid, ssid, sizeof(s_wifi_state.ssid) - 1);
        wifi_update_ip_from_wcm();
        s_wifi_last_error = CY_RSLT_SUCCESS;
        wifi_ipc_reply(req->resp, req->cmd, 0, NULL, 0);

        /* Push WiFi connected state to CM55 topbar */
        push_wifi_state_to_cm55(true);
        break;
    }

    default:
        err = AUTO_WIFI_ERR_BAD_ARG;
        wifi_ipc_reply(req->resp, req->cmd, 0xFF, &err, 4);
        break;
    }
}

/*******************************************************************************
 * WiFi IPC Worker Task — dedicated task for WiFi commands from CM55
 *
 * Runs at priority 2 (same as SensorAuto, BELOW TCPIP/WCM/WHD) to prevent
 * priority deadlock during cy_wcm_init(). Blocks on queue — zero CPU when idle.
 *******************************************************************************/
//! [j3_wifi_boot_auto_connect]
static void wifi_boot_auto_connect(void)
{
#if BENTO_HAS_MPY
    /* Wait for mpy_main.c to load credentials from LFS into globals.
     * g_boot_wifi_creds_count is set by MicroPython task after VFS mount. */
    printf("[WiFi-Boot] Waiting for credentials from MicroPython...\r\n");
    for (int wait = 0; wait < 100; wait++) {   /* 100 x 100ms = 10s */
        if (g_boot_wifi_creds_count != 0) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
#else
    /* No VM, so no mpy_main.c to fill the globals. Read the same file
     * through the C store; bento_storage_init() ran from main(). */
    {
        extern int lfs_wifi_creds_read(qspi_wifi_entry_t *entries, int max_entries);
        int n = lfs_wifi_creds_read(g_boot_wifi_creds, QSPI_WIFI_CREDS_MAX);
        if (n > 0) g_boot_wifi_creds_count = n;
    }
#endif

    int count = (int)g_boot_wifi_creds_count;

    if (count <= 0) {
        printf("[WiFi-Boot] No saved credentials — skipping\r\n");
        return;
    }

    printf("[WiFi-Boot] %d saved network(s)\r\n", count);

    if (!wifi_ensure_ready()) {
        printf("[WiFi-Boot] SDIO/WCM init failed (0x%08lX)\r\n",
               (unsigned long)s_wifi_last_error);
        return;
    }

    /* Targeted scan per saved SSID: send a directed probe request for each
     * saved network. This verifies the AP exists before attempting connect,
     * avoiding 25+ second timeouts on unreachable networks.
     *
     * Timeout: 8s per SSID to cover 5GHz channels (iPhone hotspots etc.)
     * where directed probe responses take longer than 2.4GHz. */

    for (int i = 0; i < count; i++) {
        /* Snapshot the credential into stack locals under the lock so
         * the rest of the iteration body (scan, connect, retry loop,
         * NTP sync) runs against an immutable view — concurrent writers
         * on the BLE worker / IPC handler can't tear the strncpy of a
         * password mid-copy. ~100 bytes of stack per iteration; the
         * SensorAuto task has plenty of headroom. */
        char ssid_local[33];
        char pass_local[65];
        bool slot_empty;
        wifi_creds_lock();
        slot_empty = (g_boot_wifi_creds[i].ssid[0] == '\0');
        if (!slot_empty) {
            strncpy(ssid_local, g_boot_wifi_creds[i].ssid,
                    sizeof(ssid_local) - 1);
            ssid_local[sizeof(ssid_local) - 1] = '\0';
            strncpy(pass_local, g_boot_wifi_creds[i].password,
                    sizeof(pass_local) - 1);
            pass_local[sizeof(pass_local) - 1] = '\0';
        }
        wifi_creds_unlock();
        if (slot_empty) continue;

        /* Targeted SSID scan: probe for this specific network */
        static wifi_scan_ctx_t boot_scan;
        memset(&boot_scan, 0, sizeof(boot_scan));
        boot_scan.done = xSemaphoreCreateBinary();
        bool found = false;

        if (boot_scan.done != NULL) {
            cy_wcm_scan_filter_t filter;
            memset(&filter, 0, sizeof(filter));
            filter.mode = CY_WCM_SCAN_FILTER_TYPE_SSID;
            strncpy((char *)filter.param.SSID, ssid_local,
                    sizeof(filter.param.SSID) - 1);

            cy_rslt_t sr = cy_wcm_start_scan(wifi_scan_callback, &boot_scan, &filter);
            if (sr == CY_RSLT_SUCCESS) {
                /* 8s timeout — covers 5GHz channel scan for iPhone hotspots */
                if (xSemaphoreTake(boot_scan.done, pdMS_TO_TICKS(8000)) == pdTRUE) {
                    found = (boot_scan.count > 0);
                } else {
                    cy_wcm_stop_scan();
                    found = (boot_scan.count > 0);
                }
            }
            vSemaphoreDelete(boot_scan.done);
            boot_scan.done = NULL;
        }

        if (!found) {
            printf("[WiFi-Boot] '%s' not found in scan — skipped\r\n",
                   ssid_local);
            continue;
        }

        printf("[WiFi-Boot] '%s' found (RSSI=%d) — connecting...\r\n",
               ssid_local,
               (boot_scan.count > 0) ? boot_scan.entries[0].rssi : 0);

        cy_wcm_connect_params_t params;
        cy_wcm_ip_address_t ip;
        memset(&params, 0, sizeof(params));
        memset(&ip, 0, sizeof(ip));
        strncpy((char *)params.ap_credentials.SSID, ssid_local,
                sizeof(params.ap_credentials.SSID) - 1);
        strncpy((char *)params.ap_credentials.password, pass_local,
                sizeof(params.ap_credentials.password) - 1);
        /* WPA3_WPA2_PSK (SAE + PMF) — see IPC_CMD_WIFI_CONNECT handler above. */
        params.ap_credentials.security = (pass_local[0] == '\0')
            ? CY_WCM_SECURITY_OPEN
            : CY_WCM_SECURITY_WPA3_WPA2_PSK;

        cy_rslt_t r = wifi_connect_robust(&params, &ip, "WiFi-Boot");

        if (CY_RSLT_SUCCESS == r) {
#ifdef BOOT_VERBOSE
            uint32_t v4 = ip.ip.v4;
            printf("[WiFi-Boot] Connected to '%s'! IP=%lu.%lu.%lu.%lu\r\n",
                   ssid_local,
                   (unsigned long)(v4 & 0xFF), (unsigned long)((v4 >> 8) & 0xFF),
                   (unsigned long)((v4 >> 16) & 0xFF), (unsigned long)((v4 >> 24) & 0xFF));
#endif

            s_wifi_state.mode = AUTO_WIFI_MODE_STA;
            s_wifi_state.connected = true;
            strncpy(s_wifi_state.ssid, ssid_local,
                    sizeof(s_wifi_state.ssid) - 1);
            wifi_update_ip_from_wcm();

            push_wifi_state_to_cm55(true);

            if (ntp_sync_rtc()) {
                s_ntp_synced = true;
                push_time_to_cm55();
                tesaiot_bridge_ntp_synced();
#ifdef BOOT_VERBOSE
                printf("[WiFi-Boot] NTP synced + pushed to CM55\r\n");
#endif
            }
            return;  /* Connected — done */
            //! [j3_wifi_boot_auto_connect]
        }
    }

#ifdef BOOT_VERBOSE
    printf("[WiFi-Boot] All saved networks failed\r\n");
#endif
}

//! [j3_wifi_ipc_worker_loop]
static void wifi_ipc_worker_task(void *arg)
{
    (void)arg;

    /* Boot auto-connect: inline, no separate task (saves 4KB RAM).
     * Reads g_boot_wifi_creds[] globals (loaded by mpy_main.c via Python I/O).
     * Blocks WiFi IPC queue during connect (~7-17s), but CM55 WiFi Settings
     * page is not open during early boot — queued commands process after. */
    wifi_boot_auto_connect();

    wifi_ipc_req_t req;
    for (;;) {
        /* Wait up to 30s for WiFi IPC commands from CM55.
         * On timeout (no command): push updated time to CM55 topbar. */
        if (xQueueReceive(s_wifi_req_queue, &req, pdMS_TO_TICKS(30000)) == pdTRUE) {
            wifi_ipc_handle_request(&req);
        } else if (s_ntp_synced) {
            /* Periodic time push — keeps CM55 clock display current */
            push_time_to_cm55();
        } else if (s_wifi_state.connected) {
            /* WiFi is up but NTP never synced (the post-connect one-shot can miss
             * the first UDP round-trip on a freshly-up link → WiFi icon shows but
             * no clock). Retry here every ~30s until the clock lands. */
            if (ntp_sync_rtc()) {
                s_ntp_synced = true;
                push_time_to_cm55();
            }
        }
    }
    //! [j3_wifi_ipc_worker_loop]
}

/*******************************************************************************
 * Sensor Read + Push Helpers
 *******************************************************************************/

static void auto_push_bmi270(void) {
    float ax, ay, az, gx, gy, gz, temp = 0;
    sensor_i2c_lock(AUTO_I2C_LOCK_TIMEOUT_MS);
    bool ok = bmi270_read_accel(&ax, &ay, &az)
           && bmi270_read_gyro(&gx, &gy, &gz);
    if (ok) bmi270_read_temperature(&temp);
    sensor_i2c_unlock();

    if (ok) {
        /* Update cache for BentoClaw reads */
        s_bmi270_cache.ax = ax; s_bmi270_cache.ay = ay; s_bmi270_cache.az = az;
        s_bmi270_cache.gx = gx; s_bmi270_cache.gy = gy; s_bmi270_cache.gz = gz;
        s_bmi270_cache.temp = temp;
        s_bmi270_cache.valid = true;

        ipc_sensor_bmi270_t d;
        d.ax = (int16_t)(ax / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
        d.ay = (int16_t)(ay / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
        d.az = (int16_t)(az / GRAVITY_ACCEL * IPC_BMI270_ACCEL_LSB_PER_G);
        d.gx = (int16_t)(gx * IPC_BMI270_GYRO_LSB_PER_DPS);
        d.gy = (int16_t)(gy * IPC_BMI270_GYRO_LSB_PER_DPS);
        d.gz = (int16_t)(gz * IPC_BMI270_GYRO_LSB_PER_DPS);
        d.sequence = s_auto_seq++;
        auto_ipc_send(IPC_CMD_SENSOR_BMI270, &d, sizeof(d));
        Cy_SysLib_DelayUs(AUTO_IPC_GAP_US);
    }
}

#if BSP_HAS_DPS368
static void auto_push_dps368(void) {
    float pressure, temperature;
    sensor_i2c_lock(AUTO_I2C_LOCK_TIMEOUT_MS);
    bool ok = dps368_read_both(&pressure, &temperature);
    sensor_i2c_unlock();

    if (ok) {
        ipc_sensor_dps368_t d;
        d.pressure_x100 = (int32_t)(pressure * 100.0f);
        d.temperature_x100 = (int16_t)(temperature * 100.0f);
        d.sequence = s_auto_seq++;
        auto_ipc_send(IPC_CMD_SENSOR_DPS368, &d, sizeof(d));
        Cy_SysLib_DelayUs(AUTO_IPC_GAP_US);
    }
}
#endif

#if BSP_HAS_SHT40
static void auto_push_sht40(void) {
    float temperature, humidity;
    sensor_i2c_lock(AUTO_I2C_LOCK_TIMEOUT_MS);
    bool ok = sht40_read_both(&temperature, &humidity);
    sensor_i2c_unlock();

    if (ok) {
        ipc_sensor_sht40_t d;
        d.temperature_x100 = (int16_t)(temperature * 100.0f);
        d.humidity_x100 = (uint16_t)(humidity * 100.0f);
        d.sequence = s_auto_seq++;
        auto_ipc_send(IPC_CMD_SENSOR_SHT40, &d, sizeof(d));
        Cy_SysLib_DelayUs(AUTO_IPC_GAP_US);
    }
}
#endif

#if BSP_HAS_BMM350
#define BMM350_MAX_CONSEC_FAILS  10  /* Re-init after this many consecutive failures */
static uint32_t s_bmm350_fail_count = 0;

static void auto_push_bmm350(void) {
    float mx, my, mz;
    sensor_i2c_lock(AUTO_I2C_LOCK_TIMEOUT_MS);
    bool ok = bmm350_read_xyz(&mx, &my, &mz);
    sensor_i2c_unlock();

    if (ok) {
        s_bmm350_fail_count = 0;

        /* Feed hard iron calibration tracker + compute calibrated heading */
        bmm350_cal_update(mx, my);
        float heading_deg = bmm350_heading_from_xy(mx, my);

        ipc_sensor_bmm350_t d;
        d.mx_x100 = (int16_t)(mx * 100.0f);
        d.my_x100 = (int16_t)(my * 100.0f);
        d.mz_x100 = (int16_t)(mz * 100.0f);
        /* Clamp to [0, 3599] before uint16_t cast to avoid wrap on negative float */
        int32_t h10 = (int32_t)(heading_deg * 10.0f);
        if (h10 < 0)    h10 += 3600;
        if (h10 >= 3600) h10 -= 3600;
        d.heading_x10 = (uint16_t)h10;
        d.sequence = s_auto_seq++;
        auto_ipc_send(IPC_CMD_SENSOR_BMM350, &d, sizeof(d));
        Cy_SysLib_DelayUs(AUTO_IPC_GAP_US);
    } else {
        s_bmm350_fail_count++;
        if (s_bmm350_fail_count >= BMM350_MAX_CONSEC_FAILS) {
            /* I3C bus likely stuck — force full re-init */
#ifdef BOOT_VERBOSE
            printf("[BMM350] %lu consecutive failures — re-initializing I3C\r\n",
                   (unsigned long)s_bmm350_fail_count);
#endif
            bmm350_reinit();
            s_bmm350_fail_count = 0;
        }
    }
}
#endif

/* On the QWA309 base board (TESAIoT Dev Kit) the pots (VR1-4, SAR ch4-7) and
 * the external CapSense-4000T are read on CM55 (cm55_sensor_poll) — CM33 must
 * not init or push them, or it races the CM55 feed with wrong-channel data. */
#if BSP_HAS_CAPSENSE && !BSP_HAS_QWA309_BASEBOARD
static void auto_push_capsense(void) {
    capsense_data_t cs;
    sensor_i2c_lock(AUTO_I2C_LOCK_TIMEOUT_MS);
    bool ok = capsense_read(&cs);
    sensor_i2c_unlock();

    if (ok) {
        ipc_sensor_capsense_t d;
        d.btn0_pressed = cs.btn0_pressed ? 1 : 0;
        d.btn1_pressed = cs.btn1_pressed ? 1 : 0;
        d.slider = cs.slider;
        d.reserved = 0;
        d.sequence = s_auto_seq++;
        auto_ipc_send(IPC_CMD_SENSOR_CAPSENSE, &d, sizeof(d));
        Cy_SysLib_DelayUs(AUTO_IPC_GAP_US);
    }
}
#endif

#if BSP_HAS_POTENTIOMETER && !BSP_HAS_QWA309_BASEBOARD
static void auto_push_pot(void) {
    uint16_t raw;
    float pct;
    sensor_i2c_lock(AUTO_I2C_LOCK_TIMEOUT_MS);
    bool ok = potentiometer_read_raw(&raw) && potentiometer_read_percent(&pct);
    sensor_i2c_unlock();

    if (ok) {
        ipc_sensor_pot_t d;
        d.raw = raw;
        d.percent_x10 = (uint16_t)(pct * 10.0f);
        d.sequence = s_auto_seq++;
        auto_ipc_send(IPC_CMD_SENSOR_POT, &d, sizeof(d));
        Cy_SysLib_DelayUs(AUTO_IPC_GAP_US);
    }
}
#endif

/*******************************************************************************
 * Auto-Sensor Task Body
 *******************************************************************************/
static void sensor_auto_task_body(void *arg) {
    (void)arg;

    /* Initialize sensors (silent — use sensors.auto_status()).
     *
     * Eva Kit: skip SCB0 I2C init and I2C-based sensors (BMI270, CapSense,
     * Pot) because CM55 cm55_sensor_poll owns that bus. Only init BMM350
     * which uses the separate I3C bus (P3[0]/P3[1]). */

    bool ok;

#if !defined(USE_KIT_PSE84_EVAL_EPC2) || defined(CM33_OWNS_I2C_SENSORS)
    /* Init SCB0 I2C + all I2C sensors from CM33_NS.
     * Eva Kit AI-Core: CM55 cm55_sensor_poll owns I2C — skip.
     * Eva Kit BentoClaw: CM55 has no sensor polling — CM33_NS inits. */
    /* Wait for BSP peripheral clocks to stabilize before I2C init.
     * On AI Kit, SCB0 may not be ready immediately after scheduler starts. */
    vTaskDelay(pdMS_TO_TICKS(500));

    sensor_i2c_lock(AUTO_I2C_LOCK_TIMEOUT_MS);
    sensor_i2c_init();
    sensor_i2c_unlock();

    /* Extra settle time for sensors after I2C init */
    vTaskDelay(pdMS_TO_TICKS(100));

    sensor_i2c_lock(500);
    ok = bmi270_init();
    sensor_i2c_unlock();
    if (!ok) {
        s_enabled_mask &= ~SENSOR_AUTO_BMI270;
    }

#if BSP_HAS_DPS368
    sensor_i2c_lock(500);
    ok = dps368_init();
    sensor_i2c_unlock();
    if (!ok) {
        s_enabled_mask &= ~SENSOR_AUTO_DPS368;
    }
#endif

#if BSP_HAS_SHT40
    sensor_i2c_lock(500);
    ok = sht40_init();
    sensor_i2c_unlock();
    if (!ok) {
        s_enabled_mask &= ~SENSOR_AUTO_SHT40;
    }
#endif

#if BSP_HAS_CAPSENSE && !BSP_HAS_QWA309_BASEBOARD
    sensor_i2c_lock(500);
    ok = capsense_init();
    sensor_i2c_unlock();
    if (!ok) {
        s_enabled_mask &= ~SENSOR_AUTO_CAPSENSE;
    }
#endif

#if BSP_HAS_POTENTIOMETER && !BSP_HAS_QWA309_BASEBOARD
    sensor_i2c_lock(500);
    ok = potentiometer_init();
    sensor_i2c_unlock();
    if (!ok) {
        s_enabled_mask &= ~SENSOR_AUTO_POT;
    }
#endif
#endif /* !USE_KIT_PSE84_EVAL_EPC2 || CM33_OWNS_I2C_SENSORS */

#if BSP_HAS_QWA309_BASEBOARD
    /* CM55 owns pot + CapSense on this board (see comment above auto_push_capsense) */
    s_enabled_mask &= ~(SENSOR_AUTO_POT | SENSOR_AUTO_CAPSENSE);
#endif

    /* BMM350: uses I3C bus (not SCB0) — safe to init on all boards */
#if BSP_HAS_BMM350
    ok = bmm350_init();
    if (!ok) {
        s_enabled_mask &= ~SENSOR_AUTO_BMM350;
    }
#endif

    s_running = true;
    uint32_t cycle = 0;

    /* Housekeeping is scheduled on elapsed milliseconds, not on a cycle count,
     * so that changing the push interval does not silently change how fast the
     * uptime clock runs or how often the BLE state is polled. */
    uint32_t last_slow_ms   = 0;
    uint32_t last_uptime_ms = 0;
#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1)
    uint32_t last_ble_ms    = 0;
#endif

    for (;;) {
        /* WiFi commands handled by dedicated WiFiIPC worker task */

        /* Check if paused */
        if (s_paused) {
            s_running = false;
            vTaskSuspend(NULL);  /* Suspend self — zero CPU */
            /* Resumed by sensor_auto_start(). */
            s_running = true;
            if (s_paused) {
                continue;
            }
        }

        if (s_paused) {
            continue;
        }

        uint32_t mask = s_enabled_mask;
        const bool imu_fast = (s_interval_ms < SENSOR_AUTO_FAST_THRESHOLD_MS);
        const uint32_t now_ms =
            (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;

        /* Fast sensors — every cycle */
        if (mask & SENSOR_AUTO_BMI270)   auto_push_bmi270();

        if (!imu_fast) {
#if BSP_HAS_BMM350
        if (mask & SENSOR_AUTO_BMM350)   auto_push_bmm350();
#endif

#if BSP_HAS_CAPSENSE && !BSP_HAS_QWA309_BASEBOARD
        if (mask & SENSOR_AUTO_CAPSENSE) auto_push_capsense();
#endif

#if BSP_HAS_POTENTIOMETER && !BSP_HAS_QWA309_BASEBOARD
        if (mask & SENSOR_AUTO_POT)      auto_push_pot();
#endif
        }

        /* Slow sensors — about every 500 ms */
        if (!imu_fast && (now_ms - last_slow_ms) >= 500u) {
            last_slow_ms = now_ms;
#if BSP_HAS_DPS368
            if (mask & SENSOR_AUTO_DPS368)  auto_push_dps368();
#endif
#if BSP_HAS_SHT40
            if (mask & SENSOR_AUTO_SHT40)   auto_push_sht40();
#endif
        }

        /* TESAIoT uptime counter — 1 Hz by the clock, not by cycle count */
        if ((now_ms - last_uptime_ms) >= 1000u) {
            last_uptime_ms = now_ms;
            tesaiot_status_t *ts = ipc_tesaiot_get_status();
            if (ts) {
                ts->uptime_s++;
            }
        }

//! [ble_ble_nus_get_state_poll]
/* ...context: inside the sensor auto-push loop ... */
#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1)
        /* BLE NUS host-link state poll (every 5th cycle = ~500ms).
         * Why poll instead of using on_state callback: the callback
         * registration window depends on the AIROC stack delivering
         * GATT_CONNECTION_STATUS_EVT, which we observed was missing on
         * the bonded-pair fast-resume path on 2026-05-10 — the desktop
         * was actively serving fw.query verbs over the GATT link but
         * ble_nus_get_state() still read ADVERTISING. Polling closes
         * the loop deterministically: whatever the stack actually
         * thinks the state is, the LCD topbar will reflect it within
         * 500 ms of any change. */
        if ((now_ms - last_ble_ms) >= 500u) {
            last_ble_ms = now_ms;
            static int8_t last_pushed_ble = -1;  /* −1 = uninitialized */
            int8_t now_connected = (ble_nus_get_state() == BLE_NUS_STATE_CONNECTED) ? 1 : 0;
            if (now_connected != last_pushed_ble) {
                sensor_auto_push_ble_state(now_connected != 0);
                last_pushed_ble = now_connected;
            }
        }
        //! [ble_ble_nus_get_state_poll]
#endif

        cycle++;
        s_push_count++;

        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
    }
}

/*******************************************************************************
 * Public API
 *******************************************************************************/

void sensor_auto_get_bmi270(sensor_auto_bmi270_cache_t *out) {
    /* Single-copy struct read — no lock needed (fields are floats written
     * atomically on Cortex-M33, and slight tearing is acceptable for
     * display/reporting purposes). */
    out->ax    = s_bmi270_cache.ax;
    out->ay    = s_bmi270_cache.ay;
    out->az    = s_bmi270_cache.az;
    out->gx    = s_bmi270_cache.gx;
    out->gy    = s_bmi270_cache.gy;
    out->gz    = s_bmi270_cache.gz;
    out->temp  = s_bmi270_cache.temp;
    out->valid = s_bmi270_cache.valid;
}

//! [j1_sensor_auto_task_create]
void sensor_auto_task_create(void) {
    if (s_auto_task_handle != NULL) return;

    if (s_wifi_req_queue == NULL) {
        s_wifi_req_queue = xQueueCreate(AUTO_WIFI_REQ_QUEUE_LEN, sizeof(wifi_ipc_req_t));
        if (s_wifi_req_queue == NULL) {
            printf("ERROR: Failed to create SensorAuto WiFi queue\r\n");
        }
    }

    if (!s_ipc_initialized) {
        cm33_ipc_communication_setup();
        Cy_SysLib_Delay(50);
        s_ipc_initialized = true;
    }

    /* Create WiFiIPC worker task BEFORE registering IPC callback.
     * Worker blocks on queue — zero CPU when idle. Priority 2 ensures
     * TCPIP(4)/WCM(4)/WHD(5) threads can preempt during cy_wcm_init(). */
    if (s_wifi_ipc_task_handle == NULL) {
        BaseType_t wres = xTaskCreate(
            wifi_ipc_worker_task,
            AUTO_WIFI_IPC_TASK_NAME,
            AUTO_WIFI_IPC_STACK_WORDS,
            NULL,
            AUTO_WIFI_IPC_TASK_PRIORITY,
            &s_wifi_ipc_task_handle
        );
        if (wres != pdPASS) {
            printf("ERROR: Failed to create WiFiIPC task\r\n");
        }
    }

    if (!s_ctrl_cb_registered) {
        cy_en_ipc_pipe_status_t st = Cy_IPC_Pipe_RegisterCallback(
            CM33_IPC_PIPE_EP_ADDR,
            sensor_auto_ctrl_callback,
            (uint32_t)CM33_IPC_SENSOR_CTRL_CLIENT_ID);
        s_ctrl_cb_registered = (st == CY_IPC_PIPE_SUCCESS);
        if (!s_ctrl_cb_registered) {
            printf("WARN: SensorAuto IPC ctrl callback registration failed (%lu)\r\n",
                   (unsigned long)st);
        }
    }

    /* SensorAuto task: reads sensors and pushes data via IPC to CM55.
     *
     * Eva Kit (USE_KIT_PSE84_EVAL_EPC2):
     *   CM55 cm55_sensor_poll owns SCB0 I2C (P8[0]/P8[1]) for BMI270,
     *   CapSense, Potentiometer. But BMM350 is on the separate I3C bus
     *   (P3[0]/P3[1]) — no contention. So we create the task on Eva Kit
     *   too, but restrict the enabled mask to BMM350 only. */
#if defined(USE_KIT_PSE84_EVAL_EPC2) && !defined(CM33_OWNS_I2C_SENSORS)
    /* Eva Kit AI-Core: CM55 cm55_sensor_poll owns SCB0 I2C — only BMM350 (I3C) */
    s_enabled_mask = SENSOR_AUTO_BMM350;
#endif
    BaseType_t res = xTaskCreate(
        sensor_auto_task_body,
        AUTO_TASK_NAME,
        AUTO_TASK_STACK_SIZE,
        NULL,
        AUTO_TASK_PRIORITY,
        &s_auto_task_handle
    );
    //! [j1_sensor_auto_task_create]
    if (res != pdPASS) {
        printf("ERROR: Failed to create SensorAuto task\r\n");
    }
}

void sensor_auto_start(void) {
    if (s_auto_task_handle == NULL) return;
    s_paused = false;
    if (!s_running) {
        vTaskResume(s_auto_task_handle);
    }
}

void sensor_auto_stop(void) {
    s_paused = true;
    /* Force immediate suspend to avoid extra IPC burst while UI module
     * is probing CM55 after soft-reset / rapid Run transitions. */
    if (s_auto_task_handle != NULL && s_running) {
        vTaskSuspend(s_auto_task_handle);
        s_running = false;
    }
}

bool sensor_auto_is_running(void) {
    return s_running && !s_paused;
}

void sensor_auto_set_rate(uint32_t interval_ms) {
    if (interval_ms < 20)   interval_ms = 20;
    if (interval_ms > 5000) interval_ms = 5000;
    s_interval_ms = interval_ms;
}

uint32_t sensor_auto_get_rate(void) {
    return s_interval_ms;
}

void sensor_auto_set_mask(uint32_t mask) {
    s_enabled_mask = mask & SENSOR_AUTO_ALL;
}

uint32_t sensor_auto_get_mask(void) {
    return s_enabled_mask;
}

void sensor_auto_enable(uint32_t flag) {
    s_enabled_mask |= (flag & SENSOR_AUTO_ALL);
}

void sensor_auto_disable(uint32_t flag) {
    s_enabled_mask &= ~flag;
}

uint32_t sensor_auto_get_push_count(void) {
    return s_push_count;
}

bool sensor_auto_is_delete_pending(void) {
    if (s_delete_pending) {
        s_delete_pending = false;
        return true;
    }
    return false;
}

bool sensor_auto_is_restart_pending(void) {
    if (s_restart_pending) {
        s_restart_pending = false;
        return true;
    }
    return false;
}

/*******************************************************************************
 * Public WiFi State + Time Push API (callable from modwifi.c etc.)
 ******************************************************************************/

void sensor_auto_push_wifi_state(bool connected)
{
    push_wifi_state_to_cm55(connected);
}

void sensor_auto_push_ble_state(bool connected)
{
    uint8_t state = connected ? 1U : 0U;
    printf("[BLE] state push -> CM55: %s\r\n",
           connected ? "CONNECTED" : "disconnected");
    (void)auto_ipc_send(IPC_CMD_BLE_STATE_PUSH, &state, 1);
}

void sensor_auto_ntp_and_push_time(void)
{
    if (!s_ntp_synced) {
        if (ntp_sync_rtc()) {
            s_ntp_synced = true;
            push_time_to_cm55();
            tesaiot_bridge_ntp_synced();
            printf("[NTP] Synced + pushed to CM55\r\n");
        }
    } else {
        /* Already synced — just push current time */
        push_time_to_cm55();
    }
}
