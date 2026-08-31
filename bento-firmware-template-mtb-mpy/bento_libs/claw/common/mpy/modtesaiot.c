/*******************************************************************************
 * File Name: modtesaiot.c
 *
 * Description: MicroPython 'tesaiot' module for CM33_NS.
 *              Proxies OPTIGA Trust M operations to CM55 via IPC.
 *
 *              Credential storage (14 slots, slot 4 reserved):
 *              - Slots 0-3, 5-11: Type 3 data objects (max 140 bytes)
 *              - Slots 12-13: Type 2 data objects (max 1500 bytes)
 *              - Slot 4: RESERVED for Protected Update (OID 0xF1D4)
 *
 *              Crypto operations (hardware-accelerated via OPTIGA Trust M):
 *              - SHA-256 hash, HMAC-SHA256, AES-CBC encrypt/decrypt
 *              - ECDSA P-256 signing, TRNG random generation
 *              - Monotonic counters (anti-replay)
 *              - Device health check
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "ipc_communication.h"
#include <string.h>

/* IPC message buffer in shared memory */
CY_SECTION_SHAREDMEM static ipc_msg_t tesaiot_ipc_msg;

/* IPC response buffer in shared memory */
CY_SECTION_SHAREDMEM static ipc_response_t tesaiot_ipc_response;

/* IPC config */
#define TESAIOT_IPC_SEND_RETRIES    (50)
#define TESAIOT_IPC_RETRY_DELAY_US  (100)
#define TESAIOT_IPC_RESPONSE_TIMEOUT_MS (10000)  /* 10s for OPTIGA ops */

static bool tesaiot_ipc_initialized = false;

/*******************************************************************************
 * Helper: Init IPC
 *******************************************************************************/
static void tesaiot_ipc_init(void) {
    if (tesaiot_ipc_initialized) return;
    cm33_ipc_communication_setup();
    Cy_SysLib_Delay(50);
    tesaiot_ipc_initialized = true;
}

/*******************************************************************************
 * Helper: Send IPC command and wait for response
 * data_len: explicit payload length (0 for no payload)
 *******************************************************************************/
static bool tesaiot_ipc_send(uint32_t cmd, const void *data, size_t data_len) {
    cy_en_ipc_pipe_status_t status;
    int retries = TESAIOT_IPC_SEND_RETRIES;

    memset((void *)&tesaiot_ipc_response, 0, sizeof(tesaiot_ipc_response));
    tesaiot_ipc_response.ready = 0;

    memset(&tesaiot_ipc_msg, 0, sizeof(tesaiot_ipc_msg));
    tesaiot_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    tesaiot_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    tesaiot_ipc_msg.cmd = cmd;
    tesaiot_ipc_msg.value = (uint32_t)&tesaiot_ipc_response;

    if (data && data_len > 0) {
        size_t copy = (data_len > IPC_DATA_MAX_LEN) ? IPC_DATA_MAX_LEN : data_len;
        memcpy(tesaiot_ipc_msg.data, data, copy);
    }

    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&tesaiot_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) break;
        Cy_SysLib_DelayUs(TESAIOT_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (retries <= 0) return false;

    uint32_t timeout = TESAIOT_IPC_RESPONSE_TIMEOUT_MS;
    while (!tesaiot_ipc_response.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }

    return (tesaiot_ipc_response.ready && tesaiot_ipc_response.status == 0);
}

/*******************************************************************************
 * Slot name resolution
 *
 * Named slots:
 *   "device_id"(0) "license"(1) "mqtt_user"(2) "mqtt_pass"(3)
 *   "wifi_ssid"(5) "wifi_pass"(6) "api_key"(7)
 *   "user0"(8) "user1"(9) "user2"(10) "user3"(11)
 *   "large0"(12) "large1"(13)
 *
 * Slot 4 is RESERVED (Protected Update OID 0xF1D4) — always rejected.
 *******************************************************************************/
typedef struct {
    const char *name;
    uint8_t slot;
} slot_entry_t;

