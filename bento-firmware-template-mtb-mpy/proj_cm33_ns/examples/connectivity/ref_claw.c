/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/ref_claw
 * title:   Reference list — every read-only call, and the two module objects
 * teaches: which mpy_secure calls are safe to make from any task at any time,
 *          and how the MicroPython module objects are referenced
 * apis:    claw_https_connected, claw_cb_state, claw_cb_cooldown_remaining,
 *          claw_trust_get, claw_rate_check, claw_session_count,
 *          claw_session_dirty, claw_session_build_context, claw_memory_get,
 *          lfs_wifi_creds_ready, lfs_wifi_creds_needs_resave,
 *          tacp_ring_buf_readable, bento_link_at, bento_link_get,
 *          mp_module_edge_ai, mp_module_optiga
 * entry:   example_mpy_secure_reference
 */

/*******************************************************************************
 * THIS IS A REFERENCE LIST, NOT A JOB.
 *
 * It performs no task. It is one function that touches every mpy_secure call
 * that only READS state, so you can see in one place what is safe to poll —
 * from any task, at any time, without owning a bus, a port or the interpreter.
 * Copy the line you need out of it; do not copy the whole function.
 *
 * The working examples are 01..10 in this directory. If you are looking for how
 * to DO something, they are the files you want.
 *
 * NOTHING HERE MUTATES, with one honest exception noted at its call site.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "bento_link.h"
#include "claw_transport.h"
#include "claw_safety.h"
#include "claw_session.h"
#include "lfs_wifi_creds.h"
#include "tacp.h"

#include "../sdk_examples_cm33.h"

/* ── The two MicroPython module objects ───────────────────────────────────
 *
 * mp_module_edge_ai and mp_module_optiga are not functions. They are const
 * mp_obj_module_t instances — the `edge_ai` and `optiga` modules an `import`
 * resolves to — and the archive exports them so the port's module table can
 * point at them. Confirmed at
 *   BENTO-TESAIoT-libraries/claw/common/mpy/modedgeai.c:954
 *   BENTO-TESAIoT-libraries/claw/common/mpy/modoptiga.c:1771
 * where each is followed by MP_REGISTER_MODULE(MP_QSTR_<name>, <object>).
 *
 * The VM's table stores them BY ADDRESS — MP_ROM_PTR(&mp_module_edge_ai) — and
 * that is the only way a consumer ever refers to them. The address is all we
 * need, so the type is left incomplete here rather than dragging py/obj.h and
 * the whole MicroPython configuration into an SDK example. A struct tag is
 * enough to take an address, and it cannot be mis-sized because it is never
 * dereferenced.
 *
 * You do not normally write these lines. MP_REGISTER_MODULE does it, and the
 * generated table is what the interpreter reads. Write them only if you are
 * building the module table by hand.
 */
struct _mp_obj_module_t;
extern const struct _mp_obj_module_t mp_module_edge_ai;
extern const struct _mp_obj_module_t mp_module_optiga;

static char s_ctx[256];
static char s_val[CLAW_MEM_VAL_MAX];

int example_mpy_secure_reference(void);

