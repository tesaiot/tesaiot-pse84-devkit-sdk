/*******************************************************************************
 * File Name: fw_hash.h
 *
 * Description: Boot-time SHA-256 of the active firmware image. Used by the
 *              Bento Desktop Buddy firmware-update flow (SPEC §5.6) so the
 *              desktop can compare the running image against a reference and
 *              so the on-device Y/N physical-ack prompt (ISSUE-027) can
 *              display a stable hash prefix.
 *
 *              Implementation strategy (ISSUE-022 mitigation path B):
 *
 *                We do NOT pre-bake the hash into the hex at link time — that
 *                requires a circular reservation + post-link patch step and
 *                adds a fragile tool dependency to the build pipeline. Instead
 *                the firmware computes SHA-256 over a known flash region at
 *                boot and caches the hex string in RAM. The trade-off is a
 *                one-time ~80 ms CPU hit on boot in exchange for no build-
 *                pipeline complexity.
 *
 *              Boundaries:
 *                Hash window covers the CM33_NS application code region only
 *                (start/end derived from symbols exported by the linker
 *                script). Bootloader + HSM regions are excluded because they
 *                sit on a different flash bank and are not part of the image
 *                the desktop can patch via OpenOCD.
 *
 *              Call fw_hash_compute_at_boot() ONCE early in main() before the
 *              Bento Buddy UI comes up. Safe to call multiple times — second
 *              call is a no-op.
 *
 *              Reference: TESAIoT_PLAN/2026-4/Bento_Buddy/SPEC.md §7.6 +
 *                         TESAIoT_PLAN/2026-4/Bento_Buddy/ISSUES.md ISSUE-022.
 *
 ******************************************************************************/

#ifndef FW_HASH_H
#define FW_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-256 hex string length (64 lowercase hex digits + trailing NUL). */
#define FW_HASH_HEX_LEN (65)

/* Compute SHA-256 over the CM33_NS code region and cache the result.
 * Idempotent. Falls back to the sentinel "unknown" if mbedtls is unavailable
 * or the linker symbols cannot be resolved at boot. */
void fw_hash_compute_at_boot(void);

/* Return a pointer to the cached 64-char lowercase hex digest + trailing NUL.
 * Always non-NULL — returns "unknown" before fw_hash_compute_at_boot(). */
const char *fw_hash_hex(void);

/* Return the first 8 hex chars of the cached digest as a NUL-terminated
 * C string into `out8` (MUST be at least 9 bytes). Useful for the LCD
 * physical-ack prompt (ISSUE-027) which only displays a short prefix. */
void fw_hash_prefix8(char *out9);

/* Diagnostic snapshot exposed via `bento.fw.query`'s `_diag` block.
 * Populated each time fw_hash_compute_at_boot() runs. Lets the desktop
 * verify the SHA-256 is deterministic across boots (production support
 * for F2.5 — surfaced after a real-hardware session showed two
 * different hashes for one binary across two power-cycles). */
typedef struct {
    uint32_t  compute_count;     /* total times compute_at_boot() executed */
    uintptr_t vtor_addr;         /* VTOR register value seen on last compute */
    uint8_t   window_head[8];    /* first 8 bytes of the hashed flash window */
} fw_hash_diag_t;

void fw_hash_get_diagnostics(fw_hash_diag_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FW_HASH_H */
