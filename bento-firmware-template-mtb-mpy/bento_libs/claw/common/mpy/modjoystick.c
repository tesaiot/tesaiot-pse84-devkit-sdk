/*******************************************************************************
 * File Name: modjoystick.c
 *
 * Description: MicroPython 'joystick' module for CM33_NS.
 *              Reads Logitech F310 (DirectInput) HID reports via IPC from CM55.
 *
 *              5 Scenario modes:
 *              1. read()     — Raw button/axis state
 *              2. on_foot()  — On-foot character control
 *              3. vehicle()  — Land vehicle (steering/accel/brake/shift)
 *              4. plane()    — Aircraft (aileron/elevator/rudder/throttle)
 *              5. boat()     — Watercraft (steering/throttle)
 *
 *              All scenario functions share common IPC fetch logic.
 *              Scenario mapping is pure CM33-side code (no CM55 changes).
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "ipc_communication.h"
#include <string.h>
#include <math.h>

/*******************************************************************************
 * IPC Buffers (in shared memory, accessible by both cores)
 *******************************************************************************/
CY_SECTION_SHAREDMEM static ipc_msg_t js_ipc_msg;
CY_SECTION_SHAREDMEM ipc_response_t js_ipc_response;  /* non-static: shared with modbentoclaw.c */

/* IPC retry config */
#define JS_IPC_SEND_RETRIES       (20)
#define JS_IPC_RETRY_DELAY_US     (100)
#define JS_IPC_RESPONSE_TIMEOUT_MS (500)

/* State */
static bool js_ipc_initialized = false;
static int  js_deadzone = 10;  /* 0-50, applied to stick axes */

/*******************************************************************************
 * Lazy IPC pipe initialization
 *******************************************************************************/
static void ensure_js_ipc(void) {
    if (!js_ipc_initialized) {
        cm33_ipc_communication_setup();
        Cy_SysLib_Delay(50);
        js_ipc_initialized = true;
    }
}

/*******************************************************************************
 * Send IPC request and wait for response from CM55
 * Returns true on success, raises OSError on failure.
 *******************************************************************************/
static bool js_ipc_request(uint32_t cmd) {
    ensure_js_ipc();

    /* Prepare response buffer */
    memset((void *)&js_ipc_response, 0, sizeof(js_ipc_response));
    js_ipc_response.ready = 0;

    /* Prepare request */
    memset(&js_ipc_msg, 0, sizeof(js_ipc_msg));
    js_ipc_msg.client_id = CM55_IPC_SERVICE_CLIENT_ID;
    js_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;
    js_ipc_msg.cmd = cmd;
    js_ipc_msg.value = (uint32_t)&js_ipc_response;

    /* Send with busy-wait retry */
    int retries = JS_IPC_SEND_RETRIES;
    cy_en_ipc_pipe_status_t status;
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR, CM33_IPC_PIPE_EP_ADDR,
            (void *)&js_ipc_msg, NULL);
        if (CY_IPC_PIPE_SUCCESS == status) break;
        Cy_SysLib_DelayUs(JS_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    if (retries <= 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("joystick IPC send failed"));
        return false;
    }

    /* Wait for CM55 response */
    uint32_t timeout = JS_IPC_RESPONSE_TIMEOUT_MS;
    while (!js_ipc_response.ready && timeout > 0) {
        Cy_SysLib_Delay(1);
        timeout--;
    }

    if (!js_ipc_response.ready) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("joystick IPC timeout"));
        return false;
    }

    return true;
}

/*******************************************************************************
 * Fetch joystick state from CM55 via IPC
 *******************************************************************************/
static bool js_fetch_state(ipc_joystick_state_t *out) {
    if (!js_ipc_request(IPC_CMD_JOYSTICK_STATE)) {
        return false;
    }
    memcpy(out, js_ipc_response.data, sizeof(ipc_joystick_state_t));
    return true;
}

