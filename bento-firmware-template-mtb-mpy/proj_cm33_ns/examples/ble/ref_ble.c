/* sdk-example: core=cm33 variant=both group=ble
 * id:      cm33/ble/ref_ble
 * title:   Reference list — everything else in the ble_nus module
 * teaches: what each remaining call is for, when to reach for it, and what it
 *          gives back — devmode, firmware identity, OTA consent, the radio
 *          scheduler, sensor and voice streams, the CM55 bridge, and the five
 *          weak stubs you are expected to replace
 * apis:    ble_nus_get_diagnostics, fw_hash_compute_at_boot, fw_hash_hex,
 *          fw_hash_prefix8, fw_hash_get_diagnostics, bento_fw_handle_query,
 *          bento_fw_handle_update_begin, bento_fw_on_user_decision,
 *          bento_fw_emit_boot_complete, bento_devmode_init,
 *          bento_devmode_secret_fp_hex, bento_devmode_secret_hex,
 *          bento_devmode_nonce_issue, bento_devmode_unlock,
 *          bento_devmode_is_unlocked, bento_devmode_lock,
 *          bento_devmode_emit_provision, radio_scheduler_init,
 *          radio_scheduler_get_mode, radio_scheduler_get_status,
 *          radio_scheduler_get_boot_mode, radio_scheduler_set_boot_mode,
 *          radio_scheduler_request_mode, radio_scheduler_set_on_state,
 *          radio_scheduler_set_wifi_creds, radio_mode_str,
 *          nus_radio_emit_state_event, sensor_stream_init,
 *          sensor_stream_start, sensor_stream_stop, sensor_stream_stop_all,
 *          sensor_stream_is_active, sensor_stream_dropped_count,
 *          voice_capture_start, voice_capture_stop, voice_capture_is_running,
 *          ipc_bento_buddy_rx_init, ipc_bento_buddy_send,
 *          bento_buddy_request_start, bento_buddy_request_stop,
 *          bento_buddy_auto_start_install, app_wifi_connect_direct,
 *          app_wifi_disconnect, app_wifi_get_ipv4, lfs_save_wifi_creds,
 *          lfs_load_wifi_creds
 * entry:   example_ble_reference
 */

/* The ENTIRE file is behind the module's own build flag, includes and all, so
 * a default build compiles it to nothing and needs no Makefile exclusion.
 * That matters more here than in the other two files: this one DEFINES five
 * weak overrides, and a project that already provides its own (the Playground
 * variant does, in wifi_init.c) would get a duplicate-symbol link error. With
 * the flag off, none of these definitions exist. */
#if ENABLE_PAGE_BENTO_BUDDY

/*==============================================================================
 * THIS IS A REFERENCE LIST, NOT A JOB.
 *
 * 01_nus_bring_up_and_talk.c brings the transport up and moves bytes.
 * 02_host_protocol.c speaks the whole Bento Buddy wire protocol.
 * This file is everything else: each call once, in a row, with a comment
 * saying WHEN you would reach for it and WHAT it gives back. Copy from it when
 * you are writing a status screen or a support dump; do not read it as a
 * worked task.
 *
 * BUILD/LINK CAVEATS — the same ones stated at length in
 * 01_nus_bring_up_and_talk.c, repeated here because this is the file people
 * open first:
 *
 *   * `libbento_secure.a` appears in NO makefile's LDLIBS, and
 *     `template/bento_libs/lib.mk` — which proj_cm33_ns/Makefile:350 includes
 *     unconditionally when ENABLE_PAGE_BENTO_BUDDY=1 — does not exist.
 *     So every example in this directory is COMPILE-VERIFIED and CANNOT be
 *     linked into a device image today. Fixing that wiring is somebody else's
 *     change; do not paper over it here.
 *   * ENABLE_PAGE_BENTO_BUDDY=1 cuts the MicroPython GC heap from 112 KB to
 *     85 KB — about 27 KB goes to the AIROC BLE host instead. That is the
 *     price of turning this module on, and it is why the flag ships at 0.
 *
 * WHAT THIS FILE DOES TO YOUR BOARD, BY DEFAULT
 *   * NO RADIO IS STARTED. Everything that would bring one up is behind a
 *     run-time opt-in flag, all four of them off.
 *   * NO CREDENTIAL IS WRITTEN and NO SECRET IS PUT ON THE AIR.
 *   * NO OPTIGA STATE IS TOUCHED. (This module never touches it at all: the
 *     devmode secret is RAM-only — the OPTIGA 0xE120 persistence described in
 *     bento_devmode.h is not implemented.)
 *   * NOTHING IS WRITTEN TO FLASH.
 *   * It DOES commit ~16 KB of FreeRTOS heap permanently if nobody has
 *     initialised the radio scheduler yet, briefly runs a sensor stream, and
 *     raises then dismisses a Y/N prompt on the CM55 LCD. Each is flagged at
 *     the call site.
 *
 * SECRETS. Nothing here prints key material. The devmode secret is read
 * because this file plays both ends of one protocol, and only its 8-hex
 * fingerprint and its length ever reach the console.
 *============================================================================*/

#ifndef JSMN_PARENT_LINKS
#define JSMN_PARENT_LINKS
#endif
#ifndef JSMN_STRICT
#define JSMN_STRICT
#endif
/* jsmntok_t changes SIZE with JSMN_PARENT_LINKS, and libbento_secure.a was
 * built WITH it (proj_cm33_ns/Makefile:333). Hand the archive an array built
 * without the flag and every index past the first is read at the wrong
 * stride: it links, it does not fault, it just parses the wrong bytes. So
 * the flags are set here rather than trusted to the makefile. JSMN_STATIC
 * additionally keeps jsmn_init/jsmn_parse out of the global symbol table, so
 * this file and 02_host_protocol.c do not collide at link. */
#ifndef JSMN_STATIC
#define JSMN_STATIC
#endif

/*----------------------------------------------------------------------------
 * Run-time opt-ins. All default to 0, all are read as ordinary constants so
 * both branches compile and type-check in every build.
 *--------------------------------------------------------------------------*/

/* Puts the 64-hex devmode secret ON THE AIR to whoever is connected. */
#ifndef BLE_NUS_EXAMPLE_EMIT_DEVMODE_SECRET
#define BLE_NUS_EXAMPLE_EMIT_DEVMODE_SECRET 0
#endif
/* Writes a WiFi credential through your lfs_save_wifi_creds, and queues a
 * radio switch that brings the BLE stack UP. */
#ifndef BLE_NUS_EXAMPLE_ALLOW_RADIO_SWITCH
#define BLE_NUS_EXAMPLE_ALLOW_RADIO_SWITCH 0
#endif
/* STARTS THE BLE RADIO through the lazy wrapper. */
#ifndef BLE_NUS_EXAMPLE_ALLOW_BLE_START
#define BLE_NUS_EXAMPLE_ALLOW_BLE_START 0
#endif
/* Installs the headless auto-start task — not safe to call twice in a boot. */
#ifndef BLE_NUS_EXAMPLE_INSTALL_AUTOSTART
#define BLE_NUS_EXAMPLE_INSTALL_AUTOSTART 0
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "mbedtls/md.h"

#include "vendor/jsmn.h"

#include "ble_nus.h"
#include "ble_nus_lazy.h"
#include "bento_devmode.h"
#include "bento_fw.h"
#include "fw_hash.h"
#include "nus_commands.h"      /* nus_radio_emit_state_event */
#include "radio_scheduler.h"
#include "sensor_stream.h"
#include "voice_capture.h"
/* ipc_bento_buddy_rx_init()'s prototype was recovered into this header
 * because no shipped module header declares it. */
#include "bento_secure_undeclared.h"
/* The IPC command codes and payload layouts shared with the CM55 page. */
#include "ipc_bento_buddy_defs.h"

#include "../sdk_examples_cm33.h"

#define EX_TOKENS_MAX 32

/* ipc_bento_buddy_send is exported by libbento_secure.a but declared in NO
 * shipped header — not even bento_secure_undeclared.h, which lists it as
 * "exported, but no declaration found in the tree". This prototype is copied
 * from the callers inside the archive's own sources, which all agree:
 *   BENTO-TESAIoT-libraries/claw/common/ble_nus/ble_nus_lazy.c:43
 *   BENTO-TESAIoT-libraries/claw/common/ble_nus/nus_commands.c:41
 *   BENTO-TESAIoT-libraries/claw/common/ble_nus/nus_agent.c:35
 *   BENTO-TESAIoT-libraries/claw/common/ble_nus/bento_fw.c:54
 * and it matches the definition at ipc_bento_buddy_bridge.c:45. Get this
 * wrong and it still links: the arguments simply land in the wrong registers.
 */