static const slot_entry_t slot_table[] = {
    { "device_id",  TESAIOT_SLOT_DEVICE_ID },
    { "license",    TESAIOT_SLOT_LICENSE_KEY },
    { "mqtt_user",  TESAIOT_SLOT_MQTT_USER },
    { "mqtt_pass",  TESAIOT_SLOT_MQTT_PASS },
    { "wifi_ssid",  TESAIOT_SLOT_WIFI_SSID },
    { "wifi_pass",  TESAIOT_SLOT_WIFI_PASS },
    { "api_key",    TESAIOT_SLOT_API_KEY },
    { "user0",      TESAIOT_SLOT_USER0 },
    { "user1",      TESAIOT_SLOT_USER1 },
    { "user2",      TESAIOT_SLOT_USER2 },
    { "user3",      TESAIOT_SLOT_USER3 },
    { "large0",     TESAIOT_SLOT_LARGE0 },
    { "large1",     TESAIOT_SLOT_LARGE1 },
};
#define SLOT_TABLE_LEN (sizeof(slot_table) / sizeof(slot_table[0]))

static int resolve_slot(mp_obj_t slot_obj) {
    if (mp_obj_is_int(slot_obj)) {
        int slot = mp_obj_get_int(slot_obj);
        if (slot == TESAIOT_SLOT_RESERVED) {
            mp_raise_ValueError(MP_ERROR_TEXT("slot 4 reserved (Protected Update)"));
        }
        if (slot < 0 || slot >= TESAIOT_SLOT_COUNT) {
            mp_raise_ValueError(MP_ERROR_TEXT("slot must be 0-13 (except 4)"));
        }
        return slot;
    }

    size_t len;
    const char *name = mp_obj_str_get_data(slot_obj, &len);

    for (size_t i = 0; i < SLOT_TABLE_LEN; i++) {
        if (strlen(slot_table[i].name) == len &&
            strncmp(name, slot_table[i].name, len) == 0) {
            return slot_table[i].slot;
        }
    }

    mp_raise_ValueError(MP_ERROR_TEXT("unknown slot name"));
    return -1;
}

/*******************************************************************************
 * tesaiot.init() → bool
 *******************************************************************************/
static mp_obj_t tesaiot_init_func(void) {
    tesaiot_ipc_init();
    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_INIT, NULL, 0);
    if (ok) {
        mp_printf(&mp_plat_print, "tesaiot: OPTIGA Trust M initialized OK\n");
    } else {
        mp_printf(&mp_plat_print, "tesaiot: OPTIGA Trust M init FAILED\n");
    }
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_init_obj, tesaiot_init_func);

/*******************************************************************************
 * tesaiot.device_id() → bytes (27-byte factory UID)
 *******************************************************************************/
static mp_obj_t tesaiot_device_id(void) {
    tesaiot_ipc_init();
    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_DEVICE_ID, NULL, 0);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Trust M device ID read failed"));
    }
    return mp_obj_new_bytes(tesaiot_ipc_response.data, tesaiot_ipc_response.data_len);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_device_id_obj, tesaiot_device_id);

/*******************************************************************************
 * tesaiot.license_verify() → bool
 *******************************************************************************/
static mp_obj_t tesaiot_license_verify(void) {
    tesaiot_ipc_init();
    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_LICENSE, NULL, 0);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_license_verify_obj, tesaiot_license_verify);

/*******************************************************************************
 * tesaiot.cred_read(slot) → bytes
 *******************************************************************************/
static mp_obj_t tesaiot_cred_read(mp_obj_t slot_obj) {
    tesaiot_ipc_init();

    int slot = resolve_slot(slot_obj);
    uint8_t slot_byte = (uint8_t)slot;

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_CRED_READ, &slot_byte, 1);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("credential read failed"));
    }

    return mp_obj_new_bytes(tesaiot_ipc_response.data, tesaiot_ipc_response.data_len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(tesaiot_cred_read_obj, tesaiot_cred_read);

/*******************************************************************************
 * tesaiot.cred_write(slot, data) → bool
 * IPC format: [slot(1), data_len(1), payload(N)]
 *******************************************************************************/
static mp_obj_t tesaiot_cred_write(mp_obj_t slot_obj, mp_obj_t data_obj) {
    tesaiot_ipc_init();

    int slot = resolve_slot(slot_obj);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);

    uint16_t max_sz = tesaiot_slot_max_size((uint8_t)slot);
    if (bufinfo.len > max_sz) {
        mp_raise_ValueError(MP_ERROR_TEXT("data exceeds slot capacity"));
    }

    /* IPC payload limit: 128 - 2 (header) = 126 bytes */
    if (bufinfo.len > IPC_DATA_MAX_LEN - 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("data exceeds IPC transfer limit (126 bytes)"));
    }

    /* Pack: [slot(1), len(1), data(N)] */
    uint8_t buf[IPC_DATA_MAX_LEN];
    buf[0] = (uint8_t)slot;
    buf[1] = (uint8_t)bufinfo.len;
    memcpy(buf + 2, bufinfo.buf, bufinfo.len);

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_CRED_WRITE, buf, 2 + bufinfo.len);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_2(tesaiot_cred_write_obj, tesaiot_cred_write);

