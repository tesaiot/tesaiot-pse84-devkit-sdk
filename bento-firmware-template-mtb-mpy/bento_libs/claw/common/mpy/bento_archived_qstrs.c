/*
 * bento_archived_qstrs.c - GENERATED. Do not edit by hand.
 *   Regenerate:  ./bento-release.sh gen-qstr-shim
 *
 * The sources listed below ship as lib/mpy_secure/.../libbento_mpy.a. A
 * prebuilt archive stores qstr INDICES, not strings, with no relocation, so
 * dropping those sources from the scan would shift every index above them and
 * leave the archive using the wrong names - with a clean link and no runtime
 * diagnostic. This file keeps the pool the same shape and asserts that it did.
 *
 * It holds no code and no data: every line is either a declaration the qstr
 * scanner reads or a static assertion the compiler evaluates. That matters,
 * because libmicropython.a is linked --whole-archive.
 *
 * The pinned values are for ONE board and ONE flag set. Building this template
 * for another TARGET, or changing a board feature flag, reshapes the pool and
 * fires these asserts. That is correct: the archive really is invalid there,
 * and a build error is the only honest way to say so.
 */
#include "py/obj.h"

/* Stands in for: tacp.c lfs_wifi_creds.c claw_session.c claw_safety.c claw_transport.c modoptiga.c modedgeai.c  */

/* qstr carriers and index pins, sorted by id. Each line does two jobs:
 * the -DNO_QSTR scan sees the identifier and keeps it in the pool; the
 * normal compile evaluates the enumerator and fails if it moved. The
 * message names the qstr WITHOUT the MP_QSTR_ prefix on purpose - the
 * scanner matches string literals too, so a prefixed name in a message
 * would insert a qstr of its own. */