extern int ipc_bento_buddy_send(uint8_t cmd, uint16_t value,
                                const uint8_t *data, size_t data_len);

static int tokenise(const char *json, jsmntok_t *toks, int cap)
{
    jsmn_parser p;
    jsmn_init(&p);
    int n = jsmn_parse(&p, json, strlen(json), toks, (unsigned int)cap);
    if (n < 1 || toks[0].type != JSMN_OBJECT) return -1;
    return n;
}

/* Scrub a buffer that held key material before it goes out of scope. memset
 * can be optimised away on a dying object; a volatile pointer cannot. */
static void burn(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) *v++ = 0;
}

static const char *state_str(ble_nus_state_t s)
{
    switch (s) {
        case BLE_NUS_STATE_OFF:         return "OFF";
        case BLE_NUS_STATE_ADVERTISING: return "ADVERTISING";
        case BLE_NUS_STATE_CONNECTED:   return "CONNECTED";
        case BLE_NUS_STATE_ERROR:       return "ERROR";
        default:                        return "?";
    }
}

static const char *boot_mode_str(radio_boot_mode_t m)
{
    switch (m) {
        case RADIO_BOOT_AUTO:       return "auto";
        case RADIO_BOOT_FORCE_BLE:  return "force_ble";
        case RADIO_BOOT_FORCE_WIFI: return "force_wifi";
        default:                    return "?";
    }
}

/*==============================================================================
 * SECTION W — THE FIVE WEAK OVERRIDES
 *==============================================================================
 * libbento_secure.a defines five functions with __attribute__((weak)):
 *
 *     $ arm-none-eabi-nm lib/ble_nus/.../libbento_secure.a | grep ' W '
 *     00000000 W app_wifi_connect_direct
 *     00000000 W app_wifi_disconnect
 *     00000000 W app_wifi_get_ipv4
 *     00000000 W lfs_load_wifi_creds
 *     00000000 W lfs_save_wifi_creds
 *     00000000 W ble_nus_passkey_cb   <- overridden in 01_nus_bring_up_and_talk
 *
 * A weak definition loses to any strong definition of the same name anywhere
 * in the link. So you do not register anything and you do not pass a function
 * pointer: you simply DEFINE the function, and the linker prefers yours. That
 * is how the archive calls YOUR WiFi driver and YOUR storage without ever
 * having heard of either.
 *
 * The archive is honest about the cost, in dist/ble_nus/README.md:
 *
 *     "Leaving a weak stub in place is legal and silent — the call succeeds
 *      and does nothing. That is the failure mode to watch for."
 *
 * Concretely, with the stubs in place:
 *     radio_scheduler_set_wifi_creds() returns false, always. Nothing is
 *     stored, and the only clue is one printf.
 *     radio_scheduler_request_mode(WIFI_ACTIVE) is rejected with "no saved
 *     creds", forever, because lfs_load_wifi_creds always says false.
 *     A switch to WiFi that somehow got queued reports failure because
 *     app_wifi_connect_direct returns -1.
 * Nothing crashes and nothing warns at build time. The board just never
 * joins a network.
 *
 * THE SIGNATURES — AND WHY GETTING ONE WRONG IS WORSE THAN GETTING IT MISSING
 * There is no header for any of these. The linker matches on NAME ONLY: C has
 * no argument-type mangling, so an override declared with the wrong parameter
 * list links cleanly and then reads whatever happens to be in r0..r3. A
 * wrong-arity lfs_load_wifi_creds writes through a "buffer pointer" that is
 * really a capacity integer.
 *
 * Every signature below was read off the archive's own weak definitions in
 *   BENTO-TESAIoT-libraries/claw/common/ble_nus/radio_scheduler.c
 *     :40  app_wifi_connect_direct   :47  app_wifi_disconnect
 *     :48  app_wifi_get_ipv4         :49  lfs_save_wifi_creds
 *     :56  lfs_load_wifi_creds
 * and cross-checked against the call sites in the same file (:166, :174, :209,
 * :297, :336, :365). That tree is read-only and was not modified.
 *
 * !! IF YOUR PROJECT ALREADY DEFINES ANY OF THESE — the Playground variant
 * !! does, in its wifi_init.c glue — DELETE THE MATCHING FUNCTION FROM THIS
 * !! FILE. Two strong definitions of one name is a duplicate-symbol link
 * !! error, and it is the good outcome: the linker tells you.
 *============================================================================*/

/* The scheduler's own buffers are ssid[33], password[65], security[16], and it
 * passes those capacities in. Size your store to match or you will truncate
 * credentials the caller considered valid. */
typedef struct {
    char ssid[33];
    char password[65];
    char security[16];
    bool valid;
} ex_creds_t;

static ex_creds_t s_creds;

/* Copy with explicit truncation reporting — strncpy alone leaves an
 * unterminated buffer when the source is exactly cap bytes. */
static bool copy_bounded(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) return false;
    size_t n = (src != NULL) ? strlen(src) : 0;
    if (n >= cap) { dst[0] = '\0'; return false; }   /* refuse, do not truncate */
    memcpy(dst, src ? src : "", n);
    dst[n] = '\0';
    return true;
}

/*----------------------------------------------------------------------------
 * int lfs_save_wifi_creds(const char *ssid, const char *password,
 *                         const char *security)
 *
 * WHEN: the archive calls it for you, from radio_scheduler_set_wifi_creds
 * (radio_scheduler.c:365), AFTER it has validated that ssid is non-empty and
 * <= 32 chars and password is <= 64. It never passes NULL: absent values
 * arrive as "" and an absent security arrives as "WPA2".
 * RETURNS: 0 on success, NON-ZERO on failure. Note the polarity — it is the
 * opposite of lfs_load_wifi_creds, which returns a bool. Non-zero makes
 * radio_scheduler_set_wifi_creds return false and print
 * "[radio] lfs_save_wifi_creds failed".
 *
 * This implementation is COMPLETE but RAM-backed: it stores what it is given
 * and enforces the capacities the caller declares. What it does not do is
 * survive a reboot.
 *--------------------------------------------------------------------------*/
int lfs_save_wifi_creds(const char *ssid, const char *password,
                        const char *security)
{
    if (ssid == NULL || ssid[0] == '\0') return -1;

    ex_creds_t staged;
    memset(&staged, 0, sizeof(staged));
    if (!copy_bounded(staged.ssid, sizeof(staged.ssid), ssid))          return -1;
    if (!copy_bounded(staged.password, sizeof(staged.password),
                      password ? password : ""))                        return -1;
    if (!copy_bounded(staged.security, sizeof(staged.security),
                      (security && security[0]) ? security : "WPA2"))   return -1;
    staged.valid = true;

    /* >>> REPLACE THIS with your persistent write. Commit atomically: a
     * half-written credential file that still parses is worse than none. <<< */
    s_creds = staged;

    return 0;
}

/*----------------------------------------------------------------------------
 * bool lfs_load_wifi_creds(char *ssid_out, size_t ssid_cap,
 *                          char *pass_out, size_t pass_cap,
 *                          char *sec_out,  size_t sec_cap)
 *
 * WHEN: from radio_scheduler_init (:297) to decide the boot mode, and from
 * radio_scheduler_request_mode (:336) before queueing a switch to WiFi. Both
 * pass ssid[33], pass[65], sec[16] on the stack.
 * RETURNS: true only when ALL THREE were filled. Return false and the
 * scheduler treats the board as unconfigured and refuses to switch to WiFi.
 * Partial success is not representable, so do not half-fill and return true.
 *--------------------------------------------------------------------------*/
bool lfs_load_wifi_creds(char *ssid_out, size_t ssid_cap,
                         char *pass_out, size_t pass_cap,
                         char *sec_out,  size_t sec_cap)
{
    if (ssid_out == NULL || pass_out == NULL || sec_out == NULL) return false;
    if (ssid_cap == 0 || pass_cap == 0 || sec_cap == 0)          return false;

    /* >>> REPLACE THIS with your persistent read. <<< */
    if (!s_creds.valid) return false;

    /* The caller's capacity wins. If your stored SSID does not fit the buffer
     * you were handed, that is a failure, not a truncation: a truncated SSID
     * silently joins the wrong network or none at all. */
    if (!copy_bounded(ssid_out, ssid_cap, s_creds.ssid))     return false;
    if (!copy_bounded(pass_out, pass_cap, s_creds.password)) return false;
    if (!copy_bounded(sec_out,  sec_cap,  s_creds.security)) return false;
    return true;
}

