/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/02_link_ipc_backend
 * title:   Bring up the IPC backend and pull data back from CM55
 * teaches: the bidirectional QUERY — send a request, wait for CM55 to fill a
 *          shared-memory response, and why that response must be SHAREDMEM
 * apis:    bento_link_ipc_init, bento_link_ipc_query, bento_link_get
 * entry:   example_mpy_secure_link_ipc
 */

/*******************************************************************************
 * "ipc" is the one bento_link backend that ships in v1. It carries the
 * DEEPCraft-shaped control plane (u8 cmd + u32 value) between CM33_NS and the
 * inference engine on CM55.
 *
 * TWO ENTRY POINTS, AND NEITHER IS IN A NORMAL HEADER
 * ---------------------------------------------------
 * bento_link_ipc_init()  — prototype recovered in bento_secure_undeclared.h,
 *                          which the release pipeline generates by copying the
 *                          declaration verbatim from the caller that already
 *                          has it.
 *
 *                          MIND THE NAME COLLISION. Every module ships a file
 *                          with that exact name, each with its own contents,
 *                          and all of their include dirs are on one -I list.
 *                          `#include "bento_secure_undeclared.h"` therefore
 *                          resolves to whichever -I comes first — ble_nus, in
 *                          the default order — and #pragma once then keeps the
 *                          mpy_secure copy out. So the include below is kept
 *                          for provenance and the one prototype it would have
 *                          supplied is ALSO written out. A duplicate identical
 *                          declaration is legal C; a missing one is a
 *                          -Wimplicit-function-declaration error under -Werror.
 * bento_link_ipc_query() — no shipped declaration at all. Its true signature,
 *                          confirmed by reading (never modifying)
 *                          BENTO-TESAIoT-libraries/claw/common/bento_link/
 *                          bento_link_ipc.c:117 and the matching extern in
 *                          claw/common/mpy/modedgeai.c:78, is:
 *
 *                              bool bento_link_ipc_query(uint8_t sub,
 *                                                        uint32_t value,
 *                                                        ipc_model_link_resp_t *resp);
 *
 *                          It is declared below because nothing else declares it.
 *
 * THE RESPONSE BUFFER MUST LIVE IN SHARED MEMORY
 * ----------------------------------------------
 * Only the POINTER crosses the pipe; CM55 writes the body in place and then
 * sets resp->ready. A buffer on this task's stack, or in ordinary CM33 SRAM
 * CM55 cannot see, gives a query that always times out. CY_SECTION_SHAREDMEM
 * puts it in the region both cores map — and that region is 4 KB and has been
 * over-full before, so keep exactly one of these and reuse it, as the shipped
 * MicroPython module does.
 *
 * WHAT init() ACTUALLY DOES, in order (bento_link_ipc.c:212)
 *   creates the RX queue -> creates the "mlink_rx" delivery task ->
 *   registers the pipe callback -> registers itself as "ipc" in the registry.
 * So after a true return, bento_link_get("ipc") finds it. Calling it a second
 * time returns true immediately without redoing any of that, which makes it
 * safe to call from every module that needs the link.
 *
 * COST OF A QUERY: bounded, ~500 ms worst case, and it BUSY-WAITS. It pumps the
 * TACP/REPL UART while it spins so a Ctrl-C still arrives — which also means a
 * query consumes bytes destined for the REPL. Do not call it in a tight loop
 * from a task that is not the one that owns the REPL.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "cy_pdl.h"                 /* CY_SECTION_SHAREDMEM */
#include "bento_link.h"
#include "bento_secure_undeclared.h"  /* provenance — see the note above */
#include "ipc_model_link_defs.h"      /* ipc_model_link_resp_t, MODEL_LINK_Q_* */

#include "../sdk_examples_cm33.h"

/* Recovered in dist/mpy_secure/include/bento_secure_undeclared.h; repeated here
 * because the -I order above means that file is not the one the preprocessor
 * finds. Identical to the declaration in it, character for character. */
extern bool bento_link_ipc_init(void);

/* The one symbol with no shipped declaration anywhere. Signature confirmed
 * against bento_link_ipc.c:117 (definition) and modedgeai.c:78 (the extern the
 * firmware itself links against). */
bool bento_link_ipc_query(uint8_t sub, uint32_t value,
                          ipc_model_link_resp_t *resp);

/* One shared-memory response, reused by every query below. */
CY_SECTION_SHAREDMEM static ipc_model_link_resp_t s_resp;