_Static_assert(MP_QSTR_AES_KEY                    ==  192, "bento: qstr pool moved (AES_KEY)");
_Static_assert(MP_QSTR_CERT_2                     ==  212, "bento: qstr pool moved (CERT_2)");
_Static_assert(MP_QSTR_CERT_3                     ==  213, "bento: qstr pool moved (CERT_3)");
_Static_assert(MP_QSTR_CERT_DEVICE                ==  214, "bento: qstr pool moved (CERT_DEVICE)");
_Static_assert(MP_QSTR_CERT_FACTORY               ==  215, "bento: qstr pool moved (CERT_FACTORY)");
_Static_assert(MP_QSTR_CONF_FLOOR                 ==  216, "bento: qstr pool moved (CONF_FLOOR)");
_Static_assert(MP_QSTR_COUNTER_0                  ==  217, "bento: qstr pool moved (COUNTER_0)");
_Static_assert(MP_QSTR_COUNTER_1                  ==  218, "bento: qstr pool moved (COUNTER_1)");
_Static_assert(MP_QSTR_COUNTER_2                  ==  219, "bento: qstr pool moved (COUNTER_2)");
_Static_assert(MP_QSTR_COUNTER_3                  ==  220, "bento: qstr pool moved (COUNTER_3)");
_Static_assert(MP_QSTR_DATA_0                     ==  227, "bento: qstr pool moved (DATA_0)");
_Static_assert(MP_QSTR_DATA_1                     ==  228, "bento: qstr pool moved (DATA_1)");
_Static_assert(MP_QSTR_DATA_2                     ==  229, "bento: qstr pool moved (DATA_2)");
_Static_assert(MP_QSTR_DATA_3                     ==  230, "bento: qstr pool moved (DATA_3)");
_Static_assert(MP_QSTR_DATA_5                     ==  231, "bento: qstr pool moved (DATA_5)");
_Static_assert(MP_QSTR_DATA_6                     ==  232, "bento: qstr pool moved (DATA_6)");
_Static_assert(MP_QSTR_DATA_LARGE_0               ==  233, "bento: qstr pool moved (DATA_LARGE_0)");
_Static_assert(MP_QSTR_DATA_LARGE_1               ==  234, "bento: qstr pool moved (DATA_LARGE_1)");
_Static_assert(MP_QSTR_KEY_2                      ==  331, "bento: qstr pool moved (KEY_2)");
_Static_assert(MP_QSTR_KEY_3                      ==  332, "bento: qstr pool moved (KEY_3)");
_Static_assert(MP_QSTR_KEY_DEVICE                 ==  333, "bento: qstr pool moved (KEY_DEVICE)");
_Static_assert(MP_QSTR_SENSOR_IMU                 ==  537, "bento: qstr pool moved (SENSOR_IMU)");
_Static_assert(MP_QSTR_SENSOR_MIC                 ==  538, "bento: qstr pool moved (SENSOR_MIC)");
_Static_assert(MP_QSTR_SENSOR_RADAR               ==  539, "bento: qstr pool moved (SENSOR_RADAR)");
_Static_assert(MP_QSTR_UID_OID                    ==  611, "bento: qstr pool moved (UID_OID)");
_Static_assert(MP_QSTR___name__                   ==   23, "bento: qstr pool moved (__name__)");
_Static_assert(MP_QSTR__lt_stdin_gt_              ==  190, "bento: qstr pool moved (_lt_stdin_gt_)");
_Static_assert(MP_QSTR__wcf                       ==  654, "bento: qstr pool moved (_wcf)");
_Static_assert(MP_QSTR__wcok                      ==  655, "bento: qstr pool moved (_wcok)");
_Static_assert(MP_QSTR__wcr                       ==  656, "bento: qstr pool moved (_wcr)");
_Static_assert(MP_QSTR__wcw                       ==  657, "bento: qstr pool moved (_wcw)");
_Static_assert(MP_QSTR_active                     ==  663, "bento: qstr pool moved (active)");
_Static_assert(MP_QSTR_addr                       ==  676, "bento: qstr pool moved (addr)");
_Static_assert(MP_QSTR_aes_decrypt                ==  678, "bento: qstr pool moved (aes_decrypt)");
_Static_assert(MP_QSTR_aes_encrypt                ==  679, "bento: qstr pool moved (aes_encrypt)");
_Static_assert(MP_QSTR_aes_generate_key           ==  680, "bento: qstr pool moved (aes_generate_key)");
_Static_assert(MP_QSTR_busy                       ==  736, "bento: qstr pool moved (busy)");
_Static_assert(MP_QSTR_capacity                   ==  747, "bento: qstr pool moved (capacity)");
_Static_assert(MP_QSTR_cb_entries                 ==  749, "bento: qstr pool moved (cb_entries)");
_Static_assert(MP_QSTR_clm_menu                   ==  760, "bento: qstr pool moved (clm_menu)");
_Static_assert(MP_QSTR_cm55_heap_free             ==  763, "bento: qstr pool moved (cm55_heap_free)");
_Static_assert(MP_QSTR_conf                       ==  773, "bento: qstr pool moved (conf)");
_Static_assert(MP_QSTR_count                      ==   74, "bento: qstr pool moved (count)");
_Static_assert(MP_QSTR_counter_increment          ==  789, "bento: qstr pool moved (counter_increment)");
_Static_assert(MP_QSTR_counter_read               ==  790, "bento: qstr pool moved (counter_read)");
_Static_assert(MP_QSTR_csr                        ==  795, "bento: qstr pool moved (csr)");
_Static_assert(MP_QSTR_ctrl_dropped               ==  796, "bento: qstr pool moved (ctrl_dropped)");
_Static_assert(MP_QSTR_ctrl_handled               ==  797, "bento: qstr pool moved (ctrl_handled)");
_Static_assert(MP_QSTR_ctrl_queued                ==  798, "bento: qstr pool moved (ctrl_queued)");
_Static_assert(MP_QSTR_ctrl_rx                    ==  799, "bento: qstr pool moved (ctrl_rx)");
_Static_assert(MP_QSTR_data                       ==  802, "bento: qstr pool moved (data)");
_Static_assert(MP_QSTR_data_type                  ==  803, "bento: qstr pool moved (data_type)");
_Static_assert(MP_QSTR_declared                   ==  813, "bento: qstr pool moved (declared)");
_Static_assert(MP_QSTR_deinit                     ==  820, "bento: qstr pool moved (deinit)");
_Static_assert(MP_QSTR_diag                       ==  829, "bento: qstr pool moved (diag)");
_Static_assert(MP_QSTR_digest                     ==  833, "bento: qstr pool moved (digest)");
_Static_assert(MP_QSTR_ecdh                       ==  860, "bento: qstr pool moved (ecdh)");
_Static_assert(MP_QSTR_fail                       ==  881, "bento: qstr pool moved (fail)");
_Static_assert(MP_QSTR_gen_keypair                ==  905, "bento: qstr pool moved (gen_keypair)");
_Static_assert(MP_QSTR_header_bytes               ==  930, "bento: qstr pool moved (header_bytes)");
_Static_assert(MP_QSTR_hkdf                       ==  946, "bento: qstr pool moved (hkdf)");
_Static_assert(MP_QSTR_hmac                       ==  947, "bento: qstr pool moved (hmac)");
_Static_assert(MP_QSTR_index                      ==   92, "bento: qstr pool moved (index)");
_Static_assert(MP_QSTR_info                       ==  958, "bento: qstr pool moved (info)");
_Static_assert(MP_QSTR_init                       ==  959, "bento: qstr pool moved (init)");
_Static_assert(MP_QSTR_is_configured              ==  970, "bento: qstr pool moved (is_configured)");
_Static_assert(MP_QSTR_is_ready                   ==  973, "bento: qstr pool moved (is_ready)");
_Static_assert(MP_QSTR_key_oid                    ==  989, "bento: qstr pool moved (key_oid)");
_Static_assert(MP_QSTR_label                      ==  991, "bento: qstr pool moved (label)");
_Static_assert(MP_QSTR_labels                     ==  992, "bento: qstr pool moved (labels)");
_Static_assert(MP_QSTR_last_cmd                   ==  993, "bento: qstr pool moved (last_cmd)");
_Static_assert(MP_QSTR_last_init_rc               ==  994, "bento: qstr pool moved (last_init_rc)");
_Static_assert(MP_QSTR_last_opcode                ==  995, "bento: qstr pool moved (last_opcode)");
_Static_assert(MP_QSTR_latency                    ==  997, "bento: qstr pool moved (latency)");
_Static_assert(MP_QSTR_latency_ms                 ==  998, "bento: qstr pool moved (latency_ms)");
_Static_assert(MP_QSTR_length                     == 1008, "bento: qstr pool moved (length)");
_Static_assert(MP_QSTR_links                      == 1010, "bento: qstr pool moved (links)");
_Static_assert(MP_QSTR_loop_iters                 == 1019, "bento: qstr pool moved (loop_iters)");
_Static_assert(MP_QSTR_manifest_anchor            == 1024, "bento: qstr pool moved (manifest_anchor)");
_Static_assert(MP_QSTR_ml_state                   == 1047, "bento: qstr pool moved (ml_state)");
_Static_assert(MP_QSTR_model                      == 1049, "bento: qstr pool moved (model)");
_Static_assert(MP_QSTR_models                     == 1050, "bento: qstr pool moved (models)");
_Static_assert(MP_QSTR_name                       == 1065, "bento: qstr pool moved (name)");
_Static_assert(MP_QSTR_npu_cycles                 == 1070, "bento: qstr pool moved (npu_cycles)");
_Static_assert(MP_QSTR_oid                        == 1076, "bento: qstr pool moved (oid)");
_Static_assert(MP_QSTR_ok                         == 1077, "bento: qstr pool moved (ok)");
_Static_assert(MP_QSTR_on_result                  == 1080, "bento: qstr pool moved (on_result)");
_Static_assert(MP_QSTR_other_seen                 == 1085, "bento: qstr pool moved (other_seen)");
_Static_assert(MP_QSTR_payload_version            == 1093, "bento: qstr pool moved (payload_version)");
_Static_assert(MP_QSTR_peer_pubkey                == 1096, "bento: qstr pool moved (peer_pubkey)");
_Static_assert(MP_QSTR_phase                      == 1102, "bento: qstr pool moved (phase)");
_Static_assert(MP_QSTR_q_seen                     == 1135, "bento: qstr pool moved (q_seen)");
_Static_assert(MP_QSTR_random                     == 1146, "bento: qstr pool moved (random)");
_Static_assert(MP_QSTR_read_data                  == 1152, "bento: qstr pool moved (read_data)");
_Static_assert(MP_QSTR_read_metadata              == 1153, "bento: qstr pool moved (read_metadata)");
_Static_assert(MP_QSTR_require_setup              == 1170, "bento: qstr pool moved (require_setup)");
_Static_assert(MP_QSTR_result                     == 1173, "bento: qstr pool moved (result)");
_Static_assert(MP_QSTR_running                    == 1191, "bento: qstr pool moved (running)");
_Static_assert(MP_QSTR_salt                       == 1194, "bento: qstr pool moved (salt)");
_Static_assert(MP_QSTR_scores                     == 1202, "bento: qstr pool moved (scores)");
_Static_assert(MP_QSTR_secret_oid                 == 1207, "bento: qstr pool moved (secret_oid)");
_Static_assert(MP_QSTR_select                     == 1211, "bento: qstr pool moved (select)");
_Static_assert(MP_QSTR_sensor                     == 1212, "bento: qstr pool moved (sensor)");
_Static_assert(MP_QSTR_seq                        == 1216, "bento: qstr pool moved (seq)");
_Static_assert(MP_QSTR_set_define                 == 1220, "bento: qstr pool moved (set_define)");
_Static_assert(MP_QSTR_setup                      == 1227, "bento: qstr pool moved (setup)");
_Static_assert(MP_QSTR_sha256                     == 1229, "bento: qstr pool moved (sha256)");
_Static_assert(MP_QSTR_sig_rc                     == 1235, "bento: qstr pool moved (sig_rc)");
_Static_assert(MP_QSTR_sign                       == 1236, "bento: qstr pool moved (sign)");
_Static_assert(MP_QSTR_signed                     == 1237, "bento: qstr pool moved (signed)");
_Static_assert(MP_QSTR_slot_info                  == 1245, "bento: qstr pool moved (slot_info)");
_Static_assert(MP_QSTR_stack_free                 == 1255, "bento: qstr pool moved (stack_free)");
_Static_assert(MP_QSTR_stage_begin                == 1256, "bento: qstr pool moved (stage_begin)");
_Static_assert(MP_QSTR_stage_info                 == 1257, "bento: qstr pool moved (stage_info)");
_Static_assert(MP_QSTR_stage_load                 == 1258, "bento: qstr pool moved (stage_load)");
_Static_assert(MP_QSTR_stage_write                == 1259, "bento: qstr pool moved (stage_write)");
_Static_assert(MP_QSTR_staged_last_rc             == 1260, "bento: qstr pool moved (staged_last_rc)");
_Static_assert(MP_QSTR_staged_loaded              == 1261, "bento: qstr pool moved (staged_loaded)");
_Static_assert(MP_QSTR_staged_rejects             == 1262, "bento: qstr pool moved (staged_rejects)");
_Static_assert(MP_QSTR_stale_drops                == 1263, "bento: qstr pool moved (stale_drops)");
_Static_assert(MP_QSTR_start                      ==  146, "bento: qstr pool moved (start)");
_Static_assert(MP_QSTR_stop                       ==  150, "bento: qstr pool moved (stop)");
_Static_assert(MP_QSTR_top                        == 1318, "bento: qstr pool moved (top)");
_Static_assert(MP_QSTR_uid                        == 1327, "bento: qstr pool moved (uid)");
_Static_assert(MP_QSTR_unload                     == 1333, "bento: qstr pool moved (unload)");
_Static_assert(MP_QSTR_unload_done                == 1334, "bento: qstr pool moved (unload_done)");
_Static_assert(MP_QSTR_unload_refused             == 1335, "bento: qstr pool moved (unload_refused)");
_Static_assert(MP_QSTR_used_size                  == 1344, "bento: qstr pool moved (used_size)");
_Static_assert(MP_QSTR_verify_pair                == 1349, "bento: qstr pool moved (verify_pair)");
_Static_assert(MP_QSTR_write_data                 == 1363, "bento: qstr pool moved (write_data)");
_Static_assert(MP_QSTR_write_metadata             == 1364, "bento: qstr pool moved (write_metadata)");

//! [mpy_module_registration_qstr_block]
/* Copied byte for byte from genhdr/module and genhdr/root_pointer.
 * Not re-derived from the sources: root_pointers.h sorts by the full
 * declaration string, so a changed space moves a struct offset. */
MP_REGISTER_MODULE(MP_QSTR_optiga, mp_module_optiga);
MP_REGISTER_MODULE(MP_QSTR_edge_ai, mp_module_edge_ai);
//! [mpy_module_registration_qstr_block]
MP_REGISTER_ROOT_POINTER(mp_obj_t bento_edgeai_result_cb);