int example_mpy_secure_reference(void)
{
    printf("\r\n--- mpy_secure/ref_mpy_secure (reference list) ---\r\n");

    /* ── Transport ──────────────────────────────────────────────────────── */

    /* Is a session open? Ask before every request; a disconnect callback can
     * clear this behind your back when the peer drops. */
    printf("  claw_https_connected()          = %s\r\n",
           claw_https_connected() ? "true" : "false");

    /* ── Safety ─────────────────────────────────────────────────────────── */

    /* For a status screen. NOTE, the one exception to "nothing here mutates":
     * reading the breaker moves it OPEN -> HALFOPEN once the cooldown expires.
     * See example 04. */
    printf("  claw_cb_state()                 = %d\r\n", (int)claw_cb_state());

    /* Milliseconds until the breaker will let a probe through. 0 in every state
     * but OPEN — so 0 means "not waiting", never "0 ms left". */
    printf("  claw_cb_cooldown_remaining()    = %lu ms\r\n",
           (unsigned long)claw_cb_cooldown_remaining());

    /* Which transport the current session was opened by. Use it to label the
     * UI; use claw_trust_allows() to make decisions. */
    printf("  claw_trust_get()                = %d\r\n", (int)claw_trust_get());

    /* Would this tool be allowed right now? A pure question — it does not
     * consume budget. claw_rate_record() is what consumes it. Pass a name with
     * static storage duration: the limiter keeps the pointer. */
    printf("  claw_rate_check(\"probe\")        = %s\r\n",
           claw_rate_check("probe") ? "true" : "false");

    /* ── Session and memory (all RAM; safe off the MicroPython task) ────── */

    printf("  claw_session_count()            = %u of %u\r\n",
           (unsigned)claw_session_count(), (unsigned)CLAW_SESSION_MAX_MSGS);

    /* "Is there anything that would be lost on a reset?" Covers the message
     * ring AND the key-value store. The cheap check before a flush. */
    printf("  claw_session_dirty()            = %s\r\n",
           claw_session_dirty() ? "true" : "false");

    /* Assemble memory + recent messages into a prompt. Returns bytes written;
     * a return equal to your buffer size means the tail was dropped. */
    memset(s_ctx, 0, sizeof(s_ctx));
    printf("  claw_session_build_context()    = %u byte(s)\r\n",
           (unsigned)claw_session_build_context(s_ctx, sizeof(s_ctx)));

    /* A fact that outlives the conversation. false = not present, and the
     * output buffer is left untouched on a miss. */
    memset(s_val, 0, sizeof(s_val));
    printf("  claw_memory_get(\"owner\")        = %s\r\n",
           claw_memory_get("owner", s_val, sizeof(s_val)) ? "found" : "absent");

    /* ── Credential store ───────────────────────────────────────────────── */

    /* false = the MicroPython VFS is not mounted yet (or deinit() has run), NOT
     * "there are no saved networks". */
    printf("  lfs_wifi_creds_ready()          = %s\r\n",
           lfs_wifi_creds_ready() ? "true" : "false");

    /* true = the file still carries the old XOR-32 checksum. Read then write
     * once, on the MicroPython task, to migrate it. */
    printf("  lfs_wifi_creds_needs_resave()   = %s\r\n",
           lfs_wifi_creds_needs_resave() ? "true" : "false");

    /* ── Host protocol ──────────────────────────────────────────────────── */

    /* "Has the host sent us anything?" Reads two ring indices; it does not take
     * the port and does not consume a byte. tacp_ring_buf_read() does both. */
    printf("  tacp_ring_buf_readable()        = %s\r\n",
           tacp_ring_buf_readable() ? "true" : "false");

    /* ── Transport registry ─────────────────────────────────────────────── */

    /* Enumerate: index 0..n-1, NULL past the end. */
    unsigned n = 0u;
    while (bento_link_at(n) != NULL) {
        n++;
    }
    printf("  bento_link_at() enumerates      = %u backend(s)\r\n", n);

    /* Or ask for one by name. NULL means "not on this board's persona", which
     * is a normal answer, not a failure. */
    printf("  bento_link_get(\"ipc\")           = %s\r\n",
           (bento_link_get("ipc") != NULL) ? "present" : "NULL");

    /* ── The MicroPython module objects ─────────────────────────────────── */

    printf("  &mp_module_edge_ai              = %p\r\n",
           (const void *)&mp_module_edge_ai);
    printf("  &mp_module_optiga               = %p\r\n",
           (const void *)&mp_module_optiga);
    printf("    (the `edge_ai` and `optiga` modules an import resolves to;\r\n"
           "     the VM's table holds these addresses, nothing more)\r\n");

    return SDK_EX_OK;
}