/*******************************************************************************
 * tesaiot.cred_erase(slot) → bool
 *******************************************************************************/
static mp_obj_t tesaiot_cred_erase(mp_obj_t slot_obj) {
    tesaiot_ipc_init();

    int slot = resolve_slot(slot_obj);
    uint8_t slot_byte = (uint8_t)slot;

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_CRED_ERASE, &slot_byte, 1);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_1(tesaiot_cred_erase_obj, tesaiot_cred_erase);

/*******************************************************************************
 * tesaiot.random(n) → bytes  (TRNG, 8-240 bytes)
 *******************************************************************************/
static mp_obj_t tesaiot_random(mp_obj_t n_obj) {
    tesaiot_ipc_init();

    int n = mp_obj_get_int(n_obj);
    if (n < 8 || n > (int)IPC_RESPONSE_DATA_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("n must be 8-240"));
    }

    uint8_t nbuf = (uint8_t)n;
    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_RANDOM, &nbuf, 1);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("TRNG failed"));
    }

    return mp_obj_new_bytes(tesaiot_ipc_response.data, tesaiot_ipc_response.data_len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(tesaiot_random_obj, tesaiot_random);

/*******************************************************************************
 * tesaiot.hash(data) → bytes (32-byte SHA-256)
 *******************************************************************************/
static mp_obj_t tesaiot_hash(mp_obj_t data_obj) {
    tesaiot_ipc_init();

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);

    if (bufinfo.len > IPC_DATA_MAX_LEN - 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("data too large for single IPC (max 127)"));
    }

    /* Pack: [data_len(1), data(N)] */
    uint8_t buf[IPC_DATA_MAX_LEN];
    buf[0] = (uint8_t)bufinfo.len;
    memcpy(buf + 1, bufinfo.buf, bufinfo.len);

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_HASH, buf, 1 + bufinfo.len);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SHA-256 hash failed"));
    }

    return mp_obj_new_bytes(tesaiot_ipc_response.data, tesaiot_ipc_response.data_len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(tesaiot_hash_obj, tesaiot_hash);

/*******************************************************************************
 * tesaiot.hmac(secret_slot, data) → bytes (32-byte HMAC-SHA256)
 * Secret key must be pre-stored in the specified slot.
 *******************************************************************************/
static mp_obj_t tesaiot_hmac(mp_obj_t slot_obj, mp_obj_t data_obj) {
    tesaiot_ipc_init();

    int slot = resolve_slot(slot_obj);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);

    if (bufinfo.len > IPC_DATA_MAX_LEN - 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("data too large (max 126 bytes)"));
    }

    /* Pack: [slot(1), data_len(1), data(N)] */
    uint8_t buf[IPC_DATA_MAX_LEN];
    buf[0] = (uint8_t)slot;
    buf[1] = (uint8_t)bufinfo.len;
    memcpy(buf + 2, bufinfo.buf, bufinfo.len);

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_HMAC, buf, 2 + bufinfo.len);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("HMAC-SHA256 failed"));
    }

    return mp_obj_new_bytes(tesaiot_ipc_response.data, tesaiot_ipc_response.data_len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(tesaiot_hmac_obj, tesaiot_hmac);

/*******************************************************************************
 * tesaiot.aes_keygen(bits=256) → bool
 * Generate AES key inside OPTIGA (OID 0xE200, never leaves hardware)
 *******************************************************************************/
static mp_obj_t tesaiot_aes_keygen(size_t n_args, const mp_obj_t *args) {
    tesaiot_ipc_init();

    uint16_t bits = 256;
    if (n_args > 0) {
        bits = (uint16_t)mp_obj_get_int(args[0]);
    }

    if (bits != 128 && bits != 192 && bits != 256) {
        mp_raise_ValueError(MP_ERROR_TEXT("bits must be 128, 192, or 256"));
    }

    uint8_t buf[2] = { (uint8_t)(bits >> 8), (uint8_t)(bits & 0xFF) };
    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_AES_KEYGEN, buf, 2);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tesaiot_aes_keygen_obj, 0, 1, tesaiot_aes_keygen);

