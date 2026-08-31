/*******************************************************************************
 * File Name        : moddeepcraft.c
 *
 * Description      : MicroPython 'deepcraft' module — DEEPCraft model control
 *                    over the bento_link transport layer.
 *
 *                    Replaces the upstream deepcraft_interface.c MPY adapter
 *                    (its machine.IPC duck-type does not exist in our port).
 *                    The vendored deepcraft_engine.c is driven UNMODIFIED via
 *                    the vtable seam: our adapter embeds deepcraft_interface_t
 *                    as first member and forwards to bento_link's control
 *                    plane ("ipc" backend -> CM55 deepcraft_task).
 *
 *                    Python API (v1):
 *                      deepcraft.links()              -> ('ipc',)
 *                      m = deepcraft.DeepcraftModel() -> singleton
 *                      m.set_event_cb(fn)             -> fn(event:int, value:int)
 *                      m.start() / m.stop() / m.state()
 *                      deepcraft.EVENT_READY..EVENT_ERROR constants (0..5)
 *
 *                    Event delivery: engine cb fires in the bento_link RX task
 *                    (NOT the MPY thread), so it only packs event+value into a
 *                    small-int and mp_sched_schedule()s a trampoline — the
 *                    Python callback runs on the MPY scheduler (GC-safe).
 *******************************************************************************/

#include "bsp_feature_flags.h"

#if defined(BENTO_HAS_MODEL_LINK) && (BENTO_HAS_MODEL_LINK == 1)

#include "py/runtime.h"
#include "py/obj.h"
#include "bento_link.h"
#include "deepcraft_interface.h"
#include "sensor_auto_task.h"

extern bool bento_link_ipc_init(void);

/* ── Adapter: deepcraft vtable -> bento_link control plane ──────────────── */
typedef struct {
    deepcraft_interface_t base;        /* MUST be first (vtable cast rule)   */
    bento_link_t *link;
    void (*engine_rx)(uint8_t cmd, uint32_t value);
} dc_adapter_t;

static dc_adapter_t       s_adapter;
static interface_type_t   s_iface;
static deepcraft_engine_t s_engine;
static bool               s_inited = false;

static bool adapter_send(deepcraft_interface_t *self, uint8_t cmd, uint32_t value)
{
    dc_adapter_t *a = (dc_adapter_t *)self;
    return (a->link != NULL) && a->link->send_ctrl(a->link, cmd, value);
}

/* bento_link ctrl_cb (RX task context) -> engine dispatcher */
static void adapter_link_rx(uint8_t cmd, uint32_t value)
{
    if (s_adapter.engine_rx != NULL) {
        s_adapter.engine_rx(cmd, value);
    }
}

static void adapter_register_receive(deepcraft_interface_t *self,
                                     void (*cb)(uint8_t cmd, uint32_t value))
{
    dc_adapter_t *a = (dc_adapter_t *)self;
    a->engine_rx = cb;
    if (a->link != NULL) {
        a->link->register_receive(a->link, adapter_link_rx, NULL);
    }
}

/* ── Event trampoline into the MicroPython scheduler ────────────────────── */
MP_REGISTER_ROOT_POINTER(mp_obj_t bento_deepcraft_event_cb);

static mp_obj_t deepcraft_sched_trampoline(mp_obj_t packed_obj)
{
    mp_int_t packed = mp_obj_get_int(packed_obj);
    mp_obj_t cb = MP_STATE_VM(bento_deepcraft_event_cb);
    if (cb != mp_const_none && cb != MP_OBJ_NULL) {
        mp_obj_t args[2] = {
            MP_OBJ_NEW_SMALL_INT((packed >> 16) & 0xFF),   /* event */
            MP_OBJ_NEW_SMALL_INT(packed & 0xFFFF),         /* value */
        };
        mp_call_function_n_kw(cb, 2, 0, args);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(deepcraft_sched_trampoline_obj,
                                 deepcraft_sched_trampoline);

/* DEEPCRAFT models are trained at a fixed sample rate -- 50 Hz for the motion
 * model -- and the sensor task's 10 Hz dashboard default stretches every
 * gesture far past the window the model learned. The rate has to follow the
 * model's lifetime, and this callback is the only place that sees that
 * lifetime from the core that owns the sensor: CM33.
 *
 * Doing it here rather than from CM55 is deliberate. CM55 has no direct handle
 * on the rate and its control message to CM33 proved unreliable, whereas these
 * events already arrive over the model link on every path -- a MicroPython
 * m.start() and a tap on the Edge AI page both end in a READY event here. */
/* Engine event cb — RX task context: no allocation, schedule only. */
static void engine_event_cb(va_model_events_t event, uint32_t value)
{
    /* No rate control here. The rate is set on CM55 by the deepcraft task's
     * START/STOP handler (ai_engine_set_sensor_rate), which runs for BOTH the
     * MicroPython start and the on-screen C tap. Setting it here too only
     * masked whether that shared path actually works. */
    mp_int_t packed = ((mp_int_t)event << 16) | (mp_int_t)(value & 0xFFFFu);
    (void)mp_sched_schedule(MP_OBJ_FROM_PTR(&deepcraft_sched_trampoline_obj),
                            MP_OBJ_NEW_SMALL_INT(packed));
}

/* ── One-time bring-up ──────────────────────────────────────────────────── */
//! [mpy_bento_link_ipc_init_get]
static void ensure_inited(void)
{
    if (s_inited) {
        return;
    }
    if (!bento_link_ipc_init()) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("model link init failed"));
    }
    s_adapter.base.send                = adapter_send;
    s_adapter.base.register_receive_cb = adapter_register_receive;
    s_adapter.link                     = bento_link_get("ipc");
    if (s_adapter.link == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("ipc link unavailable"));
    }
    s_iface.IPC = &s_adapter.base;
    model_type_t model = { .va_model = DEEPCRAFT_VA_MODEL };
    deepcraft_engine_init(&s_engine, model, &s_iface);
    deepcraft_engine_set_event_cb(&s_engine, engine_event_cb);
    //! [mpy_bento_link_ipc_init_get]
    MP_STATE_VM(bento_deepcraft_event_cb) = mp_const_none;
    s_inited = true;
}

