/*******************************************************************************
 * File Name: mod_dualband.c
 *
 * Description: MicroPython module `dualband` — informational facade for the
 *              Dual-band TinyPython BentoClaw variant.
 *
 *              The variant runs WiFi + BLE NUS concurrently on CM33_NS with a
 *              24 KB MicroPython GC heap. User scripts are expected to be
 *              small (sensor read, MQTT publish, BLE event handler). Anything
 *              that needs HTTPS handshake, large JSON parsing, or on-device
 *              CV should run on the AI-Core (WiFi-only, 64 KB GC) variant.
 *
 *              Compiled only when BENTO_HAS_DUAL_BAND=1 propagates into
 *              libmicropython.a via proj_cm33_ns/Makefile.micropython. The
 *              existing AI-agent `bentoclaw` module in modbentoclaw.c is
 *              orthogonal and stays available across all variants.
 *
 *              Python API (deliberately minimal):
 *                dualband.info()           -> dict of build identity
 *                dualband.heap_size()      -> int (MPY_GC_HEAP_SIZE bytes)
 *                dualband.has_dual_band()  -> bool
 *                dualband.has_ble()        -> bool
 *                dualband.has_wifi()       -> bool
 *                dualband.ble_state()      -> int (mirrors bento_buddy.state)
 *
 *              Heavy operations (start/stop BLE, scan/connect WiFi) live in
 *              the existing `bento_buddy` and `wifi` modules — this module
 *              only exposes read-only introspection so a user script can
 *              branch on capability without bringing the full surfaces into
 *              its import graph.
 ******************************************************************************/

#if defined(BENTO_HAS_DUAL_BAND) && (BENTO_HAS_DUAL_BAND == 1)

#include "py/runtime.h"
#include "py/objstr.h"
#include <stdio.h>
#include <string.h>

#ifndef MPY_GC_HEAP_SIZE
#define MPY_GC_HEAP_SIZE (24 * 1024)
#endif

/* NUS event emitter (BENTO-TESAIoT-Claw-libraries/common/ble_nus/nus_events.c).
 * Returns 0 on success, -1 on transport error or disconnected link. */
extern int nus_emit_event(const char *json);
extern int ble_nus_get_state(void);

/* Per-process monotonic sequence number for outbound bento.net.* events.
 * Matches the SPEC §4.4 `n` field convention. Wraps at 32 bits. */
static uint32_t s_net_seq = 0;

/*---------------------------------------------------------------------------
 * info() -- return a dict of the build identity. Allocates a small dict
 * from the GC heap; cheap enough to call from boot.py for logging.
 *-------------------------------------------------------------------------*/
static mp_obj_t mp_dualband_info(void)
{
    mp_obj_t d = mp_obj_new_dict(6);
    mp_obj_dict_store(d,
        MP_OBJ_NEW_QSTR(MP_QSTR_variant),
        mp_obj_new_str("DualBand-TinyPython", 19));
    mp_obj_dict_store(d,
        MP_OBJ_NEW_QSTR(MP_QSTR_heap),
        MP_OBJ_NEW_SMALL_INT(MPY_GC_HEAP_SIZE));
    mp_obj_dict_store(d,
        MP_OBJ_NEW_QSTR(MP_QSTR_ble),
        mp_obj_new_bool(1));
    mp_obj_dict_store(d,
        MP_OBJ_NEW_QSTR(MP_QSTR_wifi),
        mp_obj_new_bool(1));
    mp_obj_dict_store(d,
        MP_OBJ_NEW_QSTR(MP_QSTR_dual_band),
        mp_obj_new_bool(1));
    mp_obj_dict_store(d,
        MP_OBJ_NEW_QSTR(MP_QSTR_ble_state),
        MP_OBJ_NEW_SMALL_INT((int)ble_nus_get_state()));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_dualband_info_obj, mp_dualband_info);

static mp_obj_t mp_dualband_heap_size(void)
{
    return MP_OBJ_NEW_SMALL_INT(MPY_GC_HEAP_SIZE);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_dualband_heap_size_obj,
                                 mp_dualband_heap_size);