/*******************************************************************************
 * Stick axis normalization: raw (0-255) -> float (-1.0 to +1.0) with deadzone
 * center_val: 0x80 (128) for X-axes, 0x7F (127) for Y-axes
 *******************************************************************************/
static mp_obj_t normalize_axis(uint8_t raw, uint8_t center_val) {
    float centered = (float)raw - (float)center_val;
    float range = (centered >= 0) ? (255.0f - (float)center_val) : (float)center_val;
    float normalized = centered / range;

    /* Apply deadzone */
    float dz = (float)js_deadzone / 100.0f;
    if (normalized > -dz && normalized < dz) {
        normalized = 0.0f;
    }

    /* Clamp to -1.0 .. 1.0 */
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < -1.0f) normalized = -1.0f;

    return mp_obj_new_float((double)normalized);
}

/*******************************************************************************
 * Hat switch decode
 *******************************************************************************/
typedef struct {
    bool up, down, left, right;
} hat_state_t;

static hat_state_t decode_hat(uint8_t hat_val) {
    hat_state_t h = {false, false, false, false};
    switch (hat_val & 0x0F) {
    case 0: h.up = true; break;                          /* Up */
    case 1: h.up = true; h.right = true; break;          /* Up-Right */
    case 2: h.right = true; break;                       /* Right */
    case 3: h.down = true; h.right = true; break;        /* Down-Right */
    case 4: h.down = true; break;                        /* Down */
    case 5: h.down = true; h.left = true; break;         /* Down-Left */
    case 6: h.left = true; break;                        /* Left */
    case 7: h.up = true; h.left = true; break;           /* Up-Left */
    default: break;                                      /* Neutral (8+) */
    }
    return h;
}

/*******************************************************************************
 * joystick.init() — Initialize USB Host HID on CM55 via IPC
 *******************************************************************************/
static mp_obj_t joystick_init(void) {
    js_ipc_request(IPC_CMD_JOYSTICK_INIT);
    mp_printf(&mp_plat_print, "joystick: USB Host HID initialized on CM55\n");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_init_obj, joystick_init);

/*******************************************************************************
 * joystick.connected() — Check if gamepad is connected
 *******************************************************************************/
static mp_obj_t joystick_connected(void) {
    ipc_joystick_state_t st;
    if (!js_fetch_state(&st)) return mp_const_false;
    return mp_obj_new_bool(st.connected);
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_connected_obj, joystick_connected);

/*******************************************************************************
 * joystick.controller() — name of the connected gamepad ("F310"/"NJ43"/...)
 * Auto-detected by USB VID/PID on the firmware side. Returns "none" if nothing
 * is connected. Handy for showing the active controller on screen / in the REPL.
 *******************************************************************************/
static mp_obj_t joystick_controller(void) {
    ipc_joystick_state_t st;
    if (!js_fetch_state(&st) || !st.connected) {
        return mp_obj_new_str("none", 4);
    }
    const char *name;
    switch (st.source) {
        case IPC_JOY_SRC_F310: name = "F310";    break;
        case IPC_JOY_SRC_NJ43: name = "NJ43";    break;
        case IPC_JOY_SRC_XBOX: name = "Xbox";    break;
        default:               name = "unknown"; break;
    }
    return mp_obj_new_str(name, strlen(name));
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_controller_obj, joystick_controller);

/*******************************************************************************
 * joystick.read() — Scenario 1: Raw Reading
 *
 * Returns dict with all buttons and axes as raw values:
 *   left_x, left_y, right_x, right_y (0-255)
 *   a, b, x, y, lb, rb, lt, rt, back, start, l3, r3 (bool)
 *   dpad (0-8), dpad_up, dpad_down, dpad_left, dpad_right (bool)
 *******************************************************************************/
