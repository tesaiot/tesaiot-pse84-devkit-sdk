/*******************************************************************************
 * File Name: modbentoclaw.c
 *
 * Description: MicroPython 'bentoclaw' module for CM33_NS.
 *              BentoClaw embedded AI assistant — complete agent runtime.
 *
 *              Transport:
 *                - TACP binary sub-protocol (USB/WebSerial ↔ BENTO AI IDE)
 *                - HTTPS POST via cy_http_client (↔ TESAIoT APISIX gateway)
 *
 *              API:
 *                bentoclaw.tools()                -> list of tool dicts
 *                bentoclaw.execute(name, params)  -> dict result
 *                bentoclaw.status()               -> dict agent status
 *                bentoclaw.version()              -> str version
 *                bentoclaw.connect(host, port, key) -> bool
 *                bentoclaw.ask(prompt)            -> str response
 *                bentoclaw.disconnect()           -> None
 *                bentoclaw.remember(key, value)   -> bool
 *                bentoclaw.history()              -> list of dicts
 *                bentoclaw.session_clear()        -> None
 *                bentoclaw.safety()               -> dict safety status
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mphal.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/compile.h"
#include "py/builtin.h"
#include "py/objtype.h"
#include "py/nlr.h"
#include "ipc_communication.h"
#include "ipc_bentoclaw_defs.h"
#include "tacp.h"
#include "claw_safety.h"
#include "claw_session.h"
#include "claw_transport.h"

/* Sensor + peripheral headers for tool execution */
#include "bsp_feature_flags.h"
#include "sensor_i2c.h"
#include "sensor_bmi270.h"
#include "sensor_auto_task.h"
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

#include "cy_pdl.h"
#include "cy_rtc.h"
#include "cy_wcm.h"
#include "cybsp_types.h"
#include "cycfg_pins.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdio.h>

/* Logging disabled — printf output on the same UART as TACP binary frames
 * corrupts the binary protocol, causing IDE-side parse timeouts.
 * Tool results use snprintf into s_work_large, not printf to UART.
 * Remove this #define to re-enable debug logging. */
#define printf(...) ((void)0)

/* External helpers — no dedicated headers in build include path */
extern void mpy_request_safe_boot_once(void);    /* mpy_main.c */

/* NTP sync check — weak-linked so builds work without tesaiot module.
 * When tesaiot module IS linked, the real implementation wins. */
__attribute__((weak)) bool tesaiot_sntp_is_time_synced(void) { return false; }

#include "bentoclaw_version.h"

/*******************************************************************************
 * JSON Escape Helper — escapes \, ", and control chars for safe JSON strings
 ******************************************************************************/
static size_t json_escape(char *dst, size_t dst_max, const char *src, size_t src_len) {
    size_t di = 0;
    for (size_t si = 0; si < src_len && di < dst_max - 2; si++) {
        char c = src[si];
        switch (c) {
        case '"':  dst[di++] = '\\'; if (di < dst_max - 1) dst[di++] = '"';  break;
        case '\\': dst[di++] = '\\'; if (di < dst_max - 1) dst[di++] = '\\'; break;
        case '\n': dst[di++] = '\\'; if (di < dst_max - 1) dst[di++] = 'n';  break;
        case '\r': dst[di++] = '\\'; if (di < dst_max - 1) dst[di++] = 'r';  break;
        case '\t': dst[di++] = '\\'; if (di < dst_max - 1) dst[di++] = 't';  break;
        default:
            if ((unsigned char)c < 0x20) {
                /* Skip other control characters */
            } else {
                dst[di++] = c;
            }
            break;
        }
    }
    dst[di] = '\0';
    return di;
}

/*******************************************************************************
 * IPC to CM55 — send UI updates for BentoClaw status page
 ******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t claw_ipc_msg;
/* Reuse joystick's shared IPC response buffer — MicroPython GIL means
 * only one tool runs at a time, so no concurrent access. */
extern ipc_response_t js_ipc_response;  /* defined in modjoystick.c */
#define claw_ipc_resp js_ipc_response

static bool claw_ipc_initialized = false;

#define CLAW_IPC_SEND_RETRIES   (20)
#define CLAW_IPC_RETRY_DELAY_US (200)

static void claw_ipc_init(void) {
    if (claw_ipc_initialized) return;
    cm33_ipc_communication_setup();
    Cy_SysLib_Delay(50);
    claw_ipc_initialized = true;
}

static bool claw_ipc_send_ui(uint8_t update_type, const char *text, size_t len) {
    cy_en_ipc_pipe_status_t status;
    int retries = CLAW_IPC_SEND_RETRIES;

    memset(&claw_ipc_msg, 0, sizeof(claw_ipc_msg));
    claw_ipc_msg.client_id = CM55_IPC_CLAW_CLIENT_ID;
    claw_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    claw_ipc_msg.cmd = IPC_CMD_CLAW_UI_UPDATE;
    claw_ipc_msg.value = 0;

    claw_ipc_msg.data[0] = (char)update_type;
    if (text && len > 0) {
        size_t copy_len = (len >= IPC_DATA_MAX_LEN - 2) ? (IPC_DATA_MAX_LEN - 2) : len;
        memcpy(&claw_ipc_msg.data[1], text, copy_len);
        claw_ipc_msg.data[1 + copy_len] = '\0';
    }

    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR,
            CM33_IPC_PIPE_EP_ADDR,
            (void *)&claw_ipc_msg,
            NULL
        );
        if (CY_IPC_PIPE_SUCCESS == status) return true;
        Cy_SysLib_DelayUs(CLAW_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    return false;
}

/*******************************************************************************
 * Tool Registry — static tool definitions with rate limits
 ******************************************************************************/

typedef struct {
    const char *name;
    const char *description;
    uint8_t     risk;          /* 0=low, 1=medium, 2=high */
    bool        confirm;       /* requires confirmation */
    uint16_t    rate_per_min;  /* max calls per minute (0=default) */
} claw_tool_def_t;

static const claw_tool_def_t s_tools[] = {
    { "gpio_set",         "Set LED on/off",               1, false, 30 },
    { "gpio_read",        "Read LED + button state",       0, false, 60 },
    { "sensor_read_all",  "Read all sensors",              0, false, 30 },
    { "lcd_console",      "Print text to LCD (Thai+EN)",   1, false, 20 },
    { "ui_notify",        "Show notification on screen",   1, false, 10 },
    { "wifi_status",      "Get WiFi connection info",      0, false, 10 },
    { "trustm_info",      "Read OPTIGA Trust M info",      0, false, 5  },
    { "trustm_sign",      "ECDSA sign with Trust M",       2, true,  2  },
    { "joystick_read",    "Read joystick state",            0, false, 60 },
    { "rule_set",         "Create automation rule",         2, true,  5  },
    { "rule_clear",       "Clear automation rules",         2, true,  2  },
    { "diagnostics_read", "Read system diagnostics",        0, false, 10 },
    { "rtc_read",         "Read current date/time",         0, false, 60 },
#if BSP_HAS_RADAR
    { "radar_read",       "Read radar presence + energy",   0, false, 30 },
#endif
    { "heap_info",        "Read heap memory usage",         0, false, 10 },
    { "fs_list",          "List files in directory",         0, false, 10 },
    { "fs_stat",          "Check file exists + size",        0, false, 30 },
    { "fs_read",          "Read text file content",          0, false, 10 },
    { "py_exec",          "Execute MicroPython code",        2, true,  5  },
    /* ── Sprint 1 (Batch A): 11 tools calling existing APIs ── */
    { "wifi_scan",        "Scan nearby WiFi APs",            0, false, 2  },
    { "wifi_ping",        "ICMP ping test",                  0, false, 5  },
    { "wifi_packet_stats","WiFi TX/RX packet counters",      0, false, 10 },
    { "ntp_status",       "NTP time sync status",            0, false, 30 },
    { "rtc_set",          "Set RTC date/time",               2, true,  5  },
    { "fs_write",         "Write text file to flash",        2, true,  5  },
    { "fs_delete",        "Delete file from flash",          2, true,  5  },
    { "trustm_random",    "Hardware RNG (OPTIGA TRNG)",      0, false, 10 },
    { "trustm_sha256",    "SHA-256 hash (OPTIGA HW)",        0, false, 10 },
    { "hard_reset",       "Force hardware reset (all cores)", 2, true,  1 },
    { "safe_boot",        "Soft reset, skip main.py",        2, true,  1  },
    /* ── Sprint 2 (Batch B): BSP-guarded Eva Kit tools ── */
#if BSP_HAS_CAPSENSE
    { "capsense_read",    "Read CapSense buttons + slider",  0, false, 30 },
#endif
#if BSP_HAS_POTENTIOMETER
    { "potentiometer_read","Read potentiometer value",       0, false, 30 },
#endif
};

#define TOOL_COUNT (sizeof(s_tools) / sizeof(s_tools[0]))

/* Agent state */
static uint8_t  s_agent_state = CLAW_STATE_IDLE;
static uint16_t s_exec_count  = 0;
static uint16_t s_error_count = 0;
static char     s_last_cmd[48];
static char     s_last_result[128];
static bool     s_module_inited = false;

/*******************************************************************************
 * Shared work buffers — single-threaded, so all MicroPython API and TACP
 * handlers can safely reuse these.  Saves ~20 KB vs per-function statics.
 ******************************************************************************/
static char s_work_large[3072];   /* JSON build, HTTP response, TOOLS list */
static char s_work_small[512];    /* escape, history, misc */

/* One-time init for safety + session subsystems */
//! [mpy_claw_ensure_init]
static void bentoclaw_ensure_init(void) {
    if (s_module_inited) return;
    claw_rate_init();
    claw_cb_init();
    claw_session_init();

    /* Set per-tool rate limits from registry */
    for (size_t i = 0; i < TOOL_COUNT; i++) {
        if (s_tools[i].rate_per_min > 0) {
            claw_rate_set(s_tools[i].name, s_tools[i].rate_per_min);
        }
    }

    s_module_inited = true;
}
//! [mpy_claw_ensure_init]

/*******************************************************************************
 * bentoclaw.tools() -> list of dicts
 ******************************************************************************/
static mp_obj_t bentoclaw_tools(void) {
    mp_obj_list_t *lst = MP_OBJ_TO_PTR(mp_obj_new_list(0, NULL));

    for (size_t i = 0; i < TOOL_COUNT; i++) {
        mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(4));
        mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
            MP_OBJ_NEW_QSTR(MP_QSTR_name),
            mp_obj_new_str(s_tools[i].name, strlen(s_tools[i].name)));
        mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
            MP_OBJ_NEW_QSTR(MP_QSTR_description),
            mp_obj_new_str(s_tools[i].description, strlen(s_tools[i].description)));
        mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
            MP_OBJ_NEW_QSTR(MP_QSTR_risk),
            mp_obj_new_int(s_tools[i].risk));
        mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
            MP_OBJ_NEW_QSTR(MP_QSTR_confirm),
            mp_obj_new_bool(s_tools[i].confirm));
        mp_obj_list_append(MP_OBJ_FROM_PTR(lst), MP_OBJ_FROM_PTR(d));
    }

    return MP_OBJ_FROM_PTR(lst);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bentoclaw_tools_obj, bentoclaw_tools);

/*******************************************************************************
 * bentoclaw.execute(tool_name, params_json) -> dict
 * With safety guardrails: rate limiter, circuit breaker, trust level
 ******************************************************************************/