static const char *resp_status_str(uint8_t status)
{
    switch (status) {
        case MODEL_LINK_RESP_OK:          return "OK";
        case MODEL_LINK_RESP_BAD_INDEX:   return "BAD_INDEX";
        case MODEL_LINK_RESP_ENGINE_DEAD: return "ENGINE_DEAD";
        default:                          return "unknown";
    }
}

int example_mpy_secure_link_ipc(void);

int example_mpy_secure_link_ipc(void)
{
    printf("\r\n--- mpy_secure/02_link_ipc_backend ---\r\n");

    /* 1. Bring the backend up. Idempotent: true also means "already up". */
    if (!bento_link_ipc_init()) {
        /* The queue, the task, or the pipe-callback registration failed. All
         * three are resource failures at boot, not transient conditions — there
         * is nothing to retry. */
        printf("  bento_link_ipc_init() = false — no RX queue / task / pipe "
               "callback. The IPC link is unavailable this boot.\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  bento_link_ipc_init() = true\r\n");

    /* 2. It registered itself under "ipc". Proving that is worth one line:
     *    it is the contract every other module relies on. */
    bento_link_t *ipc = bento_link_get("ipc");
    if (ipc == NULL) {
        printf("  init() succeeded but bento_link_get(\"ipc\") = NULL\r\n");
        return SDK_EX_UNAVAILABLE;
    }
    printf("  registered as \"%s\": mtu=%u bandwidth=%lu bps flags=0x%02X\r\n",
           ipc->name, (unsigned)ipc->mtu,
           (unsigned long)ipc->bandwidth_bps, (unsigned)ipc->flags);

    /* 3. A QUERY. Ask CM55 how many Edge AI models it has registered.
     *
     *    false means CM55 never set resp.ready inside the budget — the engine
     *    is not running, or another send was in flight. It does NOT mean the
     *    answer was zero, which is why the return value is checked before the
     *    payload is read. */
    memset((void *)&s_resp, 0, sizeof(s_resp));
    if (!bento_link_ipc_query(MODEL_LINK_Q_COUNT, 0u, &s_resp)) {
        printf("  Q_COUNT: no answer within the budget — CM55 inference task "
               "is not running, or a send was already in flight\r\n");
        return SDK_EX_NO_DATA;
    }
    printf("  Q_COUNT: status=%s count=%ld\r\n",
           resp_status_str(s_resp.status), (long)s_resp.u.count);

    /* 4. Which model is loaded right now. Q_ACTIVE answers with the model whose
     *    init() has COMPLETED, and -1 when none has. Q_REQUESTED (0x05) answers
     *    with the one last asked for — the pair is how a caller tells "still
     *    loading" from "load failed". */
    memset((void *)&s_resp, 0, sizeof(s_resp));
    if (bento_link_ipc_query(MODEL_LINK_Q_ACTIVE, 0u, &s_resp)) {
        if (s_resp.u.count < 0) {
            printf("  Q_ACTIVE: no model loaded\r\n");
        } else {
            printf("  Q_ACTIVE: model index %ld is loaded\r\n",
                   (long)s_resp.u.count);
        }
    } else {
        printf("  Q_ACTIVE: no answer within the budget\r\n");
    }

    /* 5. A descriptor, if there is at least one model. Q_MODEL takes the index
     *    as the sub-argument, and BAD_INDEX is a normal answer, not a fault. */
    memset((void *)&s_resp, 0, sizeof(s_resp));
    if (bento_link_ipc_query(MODEL_LINK_Q_MODEL, 0u, &s_resp)) {
        if (s_resp.status == MODEL_LINK_RESP_OK) {
            /* .name is a fixed 48-byte field; print it length-bounded rather
             * than trusting a terminator that a truncated name would not have. */
            printf("  Q_MODEL[0]: sensor=%u classes=%u name=\"%.*s\"\r\n",
                   (unsigned)s_resp.u.desc.sensor,
                   (unsigned)s_resp.u.desc.class_count,
                   (int)sizeof(s_resp.u.desc.name), s_resp.u.desc.name);
        } else {
            printf("  Q_MODEL[0]: status=%s (no model at index 0)\r\n",
                   resp_status_str(s_resp.status));
        }
    } else {
        printf("  Q_MODEL[0]: no answer within the budget\r\n");
    }

    return SDK_EX_OK;
}