static mp_obj_t joystick_read(void) {
    ipc_joystick_state_t st;
    if (!js_fetch_state(&st)) {
        return mp_obj_new_dict(0);
    }

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(20));

    /* Stick axes (raw 0-255) */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_left_x),  mp_obj_new_int(st.left_x));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_left_y),  mp_obj_new_int(st.left_y));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_right_x), mp_obj_new_int(st.right_x));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_right_y), mp_obj_new_int(st.right_y));

    /* Face buttons (byte 4 bits 4-7) */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_x), mp_obj_new_bool(st.buttons1 & (1 << 4)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_a), mp_obj_new_bool(st.buttons1 & (1 << 5)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_b), mp_obj_new_bool(st.buttons1 & (1 << 6)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_y), mp_obj_new_bool(st.buttons1 & (1 << 7)));

    /* Shoulder/system buttons (byte 5) */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_lb),    mp_obj_new_bool(st.buttons2 & (1 << 0)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_rb),    mp_obj_new_bool(st.buttons2 & (1 << 1)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_lt),    mp_obj_new_bool(st.buttons2 & (1 << 2)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_rt),    mp_obj_new_bool(st.buttons2 & (1 << 3)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_back),  mp_obj_new_bool(st.buttons2 & (1 << 4)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_start), mp_obj_new_bool(st.buttons2 & (1 << 5)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_l3),    mp_obj_new_bool(st.buttons2 & (1 << 6)));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_r3),    mp_obj_new_bool(st.buttons2 & (1 << 7)));

    /* D-Pad / Hat switch */
    uint8_t hat = st.buttons1 & 0x0F;
    hat_state_t hs = decode_hat(hat);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_dpad),       mp_obj_new_int(hat));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_dpad_up),    mp_obj_new_bool(hs.up));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_dpad_down),  mp_obj_new_bool(hs.down));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_dpad_left),  mp_obj_new_bool(hs.left));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_dpad_right), mp_obj_new_bool(hs.right));

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_read_obj, joystick_read);

/*******************************************************************************
 * joystick.on_foot() — Scenario 2: On-Foot Character Control
 *
 * Mapping (from Rigs of Rods "On Foot" layout):
 *   walk_x/walk_y     — left stick (normalized -1.0 to 1.0)
 *   camera_x/camera_y — right stick (normalized -1.0 to 1.0)
 *   jump              — B (red)
 *   run               — A (green)
 *   enter_exit         — Y (yellow)
 *   change_camera     — Back
 *   pause             — Start
 *******************************************************************************/
static mp_obj_t joystick_on_foot(void) {
    ipc_joystick_state_t st;
    if (!js_fetch_state(&st)) {
        return mp_obj_new_dict(0);
    }

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(9));

    /* Walk control — left stick */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_walk_x),   normalize_axis(st.left_x, 0x80));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_walk_y),   normalize_axis(st.left_y, 0x7F));

    /* Camera — right stick */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_camera_x), normalize_axis(st.right_x, 0x80));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_camera_y), normalize_axis(st.right_y, 0x7F));

    /* Action buttons */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_jump),          mp_obj_new_bool(st.buttons1 & (1 << 6)));  /* B */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_run),           mp_obj_new_bool(st.buttons1 & (1 << 5)));  /* A */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_enter_exit),    mp_obj_new_bool(st.buttons1 & (1 << 7)));  /* Y */

    /* System */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_change_camera), mp_obj_new_bool(st.buttons2 & (1 << 4))); /* Back */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_pause),         mp_obj_new_bool(st.buttons2 & (1 << 5))); /* Start */

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_on_foot_obj, joystick_on_foot);

/*******************************************************************************
 * joystick.vehicle() — Scenario 3: Land Vehicle Control
 *
 * Mapping (from Rigs of Rods "Land Vehicles" layout):
 *   steering          — left stick X (normalized -1.0 to 1.0)
 *   accelerate        — RT (digital)
 *   brake             — LT (digital)
 *   clutch            — LB
 *   parking_brake     — RB
 *   shift_up          — X (blue)
 *   shift_down        — B (red)
 *   auto_shift_up     — D-pad Up
 *   auto_shift_down   — D-pad Down
 *   toggle_lights     — D-pad Left
 *   horn              — L3 (left stick click)
 *   starter           — A (green)
 *   ignition          — R3 (right stick click)
 *   camera_x/camera_y — right stick
 *   enter_exit        — Y
 *   change_camera     — Back
 *   pause             — Start
 *******************************************************************************/
