/*******************************************************************************
 * File Name: wifi_manager.c
 *
 * Description: WiFi manager for CM55 using CM33-owned WiFi bridge.
 *              CM55 cannot access SDHC0/WHD directly, so all WiFi operations
 *              are proxied to CM33 via IPC and returned synchronously.
 *
 *******************************************************************************/

#include "wifi_manager.h"
#include "ipc_communication.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* IPC retry/timeout policy */
#define WIFI_IPC_SEND_RETRIES              (20)
#define WIFI_IPC_RETRY_DELAY_US            (100)
#define WIFI_IPC_RESPONSE_TIMEOUT_SCAN_MS  (35000)  /* WiFi init(~22s) + scan(~5s) + margin */
#define WIFI_IPC_RESPONSE_TIMEOUT_CONN_MS  (95000)  /* WiFi init(~20s) + robust connect: up
                                                     * to 6 cy_wcm attempts x ~13s + settle to
                                                     * ride out the marginal cold first-join.
                                                     * CM55 wait yields via vTaskDelay(1), so
                                                     * the long wait just holds "Connecting..." */
#define WIFI_IPC_RESPONSE_TIMEOUT_MS       (5000)   /* Status, disconnect, etc. */

/* Local synthetic error codes (cy_rslt_t-compatible width) */
#define WIFI_ERR_IPC_SEND_TIMEOUT     (0xDEAD5501UL)
#define WIFI_ERR_IPC_RESP_TIMEOUT     (0xDEAD5502UL)
#define WIFI_ERR_IPC_BAD_RESPONSE     (0xDEAD5503UL)
#define WIFI_ERR_INVALID_ARG          (0xDEAD5504UL)
#define WIFI_ERR_IPC_STATUS_BASE      (0xDEAD5600UL) /* ORed with nonzero CM33 status byte */

/* Shared-memory IPC request/response buffers */
CY_SECTION_SHAREDMEM static ipc_msg_t s_wifi_ipc_msg;
CY_SECTION_SHAREDMEM static ipc_response_t s_wifi_ipc_resp;

static bool s_ipc_initialized = false;

/* Current status cache (kept coherent via wifi_manager_get_status). */
static wifi_mgr_status_t s_status = {
    .mode = WIFI_MGR_MODE_NONE,
    .connected = false,
    .ip_addr = "0.0.0.0",
    .ssid = "",
    .rssi = 0,
    .mac_addr = {0},
};

static cy_rslt_t s_last_error = CY_RSLT_SUCCESS;

/* Count of pipe self-heal events (diagnostic — read by the board's watchman).
 * Non-static so project-side black-box instrumentation can extern it. */
volatile uint32_t g_wifi_ipc_pipe_recoveries = 0;

/* CM55-side pipe drain — declared in ipc_communication.h, defined in
 * cm55_ipc_communication.c. Clears a stuck EP busy flag and a stuck
 * hardware channel lock; safe to call from task context. */
void cm55_ipc_pipe_drain_release(void);

static void wifi_manager_ensure_ipc(void)
{
    /* IPC pipe is already initialized by tesaiot_display.c at GFX task start.
     * Do NOT call cm55_ipc_communication_setup() again — re-initializing the
     * pipe after callbacks are registered can corrupt IPC state and cause
     * Cy_IPC_Pipe_SendMessage() to silently fail. */
    s_ipc_initialized = true;
}