/*******************************************************************************
 * tesaiot.encrypt(plaintext, iv=None) → (ciphertext, iv)
 * AES-CBC with hardware key. PKCS7 padding applied automatically.
 * Must call aes_keygen() first.
 *******************************************************************************/
static mp_obj_t tesaiot_encrypt(size_t n_args, const mp_obj_t *args) {
    tesaiot_ipc_init();

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);

    /* PKCS7 padding: pad to 16-byte boundary */
    size_t pad_len = 16 - (bufinfo.len % 16);
    size_t padded_len = bufinfo.len + pad_len;

    /* IPC format: [padded_len(1), has_iv(1), iv(16 if has_iv), padded_plaintext(N)] */
    bool has_iv = (n_args > 1 && args[1] != mp_const_none);
    size_t header_len = 2 + (has_iv ? 16 : 0);

    if (padded_len + header_len > IPC_DATA_MAX_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("plaintext too large for IPC"));
    }

    uint8_t buf[IPC_DATA_MAX_LEN];
    size_t pos = 0;

    buf[pos++] = (uint8_t)padded_len;
    buf[pos++] = has_iv ? 1 : 0;

    if (has_iv) {
        mp_buffer_info_t iv_info;
        mp_get_buffer_raise(args[1], &iv_info, MP_BUFFER_READ);
        if (iv_info.len != 16) {
            mp_raise_ValueError(MP_ERROR_TEXT("IV must be 16 bytes"));
        }
        memcpy(buf + pos, iv_info.buf, 16);
        pos += 16;
    }

    /* Copy plaintext + PKCS7 padding */
    memcpy(buf + pos, bufinfo.buf, bufinfo.len);
    memset(buf + pos + bufinfo.len, (uint8_t)pad_len, pad_len);
    pos += padded_len;

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_AES_ENC, buf, pos);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("AES encrypt failed"));
    }

    /* Response: [iv(16), ciphertext(N)] */
    if (tesaiot_ipc_response.data_len < 16) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("AES encrypt: invalid response"));
    }

    mp_obj_t iv_out = mp_obj_new_bytes(tesaiot_ipc_response.data, 16);
    mp_obj_t ct_out = mp_obj_new_bytes(
        tesaiot_ipc_response.data + 16,
        tesaiot_ipc_response.data_len - 16);

    mp_obj_t tuple[2] = { ct_out, iv_out };
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tesaiot_encrypt_obj, 1, 2, tesaiot_encrypt);

/*******************************************************************************
 * tesaiot.decrypt(ciphertext, iv) → plaintext
 * AES-CBC with hardware key. PKCS7 unpadding applied automatically.
 *******************************************************************************/
static mp_obj_t tesaiot_decrypt(mp_obj_t ct_obj, mp_obj_t iv_obj) {
    tesaiot_ipc_init();

    mp_buffer_info_t ct_info, iv_info;
    mp_get_buffer_raise(ct_obj, &ct_info, MP_BUFFER_READ);
    mp_get_buffer_raise(iv_obj, &iv_info, MP_BUFFER_READ);

    if (iv_info.len != 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("IV must be 16 bytes"));
    }
    if (ct_info.len == 0 || ct_info.len % 16 != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("ciphertext must be multiple of 16"));
    }

    /* IPC format: [cipher_len(1), iv(16), ciphertext(N)] */
    if (1 + 16 + ct_info.len > IPC_DATA_MAX_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("ciphertext too large for IPC"));
    }

    uint8_t buf[IPC_DATA_MAX_LEN];
    buf[0] = (uint8_t)ct_info.len;
    memcpy(buf + 1, iv_info.buf, 16);
    memcpy(buf + 17, ct_info.buf, ct_info.len);

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_AES_DEC, buf, 1 + 16 + ct_info.len);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("AES decrypt failed"));
    }

    /* Remove PKCS7 padding */
    uint16_t plain_len = tesaiot_ipc_response.data_len;
    if (plain_len > 0) {
        uint8_t pad_val = tesaiot_ipc_response.data[plain_len - 1];
        if (pad_val >= 1 && pad_val <= 16 && pad_val <= plain_len) {
            plain_len -= pad_val;
        }
    }

    return mp_obj_new_bytes(tesaiot_ipc_response.data, plain_len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(tesaiot_decrypt_obj, tesaiot_decrypt);

/*******************************************************************************
 * tesaiot.sign(data, key_oid=0xE0F1) → bytes (DER ECDSA signature)
 *******************************************************************************/
static mp_obj_t tesaiot_sign(size_t n_args, const mp_obj_t *args) {
    tesaiot_ipc_init();

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);

    uint16_t key_oid = 0xE0F1;  /* Device key by default */
    if (n_args > 1) {
        key_oid = (uint16_t)mp_obj_get_int(args[1]);
    }

    /* IPC format: [key_oid_hi(1), key_oid_lo(1), data_len(1), data(N)] */
    if (bufinfo.len > IPC_DATA_MAX_LEN - 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("data too large for IPC"));
    }

    uint8_t buf[IPC_DATA_MAX_LEN];
    buf[0] = (uint8_t)(key_oid >> 8);
    buf[1] = (uint8_t)(key_oid & 0xFF);
    buf[2] = (uint8_t)bufinfo.len;
    memcpy(buf + 3, bufinfo.buf, bufinfo.len);

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_SIGN, buf, 3 + bufinfo.len);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("ECDSA sign failed"));
    }

    return mp_obj_new_bytes(tesaiot_ipc_response.data, tesaiot_ipc_response.data_len);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tesaiot_sign_obj, 1, 2, tesaiot_sign);