static mp_obj_t joystick_vehicle(void) {
    ipc_joystick_state_t st;
    if (!js_fetch_state(&st)) {
        return mp_obj_new_dict(0);
    }

    hat_state_t hs = decode_hat(st.buttons1 & 0x0F);
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(17));

    /* Steering — left stick X */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_steering), normalize_axis(st.left_x, 0x80));

    /* Pedals (digital) */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_accelerate),    mp_obj_new_bool(st.buttons2 & (1 << 3))); /* RT */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_brake),         mp_obj_new_bool(st.buttons2 & (1 << 2))); /* LT */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_clutch),        mp_obj_new_bool(st.buttons2 & (1 << 0))); /* LB */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_parking_brake), mp_obj_new_bool(st.buttons2 & (1 << 1))); /* RB */

    /* Shifting */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_shift_up),        mp_obj_new_bool(st.buttons1 & (1 << 4))); /* X */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_shift_down),      mp_obj_new_bool(st.buttons1 & (1 << 6))); /* B */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_auto_shift_up),   mp_obj_new_bool(hs.up));                  /* D-pad Up */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_auto_shift_down), mp_obj_new_bool(hs.down));                /* D-pad Down */

    /* Vehicle controls */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_toggle_lights), mp_obj_new_bool(hs.left));                    /* D-pad Left */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_horn),          mp_obj_new_bool(st.buttons2 & (1 << 6)));     /* L3 */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_starter),       mp_obj_new_bool(st.buttons1 & (1 << 5)));     /* A */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_ignition),      mp_obj_new_bool(st.buttons2 & (1 << 7)));     /* R3 */

    /* Camera — right stick */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_camera_x), normalize_axis(st.right_x, 0x80));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_camera_y), normalize_axis(st.right_y, 0x7F));

    /* System */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_enter_exit),    mp_obj_new_bool(st.buttons1 & (1 << 7)));  /* Y */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_change_camera), mp_obj_new_bool(st.buttons2 & (1 << 4)));  /* Back */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_pause),         mp_obj_new_bool(st.buttons2 & (1 << 5)));  /* Start */

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_vehicle_obj, joystick_vehicle);

/*******************************************************************************
 * joystick.plane() — Scenario 4: Aircraft Control
 *
 * Mapping (from Rigs of Rods "Planes" layout):
 *   aileron           — left stick X (normalized -1.0 to 1.0)
 *   elevator          — left stick Y (normalized -1.0 to 1.0)
 *   rudder_left       — LT (digital)
 *   rudder_right      — RT (digital)
 *   reverse           — LB
 *   parking_brake     — RB
 *   throttle_inc      — D-pad Up
 *   throttle_dec      — D-pad Down
 *   full_throttle     — B (red)
 *   zero_throttle     — X (blue)
 *   start_engines     — A (green)
 *   camera_x/camera_y — right stick
 *   enter_exit        — Y
 *   change_camera     — Back
 *   pause             — Start
 *******************************************************************************/
