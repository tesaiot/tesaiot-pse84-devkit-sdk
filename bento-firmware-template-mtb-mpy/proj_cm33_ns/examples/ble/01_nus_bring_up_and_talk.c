/* sdk-example: core=cm33 variant=both group=ble
 * id:      cm33/ble/01_nus_bring_up_and_talk
 * title:   Bring up NUS, pair, and exchange bytes (STARTS THE BLE RADIO)
 * teaches: the whole transport in one pass — advertise, read the link state,
 *          feed the newline framer, send a frame, answer a permission prompt,
 *          and stop cleanly
 * apis:    ble_nus_init, ble_nus_get_state, ble_nus_get_adv_name,
 *          ble_nus_send, ble_nus_rearm_advertising, ble_nus_deinit,
 *          ble_nus_passkey_cb, nus_gatt_database, nus_gatt_database_len,
 *          NUS_UUID_SERVICE, NUS_UUID_CHAR_RX, NUS_UUID_CHAR_TX,
 *          nus_protocol_init, nus_on_rx_bytes, nus_protocol_tick,
 *          nus_protocol_set_link_encrypted, nus_protocol_get_link_encrypted,
 *          nus_protocol_send_permission
 * entry:   example_ble_bring_up_and_talk
 */

/* The ENTIRE file is behind the module's own build flag, includes and all, so
 * a default build compiles it to nothing and needs no Makefile exclusion. */
#if ENABLE_PAGE_BENTO_BUDDY

/*==============================================================================
 * READ THIS BEFORE YOU TRY TO RUN ANY ble_nus EXAMPLE
 *==============================================================================
 *
 * THESE FILES DO NOT RUN ON AN UNMODIFIED TEMPLATE. Two things are missing
 * from the template as shipped, both verified against this tree:
 *
 *   (a) `libbento_secure.a` — the archive that defines every symbol these
 *       examples call — appears in NO makefile's LDLIBS. It is named only in
 *       template/lib/ble_nus/README.md, template/lib/manifest.txt and the two
 *       top-level READMEs. So the template does not LINK this archive, and
 *       every call below resolves to an undefined reference at link time until
 *       you add, per template/lib/ble_nus/README.md:
 *
 *           BENTO_DIST := /abs/path/to/lib/ble_nus
 *           INCLUDES   += $(BENTO_DIST)/include
 *           LDLIBS     += $(BENTO_DIST)/COMPONENT_CM33/COMPONENT_SOFTFP/TOOLCHAIN_GCC_ARM/libbento_secure.a
 *
 *   (b) `template/bento_libs/lib.mk` does not exist anywhere in the template,
 *       yet `proj_cm33_ns/Makefile:350` unconditionally does
 *       `include $(BENTO_LIBS_DIR)/../lib.mk` inside the
 *       `ifeq ($(ENABLE_PAGE_BENTO_BUDDY),1)` block that starts at line 321.
 *       So `make build ENABLE_PAGE_BENTO_BUDDY=1` fails in the makefile parse
 *       before a compiler is ever invoked.
 *
 * What these files therefore ARE: they COMPILE against the shipped dist
 * headers at -Wall -Werror, and they are the correct call sequences for the
 * archive's real API — signatures checked one by one against the headers under
 * dist/ble_nus/include/, semantics checked against the archive's own sources
 * under BENTO-TESAIoT-libraries/claw/common/ble_nus/. They are NOT a claim
 * that the template builds and runs them today. Fixing the build wiring is
 * somebody else's change; do not paper over it here.
 *
 * COST OF TURNING THE FLAG ON. ENABLE_PAGE_BENTO_BUDDY=1 cuts the MicroPython
 * GC heap from 112 KB to 85 KB — roughly 27 KB is handed to the AIROC BLE
 * host instead. That is why the flag ships at 0 (proj_cm33_ns/Makefile:64 and
 * bsp_features.mk) and why the three prebuilt template images carry zero
 * ble_nus_/nus_/wiced_bt_ symbols. Nothing in this directory is live until you
 * turn it on, and turning it on costs you MicroPython heap.
 *
 *==============================================================================
 * WHAT THIS EXAMPLE DOES TO YOUR BOARD
 *==============================================================================
 *
 * It STARTS THE BLE RADIO. `ble_nus_init()` calls `wiced_bt_stack_init()` and
 * the AIROC host stack begins advertising as soon as BTM_ENABLED_EVT lands.
 * Consequences you are accepting by running it:
 *
 *   * The CYW55500 has ONE RF transceiver. Once BLE is up, WiFi must not be
 *     initialized — `wifi.connect()` / `wifi.scan()` refuse with OSError in
 *     the Bento Buddy variant. If this board is on WiFi, running this stops
 *     being a good idea.
 *   * The stack claims roughly 30 KB of FreeRTOS heap and, per the comment in
 *     `ble_nus_deinit()`, NEVER gives it back for the rest of the boot.
 *     `ble_nus_deinit()` is a SOFT stop: it drops the link and stops
 *     advertising, and deliberately does not call `wiced_bt_stack_deinit()`
 *     (doing so from the IPC RX task hard-faulted CM33 — ISSUE-029).
 *   * The device becomes discoverable and, per dist/ble_nus/include/
 *     nus_gatt_db.h, NO attribute in the NUS database carries an auth or
 *     encryption requirement. An UNPAIRED central can write RX and enable TX
 *     notifications. Pairing, if a central asks for it, is Just Works: no MITM
 *     protection, bond held in RAM only. Do not run this on an untrusted desk.
 *
 * Nothing here writes flash, writes a credential, or touches OPTIGA state.
 * One frame this example injects reaches the CM55 LCD over the IPC pipe (a
 * heartbeat sets BUDDY_UI_STATE).
 *
 * Power-cycle the board to release the stack.
 *============================================================================*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ble_nus.h"
#include "nus_gatt_db.h"
#include "nus_protocol.h"
/* nus_protocol_get_link_encrypted() is exported by the archive but declared in
 * no shipped module header — the recovered prototype lives here. */
