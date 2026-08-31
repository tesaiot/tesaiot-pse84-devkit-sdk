/*******************************************************************************
 * File Name        : ipc_model_link_defs.h
 *
 * Description      : Shared CM33<->CM55 definitions for the MODEL LINK — the
 *                    IPC backend of bento_link, carrying the DEEPCraft-style
 *                    control plane (and, later, bulk-data ring negotiation).
 *
 *                    DESIGN (per the MPY_Sensor_DEEPCraft_Integration plan):
 *                    the model link owns DEDICATED pipe client IDs with its own
 *                    per-client opcode namespace (precedent: ipc_bento_buddy —
 *                    Cy_IPC_Pipe dispatch indexes by client_id, so opcodes here
 *                    can never collide with the shared 0x0..0xFF service band).
 *                    DEEPCraft wire bytes (0x82/0x83/0xA0/0xA2-0xA4/0xE1)
 *                    travel ONLY as payload inside MODEL_CTRL — never as pipe
 *                    opcodes (avoids the 0xE1/LCD collision class entirely).
 *
 * Related          : common/bento_link/  (transport layer)
 *                    common/deepcraft/   (vendored Infineon engine, MIT)
 *******************************************************************************/

#ifndef IPC_MODEL_LINK_DEFS_H
#define IPC_MODEL_LINK_DEFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol version (negotiated once per session at MODEL_CTRL START) ──── */
#define BENTO_MODEL_LINK_VERSION        (1u)

/* ── Dedicated pipe client IDs (verified free across all projects,
 *    2026-07-19: CM55 uses 3-8; CM33 uses 1-5,9) ─────────────────────────── */
#define CM55_IPC_MODEL_LINK_CLIENT_ID   (9UL)
#define CM33_IPC_MODEL_LINK_CLIENT_ID   (6UL)

/* ── Per-client opcode namespace (only meaningful for these client IDs) ──── */
#define MODEL_LINK_OP_CTRL              (0x01u)  /* control plane: u8 cmd + u32 value */
#define MODEL_LINK_OP_DATA              (0x02u)  /* data plane: framed stream chunk   */
#define MODEL_LINK_OP_DATA_ACK          (0x03u)  /* flow control (reserved, v1 unused)*/
#define MODEL_LINK_OP_QUERY             (0x04u)  /* bidir pull: .value = &ipc_model_link_resp_t (SHAREDMEM),
                                                  * .cmd = MODEL_LINK_Q_*, .seq = sub-argument (e.g. model index).
                                                  * CM55 fills the resp in place + sets resp.ready (modui pattern) */

/* ── Data-plane stream IDs ───────────────────────────────────────────────── */
#define MODEL_LINK_STREAM_MIC_PCM       (0u)
#define MODEL_LINK_STREAM_IMU_6AX       (1u)
#define MODEL_LINK_STREAM_MODEL_SCORES  (2u)
#define MODEL_LINK_STREAM_RADAR_FRAME   (3u)     /* reserved                          */

/* ── Control-plane message (fits a pipe message; no shared-mem pointers) ─── */
typedef struct __attribute__((packed)) {
    uint32_t client_id;     /* destination client id (pipe header)             */
    uint32_t intr_mask;     /* pipe interrupt mask                             */
    uint8_t  opcode;        /* MODEL_LINK_OP_*                                 */
    uint8_t  cmd;           /* control plane: DEEPCraft wire byte / intent     */
    uint16_t seq;           /* data plane: chunk sequence                      */
    uint32_t value;         /* control plane payload value                     */
} ipc_model_link_msg_t;

/* DEEPCraft wire bytes ride in .cmd — intent indices are the values BELOW
 * 0x80; everything >= 0x80 is a protocol command (START/STOP/READY/...).
 * Guard the boundary so a future intent table can never alias a command. */
#define MODEL_LINK_INTENT_MAX           (0x7Fu)
_Static_assert(0x82u > MODEL_LINK_INTENT_MAX,
               "DEEPCraft command bytes must stay above the intent range");

/* Release a loaded model: finalize() it and clear its ready flag, so the next
 * SELECT takes the cold-init path instead of feeding a freed handle. The model
 * index rides in .value.
 *
 * A command byte with a payload, not a SELECT-style BASE + n band. The band
 * looks like it has room -- SELECT is 0x90 + n with n < 0x10 and only 0..5 and
 * 13..15 are spoken for -- but n = 6..12 is the registry's declared growth room:
 * KEYWORD_ROW, FALL_ROW and GESTURE_ROW are already written into s_models[]
 * behind build flags, and enabling any of them claims those numbers. Borrowing
 * one would make the meaning of an index depend on a flag in another file, in a
 * project whose whole purpose is adding more models. */
