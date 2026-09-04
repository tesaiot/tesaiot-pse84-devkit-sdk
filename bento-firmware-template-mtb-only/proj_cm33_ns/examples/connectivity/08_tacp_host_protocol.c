/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/08_tacp_host_protocol
 * title:   Pump the TACP host link and answer the IDE
 * teaches: the one-owner rule for the UART, the poll/drain loop, framed
 *          responses, and what "_from_isr" buys you and what it costs
 * apis:    tacp_init, tacp_poll_uart, tacp_ring_buf_readable,
 *          tacp_ring_buf_read, tacp_claw_respond,
 *          tacp_request_delete_main_from_isr
 * entry:   example_mpy_secure_tacp
 */

/*******************************************************************************
 * TACP is the wire between the BENTO IDE and the board: 0xAA 0x55 <cmd> for
 * control, a binary sub-protocol for file upload, and everything else passed
 * through to the MicroPython REPL.
 *
 * ONE OWNER. EXACTLY ONE.
 * -----------------------
 * tacp_poll_uart() reads the console UART and consumes what it finds. Two tasks
 * calling it split the byte stream between them, and a split stream is not a
 * protocol — a magic byte lands in one task and the command byte in the other,
 * and both see garbage. In the shipped firmware the owner is the MicroPython
 * task. If you are writing your own host loop, it is yours, and the MicroPython
 * REPL loop must not also be running.
 *
 * That is why nearly everything below is behind a switch that is off by
 * default. The SDK example runner is not the owner of that UART:
 *
 *     DEFINES+=EXAMPLE_TACP_OWN_PORT=1
 *
 * THE LOOP, once you do own it:
 *
 *     tacp_init();                       // once, at start-up
 *     for (;;) {
 *         (void)tacp_poll_uart();        // parses commands, fills the ring
 *         while (tacp_ring_buf_readable()) {
 *             int c = tacp_ring_buf_read();
 *             feed_repl((uint8_t)c);
 *         }
 *     }
 *
 * poll_uart() returns true when it recognised and handled a TACP command. The
 * return is information, not a gate — pass-through bytes still land in the ring
 * on a false return, so drain the ring every pass either way.
 *
 * ring_buf_read() returns -1 for empty and 0..255 otherwise. Assign it to an
 * int and test for -1 BEFORE narrowing to uint8_t; a byte of 0xFF is real data
 * and is not the empty marker.
 *
 * WHAT tacp_init() COSTS: it drains the UART hardware RX FIFO. That is right at
 * start-up — stale bytes from a failed upload would otherwise be parsed as a
 * frame — and wrong later, because it discards whatever the host was sending at
 * that moment.
 *
 * "_from_isr" — WHAT IT MEANS HERE
 * --------------------------------
 * tacp_request_delete_main_from_isr() is safe to call from an interrupt
 * handler: it only sets SRAM flags, uses MicroPython's ISR-safe interrupt
 * scheduler, and pushes two bytes into the lock-free ring. It takes no mutex,
 * allocates nothing, and prints nothing — printf from an ISR is a kernel panic
 * on this platform. That is what earns the suffix.
 *
 * It is also the reason the function exists at all: its real caller is the CM55
 * "Delete main.py" button, which arrives as an IPC pipe callback, and a pipe
 * callback IS interrupt context.
 *
 * What it does is NOT reversible from here: /main.py is deleted on the next
 * boot and the running script is interrupted and soft-reset. It is the user's
 * program. It has its own switch, separate from the port switch:
 *
 *     DEFINES+=EXAMPLE_TACP_DELETE_MAIN=1
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "tacp.h"

#include "../sdk_examples_cm33.h"

#ifndef EXAMPLE_TACP_OWN_PORT
#define EXAMPLE_TACP_OWN_PORT     0
#endif
#ifndef EXAMPLE_TACP_DELETE_MAIN
#define EXAMPLE_TACP_DELETE_MAIN  0     /* deletes the user's /main.py */
#endif

/* volatile: these gates must survive to the object file, not be folded out. */
static volatile int s_own_port    = EXAMPLE_TACP_OWN_PORT;
static volatile int s_delete_main = EXAMPLE_TACP_DELETE_MAIN;

