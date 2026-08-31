/*******************************************************************************
 * File Name: modsmartcard.c
 *
 * Description: MicroPython 'smartcard' module for CM33_NS.
 *              Reads Thai National ID Card via ACS ACR39U USB CCID reader.
 *              USB CCID driver runs on CM55 — accessed via IPC.
 *
 *              API:
 *                smartcard.init()         — Init CCID driver on CM55
 *                smartcard.connected()    — ACR39U reader detected?
 *                smartcard.card_present() — Card inserted in slot?
 *                smartcard.read()         — Read Thai ID → dict (compact)
 *                smartcard.read_field(n)  — Read single field (UTF-8)
 *                smartcard.info()         — Reader debug info
 *                smartcard.trigger_read() — Force card read on CM55
 *                smartcard.wait_card(ms)  — Block until card inserted
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "ipc_communication.h"
#include <string.h>

/*******************************************************************************
 * IPC Buffers (in shared memory, accessible by both cores)
 *******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t sc_ipc_msg;
CY_SECTION_SHAREDMEM static ipc_response_t sc_ipc_response;

/* IPC retry config (same as modjoystick.c) */
#define SC_IPC_SEND_RETRIES       (20)
#define SC_IPC_RETRY_DELAY_US     (100)
#define SC_IPC_RESPONSE_TIMEOUT_MS (2000)  /* Longer than joystick: card read takes time */

/* State */
static bool sc_ipc_initialized = false;

/*******************************************************************************
 * Lazy IPC pipe initialization
 *******************************************************************************/
static void ensure_sc_ipc(void) {
    if (!sc_ipc_initialized) {
        cm33_ipc_communication_setup();
        Cy_SysLib_Delay(50);
        sc_ipc_initialized = true;
    }
}

/*******************************************************************************
 * Send IPC request and wait for response from CM55
 * Returns true on success, raises OSError on failure.
 *******************************************************************************/
static bool sc_ipc_request(uint32_t cmd, uint32_t value) {
    ensure_sc_ipc();

    /* Prepare response buffer */
    memset((void *)&sc_ipc_response, 0, sizeof(sc_ipc_response));
    sc_ipc_response.ready = 0;

    /* Prepare request */
    memset(&sc_ipc_msg, 0, sizeof(sc_ipc_msg));
    sc_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    sc_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    sc_ipc_msg.cmd = cmd;
    sc_ipc_msg.value = (uint32_t)&sc_ipc_response;

    /* Store extra value in data[0..3] if needed (e.g., field ID) */
    if (cmd == IPC_CMD_SMARTCARD_READ) {
        sc_ipc_msg.data[0] = (char)(value & 0xFF);
    }

    /* Send with busy-wait retry */
    int retries = SC_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t status;
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&sc_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) break;
        Cy_SysLib_DelayUs(SC_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (retries <= 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("smartcard IPC send failed"));
        return false;
    }

    /* Wait for CM55 response — card reads can take several seconds */
    uint32_t timeout = (cmd == IPC_CMD_SMARTCARD_READ)
                       ? 5000 : SC_IPC_RESPONSE_TIMEOUT_MS;
    while (!sc_ipc_response.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }

    if (!sc_ipc_response.ready) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("smartcard IPC timeout"));
        return false;
    }

    return true;
}

/*******************************************************************************
 * Fetch compact smartcard state from CM55 via IPC
 *******************************************************************************/
static bool sc_fetch_state(ipc_smartcard_state_t *out) {
    if (!sc_ipc_request(IPC_CMD_SMARTCARD_STATE, 0)) {
        return false;
    }
    if (sc_ipc_response.data_len >= sizeof(ipc_smartcard_state_t)) {
        memcpy(out, sc_ipc_response.data, sizeof(ipc_smartcard_state_t));
    } else {
        memcpy(out, sc_ipc_response.data, sc_ipc_response.data_len);
    }
    return true;
}

/*******************************************************************************
 * smartcard.init() — Initialize USB Host CCID on CM55 via IPC
 *******************************************************************************/
