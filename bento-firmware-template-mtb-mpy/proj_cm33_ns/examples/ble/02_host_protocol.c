/* sdk-example: core=cm33 variant=both group=ble
 * id:      cm33/ble/02_host_protocol
 * title:   Speak the Bento Buddy wire protocol end to end
 * teaches: dispatch a command, emit its ack, queue the acks a human still
 *          owes, decode chunked base64, receive a pushed folder, and
 *          accumulate a streamed agent answer
 * apis:    nus_commands_dispatch, nus_commands_emit_ack,
 *          nus_commands_handle_time_sync, nus_emit_event,
 *          nus_events_push_pending_ack, nus_events_drain_pending_ack,
 *          nus_events_pending_ack_count, nus_b64_init, nus_b64_feed,
 *          nus_b64_flush, nus_fp_char_begin, nus_fp_file, nus_fp_chunk,
 *          nus_fp_file_end, nus_fp_char_end, nus_fp_is_active,
 *          nus_fp_device_write_bytes, nus_agent_note_ask,
 *          nus_agent_handle_token, nus_agent_handle_tool_call,
 *          nus_agent_handle_done, nus_agent_buffer_len, nus_agent_reset
 * entry:   example_ble_host_protocol
 */

/* The ENTIRE file is behind the module's own build flag, includes and all, so
 * a default build compiles it to nothing and needs no Makefile exclusion. */
#if ENABLE_PAGE_BENTO_BUDDY

/*==============================================================================
 * Build/link caveats for the whole ble_nus module are stated once, in
 * 01_nus_bring_up_and_talk.c. Read that first. In short: libbento_secure.a is
 * in no makefile's LDLIBS and template/bento_libs/lib.mk does not exist, so
 * these examples are compile-verified and CANNOT be linked into a device image
 * today; and ENABLE_PAGE_BENTO_BUDDY=1 costs 27 KB of MicroPython GC heap
 * (112 KB -> 85 KB).
 *
 * WHAT THIS FILE DOES TO YOUR BOARD
 *   NO RADIO IS STARTED. Every ack and event it produces is dropped inside
 *   ble_nus_send when there is no link. A few dispatched verbs push IPC frames
 *   to the CM55 LCD (a marquee hint, an agent answer); those are harmless and
 *   are dropped if CM55 is not draining the pipe.
 *
 *   NOTHING IS WRITTEN TO FLASH by default. The folder-push section's real
 *   walk is compiled but guarded by a run-time flag,
 *   BLE_NUS_EXAMPLE_ALLOW_FLASH_WRITE, which defaults to 0.
 *
 *------------------------------------------------------------------------------
 * THE ONE THING THAT WILL SILENTLY CORRUPT YOUR PARSE
 *------------------------------------------------------------------------------
 * Most of this module's entry points take `const jsmntok_t *toks`. jsmntok_t
 * changes SIZE with a compile flag:
 *
 *     typedef struct jsmntok {
 *         jsmntype_t type; int start; int end; int size;
 *     #ifdef JSMN_PARENT_LINKS
 *         int parent;                 <-- 16 bytes here, 20 bytes there
 *     #endif
 *     } jsmntok_t;
 *
 * libbento_secure.a was compiled with JSMN_PARENT_LINKS (proj_cm33_ns/
 * Makefile:333 — `DEFINES+=JSMN_PARENT_LINKS JSMN_STRICT JSMN_STATIC`, inside
 * the ENABLE_PAGE_BENTO_BUDDY=1 block), and nus_protocol.c's prompt walk reads
 * `toks[i].parent`. Hand it an array built without the flag and every index
 * past the first is read at the wrong stride. It will not fail to link and it
 * will not fault; it will just parse the wrong bytes.
 *
 * So this file defines the flags itself rather than relying on the makefile.
 * JSMN_STATIC additionally keeps jsmn_parse/jsmn_init out of the global symbol
 * table, which matters here: several examples include a jsmn-consuming header,
 * and without it they would collide at link with "multiple definition".
 *============================================================================*/

#ifndef JSMN_PARENT_LINKS
#define JSMN_PARENT_LINKS
#endif
#ifndef JSMN_STRICT
#define JSMN_STRICT
#endif
#ifndef JSMN_STATIC
#define JSMN_STATIC
#endif

/* Opt-in. With this at 0 the folder-push section demonstrates only the refusal
 * contract, which touches no file. At 1 it writes /buddy/sdk_ex_02/hello.txt
 * into LittleFS via /buddy/.staging/sdk_ex_02/ — see the section comment for
 * exactly what the commit step deletes.
 *
 *     make build ENABLE_PAGE_BENTO_BUDDY=1 \
 *                DEFINES+=BLE_NUS_EXAMPLE_ALLOW_FLASH_WRITE=1
 */
#ifndef BLE_NUS_EXAMPLE_ALLOW_FLASH_WRITE
#define BLE_NUS_EXAMPLE_ALLOW_FLASH_WRITE 0
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vendor/jsmn.h"

#include "nus_commands.h"
#include "nus_events.h"
#include "nus_base64.h"
#include "nus_folder_push.h"
#include "nus_agent.h"
/* nus_fp_device_write_bytes is exported by the archive but declared in no
 * module header; the recovered prototype lives here. */
