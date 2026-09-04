/*******************************************************************************
 * File Name: bento_fw.h
 *
 * Description: Bento Desktop Buddy firmware-auto-update handlers (SPEC §5.6).
 *              Owns the state machine for:
 *
 *                bento.fw.query         — cheap read-only metadata probe
 *                bento.fw.update.begin  — Y/N physical-ack + pre-flash prep
 *                bento.fw.update.complete — outbound event emitted on boot
 *
 *              Physical-ack (ISSUE-027): every USB-over-SWD flash triggered
 *              from the desktop side MUST pass through an on-device LCD Y/N
 *              prompt showing the first 8 hex chars of the target SHA-256.
 *              No side-channel flash bypass, no timeout-default-to-yes.
 *
 *              Re-pair model (ISSUE-028): bonds are RAM-only, so every reboot
 *              forces the desktop to re-pair. This module emits
 *              bento.fw.update.complete unconditionally on each first
 *              post-boot NUS connection — the desktop compares to its cached
 *              hash and decides whether to treat the event as "update just
 *              landed" or "just a reboot".
 *
 *              Reference: TESAIoT_PLAN/2026-4/Bento_Buddy/SPEC.md §5.6 +
 *                         §4.8.5 physical-ack +
 *                         ISSUES.md ISSUE-022/-027/-028.
 *
 ******************************************************************************/

#ifndef BENTO_FW_H
#define BENTO_FW_H

#include <stddef.h>
#include <stdint.h>

/* jsmn tokens — forward decl so this header stays light. */
struct jsmntok;
typedef struct jsmntok jsmntok_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Compile-time firmware version. Bumped via git tag on release.
 * 1.0.9 = May 2026 — landed Phase 3 close-out:
 *   * F2.5  — fw_hash now hashes a build-deterministic identity
 *             string (BENTO_BUDDY_FW_VERSION|__DATE__|__TIME__) so
 *             the digest is stable across power-cycles of the same
 *             binary. The previous VTOR-based 128 KB scan hashed
 *             SRAM and produced a different digest every boot.
 *   * F1+F2.5 diagnostic — bento.fw.query response now carries a
 *             `_diag` block with hash + ble counters + uptime_ms
 *             so support can root-cause connectivity reports in a
 *             single round-trip without serial console access.
 *   * FW_TX_BUF 256 → 1024 to fit the new _diag payload.
 */
/* A project that declares BENTO_FW_VERSION in its Makefile wins. The constant
 * below is the fallback for builds that declare nothing - and until 2026-09-02
 * that was every project in the workspace, so an HMI Kit running 0.2.0 told the
 * desktop it was 1.4.0 over both bento.info.board and bento.fw.query. Prefer
 * the project's own number wherever one is supplied. */
#ifdef BENTO_FW_VERSION
#undef  BENTO_BUDDY_FW_VERSION
#define BENTO_BUDDY_FW_VERSION BENTO_FW_VERSION
#endif
#ifndef BENTO_BUDDY_FW_VERSION
#define BENTO_BUDDY_FW_VERSION "1.4.0"
#endif

/* Handler for bento.fw.query. Emits the JSON ack on the NUS link. */
void bento_fw_handle_query(const char *json,
                           const jsmntok_t *toks, int n_toks);

/* Handler for bento.fw.update.begin. Launches the LCD Y/N prompt and emits
 * the final ack (approve → ok:true + sensor streams stopped; decline → error;
 * timeout → error). Rate-limits duplicate prompts within 5 seconds. */
void bento_fw_handle_update_begin(const char *json,
                                  const jsmntok_t *toks, int n_toks);

/* Called from the BLE state-change hook the moment the link transitions to
 * CONNECTED. Emits bento.fw.update.complete ONCE per boot so the desktop can
 * reconcile whatever hash it cached against what's actually running.
 * Idempotent — subsequent calls within the same boot are no-ops. */
void bento_fw_emit_boot_complete(void);

/* Called from ipc_bento_buddy_bridge.c when the CM55 LCD returns a Y/N
 * decision for the current firmware-update prompt. `approve` is non-zero for
 * Y (proceed with flash), zero for N (decline). No-op when there is no
 * pending prompt. */
void bento_fw_on_user_decision(int approve);

#ifdef __cplusplus
}
#endif

#endif /* BENTO_FW_H */