static mp_obj_t bentoclaw_execute(mp_obj_t name_obj, mp_obj_t params_obj) {
    bentoclaw_ensure_init();
    claw_ipc_init();

    size_t name_len;
    const char *name = mp_obj_str_get_data(name_obj, &name_len);

    /* Look up tool */
    const claw_tool_def_t *found = NULL;
    for (size_t i = 0; i < TOOL_COUNT; i++) {
        if (strncmp(s_tools[i].name, name, name_len) == 0 &&
            strlen(s_tools[i].name) == name_len) {
            found = &s_tools[i];
            break;
        }
    }

    if (!found) {
        mp_raise_ValueError(MP_ERROR_TEXT("unknown tool"));
    }

    //! [mpy_claw_cb_allow_gate_order]
    /* ...context: inside the tool-call handler; 'found' is the resolved tool entry ... */
    /* Safety check: circuit breaker */
    if (!claw_cb_allow()) {
        s_error_count++;
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("circuit breaker open — cooldown active"));
    }

    /* Safety check: transport trust level */
    if (!claw_trust_allows(found->risk)) {
        s_error_count++;
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("tool risk exceeds transport trust level"));
    }

    /* Safety check: rate limiter */
    if (!claw_rate_check(found->name)) {
        s_error_count++;
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("rate limit exceeded for this tool"));
    }

    /* Record rate limit hit */
    claw_rate_record(found->name);
    //! [mpy_claw_cb_allow_gate_order]

    /* Update state */
    s_agent_state = CLAW_STATE_EXECUTING;
    s_exec_count++;
    snprintf(s_last_cmd, sizeof(s_last_cmd), "%.*s", (int)name_len, name);

    /* Send UI update: last command */
    claw_ipc_send_ui(CLAW_UI_LAST_CMD, s_last_cmd, strlen(s_last_cmd));

    /* Add to session history */
    char session_entry[CLAW_MSG_MAX_LEN];
    snprintf(session_entry, sizeof(session_entry), "execute: %s", s_last_cmd);
    claw_session_add(CLAW_ROLE_TOOL, session_entry);

    /* Dispatch — return tool info + "ready" status */
    s_agent_state = CLAW_STATE_IDLE;
    snprintf(s_last_result, sizeof(s_last_result), "dispatched");
    claw_cb_success();

    /* Send UI update: result */
    claw_ipc_send_ui(CLAW_UI_LAST_RESULT, s_last_result, strlen(s_last_result));

    mp_obj_dict_t *result = MP_OBJ_TO_PTR(mp_obj_new_dict(4));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_tool),
        mp_obj_new_str(found->name, strlen(found->name)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_status),
        MP_OBJ_NEW_QSTR(MP_QSTR_ready));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_risk),
        mp_obj_new_int(found->risk));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(result),
        MP_OBJ_NEW_QSTR(MP_QSTR_confirm),
        mp_obj_new_bool(found->confirm));

    return MP_OBJ_FROM_PTR(result);
}
static MP_DEFINE_CONST_FUN_OBJ_2(bentoclaw_execute_obj, bentoclaw_execute);

/*******************************************************************************
 * bentoclaw.connect(host, port, api_key) -> bool
 * Connect to TESAIoT APISIX gateway via HTTPS
 ******************************************************************************/
static mp_obj_t bentoclaw_connect(mp_obj_t host_obj, mp_obj_t port_obj,
                                  mp_obj_t key_obj) {
    bentoclaw_ensure_init();

    const char *host = mp_obj_str_get_str(host_obj);
    uint16_t port = (uint16_t)mp_obj_get_int(port_obj);
    const char *api_key = mp_obj_str_get_str(key_obj);

    //! [mpy_claw_trust_connect_pairing]
    /* ...context: inside bentoclaw_connect() ... */
    claw_ipc_init();
    claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Connecting...", 13);
    claw_trust_set(CLAW_TRUST_HTTPS);

    bool ok = claw_https_connect(host, port, api_key);

    if (ok) {
        claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Online (HTTPS)", 14);
        claw_session_add(CLAW_ROLE_SYSTEM, "Connected to TESAIoT backend");
    } else {
        claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Connect failed", 14);
        claw_trust_set(CLAW_TRUST_USB);
    }
    //! [mpy_claw_trust_connect_pairing]

    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_3(bentoclaw_connect_obj, bentoclaw_connect);

/*******************************************************************************
 * bentoclaw.ask(prompt) -> str
 * Send prompt to AI backend, return response text
 ******************************************************************************/
//! [mpy_claw_https_connected_ask_guard]
static mp_obj_t bentoclaw_ask(mp_obj_t prompt_obj) {
    bentoclaw_ensure_init();

    if (!claw_https_connected()) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("not connected — call bentoclaw.connect() first"));
    }

    if (!claw_cb_allow()) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("circuit breaker open"));
    }
    //! [mpy_claw_https_connected_ask_guard]

    size_t prompt_len;
    const char *prompt = mp_obj_str_get_data(prompt_obj, &prompt_len);

    claw_ipc_init();
    s_agent_state = CLAW_STATE_THINKING;
    claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Thinking...", 11);
    claw_ipc_send_ui(CLAW_UI_LAST_CMD, prompt,
                     prompt_len > IPC_DATA_MAX_LEN - 2 ? IPC_DATA_MAX_LEN - 2 : prompt_len);

    //! [mpy_claw_session_add_user]
    /* ...context: inside bentoclaw_ask() ... */
    /* Add user message to session */
    claw_session_add(CLAW_ROLE_USER, prompt);
    //! [mpy_claw_session_add_user]

    //! [mpy_claw_session_build_context]
    /* ...context: inside bentoclaw_ask() ... */
    /* Build context → escape both → build JSON → POST.
     * Escape prompt+context into s_work_small halves, then build JSON
     * in s_work_large (no overlap). */

    /* 1) Build context string in s_work_large */
    size_t ctx_len = claw_session_build_context(s_work_large, sizeof(s_work_large) - 256);

    /* 2) Escape prompt into s_work_small first half */
    char *esc_p = s_work_small;
    size_t ep_max = sizeof(s_work_small) / 2;
    size_t ep_len = json_escape(esc_p, ep_max, prompt,
                                prompt_len > 200 ? 200 : prompt_len);

    /* 3) Escape context into s_work_small second half */
    char *esc_c = s_work_small + ep_max;
    size_t ec_max = sizeof(s_work_small) / 2;
    size_t ec_len = json_escape(esc_c, ec_max,
                                s_work_large, ctx_len > 200 ? 200 : ctx_len);

    //! [mpy_claw_https_post_negative_check]
    /* ...context: inside bentoclaw_ask() ... */
    /* 4) Build JSON in s_work_large (no overlap with s_work_small) */
    int json_len = snprintf(s_work_large, sizeof(s_work_large),
        "{\"prompt\":\"%.*s\",\"context\":\"%.*s\"}",
        (int)ep_len, esc_p, (int)ec_len, esc_c);
        //! [mpy_claw_session_build_context]

    /* 5) POST to backend — response overwrites s_work_small */
    int resp_len = claw_https_post("/ai/v1/chat", s_work_large, (size_t)json_len,
                                    s_work_small, sizeof(s_work_small));

    //! [mpy_claw_cb_success_failure_pair]
    /* ...context: inside bentoclaw_ask(), after claw_https_post() ... */
    if (resp_len < 0) {
        s_agent_state = CLAW_STATE_ERROR;
        s_error_count++;
        claw_cb_failure();
        claw_ipc_send_ui(CLAW_UI_ERROR, "Backend error", 13);
        snprintf(s_last_result, sizeof(s_last_result), "error");
        claw_ipc_send_ui(CLAW_UI_LAST_RESULT, s_last_result, strlen(s_last_result));
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("HTTPS request failed"));
    }
    //! [mpy_claw_https_post_negative_check]

    //! [mpy_claw_session_add_assistant]
    /* ...context: inside bentoclaw_ask(), success path ... */
    s_agent_state = CLAW_STATE_IDLE;
    claw_cb_success();
    //! [mpy_claw_cb_success_failure_pair]
    snprintf(s_last_result, sizeof(s_last_result), "ok (%d bytes)", resp_len);
    claw_ipc_send_ui(CLAW_UI_LAST_RESULT, s_last_result, strlen(s_last_result));
    claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Online (HTTPS)", 14);

    /* Add assistant response to session (s_work_small holds response) */
    claw_session_add(CLAW_ROLE_ASSISTANT, s_work_small);

    return mp_obj_new_str(s_work_small, (size_t)resp_len);
    //! [mpy_claw_session_add_assistant]
}
static MP_DEFINE_CONST_FUN_OBJ_1(bentoclaw_ask_obj, bentoclaw_ask);

/*******************************************************************************
 * bentoclaw.disconnect() -> None
 ******************************************************************************/
//! [mpy_claw_https_disconnect_trust_revert]
static mp_obj_t bentoclaw_disconnect(void) {
    claw_https_disconnect();
    claw_trust_set(CLAW_TRUST_USB);
    if (claw_ipc_initialized) {
        claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Offline", 7);
    }
    claw_session_add(CLAW_ROLE_SYSTEM, "Disconnected from backend");
    return mp_const_none;
}
//! [mpy_claw_https_disconnect_trust_revert]
static MP_DEFINE_CONST_FUN_OBJ_0(bentoclaw_disconnect_obj, bentoclaw_disconnect);

/*******************************************************************************
 * bentoclaw.remember(key, value) -> bool
 ******************************************************************************/
//! [mpy_claw_memory_set_binding]
static mp_obj_t bentoclaw_remember(mp_obj_t key_obj, mp_obj_t val_obj) {
    bentoclaw_ensure_init();
    const char *key = mp_obj_str_get_str(key_obj);
    const char *val = mp_obj_str_get_str(val_obj);
    bool ok = claw_memory_set(key, val);
    return mp_obj_new_bool(ok);
}
//! [mpy_claw_memory_set_binding]
static MP_DEFINE_CONST_FUN_OBJ_2(bentoclaw_remember_obj, bentoclaw_remember);

/*******************************************************************************
 * bentoclaw.history() -> list of dicts [{role, content}, ...]
 ******************************************************************************/
static mp_obj_t bentoclaw_history(void) {
    bentoclaw_ensure_init();

    /* Build context string and return as simple string for now.
     * Full dict-per-message would require accessing ring buffer internals. */
    size_t len = claw_session_build_context(s_work_large, sizeof(s_work_large));
    return mp_obj_new_str(s_work_large, len);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bentoclaw_history_obj, bentoclaw_history);

/*******************************************************************************
 * bentoclaw.session_clear() -> None
 ******************************************************************************/
//! [mpy_claw_session_clear_ui_pair]
static mp_obj_t bentoclaw_session_clear(void) {
    bentoclaw_ensure_init();
    claw_session_clear();
    if (claw_ipc_initialized) {
        claw_ipc_send_ui(CLAW_UI_CLEAR, "", 0);
    }
    return mp_const_none;
}
//! [mpy_claw_session_clear_ui_pair]
static MP_DEFINE_CONST_FUN_OBJ_0(bentoclaw_session_clear_obj, bentoclaw_session_clear);

/*******************************************************************************
 * bentoclaw.safety() -> dict with circuit breaker + trust info
 ******************************************************************************/
//! [mpy_claw_safety_status]
static mp_obj_t bentoclaw_safety(void) {
    bentoclaw_ensure_init();

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(4));

    static const char *cb_names[] = { "closed", "open", "half-open" };
    claw_cb_state_t cb = claw_cb_state();
    const char *cb_str = (cb < 3) ? cb_names[cb] : "unknown";

    static const char *trust_names[] = { "none", "https", "usb" };
    claw_trust_level_t trust = claw_trust_get();
    const char *trust_str = (trust < 3) ? trust_names[trust] : "unknown";

    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_circuit_breaker),
        mp_obj_new_str(cb_str, strlen(cb_str)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_cooldown_ms),
        mp_obj_new_int(claw_cb_cooldown_remaining()));
        //! [mpy_claw_safety_status]
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_trust_level),
        mp_obj_new_str(trust_str, strlen(trust_str)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_connected),
        mp_obj_new_bool(claw_https_connected()));

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bentoclaw_safety_obj, bentoclaw_safety);

/*******************************************************************************
 * bentoclaw.status() -> dict
 ******************************************************************************/
static mp_obj_t bentoclaw_status(void) {
    bentoclaw_ensure_init();
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(8));

    static const char *state_names[] = {
        "idle", "thinking", "executing", "streaming", "error"
    };
    const char *state_str = (s_agent_state < 5) ? state_names[s_agent_state] : "unknown";

    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_state),
        mp_obj_new_str(state_str, strlen(state_str)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_exec_count),
        mp_obj_new_int(s_exec_count));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_error_count),
        mp_obj_new_int(s_error_count));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_tool_count),
        mp_obj_new_int(TOOL_COUNT));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_last_cmd),
        mp_obj_new_str(s_last_cmd, strlen(s_last_cmd)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_last_result),
        mp_obj_new_str(s_last_result, strlen(s_last_result)));
    //! [mpy_claw_session_count_status]
    /* ...context: inside bentoclaw_status() ... */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_session_msgs),
        mp_obj_new_int(claw_session_count()));
        //! [mpy_claw_session_count_status]
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_connected),
        mp_obj_new_bool(claw_https_connected()));

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bentoclaw_status_obj, bentoclaw_status);

/*******************************************************************************
 * bentoclaw.version() -> str
 ******************************************************************************/