static bool wifi_manager_ipc_request(uint32_t cmd, const void *payload,
                                     size_t payload_len, uint32_t timeout_ms)
{
    if (payload_len > IPC_DATA_MAX_LEN) {
        s_last_error = WIFI_ERR_INVALID_ARG;
        return false;
    }

    wifi_manager_ensure_ipc();

    memset((void *)&s_wifi_ipc_resp, 0, sizeof(s_wifi_ipc_resp));
    s_wifi_ipc_resp.ready = 0;

    memset(&s_wifi_ipc_msg, 0, sizeof(s_wifi_ipc_msg));
    s_wifi_ipc_msg.client_id = CM33_IPC_SENSOR_CTRL_CLIENT_ID;
    s_wifi_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_wifi_ipc_msg.cmd = cmd;
    s_wifi_ipc_msg.value = (uint32_t)&s_wifi_ipc_resp;
    if (payload != NULL && payload_len > 0) {
        memcpy(s_wifi_ipc_msg.data, payload, payload_len);
    }

    /* WiFi is the documented pipe killer: WHD's critical sections on CM33
     * delay its IPC interrupt long enough that the CM55->CM33 channel lock
     * or EP busy flag is left stuck, and every later send fails forever
     * (frozen screen, dead touch — the send path never self-recovers).
     * cm55_ipc_pipe_drain_release() is the designed remedy for exactly this
     * state, so run it once when the send retries are exhausted, and again
     * on a response timeout so the NEXT status poll starts from a clean
     * channel instead of inheriting the wedge. */
    int retries = WIFI_IPC_SEND_RETRIES;
    bool drained = false;
    cy_en_ipc_pipe_status_t send_status;
    for (;;) {
        send_status = Cy_IPC_Pipe_SendMessage(
            CM33_IPC_PIPE_EP_ADDR, CM55_IPC_PIPE_EP_ADDR,
            (void *)&s_wifi_ipc_msg, NULL);
        if (send_status == CY_IPC_PIPE_SUCCESS) {
            break;
        }
        if (--retries <= 0) {
            if (!drained) {
                cm55_ipc_pipe_drain_release();
                g_wifi_ipc_pipe_recoveries++;
                drained = true;
                retries = WIFI_IPC_SEND_RETRIES;
                continue;
            }
            s_last_error = WIFI_ERR_IPC_SEND_TIMEOUT;
            return false;
        }
        Cy_SysLib_DelayUs(WIFI_IPC_RETRY_DELAY_US);
    }

    uint32_t timeout = timeout_ms;
    //! [j3_wifi_mgr_response_wait_dmb]
    /* ...context: inside wifi_manager_ipc_request() ... */
    while (!s_wifi_ipc_resp.ready && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));  /* Yield to other tasks (LVGL, sensors) */
        timeout--;
    }
    __DMB();  /* Data Memory Barrier — prevent stale cache reads on shared memory */

    if (!s_wifi_ipc_resp.ready) {
        /* No response: CM33 may never have seen the message (wedged channel)
         * — clean up so the next request isn't doomed too. */
        cm55_ipc_pipe_drain_release();
        g_wifi_ipc_pipe_recoveries++;
        s_last_error = WIFI_ERR_IPC_RESP_TIMEOUT;
        return false;
    }
    //! [j3_wifi_mgr_response_wait_dmb]

    if (s_wifi_ipc_resp.cmd != (uint8_t)cmd) {
        s_last_error = WIFI_ERR_IPC_BAD_RESPONSE;
        return false;
    }

    if (s_wifi_ipc_resp.status != 0) {
        if (s_wifi_ipc_resp.data_len >= 4) {
            uint32_t err = 0;
            memcpy(&err, s_wifi_ipc_resp.data, 4);
            s_last_error = (cy_rslt_t)err;
        } else {
            s_last_error = (cy_rslt_t)(WIFI_ERR_IPC_STATUS_BASE | s_wifi_ipc_resp.status);
        }
        return false;
    }

    s_last_error = CY_RSLT_SUCCESS;
    return true;
}

static void wifi_manager_decode_status(const ipc_response_t *resp, wifi_mgr_status_t *st)
{
    if (!resp || !st || resp->data_len < WIFI_STATUS_LEN_V1) {
        return;
    }

    st->mode = (wifi_mgr_mode_t)resp->data[WIFI_STATUS_OFF_MODE];
    st->connected = (resp->data[WIFI_STATUS_OFF_CONNECTED] != 0);
    st->rssi = (int8_t)resp->data[WIFI_STATUS_OFF_RSSI];

    memset(st->ip_addr, 0, sizeof(st->ip_addr));
    memset(st->ssid, 0, sizeof(st->ssid));
    strncpy(st->ip_addr, (const char *)&resp->data[WIFI_STATUS_OFF_IP], sizeof(st->ip_addr) - 1);
    strncpy(st->ssid, (const char *)&resp->data[WIFI_STATUS_OFF_SSID], sizeof(st->ssid) - 1);

    /* MAC address at offset 53 (6 bytes) — added in status v2 */
    memset(st->mac_addr, 0, sizeof(st->mac_addr));
    if (resp->data_len >= WIFI_STATUS_LEN_V2) {
        memcpy(st->mac_addr, &resp->data[WIFI_STATUS_OFF_MAC], WIFI_MAC_ADDR_LEN);
    }
}

bool wifi_manager_init(void)
{
    wifi_mgr_status_t tmp = {0};
    wifi_manager_get_status(&tmp);
    return (s_last_error == CY_RSLT_SUCCESS);
}

bool wifi_manager_start_softap(void)
{
    /* Keep defaults on CM33 side for now. */
    uint8_t payload[33 + 65];
    memset(payload, 0, sizeof(payload));
    if (!wifi_manager_ipc_request(IPC_CMD_WIFI_SOFTAP, payload, sizeof(payload),
                                  WIFI_IPC_RESPONSE_TIMEOUT_CONN_MS)) {
        return false;
    }
    (void)wifi_manager_ipc_request(IPC_CMD_WIFI_STATUS, NULL, 0,
                                   WIFI_IPC_RESPONSE_TIMEOUT_MS);
    wifi_manager_decode_status(&s_wifi_ipc_resp, &s_status);
    return true;
}