static mp_obj_t mp_dualband_has_dual_band(void)
{
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_dualband_has_dual_band_obj,
                                 mp_dualband_has_dual_band);

static mp_obj_t mp_dualband_has_ble(void)
{
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_dualband_has_ble_obj,
                                 mp_dualband_has_ble);

static mp_obj_t mp_dualband_has_wifi(void)
{
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_dualband_has_wifi_obj,
                                 mp_dualband_has_wifi);

static mp_obj_t mp_dualband_ble_state(void)
{
    return MP_OBJ_NEW_SMALL_INT((int)ble_nus_get_state());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_dualband_ble_state_obj,
                                 mp_dualband_ble_state);

/*---------------------------------------------------------------------------
 * emit_net_ready(ipv4_str, [ports_dict]) -- send a `bento.net.ready` event
 * to the bonded Bento Desktop Buddy peer over the existing NUS link.
 *
 * The JSON shape matches DUAL_CHANNEL_DESIGN.md §IP-discovery:
 *   {"evt":"bento.net.ready","n":<seq>,
 *    "ipv4":"<dotted>","ports":{"mqtt":1883,"https":8443,...}}
 *
 * `ports_dict` is optional. When omitted, no `ports` key is emitted.
 * The caller is expected to invoke this AFTER wifi.connect() succeeds.
 *
 * Returns the sequence number used (int). Raises OSError on NUS send fail
 * (e.g. when no central is bonded — caller can ignore that case).
 *-------------------------------------------------------------------------*/
static mp_obj_t mp_dualband_emit_net_ready(size_t n_args,
                                           const mp_obj_t *args)
{
    const char *ipv4 = mp_obj_str_get_str(args[0]);
    char json[256];
    int len;

    s_net_seq++;

    if (n_args >= 2 && args[1] != mp_const_none) {
        /* Build {"mqtt":1883,...} from the dict argument. We only honour a
         * fixed allow-list of well-known keys to keep the wire output tight
         * and predictable. */
        char ports[96];
        size_t off = 0;
        const struct { const char *k; mp_obj_t key_obj; } slots[] = {
            { "mqtt",  MP_OBJ_NEW_QSTR(MP_QSTR_mqtt) },
            { "mqtts", MP_OBJ_NEW_QSTR(MP_QSTR_mqtts) },
            { "https", MP_OBJ_NEW_QSTR(MP_QSTR_https) },
            { "http",  MP_OBJ_NEW_QSTR(MP_QSTR_http) },
            { "coap",  MP_OBJ_NEW_QSTR(MP_QSTR_coap) },
        };
        ports[0] = '\0';
        for (size_t i = 0; i < sizeof(slots)/sizeof(slots[0]); ++i) {
            mp_obj_t v = mp_obj_dict_get(args[1], slots[i].key_obj);
            if (v == MP_OBJ_NULL || v == mp_const_none) continue;
            if (!mp_obj_is_int(v)) continue;
            int port = mp_obj_get_int(v);
            int w = snprintf(ports + off, sizeof(ports) - off,
                "%s\"%s\":%d", off == 0 ? "" : ",",
                slots[i].k, port);
            if (w <= 0 || (size_t)w >= sizeof(ports) - off) break;
            off += (size_t)w;
        }
        len = snprintf(json, sizeof(json),
            "{\"evt\":\"bento.net.ready\",\"n\":%lu,\"ipv4\":\"%s\","
            "\"ports\":{%s}}",
            (unsigned long)s_net_seq, ipv4, ports);
    } else {
        len = snprintf(json, sizeof(json),
            "{\"evt\":\"bento.net.ready\",\"n\":%lu,\"ipv4\":\"%s\"}",
            (unsigned long)s_net_seq, ipv4);
    }

    if (len <= 0 || (size_t)len >= sizeof(json)) {
        mp_raise_msg(&mp_type_OSError,
            MP_ERROR_TEXT("dualband.emit_net_ready: payload too large"));
    }

    int rc = nus_emit_event(json);
    if (rc != 0) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("nus_emit_event failed (%d)"), rc);
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)s_net_seq);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_dualband_emit_net_ready_obj,
                                           1, 2,
                                           mp_dualband_emit_net_ready);