#define MODEL_LINK_CMD_UNLOAD           (0x84u)
/* 0x85 is MODEL_LINK_CMD_LOAD_STAGED (ipc_model_stage_defs.h). */
#define MODEL_LINK_CMD_LOAD_STAGED_     (0x85u)  /* mirror, for the asserts below */
_Static_assert(MODEL_LINK_CMD_UNLOAD > MODEL_LINK_INTENT_MAX,
               "UNLOAD must sit above the intent range");
_Static_assert(MODEL_LINK_CMD_UNLOAD != 0x82u && MODEL_LINK_CMD_UNLOAD != 0x83u,
               "UNLOAD must not alias a DEEPCraft wire byte");

/* ── Edge AI registry menu (MicroPython 'edge_ai') ────────────────────────
 * Additive to the DEEPCraft control plane; START/STOP/INTENT are unchanged.
 *
 * SELECT rides in .cmd on the ordinary MODEL_LINK_OP_CTRL plane, so a lost
 * SELECT is detected by a follow-up Q_ACTIVE query (ack-by-observation), never
 * silent — the same reliability contract the on-screen page has by reading
 * ai_engine_active() directly. Registry reads use the OP_QUERY bidirectional
 * PULL (shared-mem response, no unacknowledged push) so the wedge class from
 * MODEL_SWITCHING_DESIGN §1-3 cannot recur. */
/* SELECT by index-in-payload. The registry ceiling, removed.
 *
 * SELECT_BASE below encodes the model index INSIDE the command byte, so it can
 * address 0x90..0x9F and no further -- and 13/14/15 are the parallel sets, so
 * the real registry ceiling was 13 rows: 6 compiled, 7 run-time. That limit was
 * never about memory. Measured on hardware 2026-08-10, a fully loaded staged
 * model costs ~52 KB (31 KB resident + a ~21 KB arena) against 1.63 MB of free
 * CM55 heap -- room for roughly thirty.
 *
 * .value is a uint32 that has always been in the message. Putting the index
 * there costs one byte of opcode space and removes the ceiling entirely.
 *
 * The old band stays. It is what the on-screen page and every shipped
 * MicroPython program already speak, and breaking it to save a byte would be a
 * poor trade. Both routes land in the same ai_engine_start(). */
#define MODEL_LINK_CMD_SELECT_IDX       (0x86u)  /* .value = model index      */

/* The parallel-set pseudo-indices, as the ENGINE spells them.
 *
 * They live here rather than only in ai_engine.h because both sides of the wire
 * need them and only one of them compiles ai_engine.h. When they moved off
 * 13/14/15 -- to let the registry grow past thirteen rows -- CM55 translated the
 * legacy band on arrival while MicroPython went on confirming against the OLD
 * number, so every Sound Watch select reported "not confirmed" on a board that
 * had switched correctly. The translation and the check were on opposite sides
 * of the link with no shared definition between them. This is that definition;
 * ai_engine.h static-asserts against it. */
#define MODEL_LINK_SET_ALL              (252u)  /* every registered model at once */
#define MODEL_LINK_SET_INTRUDER         (253u)
#define MODEL_LINK_SET_ROOM             (254u)
#define MODEL_LINK_SET_MIC              (255u)

/* What callers have always typed, and still may. */
#define MODEL_LINK_SET_LEGACY_INTRUDER  (13u)
#define MODEL_LINK_SET_LEGACY_ROOM      (14u)
#define MODEL_LINK_SET_LEGACY_MIC       (15u)

/* The one translation, so it cannot be written twice and drift. */
static inline uint32_t model_link_resolve_set(uint32_t n)
{
    switch (n) {
        case MODEL_LINK_SET_LEGACY_INTRUDER: return MODEL_LINK_SET_INTRUDER;
        case MODEL_LINK_SET_LEGACY_ROOM:     return MODEL_LINK_SET_ROOM;
        case MODEL_LINK_SET_LEGACY_MIC:      return MODEL_LINK_SET_MIC;
        default:                             return n;
    }
}
_Static_assert(MODEL_LINK_CMD_SELECT_IDX > MODEL_LINK_INTENT_MAX,
               "SELECT_IDX must sit above the intent range");
_Static_assert(MODEL_LINK_CMD_SELECT_IDX != 0x82u && MODEL_LINK_CMD_SELECT_IDX != 0x83u,
               "SELECT_IDX must not alias a DEEPCraft wire byte");
_Static_assert(MODEL_LINK_CMD_SELECT_IDX != MODEL_LINK_CMD_UNLOAD &&
               MODEL_LINK_CMD_SELECT_IDX != MODEL_LINK_CMD_LOAD_STAGED_,
               "SELECT_IDX must not alias another command");