static mp_obj_t joystick_plane(void) {
    ipc_joystick_state_t st;
    if (!js_fetch_state(&st)) {
        return mp_obj_new_dict(0);
    }

    hat_state_t hs = decode_hat(st.buttons1 & 0x0F);
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(15));

    /* Flight surfaces — left stick */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_aileron),  normalize_axis(st.left_x, 0x80));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_elevator), normalize_axis(st.left_y, 0x7F));

    /* Rudder */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_rudder_left),  mp_obj_new_bool(st.buttons2 & (1 << 2))); /* LT */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_rudder_right), mp_obj_new_bool(st.buttons2 & (1 << 3))); /* RT */

    /* Engine / brakes */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_reverse),       mp_obj_new_bool(st.buttons2 & (1 << 0))); /* LB */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_parking_brake), mp_obj_new_bool(st.buttons2 & (1 << 1))); /* RB */

    /* Throttle */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_throttle_inc),  mp_obj_new_bool(hs.up));                    /* D-pad Up */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_throttle_dec),  mp_obj_new_bool(hs.down));                  /* D-pad Down */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_full_throttle), mp_obj_new_bool(st.buttons1 & (1 << 6)));   /* B */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_zero_throttle), mp_obj_new_bool(st.buttons1 & (1 << 4)));   /* X */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_start_engines), mp_obj_new_bool(st.buttons1 & (1 << 5)));   /* A */

    /* Camera — right stick */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_camera_x), normalize_axis(st.right_x, 0x80));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_camera_y), normalize_axis(st.right_y, 0x7F));

    /* System */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_enter_exit),    mp_obj_new_bool(st.buttons1 & (1 << 7)));  /* Y */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_change_camera), mp_obj_new_bool(st.buttons2 & (1 << 4)));  /* Back */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_pause),         mp_obj_new_bool(st.buttons2 & (1 << 5)));  /* Start */

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_plane_obj, joystick_plane);

/*******************************************************************************
 * joystick.boat() — Scenario 5: Watercraft Control
 *
 * Mapping (from Rigs of Rods "Boats" layout):
 *   steering          — left stick X (normalized -1.0 to 1.0)
 *   throttle_up       — RT (digital)
 *   throttle_down     — LT (digital)
 *   center_rudder     — X (blue)
 *   reverse           — B (red)
 *   camera_x/camera_y — right stick
 *   enter_exit        — Y
 *   change_camera     — Back
 *   pause             — Start
 *******************************************************************************/
static mp_obj_t joystick_boat(void) {
    ipc_joystick_state_t st;
    if (!js_fetch_state(&st)) {
        return mp_obj_new_dict(0);
    }

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(9));

    /* Steering — left stick X */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_steering), normalize_axis(st.left_x, 0x80));

    /* Throttle / rudder */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_throttle_up),   mp_obj_new_bool(st.buttons2 & (1 << 3))); /* RT */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_throttle_down), mp_obj_new_bool(st.buttons2 & (1 << 2))); /* LT */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_center_rudder), mp_obj_new_bool(st.buttons1 & (1 << 4))); /* X */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_reverse),       mp_obj_new_bool(st.buttons1 & (1 << 6))); /* B */

    /* Camera — right stick */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_camera_x), normalize_axis(st.right_x, 0x80));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_camera_y), normalize_axis(st.right_y, 0x7F));

    /* System */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_enter_exit),    mp_obj_new_bool(st.buttons1 & (1 << 7)));  /* Y */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_change_camera), mp_obj_new_bool(st.buttons2 & (1 << 4)));  /* Back */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_pause),         mp_obj_new_bool(st.buttons2 & (1 << 5)));  /* Start */

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_boat_obj, joystick_boat);

/*******************************************************************************
 * joystick.info() — Device info (VID, PID, connected)
 *******************************************************************************/
static mp_obj_t joystick_info(void) {
    ipc_joystick_state_t st;
    if (!js_fetch_state(&st)) {
        return mp_obj_new_dict(0);
    }

    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(17));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_connected), mp_obj_new_bool(st.connected));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_vid),       mp_obj_new_int(st.vid));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_pid),       mp_obj_new_int(st.pid));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_sequence),  mp_obj_new_int(st.sequence));
    /* Debug fields */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_init_stage),  mp_obj_new_int(st.init_stage));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_usb_init),    mp_obj_new_bool(st.usb_init_done));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_add_events),  mp_obj_new_int(st.add_event_cnt));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_rm_events),   mp_obj_new_int(st.remove_event_cnt));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_reports),     mp_obj_new_int(st.report_cnt));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_isr_count),   mp_obj_new_int(st.isr_count));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_port_power),  mp_obj_new_int(st.port_power_cnt));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_running),     mp_obj_new_bool(st.usbh_running));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_num_dev),     mp_obj_new_int(st.num_devices));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_root_conns),  mp_obj_new_int(st.root_conns));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_usb_class),   mp_obj_new_int(st.usb_class));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_usb_vid),     mp_obj_new_int(st.usb_vid));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d), MP_OBJ_NEW_QSTR(MP_QSTR_usb_pid),     mp_obj_new_int(st.usb_pid));

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(joystick_info_obj, joystick_info);

