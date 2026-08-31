/*******************************************************************************
 * File: modfaceid.c
 *
 * Description:
 *   MicroPython 'faceid' module — Face ID engine control via IPC.
 *
 *   Runs on CM33_NS, sends IPC commands to CM55 face engine.
 *   Provides: detect(), recognize(), enroll(), stop(), clear(), status()
 *
 *   Usage from MicroPython:
 *     import faceid
 *     faceid.detect()        # Start detection only
 *     faceid.recognize()     # Start detection + recognition
 *     faceid.enroll("Alice") # Start 5-pose enrollment
 *     faceid.stop()          # Stop inference
 *     faceid.clear()         # Clear all enrolled users + QSPI
 *     s = faceid.status()    # {'mode':2, 'faces':1, 'users':3, 'max':500}
 *
 ******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "ipc_communication.h"
#include "Cy_IPC_Pipe.h"
#include "cy_syslib.h"
#include <string.h>

/*******************************************************************************
 * IPC Command IDs (must match face_id_ipc.h on CM55)
 ******************************************************************************/
#define FACE_IPC_CMD_START_DETECT       (0xE0U)
#define FACE_IPC_CMD_START_RECOGNIZE    (0xE1U)
#define FACE_IPC_CMD_ENROLL_START       (0xE2U)
#define FACE_IPC_CMD_ENROLL_ABORT       (0xE3U)
#define FACE_IPC_CMD_CLEAR_USERS        (0xE4U)
#define FACE_IPC_CMD_GET_STATUS         (0xE5U)
#define FACE_IPC_CMD_STOP               (0xE6U)
#define FACE_IPC_CMD_SWITCH_CAMERA      (0xE7U)
#define FACE_IPC_CMD_RESULT_PUSH        (0xE8U)
#define FACE_IPC_CMD_ENROLL_PROGRESS    (0xE9U)

/* Face ID IPC client ID (same routing as UI) */
#define FACE_IPC_CLIENT_ID              CM55_IPC_UI_CLIENT_ID

/*******************************************************************************
 * IPC Status Response (must match face_ipc_status_t on CM55)
 ******************************************************************************/
typedef struct __attribute__((packed)) {
    uint8_t  mode;
    uint8_t  face_count;
    uint16_t enrolled_users;
    uint16_t max_users;
    uint8_t  camera_source;
    uint8_t  camera_connected;
} face_ipc_status_t;

/*******************************************************************************
 * IPC Buffers (shared memory)
 ******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t s_face_ipc_msg;
CY_SECTION_SHAREDMEM static ipc_response_t s_face_ipc_resp;

static bool s_face_ipc_initialized = false;

#define FACE_IPC_SEND_RETRIES    (50)
#define FACE_IPC_RETRY_DELAY_US  (200)
#define FACE_IPC_RESP_TIMEOUT_MS (3000)

/*******************************************************************************
 * IPC Helpers
 ******************************************************************************/

extern void cm33_ipc_communication_setup(void);
extern void cm33_ipc_communication_recover(void);

static void face_ipc_setup_once(void)
{
    if (!s_face_ipc_initialized) {
        cm33_ipc_communication_setup();
        s_face_ipc_initialized = true;
    }
}

static bool face_ipc_send_fire_forget(uint32_t cmd, const void *data,
                                       size_t data_len)
{
    face_ipc_setup_once();

    cy_en_ipc_pipe_status_t status;
    int retries = FACE_IPC_SEND_RETRIES;

    memset(&s_face_ipc_msg, 0, sizeof(s_face_ipc_msg));
    s_face_ipc_msg.client_id = FACE_IPC_CLIENT_ID;
    s_face_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_face_ipc_msg.cmd = cmd;

    if (data && data_len > 0) {
        size_t copy = (data_len > IPC_DATA_MAX_LEN) ? IPC_DATA_MAX_LEN : data_len;
        memcpy(s_face_ipc_msg.data, data, copy);
    }

    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&s_face_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) return true;
        Cy_SysLib_DelayUs(FACE_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    return false;
}

static bool face_ipc_send_bidir(uint32_t cmd, const void *data, size_t data_len)
{
    face_ipc_setup_once();

    cy_en_ipc_pipe_status_t status;
    int retries = FACE_IPC_SEND_RETRIES;

    memset(&s_face_ipc_msg, 0, sizeof(s_face_ipc_msg));
    memset(&s_face_ipc_resp, 0, sizeof(s_face_ipc_resp));

    s_face_ipc_msg.client_id = FACE_IPC_CLIENT_ID;
    s_face_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    s_face_ipc_msg.cmd = cmd;
    s_face_ipc_msg.value = (uint32_t)&s_face_ipc_resp;

    s_face_ipc_resp.ready = 0;

    if (data && data_len > 0) {
        size_t copy = (data_len > IPC_DATA_MAX_LEN) ? IPC_DATA_MAX_LEN : data_len;
        memcpy(s_face_ipc_msg.data, data, copy);
    }

    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&s_face_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) break;
        Cy_SysLib_DelayUs(FACE_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (CY_IPC_PIPE_SUCCESS != status) return false;

    uint32_t timeout = FACE_IPC_RESP_TIMEOUT_MS * 10;
    while (!s_face_ipc_resp.ready && timeout > 0) {
        MICROPY_EVENT_POLL_HOOK;
        Cy_SysLib_DelayUs(100);
        timeout--;
    }

    return (s_face_ipc_resp.ready == 1);
}