static mp_obj_t bentoclaw_version(void) {
    return mp_obj_new_str(BENTOCLAW_VERSION, strlen(BENTOCLAW_VERSION));
}
static MP_DEFINE_CONST_FUN_OBJ_0(bentoclaw_version_obj, bentoclaw_version);

/*******************************************************************************
 * LED pin map for gpio_set / gpio_read
 ******************************************************************************/
typedef struct {
    const char     *name;
    GPIO_PRT_Type  *port;
    uint32_t        pin;
} claw_led_map_t;

static const claw_led_map_t s_led_map[] = {
#ifdef CYBSP_USER_LED1_PORT
    { "led1",      CYBSP_USER_LED1_PORT,    CYBSP_USER_LED1_PIN },
#endif
#ifdef CYBSP_USER_LED2_PORT
    { "led2",      CYBSP_USER_LED2_PORT,    CYBSP_USER_LED2_PIN },
#endif
#ifdef CYBSP_LED_RGB_RED_PORT
    { "rgb_red",   CYBSP_LED_RGB_RED_PORT,  CYBSP_LED_RGB_RED_PIN },
#endif
#ifdef CYBSP_LED_RGB_GREEN_PORT
    { "rgb_green", CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN },
#endif
#ifdef CYBSP_LED_RGB_BLUE_PORT
    { "rgb_blue",  CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN },
#endif
};
#define LED_COUNT (sizeof(s_led_map) / sizeof(s_led_map[0]))

/*******************************************************************************
 * JSON arg helper — extract string field from JSON args
 * If args is JSON (starts with '{'), extract "key":"value".
 * Otherwise return args as-is (plain text).
 * Returns pointer to static buf (NOT reentrant).
 ******************************************************************************/
static const char *claw_extract_arg(const char *args, size_t args_len,
                                     const char *key)
{
    static char s_arg_buf[128];

    if (!args || args_len == 0) return NULL;

    /* If not JSON, return as-is (trim leading spaces) */
    const char *p = args;
    while (*p == ' ' && p < args + args_len) p++;
    if (*p != '{') {
        size_t len = args_len - (size_t)(p - args);
        if (len >= sizeof(s_arg_buf)) len = sizeof(s_arg_buf) - 1;
        memcpy(s_arg_buf, p, len);
        s_arg_buf[len] = '\0';
        return s_arg_buf;
    }

    /* Search for "key":"value" */
    char needle[32];
    int nlen = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (nlen <= 0) return NULL;

    const char *found = NULL;
    for (const char *c = args; c < args + args_len - nlen; c++) {
        if (memcmp(c, needle, (size_t)nlen) == 0) {
            found = c + nlen;
            break;
        }
    }
    if (!found) return NULL;

    /* Skip whitespace and opening quote */
    while (*found == ' ' && found < args + args_len) found++;
    if (*found != '"') return NULL;
    found++;  /* skip opening quote */

    /* Copy until closing quote */
    size_t i = 0;
    while (found < args + args_len && *found != '"' && i < sizeof(s_arg_buf) - 1) {
        s_arg_buf[i++] = *found++;
    }
    s_arg_buf[i] = '\0';
    return (i > 0) ? s_arg_buf : NULL;
}

/*******************************************************************************
 * Tool Execution Functions
 ******************************************************************************/

/* ── sensor_read_all ─────────────────────────────────────────────────────── */

#ifdef USE_KIT_PSE84_EVAL_EPC2

/* Helper: fetch sensor snapshot from CM55 via IPC (reusable by capsense/pot) */
static bool claw_fetch_sensor_snapshot(ipc_sensor_snapshot_t *ss)
{
    memset((void *)&claw_ipc_resp, 0, sizeof(claw_ipc_resp));
    claw_ipc_resp.ready = 0;

    memset(&claw_ipc_msg, 0, sizeof(claw_ipc_msg));
    claw_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    claw_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    claw_ipc_msg.cmd = IPC_CMD_SENSOR_SNAPSHOT;
    claw_ipc_msg.value = (uint32_t)(uintptr_t)&claw_ipc_resp;

    int retries = 50;
    cy_en_ipc_pipe_status_t st;
    do {
        st = Cy_IPC_Pipe_SendMessage(CM55_IPC_PIPE_EP_ADDR,
                CM33_IPC_PIPE_EP_ADDR, (void *)&claw_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == st) break;
        Cy_SysLib_DelayUs(500);
    } while (--retries > 0);

    if (st != CY_IPC_PIPE_SUCCESS) return false;

    uint32_t timeout = 1000;
    while (!claw_ipc_resp.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }
    if (!claw_ipc_resp.ready) return false;

    memcpy(ss, claw_ipc_resp.data, sizeof(*ss));
    return true;
}

/* Eva Kit: CM55 owns SCB0 I2C for BMI270/CapSense — request via IPC */
static int claw_tool_sensor_read_all(char *out, size_t out_max) {
    int n = 0;

    ipc_sensor_snapshot_t ss;
    if (!claw_fetch_sensor_snapshot(&ss)) {
        return snprintf(out, out_max, "{\"error\":\"sensor ipc timeout\"}");
    }

    /* BMI270: convert raw int16 → float */
    float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
    if (ss.has_bmi270) {
        ax = (float)ss.bmi270.ax / IPC_BMI270_ACCEL_LSB_PER_G * GRAVITY_ACCEL;
        ay = (float)ss.bmi270.ay / IPC_BMI270_ACCEL_LSB_PER_G * GRAVITY_ACCEL;
        az = (float)ss.bmi270.az / IPC_BMI270_ACCEL_LSB_PER_G * GRAVITY_ACCEL;
        gx = (float)ss.bmi270.gx / IPC_BMI270_GYRO_LSB_PER_DPS;
        gy = (float)ss.bmi270.gy / IPC_BMI270_GYRO_LSB_PER_DPS;
        gz = (float)ss.bmi270.gz / IPC_BMI270_GYRO_LSB_PER_DPS;
    }

    n = snprintf(out, out_max,
        "{\"bmi270\":{\"accel\":[%.2f,%.2f,%.2f],"
        "\"gyro\":[%.1f,%.1f,%.1f]}",
        ax, ay, az, gx, gy, gz);

    /* CapSense */
    if (ss.has_capsense) {
        n += snprintf(out + n, out_max - n,
            ",\"capsense\":{\"btn0\":%d,\"btn1\":%d,\"slider\":%u}",
            (int)ss.capsense.btn0_pressed, (int)ss.capsense.btn1_pressed,
            (unsigned)ss.capsense.slider);
    }

    /* Potentiometer */
    if (ss.has_pot) {
        n += snprintf(out + n, out_max - n,
            ",\"pot\":{\"raw\":%u,\"pct\":%.1f}",
            (unsigned)ss.pot.raw,
            (float)ss.pot.percent_x10 / 10.0f);
    }

#if BSP_HAS_BMM350
    {
        float mx = 0, my = 0, mz = 0, heading = 0;
        if (bmm350_read_xyz(&mx, &my, &mz)) {
            bmm350_read_heading(&heading);
            n += snprintf(out + n, out_max - n,
                ",\"bmm350\":{\"mag\":[%.1f,%.1f,%.1f],\"heading\":%.1f}",
                mx, my, mz, heading);
        }
    }
#endif

    n += snprintf(out + n, out_max - n, "}");
    return n;
}

#else /* AI Kit + Game Console: CM33_NS owns I2C — read from cache */

static int claw_tool_sensor_read_all(char *out, size_t out_max) {
    int n = 0;

    /* BMI270: read from sensor_auto_task cache (updated every ~100ms) */
    sensor_auto_bmi270_cache_t imu;
    sensor_auto_get_bmi270(&imu);

    n = snprintf(out, out_max,
        "{\"bmi270\":{\"accel\":[%.2f,%.2f,%.2f],"
        "\"gyro\":[%.1f,%.1f,%.1f],\"temp\":%.1f}",
        imu.valid ? imu.ax : 0, imu.valid ? imu.ay : 0, imu.valid ? imu.az : 0,
        imu.valid ? imu.gx : 0, imu.valid ? imu.gy : 0, imu.valid ? imu.gz : 0,
        imu.valid ? imu.temp : 0);

#if BSP_HAS_DPS368
    {
        float pressure = 0, dps_temp = 0;
        sensor_i2c_lock(200);
        bool ok = dps368_read_both(&pressure, &dps_temp);
        sensor_i2c_unlock();
        if (ok) {
            n += snprintf(out + n, out_max - n,
                ",\"dps368\":{\"pressure\":%.1f,\"temp\":%.1f}",
                pressure, dps_temp);
        }
    }
#endif

#if BSP_HAS_SHT40
    {
        float sht_temp = 0, humidity = 0;
        sensor_i2c_lock(200);
        bool ok = sht40_read_both(&sht_temp, &humidity);
        sensor_i2c_unlock();
        if (ok) {
            n += snprintf(out + n, out_max - n,
                ",\"sht40\":{\"temp\":%.1f,\"humidity\":%.1f}",
                sht_temp, humidity);
        }
    }
#endif

#if BSP_HAS_BMM350
    {
        float mx = 0, my = 0, mz = 0, heading = 0;
        if (bmm350_read_xyz(&mx, &my, &mz)) {
            bmm350_read_heading(&heading);
            n += snprintf(out + n, out_max - n,
                ",\"bmm350\":{\"mag\":[%.1f,%.1f,%.1f],\"heading\":%.1f}",
                mx, my, mz, heading);
        }
    }
#endif

    n += snprintf(out + n, out_max - n, "}");
    return n;
}
#endif /* USE_KIT_PSE84_EVAL_EPC2 */

/* ── gpio_set ────────────────────────────────────────────────────────────── */
static int claw_tool_gpio_set(const char *args, size_t args_len,
                               char *out, size_t out_max)
{
    if (!args || args_len == 0) {
        return snprintf(out, out_max,
            "{\"error\":\"usage: gpio_set <led_name> <on|off>\"}");
    }

    const char *led_name;
    size_t nlen;
    const char *state_str;
    size_t slen;

    /* JSON: {"pin":"led1","value":1} or {"pin":"led1","value":"on"} */
    const char *jp = claw_extract_arg(args, args_len, "pin");
    if (jp && args[0] == '{') {
        static char gpio_pin[32];
        strncpy(gpio_pin, jp, sizeof(gpio_pin) - 1);
        gpio_pin[sizeof(gpio_pin) - 1] = '\0';
        led_name = gpio_pin;
        nlen = strlen(led_name);
        /* Extract value — claw_extract_arg handles "value":"on" but not "value":1 */
        const char *jv = claw_extract_arg(args, args_len, "value");
        if (jv) {
            state_str = jv;
            slen = strlen(jv);
        } else {
            /* Try numeric: search for "value": followed by digit */
            state_str = "0";
            slen = 1;
            const char *vp = strstr(args, "\"value\":");
            if (vp) {
                vp += 8;
                while (*vp == ' ') vp++;
                if (*vp == '1' || *vp == 't') { state_str = "on"; slen = 2; }
                else { state_str = "off"; slen = 3; }
            }
        }
    } else {
        /* Plain text: "led1 on" */
        const char *space = memchr(args, ' ', args_len);
        if (!space) {
            return snprintf(out, out_max,
                "{\"error\":\"usage: gpio_set <led_name> <on|off>\"}");
        }
        led_name = args;
        nlen = (size_t)(space - args);
        state_str = space + 1;
        slen = args_len - nlen - 1;
    }

    /* Find LED in map */
    const claw_led_map_t *led = NULL;
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (strncmp(s_led_map[i].name, led_name, nlen) == 0 &&
            strlen(s_led_map[i].name) == nlen) {
            led = &s_led_map[i];
            break;
        }
    }
    if (!led) {
        return snprintf(out, out_max, "{\"error\":\"unknown LED\","
            "\"available\":[\"led1\",\"led2\",\"rgb_red\",\"rgb_green\",\"rgb_blue\"]}");
    }

    bool turn_on = (slen >= 2 && strncmp(state_str, "on", 2) == 0) ||
                   (slen >= 1 && state_str[0] == '1');

    if (turn_on) {
        Cy_GPIO_Set(led->port, led->pin);
    } else {
        Cy_GPIO_Clr(led->port, led->pin);
    }

    return snprintf(out, out_max, "{\"led\":\"%s\",\"state\":\"%s\"}",
                    led->name, turn_on ? "on" : "off");
}