/*----------------------------------------------------------------------------
 * int app_wifi_connect_direct(const char *ssid, const char *password,
 *                             const char *security)
 *
 * WHEN: from radio_scheduler.c:166, on the worker task, with credentials the
 * scheduler has already loaded. Never from an ISR, so it may block.
 * RETURNS: 0 on success, non-zero on failure. The scheduler counts three
 * consecutive failures and falls back to BLE.
 *
 * A real implementation joins the AP — the Playground variant routes this to
 * the same lazy cy_wcm entry that MicroPython's wifi.connect() uses, so a
 * credential provisioned by the desktop reaches WHD through exactly the code
 * path a manual REPL connect would take. Reusing that one path is the point:
 * a second, parallel connect path is how two subsystems end up disagreeing
 * about whether the radio is busy.
 *
 * THIS IMPLEMENTATION REFUSES. It brings up no radio, and unlike the weak
 * stub it says so. Replace the body.
 *--------------------------------------------------------------------------*/
int app_wifi_connect_direct(const char *ssid, const char *password,
                            const char *security)
{
    (void)password;   /* never logged — it is a secret */
    printf("[ref] app_wifi_connect_direct(ssid=\"%s\", security=\"%s\") "
           "REFUSED: this example does not bring up a radio. Replace this "
           "body with your cy_wcm connect.\r\n",
           ssid ? ssid : "(null)", security ? security : "(null)");
    return -1;
}

/*----------------------------------------------------------------------------
 * int app_wifi_disconnect(void)
 *
 * WHEN: from radio_scheduler.c:209 when switching back to BLE. Its return
 * value is DISCARDED — `(void)app_wifi_disconnect();` — so the switch to BLE
 * proceeds either way and a failure here cannot stop the transition. Make it
 * idempotent and safe to call when WiFi was never up.
 * RETURNS: nothing the caller reads.
 *
 * A real implementation calls cy_wcm_disconnect() and waits for the link-down
 * event before returning, so the BLE stack does not come up while WHD is
 * still holding the radio.
 *--------------------------------------------------------------------------*/
int app_wifi_disconnect(void)
{
    printf("[ref] app_wifi_disconnect(): nothing to disconnect — no radio was "
           "brought up by this example\r\n");
    return 0;   /* idempotent success: there was nothing to tear down */
}

/*----------------------------------------------------------------------------
 * uint32_t app_wifi_get_ipv4(void)
 *
 * WHEN: from radio_scheduler.c:174 right after a successful connect. The
 * result is stored verbatim in radio_status_t.ipv4.
 * RETURNS: the address in NETWORK byte order, or 0 for "no address".
 * nus_commands.c formats it low-byte-first — (ipv4 & 0xFF) is the FIRST
 * octet. Return host order and every IP the desktop displays is reversed.
 * 0 is also what the weak stub returns, so a reversed-or-zero address is the
 * symptom to watch for.
 *--------------------------------------------------------------------------*/
uint32_t app_wifi_get_ipv4(void)
{
    return 0;   /* no link, therefore no address */
}

/*==============================================================================
 * The on-state hook. It runs on the scheduler's worker task, so keep it short
 * and NEVER printf from it — the same reasoning as the BLE callbacks in
 * 01_nus_bring_up_and_talk.c. Production wires this to
 * nus_radio_emit_state_event so every transition surfaces in the desktop UI
 * without a poll.
 *============================================================================*/
static volatile uint32_t     s_state_events;
static volatile radio_mode_t s_last_mode = RADIO_MODE_UNKNOWN;

static void ex_on_state(const radio_status_t *st)
{
    if (st == NULL) return;
    s_last_mode = st->mode;
    s_state_events++;
}

/* Boot-mode persistence hooks. The scheduler does not know how to store
 * anything — it deliberately avoids linking the MicroPython VFS. You supply
 * these two, and the production firmware backs them with a one-byte LittleFS
 * file at /.radio_boot_mode. Passing NULL for either disables persistence and
 * boot always defaults to AUTO. These are backed by a static variable, so this
 * file writes nothing to flash — which is the whole reason it can exercise
 * set_boot_mode safely. */
static radio_boot_mode_t s_ram_boot_mode  = RADIO_BOOT_AUTO;
static bool              s_ram_boot_valid = false;

static void ex_persist_boot_mode(radio_boot_mode_t mode)
{
    s_ram_boot_mode  = mode;
    s_ram_boot_valid = true;
}

static bool ex_load_boot_mode(radio_boot_mode_t *out)
{
    if (!s_ram_boot_valid || out == NULL) return false;
    *out = s_ram_boot_mode;
    return true;
}

int example_ble_reference(void);