/*******************************************************************************
 * tesaiot.counter_read(id) → int  (monotonic counter 0-3)
 *******************************************************************************/
static mp_obj_t tesaiot_counter_read(mp_obj_t id_obj) {
    tesaiot_ipc_init();

    int id = mp_obj_get_int(id_obj);
    if (id < 0 || id > 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("counter id must be 0-3"));
    }

    uint8_t id_byte = (uint8_t)id;
    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_COUNTER_RD, &id_byte, 1);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("counter read failed"));
    }

    /* Response: 4 bytes big-endian uint32 */
    uint32_t val = ((uint32_t)tesaiot_ipc_response.data[0] << 24) |
                   ((uint32_t)tesaiot_ipc_response.data[1] << 16) |
                   ((uint32_t)tesaiot_ipc_response.data[2] << 8)  |
                   ((uint32_t)tesaiot_ipc_response.data[3]);
    return mp_obj_new_int_from_uint(val);
}
static MP_DEFINE_CONST_FUN_OBJ_1(tesaiot_counter_read_obj, tesaiot_counter_read);

/*******************************************************************************
 * tesaiot.counter_inc(id, step=1) → bool
 *******************************************************************************/
static mp_obj_t tesaiot_counter_inc(size_t n_args, const mp_obj_t *args) {
    tesaiot_ipc_init();

    int id = mp_obj_get_int(args[0]);
    if (id < 0 || id > 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("counter id must be 0-3"));
    }

    uint32_t step = 1;
    if (n_args > 1) {
        step = (uint32_t)mp_obj_get_int(args[1]);
    }

    /* Pack: [counter_id(1), step(4) big-endian] */
    uint8_t buf[5];
    buf[0] = (uint8_t)id;
    buf[1] = (uint8_t)(step >> 24);
    buf[2] = (uint8_t)(step >> 16);
    buf[3] = (uint8_t)(step >> 8);
    buf[4] = (uint8_t)(step);

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_COUNTER_INC, buf, 5);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tesaiot_counter_inc_obj, 1, 2, tesaiot_counter_inc);

/*******************************************************************************
 * tesaiot.health() → dict
 *******************************************************************************/
static mp_obj_t tesaiot_health(void) {
    tesaiot_ipc_init();

    bool ok = tesaiot_ipc_send(IPC_CMD_TESAIOT_HEALTH, NULL, 0);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("health check failed"));
    }

    /* Response: [optiga_ok(1), factory_cert_ok(1), device_cert_ok(1),
     *            license_ok(1), lcso(1)] = 5 bytes */
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(5));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("optiga", 6),
        mp_obj_new_bool(tesaiot_ipc_response.data[0]));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("factory_cert", 12),
        mp_obj_new_bool(tesaiot_ipc_response.data[1]));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("device_cert", 11),
        mp_obj_new_bool(tesaiot_ipc_response.data[2]));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("license", 7),
        mp_obj_new_bool(tesaiot_ipc_response.data[3]));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("lcso", 4),
        mp_obj_new_int(tesaiot_ipc_response.data[4]));

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_health_obj, tesaiot_health);

/*******************************************************************************
 * tesaiot.slots() → dict (slot name → number mapping)
 *******************************************************************************/