#include "bento_secure_undeclared.h"
/* BUDDY_PENDING_ACKS_MAX (4) and BUDDY_NOTIF_ID_MAX (16) are the shared
 * CM33/CM55 constants the pending-ack FIFO is sized from. */
#include "ipc_bento_buddy_defs.h"

#include "../sdk_examples_cm33.h"

/* nus_protocol.c uses 256. Anything the desktop sends fits well inside that;
 * the ceiling exists to bound the stack, not to bound the protocol. */
#define EX_TOKENS_MAX 64

/* Parse `json` and return the token count, or a negative jsmn error. Every
 * nus_* entry point re-finds its own fields by name, so all this has to
 * produce is a valid token array over the whole object. */
static int tokenise(const char *json, jsmntok_t *toks, int cap)
{
    jsmn_parser p;
    jsmn_init(&p);
    int n = jsmn_parse(&p, json, strlen(json), toks, (unsigned int)cap);
    if (n < 1 || toks[0].type != JSMN_OBJECT) return n < 0 ? n : -1;
    return n;
}

/* nus_commands_dispatch additionally wants the token holding the VALUE of the
 * top-level "cmd" key — not the key token. */
static const jsmntok_t *find_cmd_value(const char *json,
                                       const jsmntok_t *toks, int n)
{
    /* Top-level keys are at odd indices under token 0 in a flat object. Use
     * the parent link rather than assuming, so nested objects cannot fool us
     * — the same reason the archive needs JSMN_PARENT_LINKS. */
    for (int i = 1; i + 1 < n; i++) {
        if (toks[i].parent != 0 || toks[i].type != JSMN_STRING) continue;
        size_t klen = (size_t)(toks[i].end - toks[i].start);
        if (klen == 3 && memcmp(json + toks[i].start, "cmd", 3) == 0) {
            return &toks[i + 1];
        }
    }
    return NULL;
}

/*----------------------------------------------------------------------------
 * A base64 sink. Contract: return 0 to accept, NEGATIVE to fail. A negative
 * return aborts nus_b64_feed immediately and is passed through to its caller
 * unchanged, so use your own error codes if you want to tell failures apart.
 * The decoder does NOT retry and does NOT roll back bytes already accepted.
 *
 * In the real folder-push path this is the archive's device_sink(), which
 * forwards to nus_fp_device_write_bytes() and lands in LittleFS. Here it
 * fills a buffer.
 *--------------------------------------------------------------------------*/
typedef struct {
    uint8_t  buf[64];
    size_t   len;
    size_t   cap;
    uint32_t calls;
} ex_sink_t;

static int ex_sink(const uint8_t *bytes, size_t len, void *ctx)
{
    ex_sink_t *s = (ex_sink_t *)ctx;
    s->calls++;
    if (s->len + len > s->cap) return -2;   /* our own code, not the library's */
    memcpy(&s->buf[s->len], bytes, len);
    s->len += len;
    return 0;
}

/* Folder-push frames. Each nus_fp_* call emits its OWN ack on the NUS link;
 * you do not ack these yourself. The ack names are the bare verbs —
 * "char_begin", "file", "chunk", "file_end", "char_end" — NOT bento.*. */
static const char F_BEGIN[] =
    "{\"cmd\":\"char_begin\",\"name\":\"sdk_ex_02\",\"total\":64}";
static const char F_FILE[]  =
    "{\"cmd\":\"file\",\"path\":\"hello.txt\",\"size\":13}";
static const char F_CHUNK[] =
    "{\"cmd\":\"chunk\",\"d\":\"SGVsbG8sIEJFTlRPIQ==\"}";
static const char F_FEND[]  = "{\"cmd\":\"file_end\"}";
static const char F_CEND[]  = "{\"cmd\":\"char_end\"}";

int example_ble_host_protocol(void);