/* ── gpio_read ───────────────────────────────────────────────────────────── */
static int claw_tool_gpio_read(char *out, size_t out_max) {
    int n = snprintf(out, out_max, "{\"leds\":{");

    for (size_t i = 0; i < LED_COUNT; i++) {
        uint32_t val = Cy_GPIO_ReadOut(s_led_map[i].port, s_led_map[i].pin);
        n += snprintf(out + n, out_max - n, "%s\"%s\":%s",
                      i > 0 ? "," : "",
                      s_led_map[i].name,
                      val ? "true" : "false");
    }

#ifdef CYBSP_USER_BTN_PORT
    uint32_t btn = Cy_GPIO_Read(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN);
    n += snprintf(out + n, out_max - n, "},\"btn1_pressed\":%s}",
                  btn == 0 ? "true" : "false");
#else
    n += snprintf(out + n, out_max - n, "}}");
#endif

    return n;
}

/* ── lcd_console ─────────────────────────────────────────────────────────── */
static int claw_tool_lcd_console(const char *args, size_t args_len,
                                  char *out, size_t out_max)
{
    if (!args || args_len == 0) {
        return snprintf(out, out_max, "{\"error\":\"no text\"}");
    }

    /* Send text to LCD via IPC_CMD_LCD_PRINT (reuse shared IPC msg) */
    memset(&claw_ipc_msg, 0, sizeof(claw_ipc_msg));
    claw_ipc_msg.client_id = CM55_IPC_CLAW_CLIENT_ID;
    claw_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    claw_ipc_msg.cmd = IPC_CMD_LCD_PRINT;
    size_t copy = (args_len >= IPC_DATA_MAX_LEN - 1) ? (IPC_DATA_MAX_LEN - 1) : args_len;
    memcpy(claw_ipc_msg.data, args, copy);
    claw_ipc_msg.data[copy] = '\0';

    Cy_IPC_Pipe_SendMessage(CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
                            (void *)&claw_ipc_msg, NULL);

    return snprintf(out, out_max, "{\"status\":\"printed\",\"len\":%u}",
                    (unsigned)args_len);
}

/* ── ui_notify ───────────────────────────────────────────────────────────── */
static int claw_tool_ui_notify(const char *args, size_t args_len,
                                char *out, size_t out_max)
{
    if (!args || args_len == 0) {
        return snprintf(out, out_max, "{\"error\":\"no message\"}");
    }
    claw_ipc_send_ui(CLAW_UI_PROGRESS, args,
                     args_len > IPC_DATA_MAX_LEN - 2 ? IPC_DATA_MAX_LEN - 2 : args_len);
    return snprintf(out, out_max, "{\"status\":\"notified\"}");
}

/* ── wifi_status ─────────────────────────────────────────────────────────── */
static int claw_tool_wifi_status(char *out, size_t out_max) {
    int n = 0;
    uint8_t connected = cy_wcm_is_connected_to_ap();
    n = snprintf(out, out_max, "{\"connected\":%s",
                 connected ? "true" : "false");

    if (connected) {
        cy_wcm_ip_address_t ip = {0};
        cy_wcm_associated_ap_info_t info;
        memset(&info, 0, sizeof(info));
        cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_STA, &ip);
        cy_wcm_get_associated_ap_info(&info);

        uint32_t v4 = ip.ip.v4;
        n += snprintf(out + n, out_max - n,
            ",\"ip\":\"%u.%u.%u.%u\""
            ",\"ssid\":\"%.32s\""
            ",\"rssi\":%d"
            ",\"channel\":%u",
            (unsigned)((v4 >> 0) & 0xFF), (unsigned)((v4 >> 8) & 0xFF),
            (unsigned)((v4 >> 16) & 0xFF), (unsigned)((v4 >> 24) & 0xFF),
            (char *)info.SSID,
            info.signal_strength,
            info.channel);
    }
    /* Boot credential diagnostic */
    {
        extern volatile int g_boot_wifi_creds_count;
        extern volatile bool g_boot_wifi_creds_dirty;
        n += snprintf(out + n, out_max - n,
            ",\"saved_creds\":%d,\"dirty\":%s",
            (int)g_boot_wifi_creds_count,
            g_boot_wifi_creds_dirty ? "true" : "false");
    }
    n += snprintf(out + n, out_max - n, "}");
    return n;
}

/* ── trustm_info ─────────────────────────────────────────────────────────── */
static int claw_tool_trustm_info(char *out, size_t out_max) {
    /* OPTIGA Trust M is present on board but deep read (UID, cert validity)
       requires optiga_trust_helpers which is not linked in BentoClaw build.
       Report presence and basic info for now. */
    return snprintf(out, out_max,
        "{\"hsm\":\"OPTIGA Trust M\""
        ",\"status\":\"present\""
        ",\"note\":\"use optiga module for full UID/cert queries\"}");
}

/* ── diagnostics_read ────────────────────────────────────────────────────── */
static int claw_tool_diagnostics(char *out, size_t out_max) {
    /* heap_3 wraps malloc/free — xPortGetFreeHeapSize not available.
       Report uptime + task count which are always available. */
    uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t tasks     = (uint32_t)uxTaskGetNumberOfTasks();

    return snprintf(out, out_max,
        "{\"uptime_ms\":%lu"
        ",\"tasks\":%lu"
        ",\"transport\":\"USB\"}",
        (unsigned long)uptime_ms, (unsigned long)tasks);
}

/* ── rtc_read ────────────────────────────────────────────────────────────── */
static int claw_tool_rtc_read(char *out, size_t out_max) {
    cy_stc_rtc_config_t dt;
    memset(&dt, 0, sizeof(dt));
    Cy_RTC_GetDateAndTime(&dt);

    return snprintf(out, out_max,
        "{\"year\":%u,\"month\":%u,\"day\":%u"
        ",\"weekday\":%u,\"hour\":%u,\"min\":%u,\"sec\":%u}",
        (unsigned)(dt.year + 2000), (unsigned)dt.month, (unsigned)dt.date,
        (unsigned)dt.dayOfWeek, (unsigned)dt.hour, (unsigned)dt.min,
        (unsigned)dt.sec);
}

/* ── radar_read (presence + energy via IPC 0xB0 to CM55) ─────────────── */

static int claw_tool_radar_read(char *out, size_t out_max) {
#if BSP_HAS_RADAR
    /* Prepare response buffer */
    memset((void *)&claw_ipc_resp, 0, sizeof(claw_ipc_resp));
    claw_ipc_resp.ready = 0;

    /* Send IPC request to CM55 */
    memset(&claw_ipc_msg, 0, sizeof(claw_ipc_msg));
    claw_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    claw_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    claw_ipc_msg.cmd = IPC_CMD_RADAR_STATUS;
    claw_ipc_msg.value = (uint32_t)(uintptr_t)&claw_ipc_resp;

    int retries = 20;
    cy_en_ipc_pipe_status_t st;
    do {
        st = Cy_IPC_Pipe_SendMessage(CM55_IPC_PIPE_EP_ADDR,
                CM33_IPC_PIPE_EP_ADDR, (void *)&claw_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == st) break;
        Cy_SysLib_DelayUs(100);
    } while (--retries > 0);

    if (retries <= 0) {
        return snprintf(out, out_max, "{\"error\":\"radar ipc send failed\"}");
    }

    /* Wait for CM55 response (max 500ms) */
    uint32_t timeout = 500;
    while (!claw_ipc_resp.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }
    if (!claw_ipc_resp.ready) {
        return snprintf(out, out_max, "{\"error\":\"radar ipc timeout\"}");
    }

    /* Parse response */
    ipc_radar_status_t rd;
    memcpy(&rd, claw_ipc_resp.data, sizeof(rd));

    return snprintf(out, out_max,
        "{\"initialized\":%s,\"presence\":%s,\"energy\":%.1f}",
        rd.initialized ? "true" : "false",
        rd.presence ? "true" : "false",
        (double)rd.energy);
#else
    return snprintf(out, out_max, "{\"error\":\"radar not available\"}");
#endif
}

/* ── heap_info — MicroPython GC heap stats ───────────────────────────────── */
static int claw_tool_heap_info(char *out, size_t out_max) {
    gc_collect();
    gc_info_t info;
    gc_info(&info);

    uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    return snprintf(out, out_max,
        "{\"gc_total\":%lu,\"gc_used\":%lu,\"gc_free\":%lu"
        ",\"gc_max_free\":%lu,\"gc_fragments\":%lu"
        ",\"uptime_ms\":%lu,\"tasks\":%lu}",
        (unsigned long)info.total,
        (unsigned long)info.used,
        (unsigned long)info.free,
        (unsigned long)info.max_free,
        (unsigned long)(info.free - info.max_free),
        (unsigned long)uptime_ms,
        (unsigned long)uxTaskGetNumberOfTasks());
}

/* ── fs_list — List files in a directory ─────────────────────────────────── */
static int claw_tool_fs_list(const char *args, size_t args_len,
                              char *out, size_t out_max) {
    /* args = directory path (default "/"), supports JSON {"path":"..."} */
    const char *path = "/";
    const char *extracted = claw_extract_arg(args, args_len, "path");
    if (extracted) path = extracted;

    /* Use MicroPython to list files — most reliable with VFS */
    char py_cmd[128];
    snprintf(py_cmd, sizeof(py_cmd),
        "import os; print(','.join(os.listdir('%s')))", path);

    /* Capture output by executing and reading result from work buffer.
     * Since exec_python_str prints to stdout which goes to UART,
     * we use os.listdir() directly via mp_call_function. */

    /* Simpler: use mp_import + call */
    mp_obj_t os_mod = mp_import_name(
        MP_QSTR_os, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t os_listdir = mp_load_attr(os_mod, MP_QSTR_listdir);
    mp_obj_t dir_str = mp_obj_new_str(path, strlen(path));

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t result = mp_call_function_1(os_listdir, dir_str);

        /* Build JSON array from returned list */
        int off = 0;
        off += snprintf(out + off, out_max - off, "{\"path\":\"%s\",\"files\":[", path);

        size_t len;
        mp_obj_t *items;
        mp_obj_list_get(result, &len, &items);

        for (size_t i = 0; i < len && off < (int)out_max - 16; i++) {
            size_t slen;
            const char *s = mp_obj_str_get_data(items[i], &slen);
            off += snprintf(out + off, out_max - off,
                            "%s\"%.*s\"", i > 0 ? "," : "", (int)slen, s);
        }
        off += snprintf(out + off, out_max - off, "],\"count\":%u}", (unsigned)len);
        nlr_pop();
        return off;
    } else {
        return snprintf(out, out_max,
            "{\"error\":\"listdir failed\",\"path\":\"%s\"}", path);
    }
}

/* ── fs_stat — Check if file exists + get size ───────────────────────────── */
static int claw_tool_fs_stat(const char *args, size_t args_len,
                              char *out, size_t out_max) {
    const char *path = claw_extract_arg(args, args_len, "path");
    if (!path) {
        return snprintf(out, out_max, "{\"error\":\"no path given\"}");
    }

    size_t plen = strlen(path);
    mp_obj_t os_mod = mp_import_name(
        MP_QSTR_os, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t os_stat = mp_load_attr(os_mod, MP_QSTR_stat);
    mp_obj_t path_str = mp_obj_new_str(path, plen);

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t result = mp_call_function_1(os_stat, path_str);

        /* os.stat returns a 10-tuple: (mode, ino, dev, nlink, uid, gid, size, ...) */
        mp_obj_t *items;
        size_t len;
        mp_obj_tuple_get(result, &len, &items);

        int mode = len > 0 ? mp_obj_get_int(items[0]) : 0;
        int size = len > 6 ? mp_obj_get_int(items[6]) : 0;
        bool is_dir = (mode & 0x4000) != 0;

        nlr_pop();
        return snprintf(out, out_max,
            "{\"path\":\"%s\",\"exists\":true,\"size\":%d,\"is_dir\":%s}",
            path, size, is_dir ? "true" : "false");
    } else {
        /* OSError = file not found */
        return snprintf(out, out_max,
            "{\"path\":\"%s\",\"exists\":false}", path);
    }
}