static mp_obj_t smartcard_init(void) {
    sc_ipc_request(IPC_CMD_SMARTCARD_INIT, 0);
    mp_printf(&mp_plat_print, "smartcard: USB Host CCID initialized on CM55\n");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(smartcard_init_obj, smartcard_init);

/*******************************************************************************
 * smartcard.connected() — Check if ACR39U reader is connected
 *******************************************************************************/
static mp_obj_t smartcard_connected(void) {
    ipc_smartcard_state_t st;
    if (!sc_fetch_state(&st)) return mp_const_false;
    return mp_obj_new_bool(st.connected);
}
static MP_DEFINE_CONST_FUN_OBJ_0(smartcard_connected_obj, smartcard_connected);

/*******************************************************************************
 * smartcard.card_present() — Check if card is inserted in slot
 *******************************************************************************/
static mp_obj_t smartcard_card_present(void) {
    ipc_smartcard_state_t st;
    if (!sc_fetch_state(&st)) return mp_const_false;
    return mp_obj_new_bool(st.card_present);
}
static MP_DEFINE_CONST_FUN_OBJ_0(smartcard_card_present_obj, smartcard_card_present);

/*******************************************************************************
 * smartcard.read() — Read Thai ID card data (compact fields)
 *
 * Returns dict with:
 *   citizen_id  — 13-digit string
 *   name_en     — English name (ASCII)
 *   birthdate   — YYYYMMDD string
 *   issue_date  — YYYYMMDD string
 *   expire_date — YYYYMMDD string
 *   valid       — True if all fields read OK
 *******************************************************************************/
static mp_obj_t smartcard_read(void) {
    /* Trigger a card read on CM55, then fetch state */
    if (!sc_ipc_request(IPC_CMD_SMARTCARD_READ, 0)) {
        return mp_obj_new_dict(0);
    }

    /* Now fetch the updated state with card data */
    ipc_smartcard_state_t st;
    if (!sc_fetch_state(&st)) {
        return mp_obj_new_dict(0);
    }

    if (!st.card_read_ok) {
        mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(1));
        mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
                          MP_OBJ_NEW_QSTR(MP_QSTR_valid), mp_const_false);
        return MP_OBJ_FROM_PTR(d);
    }

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(6));

    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_citizen_id),
        mp_obj_new_str(st.citizen_id, strlen(st.citizen_id)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_name_en),
        mp_obj_new_str(st.name_en, strlen(st.name_en)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_birthdate),
        mp_obj_new_str(st.birthdate, strlen(st.birthdate)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_issue_date),
        mp_obj_new_str(st.issue_date, strlen(st.issue_date)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_expire_date),
        mp_obj_new_str(st.expire_date, strlen(st.expire_date)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_valid), mp_const_true);

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(smartcard_read_obj, smartcard_read);

/*******************************************************************************
 * smartcard.read_field(field_id) — Read a single field (UTF-8)
 *
 * Field IDs: 0=CID, 1=name_th, 2=name_en, 3=birthdate, 4=issue, 5=expire, 6=address
 * Returns the field value as a UTF-8 string.
 * name_th and address may contain Thai characters (multi-byte UTF-8).
 *******************************************************************************/
static mp_obj_t smartcard_read_field(mp_obj_t field_obj) {
    int field_id = mp_obj_get_int(field_obj);
    if (field_id < 0 || field_id > 6) {
        mp_raise_ValueError(MP_ERROR_TEXT("field must be 0-6"));
        return mp_const_none;
    }

    if (!sc_ipc_request(IPC_CMD_SMARTCARD_READ, (uint32_t)field_id)) {
        return mp_obj_new_str("", 0);
    }

    if (sc_ipc_response.status != 0) {
        return mp_obj_new_str("", 0);
    }

    /* Response data[] contains UTF-8 string */
    size_t len = sc_ipc_response.data_len;
    if (len > IPC_RESPONSE_DATA_MAX) len = IPC_RESPONSE_DATA_MAX;
    return mp_obj_new_str((const char *)sc_ipc_response.data, len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(smartcard_read_field_obj, smartcard_read_field);

/*******************************************************************************
 * smartcard.trigger_read() — Force card read on CM55 (non-blocking start)
 *******************************************************************************/
static mp_obj_t smartcard_trigger_read(void) {
    if (!sc_ipc_request(IPC_CMD_SMARTCARD_READ, 0)) {
        return mp_const_false;
    }
    return mp_obj_new_bool(sc_ipc_response.status == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(smartcard_trigger_read_obj, smartcard_trigger_read);

/*******************************************************************************
 * smartcard.info() — Reader debug info (VID, PID, init stage, ATR)
 *******************************************************************************/
static mp_obj_t smartcard_info(void) {
    ipc_smartcard_state_t st;
    if (!sc_fetch_state(&st)) {
        return mp_obj_new_dict(0);
    }

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(9));

    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_connected), mp_obj_new_bool(st.connected));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_card_present), mp_obj_new_bool(st.card_present));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_card_read_ok), mp_obj_new_bool(st.card_read_ok));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_reading), mp_obj_new_bool(st.reading));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_vid), mp_obj_new_int(st.vid));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_pid), mp_obj_new_int(st.pid));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_usb_init), mp_obj_new_bool(st.usb_init_done));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_init_stage), mp_obj_new_int(st.init_stage));

    /* ATR as hex string */
    if (st.atr_len > 0 && st.atr_len <= 33) {
        char atr_hex[67];  /* 33 * 2 + 1 */
        for (int i = 0; i < st.atr_len; i++) {
            static const char hex[] = "0123456789ABCDEF";
            atr_hex[i * 2]     = hex[(st.atr[i] >> 4) & 0x0F];
            atr_hex[i * 2 + 1] = hex[st.atr[i] & 0x0F];
        }
        atr_hex[st.atr_len * 2] = '\0';
        mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
            MP_OBJ_NEW_QSTR(MP_QSTR_atr),
            mp_obj_new_str(atr_hex, st.atr_len * 2));
    } else {
        mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
            MP_OBJ_NEW_QSTR(MP_QSTR_atr), mp_obj_new_str("", 0));
    }

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(smartcard_info_obj, smartcard_info);