//! [j3_wifi_mgr_connect_payload]
bool wifi_manager_connect(const char *ssid, const char *password)
{
    if (ssid == NULL) {
        s_last_error = WIFI_ERR_INVALID_ARG;
        return false;
    }

    uint8_t payload[IPC_DATA_MAX_LEN];
    memset(payload, 0, sizeof(payload));

    size_t ssid_len = strnlen(ssid, 32);
    memcpy(payload, ssid, ssid_len);
    payload[ssid_len] = '\0';

    const char *pass = (password != NULL) ? password : "";
    size_t pass_len = strnlen(pass, 63);
    memcpy(&payload[ssid_len + 1], pass, pass_len);
    payload[ssid_len + 1 + pass_len] = '\0';

    size_t payload_len = ssid_len + 1 + pass_len + 1;
    if (!wifi_manager_ipc_request(IPC_CMD_WIFI_CONNECT, payload, payload_len,
                                  WIFI_IPC_RESPONSE_TIMEOUT_CONN_MS)) {
        return false;
    }

    (void)wifi_manager_ipc_request(IPC_CMD_WIFI_STATUS, NULL, 0,
                                   WIFI_IPC_RESPONSE_TIMEOUT_MS);
    wifi_manager_decode_status(&s_wifi_ipc_resp, &s_status);
    return true;
}
//! [j3_wifi_mgr_connect_payload]

int wifi_manager_scan(wifi_mgr_scan_entry_t *out, size_t max_entries)
{
    if (!wifi_manager_ipc_request(IPC_CMD_WIFI_SCAN, NULL, 0,
                                  WIFI_IPC_RESPONSE_TIMEOUT_SCAN_MS)) {
        return -1;
    }

    size_t total = s_wifi_ipc_resp.data_len / sizeof(ipc_wifi_scan_entry_t);
    if (total > IPC_WIFI_SCAN_MAX_ENTRIES) {
        total = IPC_WIFI_SCAN_MAX_ENTRIES;
    }

    if (out != NULL && max_entries > 0) {
        size_t copy_n = (total < max_entries) ? total : max_entries;
        memcpy(out, s_wifi_ipc_resp.data, copy_n * sizeof(wifi_mgr_scan_entry_t));
    }

    return (int)total;
}

/*******************************************************************************
 * Non-blocking scan API — for LVGL callbacks that must not block GFX task.
 ******************************************************************************/