/* Define a parallel set's members explicitly, by registry index.
 *
 * Three commands rather than one, because a set is up to eight members and
 * OP_CTRL carries a single uint32. Packing eight indices into 32 bits would
 * mean five bits each and a ceiling that moves the next time the registry
 * grows -- the same mistake as encoding the model index in the opcode, which
 * capped the registry at seven until today.
 *
 * BEGIN clears a pending list, ADD appends one index, COMMIT applies it whole.
 * Nothing is visible to the inference task until COMMIT, so a set is never
 * observed half-defined. A dropped ADD is caught by Q_SET afterwards: the
 * caller reads the membership back and compares, which is the same
 * ack-by-observation contract SELECT uses. */
#define MODEL_LINK_CMD_SET_BEGIN        (0x87u)  /* .value = set index        */
#define MODEL_LINK_CMD_SET_ADD          (0x88u)  /* .value = model index      */
#define MODEL_LINK_CMD_SET_COMMIT       (0x89u)  /* .value = set index        */
_Static_assert(MODEL_LINK_CMD_SET_BEGIN  > MODEL_LINK_CMD_SELECT_IDX &&
               MODEL_LINK_CMD_SET_COMMIT < 0x90u,
               "the set commands must sit between SELECT_IDX and the SELECT band");

#define MODEL_LINK_CMD_SELECT_BASE      (0x90u)  /* SELECT(n) = 0x90 + n  (n < 0x10) */
_Static_assert(MODEL_LINK_CMD_UNLOAD < MODEL_LINK_CMD_SELECT_BASE,
               "UNLOAD must stay below the SELECT band");
_Static_assert(MODEL_LINK_CMD_SELECT_BASE + 0x10u <= 0xA0u,
               "SELECT band must stay below the 0xA0 event band");

/* QUERY sub-codes ride in .cmd when opcode == MODEL_LINK_OP_QUERY */
#define MODEL_LINK_Q_COUNT              (0x01u)  /* -> resp.u.count                   */
#define MODEL_LINK_Q_MODEL              (0x02u)  /* .seq = index -> resp.u.desc       */
#define MODEL_LINK_Q_RESULT             (0x03u)  /* -> resp.u.result                  */
#define MODEL_LINK_Q_ACTIVE             (0x04u)  /* -> resp.u.count = active(loaded) | -1 */
#define MODEL_LINK_Q_REQUESTED          (0x05u)  /* -> resp.u.count = requested | -1  */
#define MODEL_LINK_Q_ML_STATE           (0x06u)  /* -> resp.u.count = mtb_ml_get_init_state() */
#define MODEL_LINK_Q_DIAG               (0x07u)  /* -> resp.u.diag                    */
#define MODEL_LINK_Q_SET                (0x08u)  /* .seq = set index -> resp.u.set    */
/* One class label per query: .seq = (model_index << 4) | label_index.
 *
 * Q_MODEL used to carry all eight labels in one response. At 16 bytes each
 * that was 146 bytes and it fit; at 48 -- which is what Thai needs, since
 * 16 bytes is five Thai characters -- it became 434, and .cy_sharedmem on
 * CM33 is a 4 KB region that was ALREADY 116% full before this feature
 * existed. The linker reports that as a percentage in a table, not as an
 * error, so the build passed, the flash passed, and the control plane broke
 * at run time while queries kept answering.
 *
 * Splitting the labels out makes the descriptor 52 bytes -- smaller than the
 * 146 it replaced -- so Thai labels cost the shared region nothing. The price
 * is one extra round trip per label, on a path that runs when a menu is built,
 * not per inference. */
#define MODEL_LINK_Q_LABEL              (0x09u)
#define MODEL_LINK_LABEL_SEQ(model, lbl)  (uint16_t)(((model) << 4) | ((lbl) & 0xFu))
/* Q_ACTIVE returns ai_engine_active() (s_current, the model whose init() has
 * COMPLETED); Q_REQUESTED returns ai_engine_requested() (s_active, the model
 * SELECT last asked for, set the instant CM55 accepts the command and reset to
 * -1 if that model's init() fails). edge_ai.select() confirms on Q_ACTIVE but
 * uses Q_REQUESTED to fail fast when a cold init() fails instead of waiting out
 * its multi-second load ceiling.
 *
 * Q_ML_STATE returns the ML middleware's init refcount. It is read-only and it
 * exists because that one number decides whether any teardown experiment is
 * interpretable. mtb_ml_deinit() is refcounted across EVERY model -- all six
 * share it, the three eval archives included -- and only the 1 -> 0 transition
 * runs the branch that soft-resets the NPU, NULLs the Ethos-U driver's
 * singleton semaphore and calls Cy_SysEnableU55(false). Unloading one model out
 * of six takes the count 6 -> 5 and touches none of that.
 *
 * So a teardown test that does not watch this counter cannot tell "the model
 * link survived a teardown" from "the teardown never happened". That is the
 * whole reason this query exists, and it is why it was added before any unload
 * capability was written. */