int example_ble_reference(void)
{
    const int emit_secret_on_air = BLE_NUS_EXAMPLE_EMIT_DEVMODE_SECRET;
    const int allow_radio_switch = BLE_NUS_EXAMPLE_ALLOW_RADIO_SWITCH;
    const int allow_ble_start    = BLE_NUS_EXAMPLE_ALLOW_BLE_START;
    const int install_autostart  = BLE_NUS_EXAMPLE_INSTALL_AUTOSTART;

    jsmntok_t toks[EX_TOKENS_MAX];
    int n;
    int soft_fail = 0;

    /*========================================================================
     * A — TRANSPORT DIAGNOSTICS
     *
     * The link state and the advertised name live in
     * 01_nus_bring_up_and_talk.c, where they belong to a job. What is left
     * here is the counter set.
     *======================================================================*/
    printf("[ref] ---- transport diagnostics ---------------------------\r\n");

    /* WHEN: a link dropped and the host cannot rediscover the board without a
     * power cycle. This is the counter set that tells you whether the
     * disconnect handler tried to re-advertise and what the stack said.
     * RETURNS: by out-parameter. last_advert_restart_result and
     * last_boot_advert_result are wiced_result_t values — 0 is success. A
     * rising advert_restart_attempts with a non-zero result is the signature
     * of the rediscovery failure. */
    ble_nus_diag_t d;
    memset(&d, 0, sizeof(d));
    ble_nus_get_diagnostics(&d);
    printf("[ref] ble_nus_get_diagnostics        : disconnects=%lu "
           "last_reason=0x%02X readvert_attempts=%lu last_readvert_rc=%d\r\n",
           (unsigned long)d.disconnect_count,
           (unsigned)d.last_disconnect_reason,
           (unsigned long)d.advert_restart_attempts,
           d.last_advert_restart_result);
    printf("[ref]                                  deinit_flag_at_last_disc=%u "
           "boot_advert_attempts=%lu last_boot_advert_rc=%d\r\n",
           (unsigned)d.deinit_in_progress_at_last_disconnect,
           (unsigned long)d.boot_advert_attempts,
           d.last_boot_advert_result);

    /*========================================================================
     * B — FIRMWARE IDENTITY
     *======================================================================*/
    printf("[ref] ---- firmware identity -------------------------------\r\n");

    /* WHEN: once, early in main(), before the Buddy UI comes up. Idempotent,
     * so calling it again costs nothing; the first call burns about 80 ms.
     * RETURNS: nothing — it fills the module's digest.
     *
     * What it actually hashes is worth knowing, because the name suggests
     * something else: since v1.0.9 it hashes a build-deterministic identity
     * string (BENTO_BUDDY_FW_VERSION | __DATE__ | __TIME__), NOT the flash
     * image. The earlier VTOR-based 128 KB scan reached into SRAM and produced
     * a different digest on every power cycle — the whole reason
     * fw_hash_diag_t exists. Treat this as a BUILD identifier, not as an
     * integrity measurement: two different images from the same build minute
     * would collide. */
    fw_hash_compute_at_boot();

    /* WHEN: answering bento.fw.query, or logging which build is running.
     * RETURNS: always non-NULL — 64 lowercase hex chars, or the literal
     * "unknown" if mbedtls was unavailable or the linker symbols could not be
     * resolved. */
    const char *hex = fw_hash_hex();
    printf("[ref] BENTO_BUDDY_FW_VERSION         = %s\r\n",
           BENTO_BUDDY_FW_VERSION);
    printf("[ref] fw_hash_hex                    = %s\r\n", hex);
    if (strcmp(hex, "unknown") == 0) {
        printf("[ref] digest is the sentinel — the hash did not compute\r\n");
    }

    /* WHEN: showing a hash a human can compare, e.g. the update Y/N prompt —
     * 64 hex characters do not fit on the prompt and a human comparing 8 is
     * doing something real.
     * RETURNS: by out-parameter into a buffer of AT LEAST 9 bytes. */
    char pre[9];
    fw_hash_prefix8(pre);
    printf("[ref] fw_hash_prefix8                = %s\r\n", pre);

    /* WHEN: a support report claims the same binary hashed differently across
     * two power cycles.
     * RETURNS: how many times the hash was computed, the VTOR seen, and the
     * first 8 bytes of the window that was hashed. compute_count > 1 means
     * something called compute_at_boot more than once. */
    fw_hash_diag_t fh;
    memset(&fh, 0, sizeof(fh));
    fw_hash_get_diagnostics(&fh);
    printf("[ref] fw_hash_get_diagnostics        : compute_count=%lu "
           "vtor=0x%08lX head=%02X%02X%02X%02X\r\n",
           (unsigned long)fh.compute_count, (unsigned long)fh.vtor_addr,
           fh.window_head[0], fh.window_head[1],
           fh.window_head[2], fh.window_head[3]);

    /*========================================================================
     * C — THE OTA CONSENT GATE
     *
     * NOTHING IS FLASHED HERE, and nothing in this module can cause a flash.
     * The device never writes its own image: the desktop drives OpenOCD over
     * SWD. All this module contributes is the consent gate.
     *======================================================================*/
    printf("[ref] ---- firmware update ---------------------------------\r\n");

    /* WHEN: the desktop asks what is running. A cheap read-only probe.
     * RETURNS: nothing — it emits an ack carrying the version, the digest and
     * a `_diag` block (ble counters + hash counters + uptime) so support can
     * root-cause a connectivity report in one round trip without a serial
     * console. It changes nothing. */
    static const char q[] = "{\"cmd\":\"bento.fw.query\"}";
    n = tokenise(q, toks, EX_TOKENS_MAX);
    if (n > 0) {
        bento_fw_handle_query(q, toks, n);
        printf("[ref] bento_fw_handle_query          : ack emitted (dropped "
               "if no link)\r\n");
    }

    /* WHEN: the desktop wants to flash. It does NOT ack — that is the part
     * people get wrong. It:
     *   * auto-dismisses a stale prompt older than the TTL + 2 s grace;
     *   * collapses duplicate begins inside the dedup window, leaving the
     *     first prompt up and staying silent;
     *   * REFUSES with {"ok":false,"error":"busy"} if a sensor stream is
     *     running — the flasher must see a quiescent device;
     *   * otherwise raises a full-screen Y/N prompt on the CM55 LCD, showing
     *     the first 8 hex chars of the target digest, and returns silently.
     * The single ack for the whole exchange comes later, from
     * bento_fw_on_user_decision. A desktop waiting for an immediate ack will
     * time out for the wrong reason.
     * RETURNS: nothing.
     *
     * This is done BEFORE section F starts a sensor stream, for exactly the
     * "busy" reason above. */
    if (sensor_stream_is_active()) {
        printf("[ref] bento_fw_handle_update_begin   : SKIPPED — a sensor "
               "stream is running, so it would ack \"busy\" and raise no "
               "prompt\r\n");
    } else {
        static const char begin[] =
            "{\"cmd\":\"bento.fw.update.begin\",\"fw_version\":\"1.4.1\","
            "\"sha256\":\"deadbeefcafef00d0000000000000000"
            "00000000000000000000000000000000\"}";
        n = tokenise(begin, toks, EX_TOKENS_MAX);
        if (n > 0) {
            bento_fw_handle_update_begin(begin, toks, n);
            printf("[ref] bento_fw_handle_update_begin   : prompt raised on "
                   "the LCD showing \"deadbeef\" — no ack yet, by design\r\n");
        }
    }

    /* WHEN: the LCD tap arrives through IPC_CMD_BUDDY_USER_DECISION. Non-zero
     * is Y.
     * RETURNS: nothing — it emits the exchange's one ack and clears the
     * prompt. Calling it with no prompt pending is a no-op.
     *
     * This file passes 0, DECLINE, and always will: an automated runner has
     * no human to consent, and approve additionally calls
     * sensor_stream_stop_all() to quiesce the board for the flasher. There is
     * no timeout that defaults to yes and no side-channel bypass — that is
     * the whole point of ISSUE-027. */
    bento_fw_on_user_decision(0);
    printf("[ref] bento_fw_on_user_decision(0)   : ack "
           "{\"ok\":false,\"error\":\"user_declined\"}, prompt cleared\r\n");
    bento_fw_on_user_decision(0);
    printf("[ref] bento_fw_on_user_decision(0)   : again with nothing pending "
           "-> no-op, no second ack\r\n");

    /* WHEN: from the BLE CONNECTED transition, once per boot, so the desktop
     * can compare the running digest against whatever it cached and decide
     * for itself whether an update just landed or the board merely rebooted.
     * Bonds are RAM-only, so every reboot forces a re-pair anyway and this
     * event is the reconciliation point.
     * RETURNS: nothing. The once-per-boot flag is set ONLY when ble_nus_send
     * succeeds, so on a bench board with no link this call does nothing and
     * does NOT burn the one emission — the real CONNECTED transition will
     * still send it. That is why it is safe to call here. */
    bento_fw_emit_boot_complete();
    printf("[ref] bento_fw_emit_boot_complete    : called; with no link the "
           "once-per-boot flag stays unset\r\n");

    /*========================================================================
     * D — DEVELOPER MODE
     *
     * WHAT IT GATES: the bento.exec verb — arbitrary MicroPython from the
     * desktop. Nothing else. Leaving it unlocked on a board on someone's desk
     * means any central that can reach the NUS RX characteristic can run code,
     * and per nus_gatt_db.h no attribute in this database requires
     * authentication.
     *
     * THE SECRET: RAM-only, regenerated on every boot. OPTIGA slot 0xE120
     * persistence is NOT implemented (see bento_devmode.h), so the desktop
     * must re-provision after every reflash and every power cycle, and a
     * secret you print today is meaningless tomorrow. That is not a reason to
     * print it: only the 8-hex fingerprint reaches the console below.
     *======================================================================*/
    printf("[ref] ---- developer mode ----------------------------------\r\n");

    /* WHEN: gating bento.exec, or rendering an "unlocked" badge.
     * RETURNS: the flag, nothing more. It is cleared by bento_devmode_lock and
     * by nothing else — not by a disconnect, and not by a timeout. */
    bool dm_live = bento_devmode_is_unlocked();
    printf("[ref] bento_devmode_is_unlocked      = %s\r\n",
           dm_live ? "true" : "false");

    /* WHEN: once, to control WHEN the boot entropy is drawn. Idempotent, and
     * every other entry point calls it for you.
     * RETURNS: nothing. */
    bento_devmode_init();

    /* WHEN: an unlock keeps returning not_permitted and you need to know
     * whether the two ends hold the same secret — the desktop prints the
     * matching secret_fp, so a mismatch here is the single most common cause.
     * RETURNS: 8 hex chars of sha256(secret) into a buffer of at least 9, and
     * the length written, or 0 (writing nothing) if the buffer is too small.
     * SAFE TO LOG — the secret itself is not.
     * SIDE EFFECT: it calls bento_devmode_init() internally. */
    char fp[16] = {0};
    size_t fp_len = bento_devmode_secret_fp_hex(fp, sizeof(fp));
    printf("[ref] bento_devmode_secret_fp_hex    = %s (%u chars) — compare "
           "with the desktop's\r\n",
           fp_len ? fp : "(unavailable)", (unsigned)fp_len);
    char tiny[4];
    printf("[ref] bento_devmode_secret_fp_hex    : 4-byte buffer -> %u "
           "(refused, needs >= 9)\r\n",
           (unsigned)bento_devmode_secret_fp_hex(tiny, sizeof(tiny)));

    if (dm_live) {
        /* Somebody else's session is live and this section ends by locking.
         * Skip the whole dance rather than closing their door. */
        printf("[ref] bento_devmode_nonce_issue/unlock/lock : SKIPPED — "
               "developer mode is already unlocked, so a real session is "
               "live and this section would lock it\r\n");
    } else {
        /* WHEN: you are the desktop and need the 32-byte key to compute an
         * HMAC. In normal operation the desktop holds it and the firmware
         * never hands it out except through bento_devmode_emit_provision;
         * this file needs it only because it plays BOTH ends.
         * RETURNS: 64 hex chars + NUL and the count 64, or 0 — writing
         * nothing — if the buffer is smaller than 65. The header is explicit
         * that the caller must NOT log this. */
        char secret_hex[BENTO_DEVMODE_SECRET_LEN * 2 + 1] = {0};
        if (bento_devmode_secret_hex(secret_hex, sizeof(secret_hex)) == 0) {
            printf("[ref] bento_devmode_secret_hex       : refused the buffer "
                   "— cannot run the unlock dance\r\n");
            soft_fail = 1;
        } else {
            printf("[ref] bento_devmode_secret_hex       = 64 chars (NOT "
                   "printed, by contract); short buffer -> %u\r\n",
                   (unsigned)bento_devmode_secret_hex(tiny, sizeof(tiny)));

            uint8_t secret[BENTO_DEVMODE_SECRET_LEN];
            for (size_t i = 0; i < sizeof(secret); i++) {
                unsigned hi = (unsigned char)secret_hex[i * 2];
                unsigned lo = (unsigned char)secret_hex[i * 2 + 1];
                hi = (hi <= '9') ? (hi - '0') : ((hi | 0x20u) - 'a' + 10u);
                lo = (lo <= '9') ? (lo - '0') : ((lo | 0x20u) - 'a' + 10u);
                secret[i] = (uint8_t)((hi << 4) | lo);
            }
            burn(secret_hex, sizeof(secret_hex));

            /* WHEN: the desktop sends bento.devmode.nonce.
             * RETURNS: by out-parameter, 16 bytes. It overwrites any previous
             * pending nonce — there is only ever one challenge in flight —
             * and starts a 60-second TTL. */
            uint8_t nonce[BENTO_DEVMODE_NONCE_LEN];
            bento_devmode_nonce_issue(nonce);
            printf("[ref] bento_devmode_nonce_issue      : %u bytes, TTL "
                   "%u ms\r\n",
                   (unsigned)BENTO_DEVMODE_NONCE_LEN,
                   (unsigned)BENTO_DEVMODE_NONCE_TTL_MS);

            /* The response the desktop computes: HMAC-SHA256, key = the
             * 32-byte shared secret, message = the 16-byte nonce, sent back
             * as a lowercase 64-char hex string. */
            uint8_t mac[BENTO_DEVMODE_HMAC_LEN];
            const mbedtls_md_info_t *sha256 =
                mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
            if (sha256 == NULL
                || mbedtls_md_hmac(sha256, secret, sizeof(secret),
                                   nonce, sizeof(nonce), mac) != 0) {
                burn(secret, sizeof(secret));
                printf("[ref] mbedtls HMAC unavailable — cannot build a "
                       "response\r\n");
                soft_fail = 1;
            } else {
                burn(secret, sizeof(secret));

                static const char hexd[] = "0123456789abcdef";
                char mac_hex[BENTO_DEVMODE_HMAC_LEN * 2 + 1];
                for (size_t i = 0; i < sizeof(mac); i++) {
                    mac_hex[i * 2]     = hexd[mac[i] >> 4];
                    mac_hex[i * 2 + 1] = hexd[mac[i] & 0x0Fu];
                }
                mac_hex[sizeof(mac_hex) - 1] = '\0';

                /* WHEN: the desktop sends bento.devmode.unlock.
                 * RETURNS: true if the HMAC matched. Compared in constant
                 * time. The nonce is consumed on ANY call, pass or fail — one
                 * shot, so a captured response cannot be replayed. The length
                 * argument must be exactly 64; hex_decode rejects anything
                 * else.
                 *
                 * THE LOCKOUT IS REAL: five FAILED unlocks inside one hour
                 * disable unlocking entirely until the hour is up
                 * (COOL_MAX_FAILS=5, COOL_WINDOW_MS=3600000 in
                 * bento_devmode.c). A SUCCESSFUL unlock resets the counter to
                 * zero, which is why this file only ever performs the correct
                 * response and never demonstrates a failure. Do not loop
                 * this. */
                bool ok = bento_devmode_unlock(mac_hex, sizeof(mac_hex) - 1);
                printf("[ref] bento_devmode_unlock           = %s; "
                       "is_unlocked=%s\r\n",
                       ok ? "true" : "false",
                       bento_devmode_is_unlocked() ? "true" : "false");
                burn(mac_hex, sizeof(mac_hex));
                burn(mac, sizeof(mac));
                if (!ok) soft_fail = 1;
            }

            /* WHEN: from the BLE CONNECTED transition, once per boot, so the
             * desktop can store the secret in its keychain.
             * RETURNS: nothing. It puts the 64-hex secret ON THE AIR as
             * {"evt":"bento.devmode.provision","secret":"..."} — to whoever
             * is connected, since the NUS characteristics carry no
             * authentication requirement. A static per-boot flag means it
             * sends at most once per boot, and that flag is only set when
             * ble_nus_send succeeds, so a send that fails on a dead link is
             * retried at the next CONNECTED transition.
             *
             * OFF by default for the reason above:
             *   DEFINES+=BLE_NUS_EXAMPLE_EMIT_DEVMODE_SECRET=1 */
            if (emit_secret_on_air) {
                printf("[ref] bento_devmode_emit_provision   : EMITTING the "
                       "secret to the connected peer (link state %s)\r\n",
                       state_str(ble_nus_get_state()));
                bento_devmode_emit_provision();
            } else {
                printf("[ref] bento_devmode_emit_provision   : OFF "
                       "(BLE_NUS_EXAMPLE_EMIT_DEVMODE_SECRET=0) — no secret "
                       "was transmitted\r\n");
            }

            /* WHEN: the desktop sends bento.devmode.lock, or you are done.
             * RETURNS: nothing. Unconditional and instant. Leave the board as
             * you found it. */
            bento_devmode_lock();
            printf("[ref] bento_devmode_lock             : is_unlocked=%s\r\n",
                   bento_devmode_is_unlocked() ? "true" : "false");
            if (bento_devmode_is_unlocked()) soft_fail = 1;
        }
    }

    /*========================================================================
     * E — THE RADIO SCHEDULER
     *
     * WHY IT EXISTS: the CYW55500/55513 silicon can run BLE and WiFi together
     * through its on-die COEX time-division multiplexer, but this firmware
     * deliberately does not enable COEX so MicroPython keeps its GC heap free
     * of WHD's transient buffers. So the radio is ONE resource with two
     * mutually exclusive users, and this module decides which one has it.
     * Nothing else may bring up WiFi behind its back.
     *
     * NOTE ON THE BOOT DECISION: radio_scheduler.h describes boot_mode +
     * saved creds driving the initial mode. The shipped implementation does
     * NOT do that yet — radio_scheduler_init reads both, then explicitly
     * ignores them and parks the mode at BLE_ADV, because boot-time BLE
     * bring-up is owned by bento_buddy_auto_start_install, whose 3-second
     * delay is what the AIROC HCI handshake actually needs (routing it
     * through the scheduler worker fails BTM_ENABLED_EVT — measured
     * 2026-05-26). Verified in radio_scheduler.c. Do not design around the
     * header's Phase 2 description.
     *======================================================================*/
    printf("[ref] ---- radio scheduler ---------------------------------\r\n");

    /* WHEN: anywhere, from any task — it is a lock-free enum read, atomic on
     * Cortex-M33 and safe even from an ISR.
     * RETURNS: UNKNOWN before radio_scheduler_init, one of the live modes
     * after. radio_mode_str never returns NULL and never allocates, so it is
     * safe in a log line. */
    bool sched_was_up = (radio_scheduler_get_mode() != RADIO_MODE_UNKNOWN);

    /* WHEN: once, from main(). COST: it creates a FreeRTOS task at
     * tskIDLE_PRIORITY+3 with a 4096-WORD stack — 16 KB of the FreeRTOS heap
     * — plus a static mutex and work queue, and it NEVER gives them back. If
     * the board's own main.c already called it, the double-init guard returns
     * true and nothing is allocated.
     * RETURNS: true if the scheduler is usable afterwards. */
    if (sched_was_up) {
        printf("[ref] radio_scheduler_init           : SKIPPED — already "
               "initialised (mode=%s), and re-running it would install OUR "
               "persistence hooks over the firmware's own\r\n",
               radio_mode_str(radio_scheduler_get_mode()));
    } else {
        const radio_scheduler_config_t cfg = {
            .persist_boot_mode = ex_persist_boot_mode,
            .load_boot_mode    = ex_load_boot_mode,
        };
        if (!radio_scheduler_init(&cfg)) {
            printf("[ref] radio_scheduler_init           = false — the worker "
                   "task could not be created (FreeRTOS heap exhausted?)\r\n");
            return SDK_EX_UNAVAILABLE;
        }
        printf("[ref] radio_scheduler_init           = true; a 16 KB worker "
               "task now exists for the rest of this boot\r\n");
    }

    radio_mode_t rm = radio_scheduler_get_mode();
    printf("[ref] radio_scheduler_get_mode       = %s\r\n", radio_mode_str(rm));

    /* WHEN: building a status frame or a status screen.
     * RETURNS: by out-parameter, filled under the scheduler's mutex with a
     * 50 ms timeout. ON TIMEOUT the struct is ZEROED except for the mode
     * rather than left stale — so an all-zero ssid/ipv4 can mean "no WiFi" OR
     * "could not take the lock", and you cannot tell them apart.
     *
     * GUARDED HERE ON PURPOSE: before radio_scheduler_init the mutex handle
     * is still NULL and this call takes it unconditionally. In a build with
     * configASSERT compiled out that is a NULL dereference, not an error
     * return. Never call it while get_mode() reports UNKNOWN.
     *
     * The ssid pointer aliases scheduler-owned storage and is valid only
     * until the next mode transition. COPY IT OUT before you yield. */
    radio_status_t rs;
    memset(&rs, 0, sizeof(rs));
    if (rm != RADIO_MODE_UNKNOWN) {
        radio_scheduler_get_status(&rs);
        char ssid_copy[33] = "";
        if (rs.ssid != NULL) {
            strncpy(ssid_copy, rs.ssid, sizeof(ssid_copy) - 1);
        }
        /* ipv4 is in NETWORK byte order, which is why the first octet is the
         * low byte here. nus_commands.c formats it the same way. */
        printf("[ref] radio_scheduler_get_status     : mode=%s ssid=\"%s\" "
               "ipv4=%lu.%lu.%lu.%lu ble_paired=%s wifi_fail_count=%u\r\n",
               radio_mode_str(rs.mode), ssid_copy,
               (unsigned long)(rs.ipv4 & 0xFFu),
               (unsigned long)((rs.ipv4 >> 8) & 0xFFu),
               (unsigned long)((rs.ipv4 >> 16) & 0xFFu),
               (unsigned long)((rs.ipv4 >> 24) & 0xFFu),
               rs.ble_paired ? "true" : "false",
               (unsigned)rs.wifi_fail_count);
    } else {
        printf("[ref] radio_scheduler_get_status     : SKIPPED — scheduler "
               "not initialised, and this call would take a NULL mutex\r\n");
    }

    /* WHEN: rendering an "override" badge next to the radio state.
     * RETURNS: AUTO whenever no load hook is installed OR the hook reports
     * nothing stored. It cannot tell you which of the two happened. */
    printf("[ref] radio_scheduler_get_boot_mode  = %s\r\n",
           boot_mode_str(radio_scheduler_get_boot_mode()));

    /* WHEN: the user picks a boot override in the UI.
     * RETURNS: nothing, and it does nothing at all without a persist hook —
     * there is no in-RAM copy behind it. We only WRITE it when we own the
     * hooks; if the firmware installed its own, a write here would land in
     * the board's real /.radio_boot_mode. */
    if (!sched_was_up) {
        radio_scheduler_set_boot_mode(RADIO_BOOT_FORCE_BLE);
        printf("[ref] radio_scheduler_set_boot_mode  : force_ble -> reads "
               "back %s (into OUR RAM hook, not flash)\r\n",
               boot_mode_str(radio_scheduler_get_boot_mode()));
        radio_scheduler_set_boot_mode(RADIO_BOOT_AUTO);
        printf("[ref] radio_scheduler_set_boot_mode  : restored to %s\r\n",
               boot_mode_str(radio_scheduler_get_boot_mode()));
    } else {
        printf("[ref] radio_scheduler_set_boot_mode  : SKIPPED — the "
               "firmware's own persist hook is installed and this would write "
               "the board's real /.radio_boot_mode\r\n");
    }

    /* WHEN: the user asks for the other radio.
     * RETURNS: true only if the request was QUEUED — never "done"; the
     * transition is async, so poll get_mode or listen on the state hook. It
     * accepts only resting states: WIFI_ACTIVE and BLE_ADV are queued (the
     * SWITCHING_* transients are mapped onto them), and anything else —
     * WIFI_FAILED here — falls through and returns false having queued
     * nothing. It also returns false before init, when the mode is UNKNOWN.
     * This call is safe because it is refused. */
    printf("[ref] radio_scheduler_request_mode   : wifi_failed -> %s "
           "(transients and error states are not requestable)\r\n",
           radio_scheduler_request_mode(RADIO_MODE_WIFI_FAILED)
               ? "true" : "false");

    /* WHEN: you want a push instead of a poll.
     * RETURNS: nothing. There is no getter for the current callback, so
     * installing one is DESTRUCTIVE: whatever main.c registered is gone.
     * Production registers nus_radio_emit_state_event, so install yours, then
     * put THAT back — not NULL, which would silently stop the desktop's radio
     * events. */
    radio_scheduler_set_on_state(ex_on_state);
    printf("[ref] radio_scheduler_set_on_state   : local hook installed\r\n");

    /* WHEN: you want to push the radio state to the desktop by hand.
     * RETURNS: nothing. Emits
     * {"event":"bento.radio.state","mode":"...","ssid":"...","ipv4":"...",
     *  "ble_paired":...,"wifi_fail_count":...}
     * It is NULL-safe and, with no link, is dropped inside nus_emit_event. */
    nus_radio_emit_state_event(&rs);
    nus_radio_emit_state_event(NULL);   /* guarded, returns immediately */
    printf("[ref] nus_radio_emit_state_event     : one frame emitted (and one "
           "NULL, which is a no-op)\r\n");

    radio_scheduler_set_on_state(nus_radio_emit_state_event);
    printf("[ref] radio_scheduler_set_on_state   : production hook restored; "
           "local hook saw %lu event(s), last mode %s\r\n",
           (unsigned long)s_state_events, radio_mode_str(s_last_mode));

    /* WHEN: the desktop provisions WiFi over BLE. THIS WRITES A CREDENTIAL.
     * RETURNS: true only if the credential validated (ssid non-empty and
     * <= 32 chars, password <= 64) AND lfs_save_wifi_creds returned 0. With
     * auto_switch=true it additionally queues a switch to WiFi, which DROPS
     * THE BLE LINK.
     *
     * request_mode(BLE_ADV) is equally live: the worker calls
     * app_wifi_disconnect() and bento_buddy_request_start(), which brings the
     * BLE stack UP.
     *
     * Both are therefore off by default:
     *   make build ENABLE_PAGE_BENTO_BUDDY=1 \
     *              DEFINES+=BLE_NUS_EXAMPLE_ALLOW_RADIO_SWITCH=1 */
    if (allow_radio_switch) {
        printf("[ref] radio_scheduler_set_wifi_creds = %s (goes through YOUR "
               "lfs_save_wifi_creds — the one defined in this file)\r\n",
               radio_scheduler_set_wifi_creds("example-ssid",
                                              "example-password",
                                              "WPA2", false)
                   ? "true" : "false");
        printf("[ref] radio_scheduler_request_mode   : ble_adv -> %s — queued "
               "only; the worker performs the switch and the BLE stack comes "
               "UP\r\n",
               radio_scheduler_request_mode(RADIO_MODE_BLE_ADV)
                   ? "true" : "false");
    } else {
        printf("[ref] radio_scheduler_set_wifi_creds : OFF "
               "(BLE_NUS_EXAMPLE_ALLOW_RADIO_SWITCH=0) — no credential was "
               "written and the radio was not switched\r\n");
    }

    /*========================================================================
     * F — THE SENSOR STREAM
     *
     * TWO THINGS TO KNOW BEFORE YOU CALL ANY OF IT
     *  1. It reads sensors by running MicroPython. Each sample executes a
     *     snippet like "import sensors; print(sensors.bmi270.acceleration())"
     *     through exec_python_capture and parses the printed result.
     *     exec_python_capture is on dist/ble_nus/consumer_must_provide.txt, so
     *     YOU supply it, and on a build with no MicroPython the stream starts
     *     and produces nothing.
     *  2. Which stream ids exist was decided when the ARCHIVE was compiled,
     *     not when you call. The id table is wrapped in BSP_HAS_BMI270 /
     *     BMM350 / DPS368 / SHT40 / CAPSENSE / POTENTIOMETER, and the shipped
     *     libbento_secure.a was built from TESAIoT_KIT_PSE84_AI-Micropython-
     *     BentoClaw. Redefining those flags in YOUR makefile changes nothing
     *     about which ids it accepts. An unknown id returns -1; that is the
     *     only way to discover the set.
     *======================================================================*/
    printf("[ref] ---- sensor stream -----------------------------------\r\n");

    /* WHEN: before a firmware update, or before starting a second stream.
     * RETURNS: true while the single v1 stream is running. */
    if (sensor_stream_is_active()) {
        printf("[ref] sensor_stream_*                : SKIPPED — a stream is "
               "already running, and v1 supports ONE, so starting ours would "
               "replace it\r\n");
    } else {
        /* WHEN: once, to control when the sampler task is created — 2 KB
         * stack at tskIDLE_PRIORITY+1. sensor_stream_start calls it for you.
         * RETURNS: 0 on success, -1 if the mutex or the task could not be
         * created. */
        int rc = sensor_stream_init();
        printf("[ref] sensor_stream_init             = %d\r\n", rc);
        if (rc != 0) {
            printf("[ref] sampler task did not start — FreeRTOS heap "
                   "exhausted\r\n");
            soft_fail = 1;
        } else {
            /* WHEN: the desktop asks for telemetry.
             * RETURNS: 0, or -1 for an unrecognised id — full stop. -1 does
             * NOT mean the sensor is missing or broken; the id simply is not
             * in the compiled-in table. The interval is CLAMPED to
             * [10, 5000] ms silently, and the first sample fires immediately.
             * Starting REPLACES any stream already running: there is no queue
             * and no second slot. */
            printf("[ref] sensor_stream_start            : "
                   "(\"not_a_sensor\",100) -> %d (unknown id)\r\n",
                   sensor_stream_start("not_a_sensor", 100));

            rc = sensor_stream_start("accel", 500);
            printf("[ref] sensor_stream_start            : (\"accel\",500) -> "
                   "%d, is_active=%s\r\n", rc,
                   sensor_stream_is_active() ? "true" : "false");
            if (rc != 0) {
                printf("[ref] \"accel\" is not in this archive's id table — "
                       "it was built without BSP_HAS_BMI270\r\n");
                soft_fail = 1;
            } else {
                /* Let a few samples fire. Each emits a bento.sensor.data
                 * event; with no link every one is refused by ble_nus_send
                 * and counted as a drop. This delay is the only reason the
                 * counter below moves. */
                vTaskDelay(pdMS_TO_TICKS(1200));
            }

            /* WHEN: diagnosing a stream the desktop says is patchy.
             * RETURNS: a monotonic count since boot of samples the NUS TX
             * queue REFUSED — a disconnected link or a full buffer. Never
             * reset. Surfaced in bento.info.board. Large with a healthy link
             * means the interval is faster than the link can carry; slow the
             * stream down rather than chasing the BLE stack. */
            printf("[ref] sensor_stream_dropped_count    = %lu (expected to "
                   "grow on a board with no desktop attached)\r\n",
                   (unsigned long)sensor_stream_dropped_count());

            /* WHEN: the desktop cancels one stream.
             * RETURNS: nothing. It takes an id, but v1 has a single stream
             * and the argument is NOT matched against it — any id stops
             * whatever is running. Idempotent. */
            sensor_stream_stop("accel");
            printf("[ref] sensor_stream_stop             : is_active=%s\r\n",
                   sensor_stream_is_active() ? "true" : "false");

            /* WHEN: quiescing the board — this is what the firmware-update
             * flow calls on Y-approve so the SWD flasher sees a quiet device.
             * RETURNS: nothing. Unconditional, idempotent, safe when nothing
             * is running. Leaving a stream up would make
             * bento.fw.update.begin ack "busy" for everyone else. */
            sensor_stream_stop_all();
            printf("[ref] sensor_stream_stop_all         : is_active=%s\r\n",
                   sensor_stream_is_active() ? "true" : "false");
            if (sensor_stream_is_active()) soft_fail = 1;
        }
    }

    /*========================================================================
     * G — VOICE CAPTURE
     *
     * HONEST STATUS: this is a STUB and the header says so. There is no mic in
     * this path yet. voice_capture_start() emits one second of SYNTHETIC
     * SILENCE as base64 `bento.voice.chunk` frames — 1600 samples per chunk at
     * 16 kHz, so ten chunks — then a terminal frame, and returns. It exists so
     * the desktop can validate its end-to-end parser before the PDM-PCM
     * pipeline lands.
     *======================================================================*/
    printf("[ref] ---- voice capture -----------------------------------\r\n");

    /* WHEN: toggling the mic button label on the LCD.
     * RETURNS: true only while a capture is in progress. With the current
     * stub, capture completes synchronously inside voice_capture_start, so
     * from any single task this only ever reads false. */
    printf("[ref] voice_capture_is_running       = %s (before)\r\n",
           voice_capture_is_running() ? "true" : "false");

    /* WHEN: the user taps the mic.
     * RETURNS: 0, or -1 if a capture is already running or the id is
     * NULL/empty. sample_rate_hz is advisory and the stub ignores it, using
     * VOICE_CAPTURE_DEFAULT_SAMPLE_RATE_HZ.
     *
     * It is SYNCHRONOUS: the whole clip is emitted inside the call, on your
     * task, with no yield. That is a property of the stub, not of the API; a
     * real capture path will be asynchronous and both calls will mean what
     * they say. */
    printf("[ref] voice_capture_start            : (NULL,16000) -> %d (id is "
           "required)\r\n",
           voice_capture_start(NULL, VOICE_CAPTURE_DEFAULT_SAMPLE_RATE_HZ));
    printf("[ref] voice_capture_start            : (\"ref-clip\",%u Hz) -> %d "
           "— emitted 1 s of silence in %u-sample chunks\r\n",
           (unsigned)VOICE_CAPTURE_DEFAULT_SAMPLE_RATE_HZ,
           voice_capture_start("ref-clip",
                               VOICE_CAPTURE_DEFAULT_SAMPLE_RATE_HZ),
           (unsigned)VOICE_CAPTURE_SAMPLES_PER_CHUNK);
    printf("[ref] voice_capture_is_running       = %s (after — false, the "
           "stub finished inside the call)\r\n",
           voice_capture_is_running() ? "true" : "false");

    /* WHEN: the user taps the mic again to end a clip.
     * RETURNS: nothing. Emits a final {"final":true} frame IF a capture is
     * active, so the desktop can close its accumulator. Here there is nothing
     * to close, so it is a no-op. */
    voice_capture_stop();
    printf("[ref] voice_capture_stop             : no-op, nothing was "
           "running\r\n");

    /*========================================================================
     * H — THE CM55 BRIDGE AND THE LAZY BLE START
     *
     * THE IPC RULE THAT MATTERS MOST: NEVER printf from the IPC receive
     * callback. It runs in interrupt context; the retarget-io mutex is not
     * available there and a UART write from it panics the kernel. Everything
     * printed here happens on the calling task, before or after the IPC call
     * — never inside it.
     *
     * WHY A LAZY WRAPPER EXISTS: ble_nus_init/deinit is not a symmetric pair.
     * Stop does NOT tear the AIROC host stack down — calling
     * wiced_bt_stack_deinit from the IPC RX task hard-faulted CM33
     * (ISSUE-029) — so the stack stays resident and a later start just re-arms
     * advertising. The wrapper holds a mutex, remembers whether the stack has
     * ever come up, and picks the cheap path when it has.
     *======================================================================*/
    printf("[ref] ---- CM55 bridge -------------------------------------\r\n");

    /* WHEN: early, before BLE is up, so a Start/Stop tap during boot is not
     * lost. Registers a callback against CM33_IPC_BENTO_BUDDY_CLIENT_ID (5).
     * Without it, a tap on the LCD reaches nothing and the user concludes the
     * button is broken. bento_buddy_request_start() calls it itself on the
     * first-ever bring-up, so an explicit call is only about registering
     * EARLY; re-registering the same client id replaces the entry rather than
     * duplicating it.
     * RETURNS: 0 on success, -1 if Cy_IPC_Pipe_RegisterCallback refused. */
    int rc = ipc_bento_buddy_rx_init();
    printf("[ref] ipc_bento_buddy_rx_init        = %d (client id %lu handles "
           "LCD Start/Stop, device commands, mic taps and Y/N decisions)\r\n",
           rc, (unsigned long)CM33_IPC_BENTO_BUDDY_CLIENT_ID);
    if (rc != 0) soft_fail = 1;

    /* WHEN: pushing anything to the Buddy page on CM55.
     * RETURNS: 0, or -1 when CM55 is not draining the pipe — on a board with
     * no Buddy page open, -1 is the expected answer. One shared-memory slot,
     * one message at a time: the pipe returns busy until CM55 releases the
     * endpoint, so the archive retries once a millisecond for 50 ms and then
     * gives up. That makes it safe to call from a task and UNSAFE from an ISR
     * — it blocks with vTaskDelay. Payload is capped at IPC_DATA_MAX_LEN and
     * TRUNCATED silently past it, which is why the hint text field is
     * documented at BUDDY_HINT_TEXT_MAX (%d) bytes. */
    static const char hint[] = "SDK reference list";
    printf("[ref] ipc_bento_buddy_send(HINT)     = %d (hint field is %d "
           "bytes; longer payloads are truncated in silence)\r\n",
           ipc_bento_buddy_send(IPC_CMD_BUDDY_HINT_TEXT, 60 /* scroll px/s */,
                                (const uint8_t *)hint, sizeof(hint)),
           BUDDY_HINT_TEXT_MAX);

    /* WHEN: the LCD Start button is tapped. THIS STARTS THE BLE RADIO, with
     * every consequence listed in 01_nus_bring_up_and_talk.c: WiFi becomes
     * unusable on the single-RF CYW55500, ~30 KB of FreeRTOS heap is claimed
     * and never returned for the rest of the boot, and the device becomes
     * discoverable on a GATT database whose characteristics carry no
     * authentication requirement.
     * RETURNS: three-way and worth distinguishing —
     *    0  newly started — ble_nus_init ran and succeeded
     *    1  already running — the state was not OFF, nothing was done
     *   -1  failed — ble_nus_init returned false, or the internal mutex timed
     *       out after 1 s (another task is mid start/stop)
     * On the first-ever call it also computes the firmware SHA-256, so the
     * cold-boot timeline of a flag=0 build is unchanged.
     *
     * bento_buddy_request_stop() is the cheap idempotent counterpart: it
     * turns advertising off, and the host stack stays resident and still
     * holds its heap. The internal "stack has been up" flag stays set, which
     * is what makes a restart take the fast re-arm path.
     *
     * OFF by default because this file's title does not say it starts a
     * radio — 01_nus_bring_up_and_talk.c is the example that does:
     *   DEFINES+=BLE_NUS_EXAMPLE_ALLOW_BLE_START=1 */
    if (allow_ble_start) {
        printf("[ref] state before start             = %s\r\n",
               state_str(ble_nus_get_state()));
        rc = bento_buddy_request_start();
        printf("[ref] bento_buddy_request_start      = %d (%s), state = %s\r\n",
               rc,
               rc == 0 ? "newly started" :
               rc == 1 ? "already running" : "FAILED",
               state_str(ble_nus_get_state()));
        if (rc < 0) {
            printf("[ref] BLE did not come up — check the UART for the "
                   "wiced_bt_stack_init result, and confirm `make getlibs` "
                   "fetched the CYW55513 HCD blob\r\n");
            soft_fail = 1;
        }
        bento_buddy_request_stop();
        printf("[ref] bento_buddy_request_stop       : state = %s "
               "(advertising off; the host stack is still resident and still "
               "holds its heap)\r\n", state_str(ble_nus_get_state()));
    } else {
        printf("[ref] bento_buddy_request_start/stop : OFF "
               "(BLE_NUS_EXAMPLE_ALLOW_BLE_START=0) — no radio was "
               "started\r\n");
    }

    /* WHEN: EXACTLY ONCE, from main() before vTaskStartScheduler, when you
     * want a CI runner to get BLE without a human tapping the LCD. It spawns
     * a one-shot task that waits 3 s and then calls request_start. The 3 s is
     * not arbitrary: it lets the scheduler settle and the IPC pipe register,
     * and it leaves a REPL window if a stack change ever wedges bring-up.
     * RETURNS: nothing, and there is no error return.
     *
     * CALLING IT TWICE CORRUPTS IT. It uses xTaskCreateStatic over a
     * file-scope `static StackType_t task_stack[1024]` and a file-scope
     * `static StaticTask_t task_tcb` — a second call hands the scheduler a
     * second task pointing at the SAME TCB and the SAME stack. There is no
     * guard inside it. That is why it is off here: the board's own main.c may
     * already have called it.
     *   DEFINES+=BLE_NUS_EXAMPLE_INSTALL_AUTOSTART=1 */
    if (install_autostart) {
        printf("[ref] bento_buddy_auto_start_install : installing — BLE will "
               "be requested 3 s from now\r\n");
        bento_buddy_auto_start_install();
    } else {
        printf("[ref] bento_buddy_auto_start_install : OFF "
               "(BLE_NUS_EXAMPLE_INSTALL_AUTOSTART=0) — calling it twice in "
               "one boot corrupts its statically allocated task\r\n");
    }

    /*========================================================================
     * I — THE WEAK OVERRIDES, CALLED DIRECTLY
     *
     * They are ordinary functions, so you can see the contract without
     * waiting for the scheduler to reach them.
     *======================================================================*/
    printf("[ref] ---- weak overrides ----------------------------------\r\n");

    char ssid[33], pass[65], sec[16];

    /* Nothing stored yet unless section E's guarded branch ran: load must say
     * false, and the scheduler reads that as "this board is unconfigured". */
    printf("[ref] lfs_load_wifi_creds            = %s (before save)\r\n",
           lfs_load_wifi_creds(ssid, sizeof(ssid), pass, sizeof(pass),
                               sec, sizeof(sec)) ? "true" : "false");

    int src = lfs_save_wifi_creds("example-ssid", "example-password", "WPA2");
    printf("[ref] lfs_save_wifi_creds            = %d (0 = stored, in RAM in "
           "this file — NOT in the board's flash)\r\n", src);
    printf("[ref] lfs_save_wifi_creds            = %d (empty ssid, "
           "refused)\r\n", lfs_save_wifi_creds("", "x", "WPA2"));

    bool got = lfs_load_wifi_creds(ssid, sizeof(ssid), pass, sizeof(pass),
                                   sec, sizeof(sec));
    printf("[ref] lfs_load_wifi_creds            = %s, ssid=\"%s\", "
           "security=\"%s\" (password deliberately not printed)\r\n",
           got ? "true" : "false", got ? ssid : "", got ? sec : "");

    /* A caller buffer too small to hold the stored SSID is a failure, not a
     * truncation. Feed it a 4-byte SSID buffer and watch it refuse. */
    printf("[ref] lfs_load_wifi_creds            = %s (4-byte ssid buffer — "
           "refused rather than truncated)\r\n",
           lfs_load_wifi_creds(tiny, sizeof(tiny), pass, sizeof(pass),
                               sec, sizeof(sec)) ? "true" : "false");

    /* The WiFi seam. These print their refusal instead of failing silently —
     * the single behavioural difference from the weak stubs they replace, and
     * the reason to override them even before you have a driver. */
    printf("[ref] app_wifi_connect_direct        = %d\r\n",
           app_wifi_connect_direct("example-ssid", "example-password", "WPA2"));
    printf("[ref] app_wifi_disconnect            = %d\r\n",
           app_wifi_disconnect());
    printf("[ref] app_wifi_get_ipv4              = %lu (0 = no address; note "
           "NETWORK byte order when you return a real one)\r\n",
           (unsigned long)app_wifi_get_ipv4());

    /* Leave the store empty so a later example does not find a credential
     * this file invented. */
    memset(&s_creds, 0, sizeof(s_creds));
    printf("[ref] credential store cleared\r\n");

    if (src != 0 || !got) soft_fail = 1;

    return soft_fail ? SDK_EX_REFUSED : SDK_EX_OK;
}

#endif /* ENABLE_PAGE_BENTO_BUDDY */