#include "bento_secure_undeclared.h"

#include "../sdk_examples_cm33.h"

/*----------------------------------------------------------------------------
 * Callbacks.
 *
 * Both run in the AIROC BLE task context — a real FreeRTOS task, not an ISR,
 * so printf would not fault. It is still wrong here: that task holds the HCI
 * transport and blocking it on the UART mutex behind a lower-priority printf
 * stalls the link. Record into plain counters and let the example body report
 * after the fact. That is the shape you want in production too.
 *--------------------------------------------------------------------------*/
static volatile uint32_t        s_rx_frames;
static volatile uint32_t        s_rx_bytes;
static volatile uint32_t        s_state_changes;
static volatile ble_nus_state_t s_last_state = BLE_NUS_STATE_OFF;

static void on_rx(const uint8_t *data, size_t len, void *user_ctx)
{
    (void)data;      /* payload is NOT NUL-terminated; copy before returning */
    (void)user_ctx;
    s_rx_frames++;
    s_rx_bytes += (uint32_t)len;
}

static void on_state(ble_nus_state_t state, void *user_ctx)
{
    (void)user_ctx;
    s_last_state = state;
    s_state_changes++;
}

/*----------------------------------------------------------------------------
 * ble_nus_passkey_cb — a WEAK symbol in libbento_secure.a (arm-none-eabi-nm
 * reports `W ble_nus_passkey_cb`). Defining it here replaces the archive's
 * no-op at link time. Exactly one translation unit in your firmware may do
 * this.
 *
 * HONEST STATUS, straight from dist/ble_nus/include/ble_nus.h: this callback
 * is NOT REACHED in this build and no passkey is ever shown. ble_nus.c
 * configures Just Works pairing —
 *      local_io_cap = BTM_IO_CAPABILITIES_NONE   (NoInputNoOutput)
 *      auth_req     = BTM_LE_AUTH_REQ_SC_BOND    (no MITM bit)
 * — and a NoInputNoOutput association never raises
 * BTM_PASSKEY_NOTIFICATION_EVT. The DisplayOnly + 6-digit flow this hook was
 * written for is not implemented. Override it anyway: it costs nothing, and
 * it is the hook the CM55 UI will forward the code through when that flow
 * ships.
 *
 * The passkey is not printed. It is a pairing secret; the count is enough to
 * tell you whether the hook ever fired.
 *--------------------------------------------------------------------------*/