/*******************************************************************************
 * MicroPython API
 ******************************************************************************/

/* faceid.detect() — Start face detection only */
static mp_obj_t faceid_detect(void)
{
    if (!face_ipc_send_fire_forget(FACE_IPC_CMD_START_DETECT, NULL, 0)) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("faceid: IPC send failed"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(faceid_detect_obj, faceid_detect);

/* faceid.recognize() — Start detection + recognition */
static mp_obj_t faceid_recognize(void)
{
    if (!face_ipc_send_fire_forget(FACE_IPC_CMD_START_RECOGNIZE, NULL, 0)) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("faceid: IPC send failed"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(faceid_recognize_obj, faceid_recognize);

/* faceid.enroll(name) — Start 5-pose enrollment */
static mp_obj_t faceid_enroll(mp_obj_t name_obj)
{
    const char *name = mp_obj_str_get_str(name_obj);
    size_t name_len = strlen(name);
    if (name_len > 20) name_len = 20;

    char buf[24];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, name, name_len);

    if (!face_ipc_send_fire_forget(FACE_IPC_CMD_ENROLL_START, buf, 24)) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("faceid: IPC send failed"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(faceid_enroll_obj, faceid_enroll);

/* faceid.enroll_abort() — Cancel ongoing enrollment */
static mp_obj_t faceid_enroll_abort(void)
{
    face_ipc_send_fire_forget(FACE_IPC_CMD_ENROLL_ABORT, NULL, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(faceid_enroll_abort_obj, faceid_enroll_abort);

/* faceid.stop() — Stop inference */
static mp_obj_t faceid_stop(void)
{
    face_ipc_send_fire_forget(FACE_IPC_CMD_STOP, NULL, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(faceid_stop_obj, faceid_stop);

/* faceid.clear() — Clear all enrolled users */
static mp_obj_t faceid_clear(void)
{
    if (!face_ipc_send_fire_forget(FACE_IPC_CMD_CLEAR_USERS, NULL, 0)) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("faceid: IPC send failed"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(faceid_clear_obj, faceid_clear);

/* faceid.status() — Get engine status dict */
static mp_obj_t faceid_status(void)
{
    if (!face_ipc_send_bidir(FACE_IPC_CMD_GET_STATUS, NULL, 0)) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("faceid: status query failed"));
    }

    if (s_face_ipc_resp.data_len < sizeof(face_ipc_status_t)) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("faceid: invalid status response"));
    }

    face_ipc_status_t *st = (face_ipc_status_t *)s_face_ipc_resp.data;

    mp_obj_dict_t *d = mp_obj_new_dict(6);
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_mode),
                      mp_obj_new_int(st->mode));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_faces),
                      mp_obj_new_int(st->face_count));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_users),
                      mp_obj_new_int(st->enrolled_users));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_max_users),
                      mp_obj_new_int(st->max_users));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_camera),
                      mp_obj_new_int(st->camera_source));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_connected),
                      mp_obj_new_bool(st->camera_connected));

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(faceid_status_obj, faceid_status);

/* faceid.switch_camera(source) — Switch camera (0=USB, 1=DVP) */
static mp_obj_t faceid_switch_camera(mp_obj_t source_obj)
{
    uint8_t source = (uint8_t)mp_obj_get_int(source_obj);
    if (source > 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("source must be 0 (USB) or 1 (DVP)"));
    }

    if (!face_ipc_send_fire_forget(FACE_IPC_CMD_SWITCH_CAMERA, &source, 1)) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("faceid: IPC send failed"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(faceid_switch_camera_obj, faceid_switch_camera);

/*******************************************************************************
 * Module Registration
 ******************************************************************************/

static const mp_rom_map_elem_t faceid_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),       MP_ROM_QSTR(MP_QSTR_faceid) },
    { MP_ROM_QSTR(MP_QSTR_detect),         MP_ROM_PTR(&faceid_detect_obj) },
    { MP_ROM_QSTR(MP_QSTR_recognize),      MP_ROM_PTR(&faceid_recognize_obj) },
    { MP_ROM_QSTR(MP_QSTR_enroll),         MP_ROM_PTR(&faceid_enroll_obj) },
    { MP_ROM_QSTR(MP_QSTR_enroll_abort),   MP_ROM_PTR(&faceid_enroll_abort_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),           MP_ROM_PTR(&faceid_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),          MP_ROM_PTR(&faceid_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_status),         MP_ROM_PTR(&faceid_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_switch_camera),  MP_ROM_PTR(&faceid_switch_camera_obj) },
    /* Mode constants */
    { MP_ROM_QSTR(MP_QSTR_MODE_IDLE),      MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_MODE_DETECT),    MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_MODE_RECOGNIZE), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_MODE_ENROLL),    MP_ROM_INT(3) },
    /* Camera source constants */
    { MP_ROM_QSTR(MP_QSTR_CAM_USB),        MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_CAM_DVP),        MP_ROM_INT(1) },
};
static MP_DEFINE_CONST_DICT(faceid_module_globals, faceid_module_globals_table);

const mp_obj_module_t mp_module_faceid = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&faceid_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_faceid, mp_module_faceid);