/*---------------------------------------------------------------------------
 * emit_net_down(reason_str) -- companion to emit_net_ready. Emits
 *   {"evt":"bento.net.down","n":<seq>,"reason":"<reason>"}
 * Reasons are free-form short strings: "link_lost", "deauth", "dhcp_fail".
 *-------------------------------------------------------------------------*/
//! [ble_nus_emit_event_contract]
static mp_obj_t mp_dualband_emit_net_down(mp_obj_t reason_obj)
{
    const char *reason = mp_obj_str_get_str(reason_obj);
    char json[128];
    s_net_seq++;
    int len = snprintf(json, sizeof(json),
        "{\"evt\":\"bento.net.down\",\"n\":%lu,\"reason\":\"%s\"}",
        (unsigned long)s_net_seq, reason);
    if (len <= 0 || (size_t)len >= sizeof(json)) {
        mp_raise_msg(&mp_type_OSError,
            MP_ERROR_TEXT("dualband.emit_net_down: payload too large"));
    }
    int rc = nus_emit_event(json);
    if (rc != 0) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("nus_emit_event failed (%d)"), rc);
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)s_net_seq);
    //! [ble_nus_emit_event_contract]
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_dualband_emit_net_down_obj,
                                 mp_dualband_emit_net_down);

/*---------------------------------------------------------------------------
 * emit_net_captive(suspected_bool) -- emit captive-portal hint event.
 * Wire shape:  {"evt":"bento.net.captive","n":<seq>,"suspected":true|false}
 * The Buddy tray flips an icon overlay; LCD shows a portal QR code.
 *-------------------------------------------------------------------------*/
static mp_obj_t mp_dualband_emit_net_captive(mp_obj_t suspected_obj)
{
    bool suspected = mp_obj_is_true(suspected_obj);
    char json[80];
    s_net_seq++;
    int len = snprintf(json, sizeof(json),
        "{\"evt\":\"bento.net.captive\",\"n\":%lu,\"suspected\":%s}",
        (unsigned long)s_net_seq, suspected ? "true" : "false");
    if (len <= 0 || (size_t)len >= sizeof(json)) {
        mp_raise_msg(&mp_type_OSError,
            MP_ERROR_TEXT("dualband.emit_net_captive: payload too large"));
    }
    int rc = nus_emit_event(json);
    if (rc != 0) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("nus_emit_event failed (%d)"), rc);
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)s_net_seq);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_dualband_emit_net_captive_obj,
                                 mp_dualband_emit_net_captive);

static const mp_rom_map_elem_t dualband_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),       MP_ROM_QSTR(MP_QSTR_dualband) },
    { MP_ROM_QSTR(MP_QSTR_info),           MP_ROM_PTR(&mp_dualband_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_heap_size),      MP_ROM_PTR(&mp_dualband_heap_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_has_dual_band),  MP_ROM_PTR(&mp_dualband_has_dual_band_obj) },
    { MP_ROM_QSTR(MP_QSTR_has_ble),        MP_ROM_PTR(&mp_dualband_has_ble_obj) },
    { MP_ROM_QSTR(MP_QSTR_has_wifi),       MP_ROM_PTR(&mp_dualband_has_wifi_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_state),      MP_ROM_PTR(&mp_dualband_ble_state_obj) },
    { MP_ROM_QSTR(MP_QSTR_emit_net_ready), MP_ROM_PTR(&mp_dualband_emit_net_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_emit_net_down),  MP_ROM_PTR(&mp_dualband_emit_net_down_obj) },
    { MP_ROM_QSTR(MP_QSTR_emit_net_captive), MP_ROM_PTR(&mp_dualband_emit_net_captive_obj) },
};
static MP_DEFINE_CONST_DICT(dualband_module_globals,
                            dualband_module_globals_table);

const mp_obj_module_t mp_module_dualband = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dualband_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_dualband, mp_module_dualband);

#endif /* BENTO_HAS_DUAL_BAND */