/*******************************************************************************
 * smartcard.wait_card(timeout_ms=10000) — Block until card inserted
 *******************************************************************************/
static mp_obj_t smartcard_wait_card(size_t n_args, const mp_obj_t *args) {
    int timeout_ms = 10000;
    if (n_args > 0) {
        timeout_ms = mp_obj_get_int(args[0]);
        if (timeout_ms < 100) timeout_ms = 100;
        if (timeout_ms > 60000) timeout_ms = 60000;
    }

    mp_printf(&mp_plat_print, "smartcard: waiting for card...\n");

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        ipc_smartcard_state_t st;
        if (sc_fetch_state(&st) && st.card_present) {
            mp_printf(&mp_plat_print, "smartcard: card detected!\n");
            return mp_const_true;
        }
        Cy_SysLib_Delay(200);
        elapsed += 200;
    }

    mp_printf(&mp_plat_print, "smartcard: timeout, no card detected\n");
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(smartcard_wait_card_obj, 0, 1, smartcard_wait_card);

/*******************************************************************************
 * smartcard.wait_reader(timeout_ms=10000) — Block until reader connected
 *******************************************************************************/
static mp_obj_t smartcard_wait_reader(size_t n_args, const mp_obj_t *args) {
    int timeout_ms = 10000;
    if (n_args > 0) {
        timeout_ms = mp_obj_get_int(args[0]);
        if (timeout_ms < 100) timeout_ms = 100;
        if (timeout_ms > 60000) timeout_ms = 60000;
    }

    mp_printf(&mp_plat_print, "smartcard: waiting for reader...\n");

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        ipc_smartcard_state_t st;
        if (sc_fetch_state(&st) && st.connected) {
            mp_printf(&mp_plat_print, "smartcard: reader connected! (VID=%04X PID=%04X)\n",
                      st.vid, st.pid);
            return mp_const_true;
        }
        Cy_SysLib_Delay(200);
        elapsed += 200;
    }

    mp_printf(&mp_plat_print, "smartcard: timeout, no reader detected\n");
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(smartcard_wait_reader_obj, 0, 1, smartcard_wait_reader);

/*******************************************************************************
 * Field ID constants (for read_field)
 *******************************************************************************/
#define SC_FIELD_CID       (0)
#define SC_FIELD_NAME_TH   (1)
#define SC_FIELD_NAME_EN   (2)
#define SC_FIELD_BIRTHDATE (3)
#define SC_FIELD_ISSUE     (4)
#define SC_FIELD_EXPIRE    (5)
#define SC_FIELD_ADDRESS   (6)

/*******************************************************************************
 * Module Definition
 *******************************************************************************/
static const mp_rom_map_elem_t smartcard_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_smartcard) },
    /* Core */
    { MP_ROM_QSTR(MP_QSTR_init),           MP_ROM_PTR(&smartcard_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_connected),      MP_ROM_PTR(&smartcard_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_card_present),   MP_ROM_PTR(&smartcard_card_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_info),           MP_ROM_PTR(&smartcard_info_obj) },
    /* Read */
    { MP_ROM_QSTR(MP_QSTR_read),           MP_ROM_PTR(&smartcard_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_field),     MP_ROM_PTR(&smartcard_read_field_obj) },
    { MP_ROM_QSTR(MP_QSTR_trigger_read),   MP_ROM_PTR(&smartcard_trigger_read_obj) },
    /* Wait */
    { MP_ROM_QSTR(MP_QSTR_wait_card),      MP_ROM_PTR(&smartcard_wait_card_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait_reader),    MP_ROM_PTR(&smartcard_wait_reader_obj) },
    /* Field ID constants */
    { MP_ROM_QSTR(MP_QSTR_FIELD_CID),       MP_ROM_INT(SC_FIELD_CID) },
    { MP_ROM_QSTR(MP_QSTR_FIELD_NAME_TH),   MP_ROM_INT(SC_FIELD_NAME_TH) },
    { MP_ROM_QSTR(MP_QSTR_FIELD_NAME_EN),   MP_ROM_INT(SC_FIELD_NAME_EN) },
    { MP_ROM_QSTR(MP_QSTR_FIELD_BIRTHDATE), MP_ROM_INT(SC_FIELD_BIRTHDATE) },
    { MP_ROM_QSTR(MP_QSTR_FIELD_ISSUE),     MP_ROM_INT(SC_FIELD_ISSUE) },
    { MP_ROM_QSTR(MP_QSTR_FIELD_EXPIRE),    MP_ROM_INT(SC_FIELD_EXPIRE) },
    { MP_ROM_QSTR(MP_QSTR_FIELD_ADDRESS),   MP_ROM_INT(SC_FIELD_ADDRESS) },
};
static MP_DEFINE_CONST_DICT(smartcard_module_globals, smartcard_module_globals_table);

const mp_obj_module_t mp_module_smartcard = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&smartcard_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_smartcard, mp_module_smartcard);