static volatile uint32_t s_passkey_calls;
static char              s_passkey[7];

void ble_nus_passkey_cb(const char *passkey_6_digits)
{
    s_passkey_calls++;
    if (passkey_6_digits != NULL) {
        strncpy(s_passkey, passkey_6_digits, sizeof(s_passkey) - 1);
        s_passkey[sizeof(s_passkey) - 1] = '\0';
    }
}

/* 128-bit UUIDs are stored little-endian (BLE on-air order). Print them the
 * way a phone app shows them: most-significant byte first. */
static void print_uuid(const char *label, const uint8_t uuid[16])
{
    printf("  %-8s ", label);
    for (int i = 15; i >= 0; i--) {
        printf("%02X", uuid[i]);
        if (i == 12 || i == 10 || i == 8 || i == 6) printf("-");
    }
    printf("\r\n");
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

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * (1000U / configTICK_RATE_HZ));
}

int example_ble_bring_up_and_talk(void);

int example_ble_bring_up_and_talk(void)
{
    /* ---- 1. What is on the air before anything happens ---------------------
     * The GATT database is a byte-packed blob the archive hands to
     * wiced_bt_gatt_db_init(). You do not build it; you only need to know it
     * exists and how big it is. Handles are contiguous 0x01..0x0F because some
     * AIROC stack versions reject gaps during GATT DB init. */
    printf("[01] NUS GATT database: %u bytes, handles 0x%02X..0x%02X\r\n",
           (unsigned)nus_gatt_database_len,
           (unsigned)HDLS_GAP, (unsigned)HDLS_NUS_END);
    printf("[01] first byte of the blob = 0x%02X (proves it is linked, not a "
           "header constant)\r\n", (unsigned)nus_gatt_database[0]);
    printf("[01] NUS UUIDs (Nordic-defined):\r\n");
    print_uuid("service", NUS_UUID_SERVICE);
    print_uuid("rx",      NUS_UUID_CHAR_RX);   /* host -> device, WRITE */
    print_uuid("tx",      NUS_UUID_CHAR_TX);   /* device -> host, NOTIFY */

    /* State before we touch anything. On a fresh boot this is OFF. OFF also
     * means "never initialised" — the two are not distinguishable here. */
    printf("[01] state before init = %s\r\n", state_str(ble_nus_get_state()));

    /* ---- 2. The framer -----------------------------------------------------
     * nus_protocol_init() resets the newline accumulator and the keepalive
     * clock. ble_nus_init() calls it for you; calling it first is how you
     * recover a framer that has been fed garbage, and it is NOT optional if
     * you ever drive nus_on_rx_bytes() without calling ble_nus_init(). */
    nus_protocol_init();
    printf("[01] framer reset\r\n");

    /* ---- 3. Here is where the radio comes up -------------------------------
     * device_name is what the stack advertises until it has resolved the
     * local BD address; after that ble_nus_get_adv_name() returns the real
     * "Bento-XXXX" string built from the MAC suffix.
     * ble_nus_init() copies the config struct, so a stack-local is fine. */
    const ble_nus_config_t cfg = {
        .device_name = "BENTO Buddy",
        .on_rx       = on_rx,
        .on_state    = on_state,
        .user_ctx    = NULL,
    };

    if (!ble_nus_init(&cfg)) {
        /* Returns false only on wiced_bt_stack_init failure. The most common
         * cause is a missing HCD blob — ENABLE_PAGE_BENTO_BUDDY=1 needs
         * `make getlibs` to fetch btstack-integration and the CYW55513
         * firmware. The UART log carries the wiced_result_t. */
        printf("[01] ble_nus_init FAILED (state=%s) — stack did not come up\r\n",
               state_str(ble_nus_get_state()));
        return SDK_EX_UNAVAILABLE;
    }

    /* init returns as soon as wiced_bt_stack_init accepts the callback. The
     * transition to ADVERTISING happens later, from the stack's own task,
     * when BTM_ENABLED_EVT lands. So do NOT assert ADVERTISING here — poll,
     * or wait for the on_state callback. This example reports what it sees
     * rather than pretending it waited. */
    printf("[01] ble_nus_init ok; state now = %s (ADVERTISING arrives "
           "asynchronously on BTM_ENABLED_EVT)\r\n",
           state_str(ble_nus_get_state()));

    /* NULL until the local BD address has been read and the suffix built.
     * NULL is normal for the first moments after start — not an error. */
    const char *name = ble_nus_get_adv_name();
    printf("[01] adv name = %s\r\n", name ? name : "(not resolved yet)");

    /* ---- 4. TX ------------------------------------------------------------
     * ble_nus_send returns the number of bytes it queued, or -1. It returns
     * -1 when ANY of these hold, per ble_nus.c:
     *     state != CONNECTED, the peer has not enabled TX notifications,
     *     data == NULL, or len == 0.
     * There is no separate "not connected" code — one -1 covers all four.
     * Higher layers (nus_emit_event, nus_commands_emit_ack) fold this into
     * "the frame was dropped", which is why a disconnected board is quiet
     * rather than noisy. On a bench board with no desktop attached, -1 here
     * is the expected and correct result. */
    static const uint8_t hello[] = "{\"cmd\":\"hello\"}\n";
    int sent = ble_nus_send(hello, sizeof(hello) - 1);
    printf("[01] ble_nus_send(%u bytes) -> %d  (state=%s; -1 means dropped: "
           "no link, or the peer never wrote the TX CCCD)\r\n",
           (unsigned)(sizeof(hello) - 1), sent,
           state_str(ble_nus_get_state()));

    /* A NULL/zero-length send is also -1, not a fault. Worth knowing before
     * you write a wrapper that trusts a non-negative return. */
    printf("[01] ble_nus_send(NULL,0) -> %d\r\n", ble_nus_send(NULL, 0));

    /* ---- 5. RX, split across chunks ----------------------------------------
     * This is the payload shape a real central writes. nus_on_rx_bytes takes
     * whatever arrives: a partial frame, a whole frame, or several frames
     * concatenated. Frames are delimited by '\n'. Nothing is dispatched until
     * the newline lands — which is exactly what the split below proves. This
     * is the same call the AIROC write handler makes when a central writes
     * the NUS RX characteristic.
     *
     * The frame chosen is a heartbeat ("running"/"waiting"/"total"). Per the
     * FRAMER_CONTRACT decision table in nus_protocol.c it derives a UI state
     * and pushes ONE IPC frame to the CM55 LCD. It sends nothing on the air
     * and touches no flash — the least invasive frame that still proves the
     * framer ran. */
    static const char frame[] = "{\"running\":0,\"waiting\":0,\"total\":0}\n";
    const size_t split = 12;

    nus_on_rx_bytes((const uint8_t *)frame, split);
    printf("[01] fed %u bytes (no newline yet) — framer is holding a partial "
           "frame, nothing dispatched\r\n", (unsigned)split);

    nus_on_rx_bytes((const uint8_t *)frame + split,
                    sizeof(frame) - 1 - split);
    printf("[01] fed the remaining %u bytes including '\\n' — frame "
           "dispatched\r\n", (unsigned)(sizeof(frame) - 1 - split));

    /* Unparseable input is dropped in silence, deliberately: acking it would
     * pollute the desktop's line reassembler. Do not expect an error. */
    static const char junk[] = "not json at all\n";
    nus_on_rx_bytes((const uint8_t *)junk, sizeof(junk) - 1);
    printf("[01] fed a non-JSON line — dropped silently by design (no ack)\r\n");

    /* ---- 6. Pairing, and the link-encrypted flag ---------------------------
     * In production ONLY the BTM_ENCRYPTION_STATUS_EVT handler inside
     * ble_nus.c calls nus_protocol_set_link_encrypted(), i.e. the flag is set
     * when the peer has actually paired. It feeds the "sec" field of the
     * status ack, so setting it by hand tells the desktop the link is
     * protected when it may not be. This example flips it, reads it back
     * through the getter, and puts it back exactly as it found it.
     *
     * Read the flag for what it is: it says the LINK is encrypted. It does
     * NOT say the peer is authenticated — pairing here is Just Works with no
     * MITM protection, and the GATT database requires no authentication for
     * RX or the TX CCCD. */
    int was = nus_protocol_get_link_encrypted();
    nus_protocol_set_link_encrypted(1);
    printf("[01] link_encrypted: was %d, forced to %d\r\n",
           was, nus_protocol_get_link_encrypted());
    nus_protocol_set_link_encrypted(was);
    printf("[01] link_encrypted restored to %d\r\n",
           nus_protocol_get_link_encrypted());

    /* ---- 7. Answering an approval prompt -----------------------------------
     * nus_protocol_send_permission() is the device -> desktop reply to a
     * pending tool-approval prompt: id identifies the prompt, and the third
     * argument is 1 for "approve once" or 0 for "deny".
     *
     * This example passes 0. Deny is the only defensible default in a
     * non-interactive runner: an example that auto-approves would approve
     * whatever prompt happens to be in flight on a real desk. The id below is
     * synthetic and matches nothing. With no link the frame is dropped inside
     * ble_nus_send anyway. */
    static const char prompt_id[] = "example-not-a-real-prompt";
    nus_protocol_send_permission(prompt_id, sizeof(prompt_id) - 1, 0);
    printf("[01] sent a DENY permission frame for id '%s' (dropped if no "
           "link)\r\n", prompt_id);

    /* ---- 8. The keepalive tick ---------------------------------------------
     * Passive watchdog. Call it periodically — the UI render loop at ~10 Hz is
     * the intended caller. If 30 s pass with no parseable frame while the link
     * is still considered up, it emits BUDDY_UI_STATE=SLEEP over IPC. It never
     * touches the radio, so calling it here is free. */
    uint32_t t = now_ms();
    nus_protocol_tick(t);
    printf("[01] nus_protocol_tick(%lu ms) — call this at ~10 Hz in your "
           "render loop; it is the only thing that detects a dead desktop\r\n",
           (unsigned long)t);

    /* ---- 9. Tear down, then come back --------------------------------------
     * This is the Start/Stop cycle the LCD button drives. deinit is a SOFT
     * stop: link dropped, advertising off, host stack still resident and
     * still holding its ~30 KB. rearm is idempotent and does nothing unless
     * the state is OFF, so the deinit first is not optional. */
    ble_nus_deinit();
    printf("[01] after deinit  state = %s (stack still resident — the heap is "
           "NOT returned)\r\n", state_str(ble_nus_get_state()));

    ble_nus_rearm_advertising();
    printf("[01] after rearm   state = %s\r\n", state_str(ble_nus_get_state()));

    printf("[01] callbacks so far: %u state changes (last=%s), %u rx frames, "
           "%u rx bytes, %u passkey notifications (the passkey itself is "
           "never printed)\r\n",
           (unsigned)s_state_changes, state_str(s_last_state),
           (unsigned)s_rx_frames, (unsigned)s_rx_bytes,
           (unsigned)s_passkey_calls);

    /* The radio is left advertising on purpose — a link that dies when the
     * example returns teaches nothing, and a desktop needs something to
     * connect to. Power-cycle to clear it. */
    return SDK_EX_OK;
}

#endif /* ENABLE_PAGE_BENTO_BUDDY */