/* ── DeepcraftModel type (singleton) ────────────────────────────────────── */
typedef struct {
    mp_obj_base_t base;
} deepcraft_model_obj_t;

static const mp_obj_type_t deepcraft_model_type;
static deepcraft_model_obj_t s_model_obj = { { &deepcraft_model_type } };

static mp_obj_t model_make_new(const mp_obj_type_t *type, size_t n_args,
                               size_t n_kw, const mp_obj_t *args)
{
    (void)type; (void)n_args; (void)n_kw; (void)args;
    ensure_inited();
    return MP_OBJ_FROM_PTR(&s_model_obj);   /* singleton */
}

static mp_obj_t model_set_event_cb(mp_obj_t self_in, mp_obj_t cb)
{
    (void)self_in;
    if (cb != mp_const_none && !mp_obj_is_callable(cb)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable or None"));
    }
    MP_STATE_VM(bento_deepcraft_event_cb) = cb;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(model_set_event_cb_obj, model_set_event_cb);

static mp_obj_t model_start(mp_obj_t self_in)
{
    (void)self_in;
    deepcraft_engine_start(&s_engine);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(model_start_obj, model_start);

static mp_obj_t model_stop(mp_obj_t self_in)
{
    (void)self_in;
    deepcraft_engine_stop(&s_engine);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(model_stop_obj, model_stop);

static mp_obj_t model_state(mp_obj_t self_in)
{
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(deepcraft_engine_get_state(&s_engine));
}
static MP_DEFINE_CONST_FUN_OBJ_1(model_state_obj, model_state);

static const mp_rom_map_elem_t model_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_set_event_cb), MP_ROM_PTR(&model_set_event_cb_obj) },
    { MP_ROM_QSTR(MP_QSTR_start),        MP_ROM_PTR(&model_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),         MP_ROM_PTR(&model_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_state),        MP_ROM_PTR(&model_state_obj) },
};
static MP_DEFINE_CONST_DICT(model_locals_dict, model_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    deepcraft_model_type,
    MP_QSTR_DeepcraftModel,
    MP_TYPE_FLAG_NONE,
    make_new, model_make_new,
    locals_dict, &model_locals_dict
);

/* ── Module-level ───────────────────────────────────────────────────────── */
//! [mpy_bento_link_at_iterate]
static mp_obj_t deepcraft_links(void)
{
    ensure_inited();
    mp_obj_t items[BENTO_LINK_MAX_BACKENDS];
    uint32_t n = 0;
    for (uint32_t i = 0; i < BENTO_LINK_MAX_BACKENDS; i++) {
        bento_link_t *l = bento_link_at(i);
        if (l == NULL) {
            break;
        }
        items[n++] = mp_obj_new_str(l->name, strlen(l->name));
    }
    return mp_obj_new_tuple(n, items);
}
//! [mpy_bento_link_at_iterate]
static MP_DEFINE_CONST_FUN_OBJ_0(deepcraft_links_obj, deepcraft_links);

static const mp_rom_map_elem_t deepcraft_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),       MP_ROM_QSTR(MP_QSTR_deepcraft) },
    { MP_ROM_QSTR(MP_QSTR_links),          MP_ROM_PTR(&deepcraft_links_obj) },
    { MP_ROM_QSTR(MP_QSTR_DeepcraftModel), MP_ROM_PTR(&deepcraft_model_type) },
    /* Event constants == upstream VA_EVENT_* values (course parity) */
    { MP_ROM_QSTR(MP_QSTR_EVENT_READY),    MP_ROM_INT(VA_EVENT_READY) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_WAKEWORD), MP_ROM_INT(VA_EVENT_WAKEWORD_DETECTED) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_INTENT),   MP_ROM_INT(VA_EVENT_INTENT) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_TIMEOUT),  MP_ROM_INT(VA_EVENT_TIMEOUT) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_STOPPED),  MP_ROM_INT(VA_EVENT_STOPPED) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_ERROR),    MP_ROM_INT(VA_EVENT_ERROR) },
};
static MP_DEFINE_CONST_DICT(deepcraft_module_globals, deepcraft_module_globals_table);

const mp_obj_module_t mp_module_deepcraft = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&deepcraft_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_deepcraft, mp_module_deepcraft);

#endif /* BENTO_HAS_MODEL_LINK */