static mp_obj_t tesaiot_slots(void) {
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(SLOT_TABLE_LEN));
    for (size_t i = 0; i < SLOT_TABLE_LEN; i++) {
        mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
            mp_obj_new_str(slot_table[i].name, strlen(slot_table[i].name)),
            mp_obj_new_int(slot_table[i].slot));
    }
    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_slots_obj, tesaiot_slots);

/*******************************************************************************
 * Config Store API — runtime-editable .tesaiot_config on LittleFS
 *******************************************************************************/
#include "tesaiot_config_store.h"

/* tesaiot.config() -> dict */
static mp_obj_t tesaiot_config_py(void)
{
    tesaiot_config_t cfg;
    tesaiot_config_get(&cfg);

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(24));

    #define SET_STR(k, v) mp_obj_dict_store(MP_OBJ_FROM_PTR(d), \
        MP_OBJ_NEW_QSTR(MP_QSTR_##k), mp_obj_new_str(v, strlen(v)))
    #define SET_INT(k, v) mp_obj_dict_store(MP_OBJ_FROM_PTR(d), \
        MP_OBJ_NEW_QSTR(MP_QSTR_##k), mp_obj_new_int(v))

    SET_STR(tls_mode, tesaiot_mode_name(cfg.tls_mode));
    SET_STR(device_id, cfg.device_id);
    SET_STR(factory_uid, cfg.factory_uid);
    SET_STR(api_key, cfg.api_key);
    SET_STR(broker, cfg.broker);
    SET_INT(port, cfg.port);
    SET_STR(sni_hostname, cfg.sni_hostname);
    SET_INT(qos, cfg.qos);
    SET_INT(keepalive, cfg.keepalive);
    SET_INT(timeout_ms, cfg.timeout_ms);
    SET_INT(max_retries, cfg.max_retries);
    SET_INT(retry_interval_ms, cfg.retry_interval_ms);
    SET_STR(api_host, cfg.api_host);
    SET_INT(api_port, cfg.api_port);
    SET_STR(api_endpoint, cfg.api_endpoint);
    SET_STR(wifi_ssid, cfg.wifi_ssid);
    SET_STR(sntp_server, cfg.sntp_server);
    SET_INT(sntp_timezone, cfg.sntp_timezone);
    SET_INT(debug_level, cfg.debug_level);

    #undef SET_STR
    #undef SET_INT

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_config_py_obj, tesaiot_config_py);

/* tesaiot.config_set(key, value) -> True/False */
static mp_obj_t tesaiot_config_set_py(mp_obj_t key_obj, mp_obj_t val_obj)
{
    const char *key = mp_obj_str_get_str(key_obj);
    const char *val = mp_obj_str_get_str(val_obj);
    bool ok = tesaiot_config_set_field(key, val);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_2(tesaiot_config_set_py_obj, tesaiot_config_set_py);

/* tesaiot.config_reset() -> None */
static mp_obj_t tesaiot_config_reset_py(void)
{
    tesaiot_config_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_config_reset_py_obj, tesaiot_config_reset_py);

/* tesaiot.config_reload() -> True/False */
static mp_obj_t tesaiot_config_reload_py(void)
{
    bool ok = tesaiot_config_load();
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_config_reload_py_obj, tesaiot_config_reload_py);

/*******************************************************************************
 * MQTT Connectivity — lazy connect/disconnect via tesaiot_mqtt.c
 *******************************************************************************/

extern bool tesaiot_mqtt_connect(void);
extern bool tesaiot_mqtt_disconnect(void);
extern bool tesaiot_mqtt_is_connected(void);
extern bool tesaiot_mqtt_publish(const char *topic, const char *payload, size_t payload_len);

/*******************************************************************************
 * HTTPS Telemetry — cy_secure_sockets TLS POST
 ******************************************************************************/
typedef struct {
    int     status_code;
    char    body[256];
    size_t  body_len;
} tesaiot_https_response_t;
extern bool tesaiot_https_post(const char *payload, size_t payload_len,
                               tesaiot_https_response_t *response);

/* tesaiot.connect() -> True/False (lazy MQTT task creation + broker connect) */
static mp_obj_t tesaiot_connect_py(void)
{
    bool ok = tesaiot_mqtt_connect();
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_connect_py_obj, tesaiot_connect_py);

/* tesaiot.disconnect() -> True/False */
static mp_obj_t tesaiot_disconnect_py(void)
{
    bool ok = tesaiot_mqtt_disconnect();
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_disconnect_py_obj, tesaiot_disconnect_py);

/* tesaiot.protected_update(target_oid="E0E1", trust_anchor_oid="E0E8", version=1)
 *
 * Asks the platform for a Protected Update bundle over the live MQTT session.
 *
 * This exists rather than letting callers hand-build the JSON, because the
 * request is what arms the reply. tesaiot_publish_protected_update() generates
 * a correlation id and stores it where the bundle ingest reads it, so a stale
 * bundle — the platform re-delivers one on connect — is recognised and
 * discarded instead of applied. Hand-built JSON leaves that check reading a
 * NULL expected id and silently accepting whatever arrives. Observed
 * 2026-08-06: a bundle from the previous request arrived 27 seconds before the
 * request that was supposed to produce it.
 *
 * csr=True generates a fresh key pair in the slot that pairs with the target
 * and sends a CSR with the request, so the certificate comes back bound to a
 * key this chip holds. Without it the platform issues against a key pair it
 * makes in memory and throws away — Protected Update then reports success at
 * every layer and installs a certificate nobody can prove possession of, which
 * is what happened on 2026-08-07. It is destructive by necessity: the chip
 * cannot hand out the public half of a key it already has.
 *
 * Returns True when the request was published.
 */
//! [hsm_publish_pu_mpy_binding]
extern int tesaiot_publish_protected_update(const char *target_oid,
                                            const char *trust_anchor_oid,
                                            uint32_t payload_version,
                                            bool with_csr)
    __attribute__((weak));

static mp_obj_t tesaiot_protected_update_py(size_t n_args, const mp_obj_t *args,
                                            mp_map_t *kw_args)
{
    if (tesaiot_publish_protected_update == NULL) {
        mp_raise_msg(&mp_type_OSError,
                     MP_ERROR_TEXT("Protected Update not built in "
                                   "(needs ENABLE_OPTIGA_CLM=1)"));
    }

    enum { ARG_target, ARG_anchor, ARG_version, ARG_csr };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_target,  MP_ARG_OBJ,  {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_anchor,  MP_ARG_OBJ,  {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_version, MP_ARG_INT,  {.u_int = 1} },
        { MP_QSTR_csr,     MP_ARG_BOOL, {.u_bool = false} },
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, args, kw_args, MP_ARRAY_SIZE(allowed), allowed, vals);

    const char *target = (vals[ARG_target].u_obj != mp_const_none)
                       ? mp_obj_str_get_str(vals[ARG_target].u_obj) : "E0E1";
    const char *anchor = (vals[ARG_anchor].u_obj != mp_const_none)
                       ? mp_obj_str_get_str(vals[ARG_anchor].u_obj) : "E0E8";

    int rc = tesaiot_publish_protected_update(target, anchor,
                                              (uint32_t)vals[ARG_version].u_int,
                                              vals[ARG_csr].u_bool);
    return mp_obj_new_bool(rc == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(tesaiot_protected_update_py_obj, 0,
                                  tesaiot_protected_update_py);
                                  //! [hsm_publish_pu_mpy_binding]

/* tesaiot.publish(payload, topic=None) -> True/False
 * payload: str or bytes — JSON telemetry data
 * topic:   str or None  — MQTT topic (None = default telemetry topic)
 */
static mp_obj_t tesaiot_publish_py(size_t n_args, const mp_obj_t *args)
{
    /* First arg: payload (required) */
    mp_buffer_info_t payload_info;
    mp_get_buffer_raise(args[0], &payload_info, MP_BUFFER_READ);

    /* Second arg: topic (optional, default = NULL → auto telemetry topic) */
    const char *topic = NULL;
    if (n_args > 1 && args[1] != mp_const_none) {
        topic = mp_obj_str_get_str(args[1]);
    }

    bool ok = tesaiot_mqtt_publish(topic,
                                   (const char *)payload_info.buf,
                                   payload_info.len);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tesaiot_publish_py_obj, 1, 2, tesaiot_publish_py);

/* tesaiot.http_post(payload) -> dict {"status": int, "body": str}
 * HTTPS POST to TESAIoT API using config store credentials.
 * Auth mode determined by tls_mode (Bearer for mode 2, apikey for mode 3).
 */
static mp_obj_t tesaiot_http_post_py(mp_obj_t payload_obj)
{
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(payload_obj, &bufinfo, MP_BUFFER_READ);

    tesaiot_https_response_t resp;
    bool ok = tesaiot_https_post((const char *)bufinfo.buf, bufinfo.len, &resp);

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(3));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("status", 6),
        mp_obj_new_int(resp.status_code));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("body", 4),
        mp_obj_new_str(resp.body, resp.body_len));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        mp_obj_new_str("ok", 2),
        mp_obj_new_bool(ok));

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_1(tesaiot_http_post_py_obj, tesaiot_http_post_py);

/* tesaiot.is_connected() -> True/False */
static mp_obj_t tesaiot_mqtt_connected_py(void)
{
    bool ok = tesaiot_mqtt_is_connected();
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_0(tesaiot_mqtt_connected_py_obj, tesaiot_mqtt_connected_py);

/*******************************************************************************
 * Module Definition
 *******************************************************************************/
static const mp_rom_map_elem_t tesaiot_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),       MP_ROM_QSTR(MP_QSTR_tesaiot) },

    /* Core */
    { MP_ROM_QSTR(MP_QSTR_init),            MP_ROM_PTR(&tesaiot_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_device_id),       MP_ROM_PTR(&tesaiot_device_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_license_verify),  MP_ROM_PTR(&tesaiot_license_verify_obj) },
    { MP_ROM_QSTR(MP_QSTR_health),          MP_ROM_PTR(&tesaiot_health_obj) },

    /* Credential storage */
    { MP_ROM_QSTR(MP_QSTR_cred_read),       MP_ROM_PTR(&tesaiot_cred_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_cred_write),      MP_ROM_PTR(&tesaiot_cred_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_cred_erase),      MP_ROM_PTR(&tesaiot_cred_erase_obj) },
    { MP_ROM_QSTR(MP_QSTR_slots),           MP_ROM_PTR(&tesaiot_slots_obj) },

    /* Crypto */
    { MP_ROM_QSTR(MP_QSTR_random),          MP_ROM_PTR(&tesaiot_random_obj) },
    { MP_ROM_QSTR(MP_QSTR_hash),            MP_ROM_PTR(&tesaiot_hash_obj) },
    { MP_ROM_QSTR(MP_QSTR_hmac),            MP_ROM_PTR(&tesaiot_hmac_obj) },
    { MP_ROM_QSTR(MP_QSTR_aes_keygen),      MP_ROM_PTR(&tesaiot_aes_keygen_obj) },
    { MP_ROM_QSTR(MP_QSTR_encrypt),         MP_ROM_PTR(&tesaiot_encrypt_obj) },
    { MP_ROM_QSTR(MP_QSTR_decrypt),         MP_ROM_PTR(&tesaiot_decrypt_obj) },
    { MP_ROM_QSTR(MP_QSTR_sign),            MP_ROM_PTR(&tesaiot_sign_obj) },

    /* Counters */
    { MP_ROM_QSTR(MP_QSTR_counter_read),    MP_ROM_PTR(&tesaiot_counter_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_counter_inc),     MP_ROM_PTR(&tesaiot_counter_inc_obj) },

    /* Config store */
    { MP_ROM_QSTR(MP_QSTR_config),          MP_ROM_PTR(&tesaiot_config_py_obj) },
    { MP_ROM_QSTR(MP_QSTR_config_set),      MP_ROM_PTR(&tesaiot_config_set_py_obj) },
    { MP_ROM_QSTR(MP_QSTR_config_reset),    MP_ROM_PTR(&tesaiot_config_reset_py_obj) },
    { MP_ROM_QSTR(MP_QSTR_config_reload),   MP_ROM_PTR(&tesaiot_config_reload_py_obj) },

    /* MQTT connectivity */
    { MP_ROM_QSTR(MP_QSTR_connect),         MP_ROM_PTR(&tesaiot_connect_py_obj) },
    { MP_ROM_QSTR(MP_QSTR_disconnect),      MP_ROM_PTR(&tesaiot_disconnect_py_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_connected),    MP_ROM_PTR(&tesaiot_mqtt_connected_py_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish),         MP_ROM_PTR(&tesaiot_publish_py_obj) },
    { MP_ROM_QSTR(MP_QSTR_protected_update), MP_ROM_PTR(&tesaiot_protected_update_py_obj) },

    /* HTTPS telemetry */
    { MP_ROM_QSTR(MP_QSTR_http_post),      MP_ROM_PTR(&tesaiot_http_post_py_obj) },
};
static MP_DEFINE_CONST_DICT(tesaiot_module_globals, tesaiot_module_globals_table);

const mp_obj_module_t mp_module_tesaiot = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&tesaiot_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_tesaiot, mp_module_tesaiot);