/* QUERY response status */
#define MODEL_LINK_RESP_OK              (0u)
#define MODEL_LINK_RESP_BAD_INDEX       (1u)
#define MODEL_LINK_RESP_ENGINE_DEAD     (2u)

/* Wire-size limits mirror ai_engine.h (AI_MAX_CLASSES / AI_MAX_LABEL_LEN); kept
 * as literals here so this shared header never pulls the CM55-only engine. */
#define MODEL_LINK_MAX_CLASSES          (8u)
/* Names get their own, larger budget. Sharing the label length truncated
 * "Motion Detection" (16 chars) to "Motion Detectio" and "Baby Cry Detection"
 * to "Baby Cry Detect" -- two of six shipped models, visible in
 * edge_ai.models() and on the page, and three more sat exactly one character
 * from the cliff. In Thai, at 3 bytes per character, 16 bytes was five
 * characters and the problem was not cosmetic. */
#define MODEL_LINK_MAX_NAME_LEN         (48u)
#define MODEL_LINK_MAX_LABEL_LEN        (48u)

/* Bidirectional QUERY response. Lives in CM33 shared memory (placed
 * CY_SECTION_SHAREDMEM by the caller); only the pointer crosses the pipe, so
 * the ~146 B descriptor never hits the pipe MTU (same as ui_ipc_resp). */
typedef struct __attribute__((packed)) {
    volatile uint8_t ready;             /* 0 -> CM55 sets 1 when filled       */
    uint8_t  status;                    /* MODEL_LINK_RESP_*                   */
    union {
        int32_t count;                                          /* Q_COUNT / Q_ACTIVE */
        struct {
            uint8_t sensor;
            uint8_t class_count;
            char    name[MODEL_LINK_MAX_NAME_LEN];
        } desc;                                                 /* Q_MODEL            */
        struct {
            char    text[MODEL_LINK_MAX_LABEL_LEN];
        } label;                                                /* Q_LABEL            */
        struct {
            uint8_t  index;
            uint8_t  top;
            uint8_t  class_count;
            uint8_t  running;
            uint8_t  score_pct[MODEL_LINK_MAX_CLASSES];         /* 0..100 per class   */
            uint32_t latency_us;
            uint32_t seq;
        } result;                                              /* Q_RESULT           */
        struct {
            uint32_t ml_state;      /* mtb_ml_get_init_state()                */
            uint32_t npu_cycles_lo; /* mtb_ml_npu_cycles, low  32             */
            uint32_t npu_cycles_hi; /* mtb_ml_npu_cycles, high 32             */
            uint32_t stale_drops;   /* verdicts discarded as timed-out        */
            int32_t  last_init_rc;  /* the model's own init() return code     */
            uint32_t unload_done;
            uint32_t unload_refused;
            /* CM55 heap, counting the sbrk headroom the allocator has not
             * claimed yet -- mallinfo().fordblks alone under-reports a young
             * heap badly. Added because every memory decision on that core was
             * being made from a linker script rather than a measurement, and
             * twice that guesswork ended in a hard fault. Fits inside the
             * existing union: desc is 146 B and diag was 28. */
            uint32_t cm55_heap_free;
            uint32_t staged_loaded;    /* models built from an upload           */
            uint32_t staged_rejects;   /* uploads refused, never silent         */
            int32_t  staged_last_rc;   /* why the last one was refused          */
            /* The CTRL-plane wedge counters. deepcraft_task has carried these
             * since the model-switching investigation and they were readable
             * only by halting the core with a debugger. Surfacing them costs
             * 16 bytes in a response that just shed 384, and they answer the
             * one question a silent control plane raises: did the command
             * arrive, was it queued, was it dequeued. */
            uint32_t ctrl_rx;
            uint32_t ctrl_queued;
            uint32_t ctrl_dropped;
            uint32_t ctrl_handled;
            uint32_t cb_entries;   /* every callback entry, before any test  */
            uint32_t q_seen;
            uint32_t other_seen;   /* opcode was neither QUERY nor CTRL      */
            uint32_t last_opcode;
            uint32_t loop_iters;   /* the task is alive iff this moves       */
            uint32_t phase;
            uint32_t last_cmd;
            uint32_t stack_free;   /* words; the 2 KB stack is the suspect   */
        } diag;                                                /* Q_DIAG             */
        struct {
            uint8_t defined;    /* 1 if membership was set at run time     */
            uint8_t count;
            uint8_t idx[MODEL_LINK_MAX_CLASSES];
        } set;                                                 /* Q_SET              */
    } u;
} ipc_model_link_resp_t;

#ifdef __cplusplus
}
#endif

#endif /* IPC_MODEL_LINK_DEFS_H */