/* ── fs_read — Read file content (text, max 512 bytes) ───────────────────── */
static int claw_tool_fs_read(const char *args, size_t args_len,
                              char *out, size_t out_max) {
    const char *path = claw_extract_arg(args, args_len, "path");
    if (!path) {
        return snprintf(out, out_max, "{\"error\":\"no path given\"}");
    }

    size_t plen = strlen(path);

    /* Open file via MicroPython builtins */
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t args_open[2] = {
            mp_obj_new_str(path, plen),
            mp_obj_new_str("r", 1)
        };
        mp_obj_t f = mp_builtin_open(2, args_open, (mp_map_t *)&mp_const_empty_map);
        mp_obj_t read_method = mp_load_attr(f, MP_QSTR_read);
        /* Read up to 512 bytes */
        mp_obj_t max_size = MP_OBJ_NEW_SMALL_INT(512);
        mp_obj_t content = mp_call_function_1(read_method, max_size);
        mp_obj_t close_method = mp_load_attr(f, MP_QSTR_close);
        mp_call_function_0(close_method);

        size_t content_len;
        const char *content_str = mp_obj_str_get_data(content, &content_len);

        /* JSON-escape the content */
        int off = snprintf(out, out_max, "{\"path\":\"%s\",\"size\":%u,\"content\":\"",
                           path, (unsigned)content_len);
        off += (int)json_escape(out + off, out_max - off - 3, content_str, content_len);
        off += snprintf(out + off, out_max - off, "\"}");

        nlr_pop();
        return off;
    } else {
        return snprintf(out, out_max,
            "{\"error\":\"cannot read file\",\"path\":\"%s\"}", path);
    }
}

/* ── py_exec — Execute arbitrary MicroPython code string ─────────────────── */
static int claw_tool_py_exec(const char *args, size_t args_len,
                              char *out, size_t out_max) {
    const char *code = claw_extract_arg(args, args_len, "code");
    if (!code) {
        return snprintf(out, out_max, "{\"error\":\"no code given\"}");
    }
    size_t code_len = strlen(code);

    /* The code is in args (null-terminated by dispatcher).
     * We capture stdout into a buffer by temporarily redirecting. */
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        /* Execute code — output goes to UART/REPL stdout */
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_bentoclaw_gt_, code, code_len, 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&pt, MP_QSTR__lt_bentoclaw_gt_,
                                          false);
        mp_call_function_0(module_fun);
        nlr_pop();
        return snprintf(out, out_max, "{\"status\":\"ok\",\"code_len\":%u}",
                         (unsigned)code_len);
    } else {
        /* Exception during execution */
        mp_obj_t exc = (mp_obj_t)nlr.ret_val;
        vstr_t vstr;
        vstr_init(&vstr, 128);
        mp_obj_print_helper(
            &(mp_print_t){.data = &vstr, .print_strn = (void *)vstr_add_strn},
            exc, PRINT_EXC);

        int off = snprintf(out, out_max, "{\"status\":\"error\",\"exception\":\"");
        off += (int)json_escape(out + off, out_max - off - 3,
                                 vstr.buf, vstr.len);
        off += snprintf(out + off, out_max - off, "\"}");
        vstr_clear(&vstr);
        return off;
    }
}

/*******************************************************************************
 * Sprint 1 (Batch A): 11 new tool functions — call existing APIs only
 ******************************************************************************/

/* ── wifi_scan — Scan nearby APs (blocking, ~3-5 seconds) ─────────────── */
#define WIFI_SCAN_MAX_RESULTS 16
static cy_wcm_scan_result_t s_scan_results[WIFI_SCAN_MAX_RESULTS];
static volatile int s_scan_count = 0;
static volatile bool s_scan_done = false;

static void wifi_scan_cb(cy_wcm_scan_result_t *result, void *user_data,
                          cy_wcm_scan_status_t status) {
    (void)user_data;
    if (status == CY_WCM_SCAN_INCOMPLETE && result &&
        s_scan_count < WIFI_SCAN_MAX_RESULTS) {
        s_scan_results[s_scan_count++] = *result;
    }
    if (status == CY_WCM_SCAN_COMPLETE) {
        s_scan_done = true;
    }
}

static int claw_tool_wifi_scan(char *out, size_t out_max) {
    s_scan_count = 0;
    s_scan_done = false;

    cy_wcm_scan_filter_t filter;
    memset(&filter, 0, sizeof(filter));
    cy_rslt_t r = cy_wcm_start_scan(wifi_scan_cb, NULL, &filter);
    if (r != CY_RSLT_SUCCESS) {
        return snprintf(out, out_max,
            "{\"error\":\"scan_start_failed\",\"code\":%u}", (unsigned)r);
    }

    /* Wait for scan complete (max 6 seconds) */
    for (int i = 0; i < 60 && !s_scan_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    cy_wcm_stop_scan();

    int n = snprintf(out, out_max, "{\"count\":%d,\"aps\":[", s_scan_count);
    for (int i = 0; i < s_scan_count && n < (int)out_max - 80; i++) {
        /* JSON-safe SSID: skip non-printable chars */
        char safe_ssid[33];
        int si = 0;
        for (int j = 0; j < 32 && s_scan_results[i].SSID[j]; j++) {
            char c = (char)s_scan_results[i].SSID[j];
            if (c >= 0x20 && c != '"' && c != '\\') {
                safe_ssid[si++] = c;
            }
        }
        safe_ssid[si] = '\0';

        n += snprintf(out + n, out_max - n,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"ch\":%d}",
            i > 0 ? "," : "",
            safe_ssid,
            s_scan_results[i].signal_strength,
            s_scan_results[i].channel);
    }
    n += snprintf(out + n, out_max - n, "]}");
    return n;
}

/* ── wifi_ping — ICMP ping (args: "8.8.8.8" or empty=gateway) ────────── */
static int claw_tool_wifi_ping(const char *args, size_t args_len,
                                char *out, size_t out_max) {
    if (!cy_wcm_is_connected_to_ap()) {
        return snprintf(out, out_max, "{\"error\":\"not connected\"}");
    }

    cy_wcm_ip_address_t target;
    memset(&target, 0, sizeof(target));
    target.version = CY_WCM_IP_VER_V4;

    const char *host = claw_extract_arg(args, args_len, "host");
    if (!host) host = args;  /* fallback to raw */
    size_t host_len = host ? strlen(host) : 0;

    if (host && host_len >= 7) {
        unsigned a, b, c, d;
        if (sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 &&
            a <= 255 && b <= 255 && c <= 255 && d <= 255) {
            target.ip.v4 = a | (b << 8) | (c << 16) | (d << 24);
        } else {
            return snprintf(out, out_max, "{\"error\":\"invalid IP format\"}");
        }
    } else {
        cy_wcm_get_gateway_ip_address(CY_WCM_INTERFACE_TYPE_STA, &target);
    }

    uint32_t elapsed_ms = 0;
    cy_rslt_t r = cy_wcm_ping(CY_WCM_INTERFACE_TYPE_STA, &target,
                               3000, &elapsed_ms);
    uint32_t v4 = target.ip.v4;
    return snprintf(out, out_max,
        "{\"success\":%s,\"elapsed_ms\":%u,"
        "\"target\":\"%u.%u.%u.%u\"}",
        r == CY_RSLT_SUCCESS ? "true" : "false",
        (unsigned)elapsed_ms,
        (unsigned)(v4 & 0xFF), (unsigned)((v4 >> 8) & 0xFF),
        (unsigned)((v4 >> 16) & 0xFF), (unsigned)((v4 >> 24) & 0xFF));
}

/* ── wifi_packet_stats — TX/RX counters ───────────────────────────────── */
static int claw_tool_wifi_packet_stats(char *out, size_t out_max) {
    if (!cy_wcm_is_connected_to_ap()) {
        return snprintf(out, out_max, "{\"error\":\"not connected\"}");
    }
    cy_wcm_wlan_statistics_t stat;
    memset(&stat, 0, sizeof(stat));
    cy_rslt_t r = cy_wcm_get_wlan_statistics(CY_WCM_INTERFACE_TYPE_STA, &stat);
    if (r != CY_RSLT_SUCCESS) {
        return snprintf(out, out_max, "{\"error\":\"stats unavailable\"}");
    }
    return snprintf(out, out_max,
        "{\"tx_packets\":%lu,\"tx_bytes\":%lu,"
        "\"rx_packets\":%lu,\"rx_bytes\":%lu,"
        "\"tx_retries\":%lu,\"tx_failed\":%lu}",
        (unsigned long)stat.tx_packets, (unsigned long)stat.tx_bytes,
        (unsigned long)stat.rx_packets, (unsigned long)stat.rx_bytes,
        (unsigned long)stat.tx_retries, (unsigned long)stat.tx_failed);
}

/* ── ntp_status — NTP sync status ─────────────────────────────────────── */
static int claw_tool_ntp_status(char *out, size_t out_max) {
    bool synced = tesaiot_sntp_is_time_synced();
    bool wifi = cy_wcm_is_connected_to_ap();
    return snprintf(out, out_max,
        "{\"ntp_synced\":%s,\"wifi_connected\":%s}",
        synced ? "true" : "false",
        wifi ? "true" : "false");
}

/* ── rtc_set — Set RTC date/time (args: "YYYY MM DD HH MM SS") ───────── */
static int claw_tool_rtc_set(const char *args, size_t args_len,
                              char *out, size_t out_max) {
    if (!args || args_len < 10) {
        return snprintf(out, out_max,
            "{\"error\":\"format: YYYY MM DD HH MM SS\"}");
    }
    unsigned y, mo, d, h, mi, s;
    if (sscanf(args, "%u %u %u %u %u %u", &y, &mo, &d, &h, &mi, &s) != 6) {
        return snprintf(out, out_max,
            "{\"error\":\"format: YYYY MM DD HH MM SS\"}");
    }
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
        h > 23 || mi > 59 || s > 59) {
        return snprintf(out, out_max, "{\"error\":\"date/time out of range\"}");
    }
    cy_en_rtc_status_t r = Cy_RTC_SetDateAndTimeDirect(
        s, mi, h, d, mo, (uint32_t)(y - 2000));
    return snprintf(out, out_max,
        "{\"success\":%s,\"set\":\"%04u-%02u-%02u %02u:%02u:%02u\"}",
        r == CY_RTC_SUCCESS ? "true" : "false", y, mo, d, h, mi, s);
}

/* ── fs_write — Write file ────────────────────────────────────────────── */
static int claw_tool_fs_write(const char *args, size_t args_len,
                               char *out, size_t out_max) {
    if (!args || args_len < 3) {
        return snprintf(out, out_max,
            "{\"error\":\"format: /path content\"}");
    }

    static char fw_path[128];
    const char *path;
    const char *content;
    size_t path_len, content_len;

    /* Try JSON args: {"path":"/file","data":"content"} */
    const char *jp = claw_extract_arg(args, args_len, "path");
    if (jp && args[0] == '{') {
        /* Copy path before second extract overwrites static buf */
        strncpy(fw_path, jp, sizeof(fw_path) - 1);
        fw_path[sizeof(fw_path) - 1] = '\0';
        path = fw_path;
        path_len = strlen(path);
        const char *jd = claw_extract_arg(args, args_len, "data");
        if (!jd) {
            return snprintf(out, out_max,
                "{\"error\":\"missing data field\"}");
        }
        content = jd;
        content_len = strlen(content);
    } else {
        /* Plain text: "/path content here..." */
        const char *space = memchr(args, ' ', args_len);
        if (!space || space == args) {
            return snprintf(out, out_max,
                "{\"error\":\"format: /path content\"}");
        }
        path_len = (size_t)(space - args);
        if (path_len > 127) {
            return snprintf(out, out_max, "{\"error\":\"path too long\"}");
        }
        static char fs_path2[128];
        memcpy(fs_path2, args, path_len);
        fs_path2[path_len] = '\0';
        path = fs_path2;
        content = space + 1;
        content_len = args_len - path_len - 1;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t f_args[2] = {
            mp_obj_new_str(path, path_len),
            mp_obj_new_str("w", 1)
        };
        mp_obj_t file = mp_builtin_open(2, f_args,
                            (mp_map_t *)&mp_const_empty_map);
        mp_obj_t write_fn = mp_load_attr(file, MP_QSTR_write);
        mp_obj_t data = mp_obj_new_str(content, content_len);
        mp_call_function_1(write_fn, data);
        mp_obj_t close_fn = mp_load_attr(file, MP_QSTR_close);
        mp_call_function_0(close_fn);
        nlr_pop();
        return snprintf(out, out_max,
            "{\"path\":\"%s\",\"bytes_written\":%u}",
            path, (unsigned)content_len);
    } else {
        return snprintf(out, out_max,
            "{\"error\":\"write failed\",\"path\":\"%s\"}", path);
    }
}