/*******************************************************************************
 * joystick.deadzone(value) — Set stick deadzone (0-50, default 10)
 *******************************************************************************/
static mp_obj_t joystick_deadzone_fn(mp_obj_t val_obj) {
    int val = mp_obj_get_int(val_obj);
    if (val < 0) val = 0;
    if (val > 50) val = 50;
    js_deadzone = val;
    return mp_obj_new_int(js_deadzone);
}
static MP_DEFINE_CONST_FUN_OBJ_1(joystick_deadzone_obj, joystick_deadzone_fn);

/*******************************************************************************
 * joystick.wait_connect(timeout_ms=5000) — Block until F310 plugged in
 *******************************************************************************/
static mp_obj_t joystick_wait_connect(size_t n_args, const mp_obj_t *args) {
    int timeout_ms = 5000;
    if (n_args > 0) {
        timeout_ms = mp_obj_get_int(args[0]);
        if (timeout_ms < 100) timeout_ms = 100;
        if (timeout_ms > 30000) timeout_ms = 30000;
    }

    mp_printf(&mp_plat_print, "joystick: waiting for gamepad...\n");

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        ipc_joystick_state_t st;
        if (js_fetch_state(&st) && st.connected) {
            mp_printf(&mp_plat_print, "joystick: gamepad connected! (VID=%04X PID=%04X)\n",
                      st.vid, st.pid);
            return mp_const_true;
        }
        Cy_SysLib_Delay(100);
        elapsed += 100;
    }

    mp_printf(&mp_plat_print, "joystick: timeout, no gamepad detected\n");
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(joystick_wait_connect_obj, 0, 1, joystick_wait_connect);

/*******************************************************************************
 * Module Definition
 *******************************************************************************/
static const mp_rom_map_elem_t joystick_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_joystick) },
    /* Core */
    { MP_ROM_QSTR(MP_QSTR_init),           MP_ROM_PTR(&joystick_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_connected),      MP_ROM_PTR(&joystick_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_controller),     MP_ROM_PTR(&joystick_controller_obj) },
    { MP_ROM_QSTR(MP_QSTR_info),           MP_ROM_PTR(&joystick_info_obj) },
    /* Scenario 1: Raw */
    { MP_ROM_QSTR(MP_QSTR_read),           MP_ROM_PTR(&joystick_read_obj) },
    /* Scenario 2: On Foot */
    { MP_ROM_QSTR(MP_QSTR_on_foot),        MP_ROM_PTR(&joystick_on_foot_obj) },
    /* Scenario 3: Land Vehicle */
    { MP_ROM_QSTR(MP_QSTR_vehicle),        MP_ROM_PTR(&joystick_vehicle_obj) },
    /* Scenario 4: Aircraft */
    { MP_ROM_QSTR(MP_QSTR_plane),          MP_ROM_PTR(&joystick_plane_obj) },
    /* Scenario 5: Boat */
    { MP_ROM_QSTR(MP_QSTR_boat),           MP_ROM_PTR(&joystick_boat_obj) },
    /* Utility */
    { MP_ROM_QSTR(MP_QSTR_deadzone),       MP_ROM_PTR(&joystick_deadzone_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait_connect),   MP_ROM_PTR(&joystick_wait_connect_obj) },
};
static MP_DEFINE_CONST_DICT(joystick_module_globals, joystick_module_globals_table);

const mp_obj_module_t mp_module_joystick = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&joystick_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_joystick, mp_module_joystick);