int example_ble_host_protocol(void)
{
    /* Run-time, not #if, so both branches are compiled and type-checked in
     * every build. At -Os the unreached branch is discarded by the optimiser,
     * which is exactly what you want: with the flag off, no call to the write
     * path survives into the image. */
    const int allow_flash_write = BLE_NUS_EXAMPLE_ALLOW_FLASH_WRITE;

    jsmntok_t toks[EX_TOKENS_MAX];
    const jsmntok_t *t_cmd;
    int n;
    int rc;

    printf("[02] sizeof(jsmntok_t) = %u bytes — must match the archive's\r\n",
           (unsigned)sizeof(jsmntok_t));

    /*========================================================================
     * PART 1 — COMMANDS AND ACKS
     *======================================================================*/
    printf("[02] ---- commands and acks -------------------------------\r\n");

    /* nus_commands_emit_ack is the ONLY thing that decides what an ack looks
     * like. The shape is fixed:
     *     {"ack":"<cmd>","ok":<bool>,"n":<int>[,"error":"<str>"]}\n
     * Rules worth knowing before you call it:
     *   * ack_cmd_len = 0 means "strlen it"; the name is truncated at 32.
     *   * ok is true when ok_true_or_null is non-NULL, OR when BOTH
     *     ok_true_or_null and error_or_null are NULL. Passing NULL/NULL does
     *     NOT produce a failure ack — it produces {"ok":true}.
     *   * n is written unconditionally, so 0 is a real value on the wire, not
     *     an omission.
     *   * the whole frame lives in a 256-byte stack buffer; an ack that would
     *     overflow is dropped rather than truncated.
     * The frame goes straight to ble_nus_send, so with no link it is
     * discarded and this call has no observable effect. */
    nus_commands_emit_ack("example.ok", 0, "true", NULL, 7);
    printf("[02] emitted {\"ack\":\"example.ok\",\"ok\":true,\"n\":7}\r\n");

    nus_commands_emit_ack("example.fail", 0, NULL, "not_permitted", 0);
    printf("[02] emitted the failure form with error=\"not_permitted\"\r\n");

    /* Dispatch a real verb. bento.hint.text is the least invasive one with a
     * visible effect: it forwards the text to the CM55 LCD marquee over the
     * IPC pipe and acks {"ok":true}. It starts nothing and persists
     * nothing. */
    static const char hint[] =
        "{\"cmd\":\"bento.hint.text\",\"text\":\"SDK example 02\","
        "\"scroll_speed\":60}";
    n = tokenise(hint, toks, EX_TOKENS_MAX);
    t_cmd = (n > 0) ? find_cmd_value(hint, toks, n) : NULL;
    if (n < 0 || t_cmd == NULL) {
        printf("[02] parse failed (jsmn rc=%d) — cannot dispatch\r\n", n);
        return SDK_EX_UNAVAILABLE;
    }
    /* Note the argument order: the WHOLE frame and its length, the token
     * array, the token count, then the cmd VALUE token. The dispatcher
     * re-finds every other field itself from those. */
    nus_commands_dispatch(hint, strlen(hint), toks, n, t_cmd);
    printf("[02] dispatched bento.hint.text (%d tokens) — marquee IPC sent to "
           "CM55, ack queued\r\n", n);

    /* An unknown verb. The dispatcher does NOT ack it. It returns having done
     * nothing, on purpose: the reference desktop's line reassembler treats an
     * unexpected ack as a protocol error. If you are debugging "the desktop
     * sent a command and nothing came back", this silence is the first thing
     * to rule out — it is not a bug. */
    static const char unknown[] = "{\"cmd\":\"bento.not.a.real.verb\"}";
    n = tokenise(unknown, toks, EX_TOKENS_MAX);
    t_cmd = (n > 0) ? find_cmd_value(unknown, toks, n) : NULL;
    if (n > 0 && t_cmd != NULL) {
        nus_commands_dispatch(unknown, strlen(unknown), toks, n, t_cmd);
        printf("[02] dispatched an unknown verb — no ack emitted, by design\r\n");
    }

    /* HONEST STATUS. nus_commands_handle_time_sync() handles the legacy
     * one-shot array frame {"time":[epoch,tz]} and, in the shipped archive,
     * it is a NO-OP: the body is `(void)json; (void)toks; (void)n;` with a
     * comment saying the RTC is driven by proj_cm33_ns's ntp_sync.c instead.
     * Verified in BENTO-TESAIoT-libraries/claw/common/ble_nus/nus_commands.c.
     *
     * Calling it changes nothing and emits no ack. The live path is the
     * "bento.time.sync" VERB, which nus_commands_dispatch routes to
     * bento_time_handle_sync() — declared in dist/ble_nus/include/bento_time.h
     * but NOT exported from the archive (it is absent from api.txt), so you
     * cannot call it directly. Send it as a frame if you need it. */
    static const char timeframe[] = "{\"time\":[1747085730,7]}";
    n = tokenise(timeframe, toks, EX_TOKENS_MAX);
    if (n > 0) {
        nus_commands_handle_time_sync(timeframe, toks, n);
        printf("[02] nus_commands_handle_time_sync: shipped implementation is "
               "a no-op — the clock did NOT move\r\n");
    }

    /* nus_emit_event owns the '\n' framing so callers never have to remember
     * it. Pass a NUL-terminated JSON object with NO trailing newline. It
     * returns 0 on success and -1 on transport error, disconnected link, or
     * a payload that would not fit its 256-byte frame buffer (oversize is
     * dropped whole, never truncated, so the desktop never sees broken JSON).
     *
     * -1 here is the expected, correct result when nothing is connected. */
    rc = nus_emit_event("{\"evt\":\"bento.example.ping\",\"n\":3}");
    printf("[02] nus_emit_event -> %d (-1 = no link, which is normal on a "
           "bench board)\r\n", rc);
    printf("[02] nus_emit_event(NULL) -> %d (guarded, does not fault)\r\n",
           nus_emit_event(NULL));

    /*========================================================================
     * PART 2 — THE PENDING-ACK FIFO
     *
     * WHERE THIS SITS IN THE REAL FLOW
     *   desktop  --{"cmd":"bento.notify.show","ack_required":true,"id":"n_42"}->
     *   nus_commands.c pushes "n_42" here and mirrors the notification to LCD
     *   user taps ACK on the LCD
     *   CM55 --IPC_CMD_BUDDY_DEVICE_CMD {"cmd":"bento.user.ack","id":"n_42"}->
     *   ipc_bento_buddy_bridge.c drains "n_42" and emits the event upstream
     *
     * The FIFO exists so the device can refuse to ack an id it was never
     * asked about. Drain returning false is the refusal. Pure RAM: no radio,
     * no flash, no IPC. This section leaves the FIFO as it found it.
     *======================================================================*/
    printf("[02] ---- pending acks ------------------------------------\r\n");

    /* The FIFO is module-global and may already hold real ids if a desktop
     * session is live. Record the starting depth so the report below is about
     * what THIS example did, not about whatever was already there. */
    size_t start = nus_events_pending_ack_count();
    printf("[02] capacity=%d, id field=%d bytes, occupied at entry=%u\r\n",
           BUDDY_PENDING_ACKS_MAX, BUDDY_NOTIF_ID_MAX, (unsigned)start);

    if (start >= (size_t)BUDDY_PENDING_ACKS_MAX) {
        /* Pushing now would evict a real notification id and the user's next
         * LCD tap would be refused. Refuse instead. */
        printf("[02] FIFO is already full with live ids — refusing to run so "
               "nothing real gets evicted\r\n");
        return SDK_EX_BUSY;
    }

    /* push is a no-op for NULL, for "", and for an id already present. The
     * dedup compares at most BUDDY_NOTIF_ID_MAX bytes, which is also the
     * truncation length: two ids that differ only after 16 characters are the
     * SAME id as far as this FIFO is concerned. Keep your ids short. */
    nus_events_push_pending_ack("ex02_a");
    nus_events_push_pending_ack("ex02_a");
    nus_events_push_pending_ack(NULL);
    nus_events_push_pending_ack("");
    printf("[02] pushed ex02_a twice plus NULL and \"\" -> count=%u "
           "(deduped; NULL and \"\" ignored)\r\n",
           (unsigned)nus_events_pending_ack_count());

    /* drain returns true if the id was present, and removes it. false means
     * "this device never showed that notification" — the correct response is
     * to NOT emit bento.user.ack for it. */
    bool hit  = nus_events_drain_pending_ack("ex02_a");
    bool miss = nus_events_drain_pending_ack("ex02_never_pushed");
    printf("[02] drain ex02_a -> %s, drain an unknown id -> %s, count=%u\r\n",
           hit ? "true" : "false", miss ? "true" : "false",
           (unsigned)nus_events_pending_ack_count());

    /* Eviction. Fill every free slot, then push one more. The OLDEST goes,
     * not the newest: SPEC §7.5 assumes the newest notification is the one
     * still on screen. Nothing warns you; the evicted id simply stops being
     * acknowledgeable and the user's tap on it will be refused. */
    size_t room = (size_t)BUDDY_PENDING_ACKS_MAX - nus_events_pending_ack_count();
    /* Ids are composed a byte at a time: BUDDY_NOTIF_ID_MAX is 16 and the
     * FIFO truncates at that, so a fixed short form is the safe shape. room
     * can never exceed BUDDY_PENDING_ACKS_MAX (4), so one digit is enough. */
    char id[8] = "ex02_f0";
    for (size_t i = 0; i < room; i++) {
        id[6] = (char)('0' + (int)i);
        nus_events_push_pending_ack(id);
    }
    nus_events_push_pending_ack("ex02_overflow");
    printf("[02] filled then pushed one more -> count=%u (still at "
           "capacity)\r\n", (unsigned)nus_events_pending_ack_count());

    bool evicted = !nus_events_drain_pending_ack("ex02_f0");
    printf("[02] oldest id ex02_f0 was %s\r\n",
           evicted ? "EVICTED (drain returned false)" : "still present");

    /* Put the FIFO back. An example that leaves four dead ids behind would
     * break the next real notification by filling the ring. */
    for (size_t i = 0; i < room; i++) {
        id[6] = (char)('0' + (int)i);
        (void)nus_events_drain_pending_ack(id);
    }
    (void)nus_events_drain_pending_ack("ex02_overflow");
    size_t end = nus_events_pending_ack_count();
    printf("[02] cleaned up: count back to %u (entry was %u)\r\n",
           (unsigned)end, (unsigned)start);

    /* This is the frame the bridge emits after a successful drain. It is the
     * only reason the FIFO exists. -1 just means no desktop is listening. */
    printf("[02] nus_emit_event(bento.user.ack) -> %d\r\n",
           nus_emit_event("{\"cmd\":\"bento.user.ack\",\"id\":\"ex02_a\"}"));

    /*========================================================================
     * PART 3 — BASE64 THAT ARRIVES SPLIT ACROSS BLE CHUNKS
     *
     * The folder-push protocol carries file content in "chunk" commands, each
     * with a "d":"<base64>" field. Chunk boundaries fall wherever the desktop
     * put them, so a 4-character base64 quantum — 3 bytes of file — is
     * routinely split across two BLE writes. This decoder keeps the 1..3
     * leftover characters in nus_b64_state_t.carry between calls so the caller
     * can feed each chunk exactly as it arrived, and streams the output
     * through a sink callback so a 1.8 MB pack never has to exist in RAM.
     *
     * Pure computation into a RAM buffer. No radio, no flash, no IPC.
     *======================================================================*/
    printf("[02] ---- base64 stream -----------------------------------\r\n");

    /* "Hello, BENTO!" is 13 bytes -> 20 base64 chars with one '=' of padding. */
    static const char b64[] = "SGVsbG8sIEJFTlRPIQ==";
    static const char expect[] = "Hello, BENTO!";

    ex_sink_t sink = { .buf = {0}, .len = 0, .cap = sizeof(((ex_sink_t *)0)->buf),
                       .calls = 0 };
    nus_b64_state_t st;

    /* init memsets the whole state — carry, carry_n, bytes_out, got_padding —
     * and installs the sink. Always init before the first feed; a reused
     * state with a stale carry decodes garbage. */
    nus_b64_init(&st, ex_sink, &sink);

    /* Feed at boundaries chosen to be hostile: 5, then 6, then the rest. None
     * is a multiple of 4, so every call ends holding a partial quantum. */
    const size_t total = sizeof(b64) - 1;
    const size_t cuts[] = { 5, 11, total };
    size_t from = 0;
    for (size_t c = 0; c < sizeof(cuts) / sizeof(cuts[0]); c++) {
        size_t chunk = cuts[c] - from;
        int frc = nus_b64_feed(&st, (const uint8_t *)b64 + from, chunk);
        printf("[02] feed(%u chars) -> %d | carry_n=%u bytes_out=%lu "
               "padding_seen=%s\r\n",
               (unsigned)chunk, frc, (unsigned)st.carry_n,
               (unsigned long)st.bytes_out, st.got_padding ? "yes" : "no");
        if (frc < 0) {
            printf("[02] decode failed at chunk %u\r\n", (unsigned)c);
            return SDK_EX_REFUSED;
        }
        from = cuts[c];
    }

    /* flush handles the tail. It returns 0 when carry_n is already 0 — the
     * case here, because the explicit "==" padding completed the last quantum
     * inside feed. It returns -1 when a partial quantum is still held, i.e.
     * the input was not properly padded. That refusal is deliberate: strict
     * base64 forbids implicit padding, and silently emitting the partial byte
     * would corrupt the file. */
    printf("[02] flush -> %d (0 because the input was explicitly padded)\r\n",
           nus_b64_flush(&st));

    sink.buf[sink.len < sink.cap ? sink.len : sink.cap - 1] = '\0';
    printf("[02] decoded %lu bytes in %lu sink calls: \"%s\"\r\n",
           (unsigned long)st.bytes_out, (unsigned long)sink.calls,
           (const char *)sink.buf);

    bool b64_ok = (sink.len == sizeof(expect) - 1)
                  && (memcmp(sink.buf, expect, sizeof(expect) - 1) == 0);
    printf("[02] round-trip %s\r\n",
           b64_ok ? "MATCHES" : "DIFFERS — investigate");

    /* Refusal paths, each with its own reason. All three return -1 and the
     * decoder does not distinguish them, so if you need to tell a desktop
     * which one happened you must check the input before feeding it. */

    /* (a) a character outside the alphabet. Whitespace does NOT count:
     *     ' ', '\r', '\n' and '\t' are skipped silently. */
    ex_sink_t s2 = { .buf = {0}, .len = 0, .cap = 8, .calls = 0 };
    nus_b64_state_t bad;
    nus_b64_init(&bad, ex_sink, &s2);
    printf("[02] feed(\"AAA*\") -> %d (invalid character)\r\n",
           nus_b64_feed(&bad, (const uint8_t *)"AAA*", 4));

    /* (b) an unpadded tail. feed accepts it — the error only surfaces at
     *     flush, which is why flush is not optional at end-of-file. */
    nus_b64_state_t tail;
    ex_sink_t s3 = { .buf = {0}, .len = 0, .cap = 8, .calls = 0 };
    nus_b64_init(&tail, ex_sink, &s3);
    printf("[02] feed(\"SGVsbG\") -> %d, then flush -> %d (unpadded tail is "
           "refused, never guessed)\r\n",
           nus_b64_feed(&tail, (const uint8_t *)"SGVsbG", 6),
           nus_b64_flush(&tail));

    /* (c) a sink that refuses. Our -2 comes straight back out of feed. */
    nus_b64_state_t full;
    ex_sink_t s4 = { .buf = {0}, .len = 0, .cap = 1, .calls = 0 };
    nus_b64_init(&full, ex_sink, &s4);
    printf("[02] feed into a 1-byte sink -> %d (the sink's own code is "
           "returned verbatim, not normalised to -1)\r\n",
           nus_b64_feed(&full, (const uint8_t *)"SGVsbG8s", 8));

    /* BOUNDARY CONDITION WORTH KNOWING. `pad_count` inside nus_b64_feed is a
     * LOCAL, reset on every call. If a "==" tail is split so the two '='
     * characters arrive in DIFFERENT feed() calls, the second call counts one
     * pad instead of two and the final quantum yields 2 bytes where it should
     * yield 1 — one byte too many, silently appended to the file. Read off
     * BENTO-TESAIoT-libraries/claw/common/ble_nus/nus_base64.c; not measured
     * on hardware. Keep padding inside a single feed() and it cannot bite. */

    /*========================================================================
     * PART 4 — A FOLDER PUSHED FROM THE DESKTOP
     *
     * THREADING — this is stated in nus_folder_push.h and it is not advisory.
     * The module is a SINGLE-INSTANCE, NON-REENTRANT state machine. Every
     * entry point shares file-scope state: the pack name, the current file
     * path, the base64 carry, the byte counters, and the script buffers. In
     * the shipped firmware that is safe only because every call originates
     * from the one BLE dispatch context. Drive these from a second task and
     * the two pushes interleave into one another's pack. If you call them
     * from anywhere but the BLE dispatch path — an example runner task
     * included — YOU own the mutex.
     *
     * The real walk needs a MicroPython-capable build: content is written by
     * generating MicroPython source and running it through
     * exec_python_str_public(), which is on dist/ble_nus/
     * consumer_must_provide.txt, meaning YOU supply it.
     *======================================================================*/
    printf("[02] ---- folder push -------------------------------------\r\n");

    /* nus_fp_is_active() is the only state inspector. Non-zero means the
     * machine is somewhere between char_begin and char_end. Check it before
     * you start: char_begin on a non-idle machine ABORTS the previous pack
     * (deleting its staging folder) before accepting yours. */
    printf("[02] nus_fp_is_active() = %d at entry\r\n", nus_fp_is_active());
    if (nus_fp_is_active()) {
        printf("[02] a folder push is already in flight — refusing, because "
               "starting one here would destroy it\r\n");
        return SDK_EX_BUSY;
    }

    /* nus_fp_device_write_bytes() appends to whichever file the last
     * nus_fp_file() opened. It is the bottom of the stack: nus_b64 decodes
     * into the archive's device_sink(), which calls this.
     *
     * Its two guard cases touch nothing at all, so they run unconditionally:
     *     n == 0     -> returns 0, a no-op
     *     b == NULL  -> returns -1
     * Anything else renders the bytes as \xHH escapes into a MicroPython
     * `_nus_fp_f.write(b'...')` statement, 48 bytes per statement, and runs
     * it. Content cannot inject Python: every byte becomes an escape, never
     * syntax. */
    printf("[02] nus_fp_device_write_bytes(NULL, 4) -> %d (guarded)\r\n",
           nus_fp_device_write_bytes(NULL, 4));
    printf("[02] nus_fp_device_write_bytes(buf, 0) -> %d (no-op, needs no "
           "open file)\r\n",
           nus_fp_device_write_bytes((const uint8_t *)"x", 0));

    if (!allow_flash_write) {
        /* The refusal contract, which costs nothing to demonstrate. Every
         * entry point checks its own state first and acks an error rather
         * than touching the filesystem. Called from idle, each of these opens
         * no file, writes no byte, and leaves is_active() at 0:
         *
         *   chunk    from idle -> {"ack":"chunk","ok":false,"error":"no_pack"}
         *   file     from idle -> {"ack":"file","ok":false,"error":"no_pack"}
         *   file_end from idle -> {"ack":"file_end",...,"error":"no_pack"}
         *   char_end from idle -> {"ack":"char_end",...,"error":"no_pack"}
         *
         * char_begin is deliberately not in this list: from idle it SUCCEEDS
         * and arms the machine, and the only way back to idle is char_end,
         * which is a commit. That is why it lives in the guarded path. */
        n = tokenise(F_CHUNK, toks, EX_TOKENS_MAX);
        if (n > 0) nus_fp_chunk(F_CHUNK, toks, n);
        n = tokenise(F_FILE, toks, EX_TOKENS_MAX);
        if (n > 0) nus_fp_file(F_FILE, toks, n);
        n = tokenise(F_FEND, toks, EX_TOKENS_MAX);
        if (n > 0) nus_fp_file_end(F_FEND, toks, n);
        n = tokenise(F_CEND, toks, EX_TOKENS_MAX);
        if (n > 0) nus_fp_char_end(F_CEND, toks, n);

        printf("[02] four out-of-order verbs acked \"no_pack\"; is_active=%d, "
               "nothing was written\r\n", nus_fp_is_active());
        printf("[02] flash-write path is OFF "
               "(BLE_NUS_EXAMPLE_ALLOW_FLASH_WRITE=0) — no pack created\r\n");
    } else {
        /* char_begin: names the pack and declares a total byte budget.
         * "total" is optional and treated as 0 when absent, which disables
         * the fit check. Names are validated hard: no '/', no '\', no leading
         * '.', not empty, max 32 chars. That rule is the only thing standing
         * between a peer-supplied name and an arbitrary filesystem path. */
        n = tokenise(F_BEGIN, toks, EX_TOKENS_MAX);
        if (n < 0) return SDK_EX_UNAVAILABLE;
        nus_fp_char_begin(F_BEGIN, toks, n);
        printf("[02] char_begin -> is_active=%d\r\n", nus_fp_is_active());

        /* file: opens /buddy/.staging/sdk_ex_02/hello.txt. "size" is REQUIRED
         * (unlike "total") and is enforced: the running total may not exceed
         * the pack budget, and one file may not exceed 512 KB. A missing
         * "size" acks {"error":"parse"} and opens nothing. */
        n = tokenise(F_FILE, toks, EX_TOKENS_MAX);
        if (n < 0) return SDK_EX_UNAVAILABLE;
        nus_fp_file(F_FILE, toks, n);
        printf("[02] file hello.txt opened in staging\r\n");

        /* chunk: the "d" field is base64, decoded by PART 3's decoder, which
         * carries a split quantum across chunk boundaries. The ack's "n" is
         * the CUMULATIVE byte count for the current file, not this chunk. */
        n = tokenise(F_CHUNK, toks, EX_TOKENS_MAX);
        if (n < 0) return SDK_EX_UNAVAILABLE;
        nus_fp_chunk(F_CHUNK, toks, n);
        printf("[02] chunk fed\r\n");

        /* file_end: flushes the base64 tail, closes the handle, returns to
         * the per-file state. Its ack "n" is the final file size. */
        n = tokenise(F_FEND, toks, EX_TOKENS_MAX);
        if (n < 0) return SDK_EX_UNAVAILABLE;
        nus_fp_file_end(F_FEND, toks, n);
        printf("[02] file_end -> file closed, still inside the pack\r\n");

        /* char_end: the atomic commit. DELETES any existing /buddy/sdk_ex_02
         * and renames staging over it — choose a pack name nothing else owns;
         * the one here is deliberately example-specific. On failure it aborts
         * (deleting staging) and acks {"ok":false,"error":"persist"}. Either
         * way the machine returns to idle — this is the only call that clears
         * is_active(). */
        n = tokenise(F_CEND, toks, EX_TOKENS_MAX);
        if (n < 0) return SDK_EX_UNAVAILABLE;
        nus_fp_char_end(F_CEND, toks, n);
        printf("[02] char_end -> committed; is_active=%d\r\n",
               nus_fp_is_active());

        if (nus_fp_is_active()) {
            printf("[02] machine did not return to idle — that is a fault\r\n");
            return SDK_EX_REFUSED;
        }
    }

    /*========================================================================
     * PART 5 — A STREAMED AGENT ANSWER
     *
     *   device: user picks ASK on the LCD
     *           -> page_bento_buddy calls nus_agent_note_ask("<id>")
     *           -> a {"cmd":"bento.agent.ask",...} frame goes to the desktop
     *   desktop: runs the LLM loop and streams back
     *           {"evt":"bento.agent.token","id":"<id>","delta":"..."}   xN
     *           {"evt":"bento.agent.tool_call","id":"<id>","name":"..."} opt.
     *           {"evt":"bento.agent.done","id":"<id>","text":"..."}
     *
     * The device NEVER executes a tool call. tool_call is observability only:
     * it puts "Agent: running <name>…" on the marquee. Tools run desktop-side.
     *
     * Note the frames arrive under "evt", not "cmd" — nus_protocol.c routes
     * any evt beginning "bento.agent." into nus_commands_dispatch, which hands
     * it here. These entry points take the frame either way.
     *======================================================================*/
    printf("[02] ---- agent stream ------------------------------------\r\n");

    /* This module holds ONE in-flight ask in file-scope state. If a real ASK
     * is running on the LCD right now, joining it would corrupt the answer
     * the user is waiting for. There is no "is busy" accessor, but a non-zero
     * accumulator is a strong signal, so check it. */
    if (nus_agent_buffer_len() != 0) {
        printf("[02] %u bytes already accumulated — an agent ask looks live; "
               "refusing rather than stealing it\r\n",
               (unsigned)nus_agent_buffer_len());
        return SDK_EX_BUSY;
    }

    /* note_ask stores the id (truncated at 24 bytes, including NUL) and
     * clears the accumulator. Passing NULL or "" is equivalent to
     * nus_agent_reset(). Every later frame is matched against this id. */
    nus_agent_note_ask("ex02-ask-1");
    printf("[02] note_ask(\"ex02-ask-1\") — buffer_len=%u\r\n",
           (unsigned)nus_agent_buffer_len());

    /* Each token frame appends its "delta" verbatim. Returns 0 when accepted,
     * -1 when the id does not match the in-flight ask OR when "delta" is
     * missing or is not a string. Those two failures share one return value,
     * so do not use it to diagnose a protocol bug — log the frame instead. */
    static const char t1[] =
        "{\"evt\":\"bento.agent.token\",\"id\":\"ex02-ask-1\","
        "\"delta\":\"The board \"}";
    static const char t2[] =
        "{\"evt\":\"bento.agent.token\",\"id\":\"ex02-ask-1\","
        "\"delta\":\"is awake.\"}";

    n = tokenise(t1, toks, EX_TOKENS_MAX);
    if (n < 0) return SDK_EX_UNAVAILABLE;
    printf("[02] token 1 -> %d, buffer_len=%u\r\n",
           nus_agent_handle_token(t1, toks, n),
           (unsigned)nus_agent_buffer_len());

    n = tokenise(t2, toks, EX_TOKENS_MAX);
    if (n < 0) return SDK_EX_UNAVAILABLE;
    printf("[02] token 2 -> %d, buffer_len=%u\r\n",
           nus_agent_handle_token(t2, toks, n),
           (unsigned)nus_agent_buffer_len());

    /* A token from a STALE ask. This is the rule that keeps a timed-out
     * request from contaminating the live one. Different id, so the delta is
     * dropped and the buffer does not grow. It is the single most useful
     * thing to remember about this module. */
    static const char stale[] =
        "{\"evt\":\"bento.agent.token\",\"id\":\"ex02-OLD\","
        "\"delta\":\"XXXXXXXX\"}";
    n = tokenise(stale, toks, EX_TOKENS_MAX);
    if (n < 0) return SDK_EX_UNAVAILABLE;
    size_t before = nus_agent_buffer_len();
    printf("[02] token with a stale id -> %d, buffer_len %u -> %u "
           "(unchanged: dropped)\r\n",
           nus_agent_handle_token(stale, toks, n),
           (unsigned)before, (unsigned)nus_agent_buffer_len());

    /* A frame with NO id at all is ACCEPTED while an ask is in flight — the
     * common case where only one conversation is outstanding and the desktop
     * omits the field. */
    static const char noid[] =
        "{\"evt\":\"bento.agent.token\",\"delta\":\" (no id)\"}";
    n = tokenise(noid, toks, EX_TOKENS_MAX);
    if (n < 0) return SDK_EX_UNAVAILABLE;
    printf("[02] token with no id -> %d, buffer_len=%u (accepted: a bare "
           "stream belongs to the only ask in flight)\r\n",
           nus_agent_handle_token(noid, toks, n),
           (unsigned)nus_agent_buffer_len());

    /* tool_call ALWAYS returns 0 — including when it drops the frame for an
     * id mismatch, and when "name" is missing. You cannot tell from the
     * return value whether the marquee was updated. Log-only by design: the
     * device does not run the tool. The name is truncated at 32 bytes and
     * rendered as "Agent: running <name>…" into the 120-byte hint field. */
    static const char tool[] =
        "{\"evt\":\"bento.agent.tool_call\",\"id\":\"ex02-ask-1\","
        "\"name\":\"read_sensor\"}";
    n = tokenise(tool, toks, EX_TOKENS_MAX);
    if (n < 0) return SDK_EX_UNAVAILABLE;
    printf("[02] tool_call -> %d (always 0; marquee hint pushed over IPC)\r\n",
           nus_agent_handle_tool_call(tool, toks, n));

    /* done. Two things differ from what you might assume:
     *  (a) "text", when present, is AUTHORITATIVE and replaces everything the
     *      accumulator collected. The deltas are only a fallback for a done
     *      frame that carries no text.
     *  (b) done does NOT enforce the id match that token enforces. The
     *      archive accepts a done for any id, deliberately: if the desktop
     *      re-issued the ask after a timeout, the answer still has to land
     *      somewhere. Verified in nus_agent.c, and it is a real difference
     *      from the "one in-flight id" wording in nus_agent.h.
     * On success the text is forwarded to CM55 as IPC_CMD_BUDDY_AGENT_RESPONSE
     * and the accumulator is reset. An empty result resets and returns 0
     * without sending anything. */
    static const char done[] =
        "{\"evt\":\"bento.agent.done\",\"id\":\"ex02-ask-1\","
        "\"text\":\"The board is awake.\"}";
    n = tokenise(done, toks, EX_TOKENS_MAX);
    if (n < 0) return SDK_EX_UNAVAILABLE;
    printf("[02] done -> %d, buffer_len now %u (reset by done)\r\n",
           nus_agent_handle_done(done, toks, n),
           (unsigned)nus_agent_buffer_len());

    /* reset clears the accumulator AND the in-flight id — the header only
     * mentions the text, but nus_agent.c clears s_cur_id too. That second
     * effect matters: after a reset, every token frame that carries an id is
     * dropped, because there is no ask to match. Call it when the user leaves
     * the ASK submode; belt and braces here so this file cannot leave state
     * behind for the next example.
     *
     * The accumulator is 256 bytes and overflow TRUNCATES in silence —
     * handle_token still returns 0. For a long answer, rely on the "text"
     * field of the done frame, not on the deltas. */
    nus_agent_reset();
    printf("[02] reset -> buffer_len=%u, no ask in flight\r\n",
           (unsigned)nus_agent_buffer_len());

    /*========================================================================
     * Verdict. Say what went wrong rather than smoothing it over.
     *======================================================================*/
    if (end != start) {
        printf("[02] WARNING: pending-ack FIFO depth changed (%u -> %u)\r\n",
               (unsigned)start, (unsigned)end);
        return SDK_EX_REFUSED;
    }
    if (nus_agent_buffer_len() != 0) {
        printf("[02] agent accumulator did not reset — that is a fault\r\n");
        return SDK_EX_REFUSED;
    }
    return b64_ok ? SDK_EX_OK : SDK_EX_NO_DATA;
}

#endif /* ENABLE_PAGE_BENTO_BUDDY */