/* ── fs_delete — Delete file ──────────────────────────────────────────── */
static int claw_tool_fs_delete(const char *args, size_t args_len,
                                char *out, size_t out_max) {
    const char *path = claw_extract_arg(args, args_len, "path");
    if (!path || path[0] != '/') {
        return snprintf(out, out_max, "{\"error\":\"invalid path\"}");
    }
    size_t plen = strlen(path);

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t os_mod = mp_import_name(
            MP_QSTR_os, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t remove_fn = mp_load_attr(os_mod, MP_QSTR_remove);
        mp_call_function_1(remove_fn, mp_obj_new_str(path, plen));
        nlr_pop();
        return snprintf(out, out_max,
            "{\"path\":\"%s\",\"deleted\":true}", path);
    } else {
        return snprintf(out, out_max,
            "{\"path\":\"%s\",\"deleted\":false,"
            "\"error\":\"not found or permission denied\"}", path);
    }
}

/* ── trustm_random — OPTIGA TRNG hardware random bytes ────────────────── */
static int claw_tool_trustm_random(const char *args, size_t args_len,
                                    char *out, size_t out_max) {
    /* OPTIGA crypt operations require the async state machine
     * (optiga_crypt_create + callback + wait loop). Full implementation
     * deferred to Sprint 5. For now, use MicroPython os.urandom(). */
    unsigned len = 32;
    const char *len_arg = claw_extract_arg(args, args_len, "length");
    if (len_arg) {
        sscanf(len_arg, "%u", &len);
    } else if (args && args_len > 0) {
        sscanf(args, "%u", &len);
    }
    if (len < 8 || len > 256) {
        return snprintf(out, out_max, "{\"error\":\"len must be 8-256\"}");
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t os_mod = mp_import_name(
            MP_QSTR_os, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t urandom_fn = mp_load_attr(os_mod, MP_QSTR_urandom);
        mp_obj_t result = mp_call_function_1(urandom_fn,
                                              MP_OBJ_NEW_SMALL_INT(len));
        mp_buffer_info_t buf_info;
        mp_get_buffer_raise(result, &buf_info, MP_BUFFER_READ);

        int n = snprintf(out, out_max, "{\"bytes\":%u,\"hex\":\"", len);
        const uint8_t *buf = (const uint8_t *)buf_info.buf;
        for (unsigned i = 0; i < len && n < (int)out_max - 4; i++) {
            n += snprintf(out + n, out_max - n, "%02x", buf[i]);
        }
        n += snprintf(out + n, out_max - n, "\"}");
        nlr_pop();
        return n;
    } else {
        return snprintf(out, out_max, "{\"error\":\"urandom failed\"}");
    }
}

/* ── trustm_sha256 — SHA-256 via MicroPython hashlib ──────────────────── */
static int claw_tool_trustm_sha256(const char *args, size_t args_len,
                                    char *out, size_t out_max) {
    /* Full OPTIGA hardware SHA-256 deferred to Sprint 5 (async state machine).
     * Use MicroPython hashlib.sha256 which uses mbedTLS. */
    const char *data = claw_extract_arg(args, args_len, "data");
    if (!data) {
        return snprintf(out, out_max, "{\"error\":\"no data to hash\"}");
    }
    size_t data_len = strlen(data);

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t hashlib_mod = mp_import_name(
            MP_QSTR_hashlib, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t sha256_cls = mp_load_attr(hashlib_mod, MP_QSTR_sha256);
        mp_obj_t hasher = mp_call_function_1(sha256_cls,
                            mp_obj_new_bytes((const byte *)data, data_len));
        mp_obj_t digest_fn = mp_load_attr(hasher, MP_QSTR_digest);
        mp_obj_t digest = mp_call_function_0(digest_fn);

        mp_buffer_info_t buf_info;
        mp_get_buffer_raise(digest, &buf_info, MP_BUFFER_READ);

        int n = snprintf(out, out_max,
            "{\"algorithm\":\"sha256\",\"input_len\":%u,\"hash\":\"",
            (unsigned)data_len);
        const uint8_t *h = (const uint8_t *)buf_info.buf;
        for (size_t i = 0; i < buf_info.len && n < (int)out_max - 4; i++) {
            n += snprintf(out + n, out_max - n, "%02x", h[i]);
        }
        n += snprintf(out + n, out_max - n, "\"}");
        nlr_pop();
        return n;
    } else {
        return snprintf(out, out_max, "{\"error\":\"sha256 failed\"}");
    }
}

/* ── hard_reset — NVIC_SystemReset (all cores restart) ────────────────── */
static volatile bool s_pending_hard_reset = false;

static int claw_tool_hard_reset(char *out, size_t out_max) {
    /* Set flag — actual reset happens after TACP response is sent */
    s_pending_hard_reset = true;
    return snprintf(out, out_max, "{\"status\":\"resetting\"}");
}

/* ── safe_boot — soft reset skipping main.py ──────────────────────────── */
static volatile bool s_pending_safe_boot = false;

static int claw_tool_safe_boot(char *out, size_t out_max) {
    mpy_request_safe_boot_once();
    s_pending_safe_boot = true;
    return snprintf(out, out_max, "{\"status\":\"safe_boot_scheduled\"}");
}

/*******************************************************************************
 * Sprint 2 (Batch B): BSP-guarded Eva Kit tools
 ******************************************************************************/

#if BSP_HAS_CAPSENSE
/* ── capsense_read — CapSense buttons + slider (Eva Kit) ──────────────── */
#ifdef USE_KIT_PSE84_EVAL_EPC2
/* Eva Kit BentoClaw: CM55 owns SCB0 — read via IPC sensor snapshot */
static int claw_tool_capsense_read(char *out, size_t out_max) {
    ipc_sensor_snapshot_t ss;
    if (!claw_fetch_sensor_snapshot(&ss)) {
        return snprintf(out, out_max, "{\"error\":\"ipc timeout\"}");
    }
    if (!ss.has_capsense) {
        return snprintf(out, out_max, "{\"error\":\"capsense not available\"}");
    }
    return snprintf(out, out_max,
        "{\"btn0\":%s,\"btn1\":%s,\"slider\":%u}",
        ss.capsense.btn0_pressed ? "true" : "false",
        ss.capsense.btn1_pressed ? "true" : "false",
        (unsigned)ss.capsense.slider);
}
#else
/* AI Kit: CM33 reads directly */
static int claw_tool_capsense_read(char *out, size_t out_max) {
    capsense_data_t data;
    memset(&data, 0, sizeof(data));
    if (!sensor_i2c_lock(200)) {
        return snprintf(out, out_max, "{\"error\":\"i2c_busy\"}");
    }
    bool ok = capsense_read(&data);
    sensor_i2c_unlock();
    if (!ok) {
        return snprintf(out, out_max, "{\"error\":\"capsense read failed\"}");
    }
    return snprintf(out, out_max,
        "{\"btn0\":%s,\"btn1\":%s,\"slider\":%u}",
        data.btn0_pressed ? "true" : "false",
        data.btn1_pressed ? "true" : "false",
        (unsigned)data.slider);
}
#endif
#endif /* BSP_HAS_CAPSENSE */

#if BSP_HAS_POTENTIOMETER
/* ── potentiometer_read — ADC pot value (Eva Kit) ─────────────────────── */
#ifdef USE_KIT_PSE84_EVAL_EPC2
/* Eva Kit BentoClaw: CM55 owns ADC — read via IPC sensor snapshot */
static int claw_tool_potentiometer_read(char *out, size_t out_max) {
    ipc_sensor_snapshot_t ss;
    if (!claw_fetch_sensor_snapshot(&ss)) {
        return snprintf(out, out_max, "{\"error\":\"ipc timeout\"}");
    }
    if (!ss.has_pot) {
        return snprintf(out, out_max, "{\"error\":\"pot not available\"}");
    }
    return snprintf(out, out_max,
        "{\"raw\":%u,\"percent\":%.1f}",
        (unsigned)ss.pot.raw,
        (float)ss.pot.percent_x10 / 10.0f);
}
#else
/* AI Kit: CM33 reads directly */
static int claw_tool_potentiometer_read(char *out, size_t out_max) {
    uint16_t raw = 0;
    float percent = 0, voltage = 0;
    bool ok = potentiometer_read_raw(&raw);
    potentiometer_read_percent(&percent);
    potentiometer_read_voltage(&voltage);
    if (!ok) {
        return snprintf(out, out_max, "{\"error\":\"pot read failed\"}");
    }
    return snprintf(out, out_max,
        "{\"raw\":%u,\"percent\":%.1f,\"voltage\":%.3f}",
        (unsigned)raw, (double)percent, (double)voltage);
}
#endif
#endif /* BSP_HAS_POTENTIOMETER */

/*******************************************************************************
 * Post-EXEC hook — handle deferred reset after TACP response is sent
 ******************************************************************************/
static void claw_post_exec_hook(void) {
    if (s_pending_hard_reset) {
        s_pending_hard_reset = false;
        Cy_SysLib_Delay(100); /* Let UART flush */
        NVIC_SystemReset();
        /* Does not return */
    }
    if (s_pending_safe_boot) {
        s_pending_safe_boot = false;
        Cy_SysLib_Delay(100);
        /* Trigger soft reset by raising SystemExit */
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_raise_type(&mp_type_SystemExit);
            nlr_pop();
        }
    }
}

/*******************************************************************************
 * joystick_read — Read gamepad state via IPC from CM55 USB Host
 ******************************************************************************/
static int claw_tool_joystick_read(char *out, size_t out_max) {
    /* Use the joystick IPC infrastructure from modjoystick.c */
    memset((void *)&claw_ipc_resp, 0, sizeof(claw_ipc_resp));
    claw_ipc_resp.ready = 0;

    memset(&claw_ipc_msg, 0, sizeof(claw_ipc_msg));
    claw_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    claw_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    claw_ipc_msg.cmd = IPC_CMD_JOYSTICK_STATE;
    claw_ipc_msg.value = (uint32_t)(uintptr_t)&claw_ipc_resp;

    int retries = 50;
    cy_en_ipc_pipe_status_t st;
    do {
        st = Cy_IPC_Pipe_SendMessage(CM55_IPC_PIPE_EP_ADDR,
                CM33_IPC_PIPE_EP_ADDR, (void *)&claw_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == st) break;
        Cy_SysLib_DelayUs(500);
    } while (--retries > 0);

    if (st != CY_IPC_PIPE_SUCCESS) {
        return snprintf(out, out_max, "{\"error\":\"joystick ipc send failed\"}");
    }

    uint32_t timeout = 1000;
    while (!claw_ipc_resp.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }
    if (!claw_ipc_resp.ready) {
        return snprintf(out, out_max, "{\"error\":\"joystick ipc timeout\"}");
    }

    ipc_joystick_state_t js;
    memcpy(&js, claw_ipc_resp.data, sizeof(js));

    if (!js.connected) {
        return snprintf(out, out_max,
            "{\"connected\":false,\"usb_init\":%d}",
            (int)js.usb_init_done);
    }

    /* Decode button bitmasks */
    uint8_t hat = js.buttons1 & 0x0F;
    return snprintf(out, out_max,
        "{\"connected\":true,"
        "\"left\":[%u,%u],\"right\":[%u,%u],"
        "\"hat\":%u,\"A\":%d,\"B\":%d,\"X\":%d,\"Y\":%d,"
        "\"LB\":%d,\"RB\":%d,\"LT\":%d,\"RT\":%d,"
        "\"back\":%d,\"start\":%d,\"L3\":%d,\"R3\":%d}",
        (unsigned)js.left_x, (unsigned)js.left_y,
        (unsigned)js.right_x, (unsigned)js.right_y,
        (unsigned)hat,
        (js.buttons1 >> 5) & 1, (js.buttons1 >> 6) & 1,
        (js.buttons1 >> 4) & 1, (js.buttons1 >> 7) & 1,
        (js.buttons2 >> 0) & 1, (js.buttons2 >> 1) & 1,
        (js.buttons2 >> 2) & 1, (js.buttons2 >> 3) & 1,
        (js.buttons2 >> 4) & 1, (js.buttons2 >> 5) & 1,
        (js.buttons2 >> 6) & 1, (js.buttons2 >> 7) & 1);
}

/*******************************************************************************
 * trustm_sign — ECDSA sign using MicroPython's ec module (mbedTLS backend)
 ******************************************************************************/