//! [j3_wifi_mgr_scan_start_sendmessage]
bool wifi_manager_scan_start(void)
{
    wifi_manager_ensure_ipc();

    memset((void *)&s_wifi_ipc_resp, 0, sizeof(s_wifi_ipc_resp));
    s_wifi_ipc_resp.ready = 0;

    memset(&s_wifi_ipc_msg, 0, sizeof(s_wifi_ipc_msg));
    s_wifi_ipc_msg.client_id = CM33_IPC_SENSOR_CTRL_CLIENT_ID;
    s_wifi_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_wifi_ipc_msg.cmd = IPC_CMD_WIFI_SCAN;
    s_wifi_ipc_msg.value = (uint32_t)&s_wifi_ipc_resp;

    int retries = WIFI_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t send_status;
    do {
        send_status = Cy_IPC_Pipe_SendMessage(
            CM33_IPC_PIPE_EP_ADDR, CM55_IPC_PIPE_EP_ADDR,
            (void *)&s_wifi_ipc_msg, NULL);
        if (send_status == CY_IPC_PIPE_SUCCESS) {
            break;
        }
        Cy_SysLib_DelayUs(WIFI_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (retries <= 0) {
        s_last_error = WIFI_ERR_IPC_SEND_TIMEOUT;
        return false;
    }

    s_last_error = CY_RSLT_SUCCESS;
    return true;
}

bool wifi_manager_scan_ready(void)
{
    __DMB();
    return (s_wifi_ipc_resp.ready != 0);
}
//! [j3_wifi_mgr_scan_start_sendmessage]

int wifi_manager_scan_result(wifi_mgr_scan_entry_t *out, size_t max_entries)
{
    __DMB();
    if (!s_wifi_ipc_resp.ready) {
        s_last_error = WIFI_ERR_IPC_RESP_TIMEOUT;
        return -1;
    }

    if (s_wifi_ipc_resp.cmd != (uint8_t)IPC_CMD_WIFI_SCAN) {
        s_last_error = WIFI_ERR_IPC_BAD_RESPONSE;
        return -1;
    }

    if (s_wifi_ipc_resp.status != 0) {
        if (s_wifi_ipc_resp.data_len >= 4) {
            uint32_t err = 0;
            memcpy(&err, s_wifi_ipc_resp.data, 4);
            s_last_error = (cy_rslt_t)err;
        } else {
            s_last_error = (cy_rslt_t)(WIFI_ERR_IPC_STATUS_BASE | s_wifi_ipc_resp.status);
        }
        return -1;
    }

    s_last_error = CY_RSLT_SUCCESS;

    size_t total = s_wifi_ipc_resp.data_len / sizeof(ipc_wifi_scan_entry_t);
    if (total > IPC_WIFI_SCAN_MAX_ENTRIES) {
        total = IPC_WIFI_SCAN_MAX_ENTRIES;
    }

    if (out != NULL && max_entries > 0) {
        size_t copy_n = (total < max_entries) ? total : max_entries;
        memcpy(out, s_wifi_ipc_resp.data, copy_n * sizeof(wifi_mgr_scan_entry_t));
    }

    return (int)total;
}

/*******************************************************************************
 * Non-blocking status API — for LVGL callbacks that must not block GFX task.
 ******************************************************************************/

bool wifi_manager_status_start(void)
{
    wifi_manager_ensure_ipc();

    memset((void *)&s_wifi_ipc_resp, 0, sizeof(s_wifi_ipc_resp));
    s_wifi_ipc_resp.ready = 0;

    memset(&s_wifi_ipc_msg, 0, sizeof(s_wifi_ipc_msg));
    s_wifi_ipc_msg.client_id = CM33_IPC_SENSOR_CTRL_CLIENT_ID;
    s_wifi_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_wifi_ipc_msg.cmd = IPC_CMD_WIFI_STATUS;
    s_wifi_ipc_msg.value = (uint32_t)&s_wifi_ipc_resp;

    int retries = WIFI_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t send_status;
    do {
        send_status = Cy_IPC_Pipe_SendMessage(
            CM33_IPC_PIPE_EP_ADDR, CM55_IPC_PIPE_EP_ADDR,
            (void *)&s_wifi_ipc_msg, NULL);
        if (send_status == CY_IPC_PIPE_SUCCESS) {
            break;
        }
        Cy_SysLib_DelayUs(WIFI_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (retries <= 0) {
        s_last_error = WIFI_ERR_IPC_SEND_TIMEOUT;
        return false;
    }

    s_last_error = CY_RSLT_SUCCESS;
    return true;
}

bool wifi_manager_status_ready(void)
{
    __DMB();
    return (s_wifi_ipc_resp.ready != 0);
}

bool wifi_manager_status_result(wifi_mgr_status_t *status)
{
    __DMB();
    if (!s_wifi_ipc_resp.ready) {
        s_last_error = WIFI_ERR_IPC_RESP_TIMEOUT;
        return false;
    }

    if (s_wifi_ipc_resp.cmd != (uint8_t)IPC_CMD_WIFI_STATUS) {
        s_last_error = WIFI_ERR_IPC_BAD_RESPONSE;
        return false;
    }

    if (s_wifi_ipc_resp.status != 0) {
        s_last_error = (cy_rslt_t)(WIFI_ERR_IPC_STATUS_BASE | s_wifi_ipc_resp.status);
        return false;
    }

    s_last_error = CY_RSLT_SUCCESS;
    wifi_manager_decode_status(&s_wifi_ipc_resp, &s_status);

    if (status != NULL) {
        memcpy(status, &s_status, sizeof(wifi_mgr_status_t));
    }
    return true;
}

void wifi_manager_disconnect(void)
{
    (void)wifi_manager_ipc_request(IPC_CMD_WIFI_DISCONNECT, NULL, 0,
                                   WIFI_IPC_RESPONSE_TIMEOUT_MS);
    (void)wifi_manager_ipc_request(IPC_CMD_WIFI_STATUS, NULL, 0,
                                   WIFI_IPC_RESPONSE_TIMEOUT_MS);
    wifi_manager_decode_status(&s_wifi_ipc_resp, &s_status);
}

void wifi_manager_get_status(wifi_mgr_status_t *status)
{
    (void)wifi_manager_ipc_request(IPC_CMD_WIFI_STATUS, NULL, 0,
                                   WIFI_IPC_RESPONSE_TIMEOUT_MS);
    if (s_last_error == CY_RSLT_SUCCESS) {
        wifi_manager_decode_status(&s_wifi_ipc_resp, &s_status);
    }

    if (status != NULL) {
        memcpy(status, &s_status, sizeof(wifi_mgr_status_t));
    }
}

bool wifi_manager_is_connected(void)
{
    wifi_mgr_status_t st;
    memset(&st, 0, sizeof(st));
    wifi_manager_get_status(&st);
    return st.connected;
}

const char *wifi_manager_get_ip(void)
{
    wifi_mgr_status_t st;
    memset(&st, 0, sizeof(st));
    wifi_manager_get_status(&st);
    return s_status.ip_addr;
}

cy_rslt_t wifi_manager_last_error(void)
{
    return s_last_error;
}