int example_mpy_secure_tacp(void);

int example_mpy_secure_tacp(void)
{
    printf("\r\n--- mpy_secure/09_tacp_host_protocol ---\r\n");

    /* Safe from any task: it only compares the ring's head and tail. Useful as
     * a "is the host talking to us?" probe without taking the port. */
    printf("  tacp_ring_buf_readable() = %s (ring is %u bytes)\r\n",
           tacp_ring_buf_readable() ? "true" : "false",
           (unsigned)TACP_RING_BUF_SIZE);

    if (s_own_port == 0) {
        printf("  SKIPPED init/poll/read/respond: this task does not own the\r\n"
               "    console UART. In the shipped firmware the MicroPython task\r\n"
               "    does, and two pollers split the byte stream between them.\r\n"
               "    Rebuild with DEFINES+=EXAMPLE_TACP_OWN_PORT=1 only if you\r\n"
               "    have taken that role.\r\n");
        return SDK_EX_REFUSED;
    }

    /* ── From here down: this task owns the port ─────────────────────────── */

    /* Once, at start-up. Resets the parser and drops anything already in the
     * hardware FIFO. */
    tacp_init();
    printf("  tacp_init(): parser reset, RX FIFO drained\r\n");

    /* One pass of the real loop. A host loop repeats this forever; an example
     * does it a bounded number of times so it can return. */
    unsigned commands = 0u, bytes = 0u;
    for (unsigned pass = 0u; pass < 64u; pass++) {
        if (tacp_poll_uart()) {
            commands++;             /* a 0xAA 0x55 <cmd> frame was handled */
        }
        /* Drain regardless of the return: pass-through REPL bytes arrive
         * without any command being recognised. */
        for (;;) {
            int c = tacp_ring_buf_read();
            if (c < 0) {
                break;              /* -1 = empty. 0xFF is data, not empty. */
            }
            bytes++;
            /* A real owner hands the byte to the REPL here:
             *     feed_repl((uint8_t)c);
             * This example only counts it, because consuming a byte and then
             * dropping it is exactly how a host session appears to hang. */
        }
    }
    printf("  64 polls: %u command(s) handled, %u pass-through byte(s) drained\r\n",
           commands, bytes);

    /* Answer the IDE with a framed BentoClaw response. The module builds the
     * whole frame — magic, sub-command, sequence, length, CRC-16/CCITT — from
     * these three arguments; do not hand-assemble one.
     *
     * sub_cmd echoes the request for a success, or TACP_CLAW_ERROR (0xFF) for a
     * failure. payload_len is a byte count, capped at TACP_CLAW_PAYLOAD_MAX. */
    static const char status_json[] = "{\"proto\":2,\"src\":\"sdk_example\"}";
    tacp_claw_respond(TACP_CLAW_STATUS, status_json,
                      (uint16_t)(sizeof(status_json) - 1u));
    printf("  tacp_claw_respond(TACP_CLAW_STATUS, %u bytes) sent\r\n",
           (unsigned)(sizeof(status_json) - 1u));

    /* And the error shape, which is a different sub-command and not a different
     * function. The payload is the message. */
    static const char err_msg[] = "example: nothing to do";
    tacp_claw_respond(TACP_CLAW_ERROR, err_msg, (uint16_t)(sizeof(err_msg) - 1u));

    if (s_delete_main != 0) {
        /* Safe here (task context is a subset of what an ISR-safe function
         * tolerates), and destructive there and everywhere: the running script
         * is interrupted, MicroPython soft-resets, and the boot path removes
         * /main.py. */
        printf("  calling tacp_request_delete_main_from_isr() — /main.py will "
               "be DELETED on the next boot\r\n");
        tacp_request_delete_main_from_isr();
        return SDK_EX_STARTED;      /* the soft reset happens after we return */
    }

    printf("  SKIPPED tacp_request_delete_main_from_isr(): it deletes the\r\n"
           "    user's /main.py and soft-resets MicroPython. Rebuild with\r\n"
           "    DEFINES+=EXAMPLE_TACP_DELETE_MAIN=1 if that is what you want.\r\n");
    return SDK_EX_OK;
}