static int claw_tool_trustm_sign(const char *args, size_t args_len,
                                  char *out, size_t out_max) {
    const char *data = claw_extract_arg(args, args_len, "data");
    if (!data) {
        return snprintf(out, out_max,
            "{\"error\":\"usage: trustm_sign {\\\"data\\\":\\\"message\\\"}\"}");
    }
    size_t data_len = strlen(data);

    /* Hash the data with SHA-256, then sign with ECDSA P-256 ephemeral key.
     * Full OPTIGA hardware signing deferred — uses mbedTLS via MicroPython. */
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        /* SHA-256 hash */
        mp_obj_t hashlib = mp_import_name(MP_QSTR_hashlib, mp_const_none,
                                           MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t sha256_cls = mp_load_attr(hashlib, MP_QSTR_sha256);
        mp_obj_t hasher = mp_call_function_1(sha256_cls,
                            mp_obj_new_bytes((const byte *)data, data_len));
        mp_obj_t digest_fn = mp_load_attr(hasher, MP_QSTR_digest);
        mp_obj_t digest = mp_call_function_0(digest_fn);

        mp_buffer_info_t dig_buf;
        mp_get_buffer_raise(digest, &dig_buf, MP_BUFFER_READ);

        /* Format digest as hex */
        int n = snprintf(out, out_max,
            "{\"algorithm\":\"sha256\",\"input_len\":%u,\"digest\":\"",
            (unsigned)data_len);
        const uint8_t *h = (const uint8_t *)dig_buf.buf;
        for (size_t i = 0; i < dig_buf.len && n < (int)out_max - 4; i++) {
            n += snprintf(out + n, out_max - n, "%02x", h[i]);
        }
        n += snprintf(out + n, out_max - n,
            "\",\"note\":\"OPTIGA ECDSA pending — digest only\"}");
        nlr_pop();
        return n;
    } else {
        return snprintf(out, out_max, "{\"error\":\"sign failed\"}");
    }
}

/*******************************************************************************
 * rule_set / rule_clear — Simple automation rules (in-memory)
 *
 * Rules: when trigger fires → perform action.
 * Trigger: "button0", "button1", "timer_5s", "pot_high", "pot_low"
 * Action:  "led1_on", "led1_off", "led2_on", "led2_off", "notify"
 ******************************************************************************/
#define CLAW_MAX_RULES  4

typedef struct {
    char trigger[24];
    char action[24];
    bool active;
} claw_rule_t;

static claw_rule_t s_rules[CLAW_MAX_RULES];

static int claw_tool_rule_set(const char *args, size_t args_len,
                               char *out, size_t out_max) {
    const char *trigger = claw_extract_arg(args, args_len, "trigger");
    if (!trigger) {
        return snprintf(out, out_max,
            "{\"error\":\"usage: {\\\"trigger\\\":\\\"button0\\\",\\\"action\\\":\\\"led1_on\\\"}\"}");
    }
    static char trig_buf[24];
    strncpy(trig_buf, trigger, sizeof(trig_buf) - 1);
    trig_buf[sizeof(trig_buf) - 1] = '\0';

    const char *action = claw_extract_arg(args, args_len, "action");
    if (!action) {
        return snprintf(out, out_max, "{\"error\":\"missing action field\"}");
    }

    /* Find empty slot or overwrite existing trigger */
    int slot = -1;
    for (int i = 0; i < CLAW_MAX_RULES; i++) {
        if (s_rules[i].active && strcmp(s_rules[i].trigger, trig_buf) == 0) {
            slot = i;
            break;
        }
        if (!s_rules[i].active && slot < 0) slot = i;
    }
    if (slot < 0) {
        return snprintf(out, out_max, "{\"error\":\"max %d rules\",\"max\":%d}",
                        CLAW_MAX_RULES, CLAW_MAX_RULES);
    }

    strncpy(s_rules[slot].trigger, trig_buf, sizeof(s_rules[slot].trigger) - 1);
    s_rules[slot].trigger[sizeof(s_rules[slot].trigger) - 1] = '\0';
    strncpy(s_rules[slot].action, action, sizeof(s_rules[slot].action) - 1);
    s_rules[slot].action[sizeof(s_rules[slot].action) - 1] = '\0';
    s_rules[slot].active = true;

    return snprintf(out, out_max,
        "{\"status\":\"ok\",\"slot\":%d,\"trigger\":\"%s\",\"action\":\"%s\"}",
        slot, s_rules[slot].trigger, s_rules[slot].action);
}

static int claw_tool_rule_clear(char *out, size_t out_max) {
    int count = 0;
    for (int i = 0; i < CLAW_MAX_RULES; i++) {
        if (s_rules[i].active) count++;
        s_rules[i].active = false;
        s_rules[i].trigger[0] = '\0';
        s_rules[i].action[0] = '\0';
    }
    return snprintf(out, out_max,
        "{\"status\":\"cleared\",\"rules_removed\":%d}", count);
}

/*******************************************************************************
 * IPC Helper — push Agent Statistics to CM55 LCD page
 ******************************************************************************/
static void claw_ipc_send_stats(void) {
    static const char *state_names[] = {
        "idle", "thinking", "executing", "streaming", "error"
    };
    const char *st = (s_agent_state < 5) ? state_names[s_agent_state] : "unknown";
    int n = snprintf(s_work_small, sizeof(s_work_small),
                     "State       %s\n"
                     "Tools       %u\n"
                     "Executed    %u\n"
                     "Errors      %u\n"
                     "Transport   USB",
                     st, (unsigned)TOOL_COUNT,
                     s_exec_count, s_error_count);
    if (n > 0) {
        claw_ipc_send_ui(CLAW_UI_STATS, s_work_small,
                         (size_t)n > IPC_DATA_MAX_LEN - 2 ? IPC_DATA_MAX_LEN - 2 : (size_t)n);
    }
}

/*******************************************************************************
 * Binary TACP response — sends [AA][55][30][sub_cmd][len][payload][CRC].
 * IDE parses binary frames via WebSerial. No text output to avoid mixing
 * with REPL and to avoid newlib printf malloc on tight heap.
 ******************************************************************************/
//! [mpy_tacp_claw_respond_framing]
static void claw_respond(uint8_t sub_cmd, const char *payload, uint16_t payload_len) {
    tacp_claw_respond(sub_cmd, payload, payload_len);
}

static void claw_respond_err(const char *msg, uint16_t msg_len) {
    tacp_claw_respond(TACP_CLAW_ERROR, msg, msg_len);
}

/*******************************************************************************
 * READY frame — sent once at boot to announce firmware readiness.
 * IDE listens for this instead of polling STATUS with delay+retry.
 ******************************************************************************/
void bentoclaw_send_ready(void) {
    bentoclaw_ensure_init();
    claw_ipc_init();

    int n = snprintf(s_work_small, sizeof(s_work_small),
        "{\"fw\":\"%s\",\"board\":\"%s\",\"tools\":%u,\"proto\":2}",
        BENTOCLAW_VERSION, MICROPY_HW_BOARD_NAME, (unsigned)TOOL_COUNT);
    tacp_claw_respond(TACP_CLAW_READY, s_work_small, (uint16_t)n);

    /* Push initial stats to CM55 LCD so it shows real tool count immediately */
    claw_ipc_send_stats();
    //! [mpy_tacp_claw_respond_framing]
}

/*******************************************************************************
 * TACP Dispatch — called from tacp.c when TACP_CMD_BENTOCLAW is received.
 * All responses are binary TACP frames (no text output).
 ******************************************************************************/
void bentoclaw_tacp_dispatch(uint8_t sub_cmd, const char *payload,
                             uint16_t payload_len)
{
    bentoclaw_ensure_init();
    claw_ipc_init();

    /* Auto-recover stale agent state (P0-6): if we're processing a new
     * command, any previous "executing" or "thinking" state must have
     * completed or crashed (single-threaded MicroPython). Reset to idle. */
    if (s_agent_state == CLAW_STATE_EXECUTING ||
        s_agent_state == CLAW_STATE_THINKING) {
        s_agent_state = CLAW_STATE_IDLE;
    }

    switch (sub_cmd) {
    case TACP_CLAW_EXEC: {
        /* payload = "tool_name" or "tool_name args..." */

        /* Split tool name from args at first space */
        size_t name_len = payload_len;
        const char *args = NULL;
        size_t args_len = 0;
        for (size_t j = 0; j < payload_len; j++) {
            if (payload[j] == ' ') {
                name_len = j;
                args = payload + j + 1;
                args_len = payload_len - j - 1;
                break;
            }
        }
        /* args/args_len used by tool dispatch below */

        /* Find tool */
        const claw_tool_def_t *found = NULL;
        for (size_t i = 0; i < TOOL_COUNT; i++) {
            if (strncmp(s_tools[i].name, payload, name_len) == 0 &&
                strlen(s_tools[i].name) == name_len) {
                found = &s_tools[i];
                break;
            }
        }
        if (!found) {
            claw_respond_err("unknown tool", 12);
            return;
        }

        /* Safety checks */
        if (!claw_cb_allow()) {
            claw_respond_err("circuit breaker open", 20);
            return;
        }
        if (!claw_trust_allows(found->risk)) {
            claw_respond_err("trust level insufficient", 24);
            return;
        }
        if (!claw_rate_check(found->name)) {
            claw_respond_err("rate limited", 12);
            return;
        }

        claw_rate_record(found->name);
        s_agent_state = CLAW_STATE_EXECUTING;
        s_exec_count++;
        snprintf(s_last_cmd, sizeof(s_last_cmd), "%.*s", (int)payload_len, payload);
        claw_ipc_send_ui(CLAW_UI_LAST_CMD, s_last_cmd, strlen(s_last_cmd));

        /* ─── Tool Dispatch ─── */
        int result_len = 0;
        const char *tn = found->name;

        if (strcmp(tn, "sensor_read_all") == 0) {
            result_len = claw_tool_sensor_read_all(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "gpio_set") == 0) {
            result_len = claw_tool_gpio_set(args, args_len, s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "gpio_read") == 0) {
            result_len = claw_tool_gpio_read(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "lcd_console") == 0) {
            result_len = claw_tool_lcd_console(args, args_len, s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "ui_notify") == 0) {
            result_len = claw_tool_ui_notify(args, args_len, s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "wifi_status") == 0) {
            result_len = claw_tool_wifi_status(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "trustm_info") == 0) {
            result_len = claw_tool_trustm_info(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "diagnostics_read") == 0) {
            result_len = claw_tool_diagnostics(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "rtc_read") == 0) {
            result_len = claw_tool_rtc_read(s_work_large, sizeof(s_work_large));
#if BSP_HAS_RADAR
        } else if (strcmp(tn, "radar_read") == 0) {
            result_len = claw_tool_radar_read(s_work_large, sizeof(s_work_large));
#endif
        } else if (strcmp(tn, "heap_info") == 0) {
            result_len = claw_tool_heap_info(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "fs_list") == 0) {
            result_len = claw_tool_fs_list(args, args_len,
                                            s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "fs_stat") == 0) {
            result_len = claw_tool_fs_stat(args, args_len,
                                            s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "fs_read") == 0) {
            result_len = claw_tool_fs_read(args, args_len,
                                            s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "py_exec") == 0) {
            result_len = claw_tool_py_exec(args, args_len,
                                            s_work_large, sizeof(s_work_large));
        /* ── Sprint 1 (Batch A): 11 new tools ── */
        } else if (strcmp(tn, "wifi_scan") == 0) {
            result_len = claw_tool_wifi_scan(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "wifi_ping") == 0) {
            result_len = claw_tool_wifi_ping(args, args_len,
                                              s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "wifi_packet_stats") == 0) {
            result_len = claw_tool_wifi_packet_stats(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "ntp_status") == 0) {
            result_len = claw_tool_ntp_status(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "rtc_set") == 0) {
            result_len = claw_tool_rtc_set(args, args_len,
                                            s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "fs_write") == 0) {
            result_len = claw_tool_fs_write(args, args_len,
                                             s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "fs_delete") == 0) {
            result_len = claw_tool_fs_delete(args, args_len,
                                              s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "trustm_random") == 0) {
            result_len = claw_tool_trustm_random(args, args_len,
                                                  s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "trustm_sha256") == 0) {
            result_len = claw_tool_trustm_sha256(args, args_len,
                                                  s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "hard_reset") == 0) {
            result_len = claw_tool_hard_reset(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "safe_boot") == 0) {
            result_len = claw_tool_safe_boot(s_work_large, sizeof(s_work_large));
        /* ── Sprint 2 (Batch B): BSP-guarded Eva Kit tools ── */
#if BSP_HAS_CAPSENSE
        } else if (strcmp(tn, "capsense_read") == 0) {
            result_len = claw_tool_capsense_read(s_work_large, sizeof(s_work_large));
#endif
#if BSP_HAS_POTENTIOMETER
        } else if (strcmp(tn, "potentiometer_read") == 0) {
            result_len = claw_tool_potentiometer_read(s_work_large, sizeof(s_work_large));
#endif
        } else if (strcmp(tn, "joystick_read") == 0) {
            result_len = claw_tool_joystick_read(s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "trustm_sign") == 0) {
            result_len = claw_tool_trustm_sign(args, args_len,
                                                s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "rule_set") == 0) {
            result_len = claw_tool_rule_set(args, args_len,
                                             s_work_large, sizeof(s_work_large));
        } else if (strcmp(tn, "rule_clear") == 0) {
            result_len = claw_tool_rule_clear(s_work_large, sizeof(s_work_large));
        } else {
            result_len = snprintf(s_work_large, sizeof(s_work_large),
                "{\"tool\":\"%s\",\"status\":\"unknown\"}", tn);
        }

        /* ─── Validate + Send result ─── */
        if (result_len <= 0) {
            result_len = snprintf(s_work_large, sizeof(s_work_large),
                "{\"tool\":\"%s\",\"status\":\"error\",\"code\":-1}", tn);
            s_error_count++;
            claw_cb_failure();
        } else {
            if (result_len > (int)sizeof(s_work_large))
                result_len = (int)sizeof(s_work_large);
            claw_cb_success();
        }
        s_agent_state = CLAW_STATE_IDLE;

        /* Update LCD last-result (truncate to fit IPC) */
        snprintf(s_last_result, sizeof(s_last_result), "%.*s",
                 result_len > (int)sizeof(s_last_result) - 1
                     ? (int)sizeof(s_last_result) - 1 : result_len,
                 s_work_large);
        claw_ipc_send_ui(CLAW_UI_LAST_RESULT, s_last_result, strlen(s_last_result));

        /* Binary TACP response to IDE */
        claw_respond(TACP_CLAW_EXEC, s_work_large, (uint16_t)result_len);
        claw_ipc_send_stats();

        /* Reclaim MicroPython GC heap after tool execution.
         * Tools like fs_read, py_exec, trustm_sha256 allocate temporary
         * objects that fragment the heap. gc_collect() defragments so
         * IDE file transfers (which need ~16KB contiguous) don't fail. */
        gc_collect();

        /* Deferred reset/safe-boot after response is sent */
        claw_post_exec_hook();
        break;
    }

    case TACP_CLAW_ASK:
        /* payload = user prompt text */
        claw_session_add(CLAW_ROLE_USER, payload);
        s_agent_state = CLAW_STATE_THINKING;
        claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Thinking...", 11);
        claw_ipc_send_ui(CLAW_UI_LAST_CMD, payload,
                         payload_len > IPC_DATA_MAX_LEN - 2 ? IPC_DATA_MAX_LEN - 2 : payload_len);

        if (claw_https_connected()) {
            /* Escape payload → build JSON → POST, reusing shared buffers */
            size_t epl = json_escape(s_work_small, sizeof(s_work_small), payload,
                                     payload_len > 256 ? 256 : payload_len);

            int jlen = snprintf(s_work_large, sizeof(s_work_large),
                "{\"prompt\":\"%.*s\"}", (int)epl, s_work_small);

            int rlen = claw_https_post("/ai/v1/chat", s_work_large, (size_t)jlen,
                                        s_work_small, sizeof(s_work_small));
            if (rlen > 0) {
                s_agent_state = CLAW_STATE_IDLE;
                claw_cb_success();
                claw_session_add(CLAW_ROLE_ASSISTANT, s_work_small);
                claw_ipc_send_ui(CLAW_UI_LAST_RESULT, s_work_small,
                                 rlen > IPC_DATA_MAX_LEN - 2 ? IPC_DATA_MAX_LEN - 2 : (size_t)rlen);
                claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Online (HTTPS)", 14);
                claw_respond(TACP_CLAW_ASK, s_work_small, (uint16_t)rlen);
            } else {
                s_agent_state = CLAW_STATE_ERROR;
                s_error_count++;
                claw_cb_failure();
                claw_ipc_send_ui(CLAW_UI_ERROR, "Backend error", 13);
                claw_respond_err("backend request failed", 22);
            }
        } else {
            /* No backend — echo prompt for IDE to proxy via APISIX */
            s_agent_state = CLAW_STATE_IDLE;
            claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Local (USB)", 11);
            claw_respond(TACP_CLAW_ASK, payload, payload_len);
        }
        claw_ipc_send_stats();
        gc_collect();
        break;

    case TACP_CLAW_STATUS: {
        static const char *state_names[] = {
            "idle", "thinking", "executing", "streaming", "error"
        };
        const char *st = (s_agent_state < 5) ? state_names[s_agent_state] : "unknown";
        int n = snprintf(s_work_small, sizeof(s_work_small),
                         "{\"state\":\"%s\",\"exec\":%u,\"err\":%u,"
                         "\"tools\":%u,\"session\":%u,\"connected\":%s}",
                         st, s_exec_count, s_error_count, (unsigned)TOOL_COUNT,
                         claw_session_count(),
                         claw_https_connected() ? "true" : "false");
        claw_respond(TACP_CLAW_STATUS, s_work_small, (uint16_t)n);

        /* Push status to LCD page */
        claw_ipc_send_ui(CLAW_UI_STATUS_TEXT,
                         claw_https_connected() ? "Online (HTTPS)" : "Local (USB)",
                         claw_https_connected() ? 14 : 11);
        claw_ipc_send_stats();
        break;
    }

    case TACP_CLAW_TOOLS: {
        /* Build JSON array in s_work_large */
        int off = 0;
        off += snprintf(s_work_large + off, sizeof(s_work_large) - off, "[");
        for (size_t i = 0; i < TOOL_COUNT && off < (int)sizeof(s_work_large) - 64; i++) {
            off += snprintf(s_work_large + off, sizeof(s_work_large) - off,
                            "%s{\"name\":\"%s\",\"desc\":\"%s\",\"risk\":%d,\"confirm\":%s}",
                            i > 0 ? "," : "",
                            s_tools[i].name, s_tools[i].description,
                            s_tools[i].risk,
                            s_tools[i].confirm ? "true" : "false");
        }
        off += snprintf(s_work_large + off, sizeof(s_work_large) - off, "]");
        claw_respond(TACP_CLAW_TOOLS, s_work_large, (uint16_t)off);
        break;
    }

    case TACP_CLAW_HISTORY: {
        size_t hlen = claw_session_build_context(s_work_large, sizeof(s_work_large));
        claw_respond(TACP_CLAW_HISTORY, s_work_large, (uint16_t)hlen);
        break;
    }

    case TACP_CLAW_REMEMBER:
        /* payload format: "key=value" */
        {
            const char *eq = memchr(payload, '=', payload_len);
            if (eq && eq > payload) {
                char key[CLAW_MEM_KEY_MAX];
                char val[CLAW_MEM_VAL_MAX];
                size_t klen = (size_t)(eq - payload);
                size_t vlen = payload_len - klen - 1;
                if (klen >= CLAW_MEM_KEY_MAX) klen = CLAW_MEM_KEY_MAX - 1;
                if (vlen >= CLAW_MEM_VAL_MAX) vlen = CLAW_MEM_VAL_MAX - 1;
                memcpy(key, payload, klen); key[klen] = '\0';
                memcpy(val, eq + 1, vlen); val[vlen] = '\0';
                bool ok = claw_memory_set(key, val);
                claw_respond(TACP_CLAW_REMEMBER,
                             ok ? "ok" : "full", ok ? 2 : 4);
            } else {
                claw_respond_err("format: key=value", 17);
            }
        }
        break;

    case TACP_CLAW_CLEAR:
        claw_session_clear();
        claw_ipc_send_ui(CLAW_UI_CLEAR, "", 0);
        claw_respond(TACP_CLAW_CLEAR, "ok", 2);
        claw_ipc_send_stats();
        break;

    case TACP_CLAW_CONNECT:
        /* payload format: "host:port:api_key" */
        {
            char host[128] = {0};
            uint16_t port = 443;
            char api_key[128] = {0};

            const char *p1 = memchr(payload, ':', payload_len);
            if (p1) {
                size_t hlen = (size_t)(p1 - payload);
                if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
                memcpy(host, payload, hlen);
                host[hlen] = '\0';

                const char *p2 = memchr(p1 + 1, ':', payload_len - hlen - 1);
                if (p2) {
                    port = 0;
                    bool port_valid = (p2 > p1 + 1);
                    for (const char *c = p1 + 1; c < p2; c++) {
                        if (*c < '0' || *c > '9') { port_valid = false; break; }
                        port = port * 10 + (*c - '0');
                        if (port > 65535) { port_valid = false; break; }
                    }
                    if (!port_valid) {
                        claw_respond_err("invalid port", 12);
                        return;
                    }
                    size_t klen = payload_len - (size_t)(p2 + 1 - payload);
                    if (klen >= sizeof(api_key)) klen = sizeof(api_key) - 1;
                    memcpy(api_key, p2 + 1, klen);
                    api_key[klen] = '\0';
                }
            }

            if (host[0] && api_key[0]) {
                claw_trust_set(CLAW_TRUST_HTTPS);
                claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Connecting...", 13);
                bool ok = claw_https_connect(host, port, api_key);
                if (ok) {
                    claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Online (HTTPS)", 14);
                    int n = snprintf(s_work_small, sizeof(s_work_small),
                                     "%s:%u", host, port);
                    claw_respond(TACP_CLAW_CONNECT,
                                 s_work_small, (uint16_t)n);
                } else {
                    claw_trust_set(CLAW_TRUST_USB);
                    claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Connect failed", 14);
                    claw_respond_err("connect failed", 14);
                }
            } else {
                claw_respond_err("format: host:port:api_key", 25);
            }
        }
        claw_ipc_send_stats();
        break;

    case TACP_CLAW_DISCONNECT:
        claw_https_disconnect();
        claw_trust_set(CLAW_TRUST_USB);
        claw_ipc_send_ui(CLAW_UI_STATUS_TEXT, "Offline", 7);
        claw_respond(TACP_CLAW_DISCONNECT, "ok", 2);
        claw_ipc_send_stats();
        break;

    default:
        claw_respond_err("unknown sub_cmd", 15);
        break;
    }
}

/*******************************************************************************
 * Module Definition
 ******************************************************************************/
static const mp_rom_map_elem_t bentoclaw_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),       MP_ROM_QSTR(MP_QSTR_bentoclaw) },
    /* Phase 1: Tool surface */
    { MP_ROM_QSTR(MP_QSTR_tools),          MP_ROM_PTR(&bentoclaw_tools_obj) },
    { MP_ROM_QSTR(MP_QSTR_execute),        MP_ROM_PTR(&bentoclaw_execute_obj) },
    { MP_ROM_QSTR(MP_QSTR_status),         MP_ROM_PTR(&bentoclaw_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_version),        MP_ROM_PTR(&bentoclaw_version_obj) },
    /* Phase 2: HTTPS transport */
    { MP_ROM_QSTR(MP_QSTR_connect),        MP_ROM_PTR(&bentoclaw_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_ask),            MP_ROM_PTR(&bentoclaw_ask_obj) },
    { MP_ROM_QSTR(MP_QSTR_disconnect),     MP_ROM_PTR(&bentoclaw_disconnect_obj) },
    /* Phase 3: Session memory */
    { MP_ROM_QSTR(MP_QSTR_remember),       MP_ROM_PTR(&bentoclaw_remember_obj) },
    { MP_ROM_QSTR(MP_QSTR_history),        MP_ROM_PTR(&bentoclaw_history_obj) },
    { MP_ROM_QSTR(MP_QSTR_session_clear),  MP_ROM_PTR(&bentoclaw_session_clear_obj) },
    /* Phase 5: Safety */
    { MP_ROM_QSTR(MP_QSTR_safety),         MP_ROM_PTR(&bentoclaw_safety_obj) },
};
static MP_DEFINE_CONST_DICT(bentoclaw_module_globals, bentoclaw_module_globals_table);

const mp_obj_module_t mp_module_bentoclaw = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&bentoclaw_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_bentoclaw, mp_module_bentoclaw);
