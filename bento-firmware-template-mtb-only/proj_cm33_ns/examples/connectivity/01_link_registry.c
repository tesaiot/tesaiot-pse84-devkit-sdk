/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/01_link_registry
 * title:   Register and look up a transport link
 * teaches: how a backend joins the bento_link registry, and why registration
 *          is one-way — look up before you register, never register twice
 * apis:    bento_link_register, bento_link_get, bento_link_at
 * entry:   example_mpy_secure_link_registry
 */

/*******************************************************************************
 * bento_link is one interface over four possible transports ("ipc", "usb",
 * "ble", "wifi"). A backend fills in a bento_link_t and hands it to
 * bento_link_register(); everything above the link layer then finds it by name
 * with bento_link_get(), or walks the whole set with bento_link_at().
 *
 * THREE FACTS THAT DECIDE HOW YOU WRITE THE CALLING CODE
 *
 *  1. The registry holds BENTO_LINK_MAX_BACKENDS (4) entries and there is no
 *     unregister. A backend registered at boot is registered for the boot.
 *  2. bento_link_register() returns false for a duplicate NAME, not just for a
 *     full table. So "register once" is expressed as "get, and register only if
 *     the get missed" — which is what this example does, and which is why
 *     running it twice is harmless.
 *  3. The struct you register is stored BY POINTER. It must outlive the call:
 *     file-scope or heap, never a local. s_example_link below is file-scope for
 *     exactly this reason.
 *
 * The registry itself does no locking. Register from one place, early, before
 * the tasks that will read it start — the same discipline the shipped "ipc"
 * backend follows (bento_link_ipc_init(), example 02).
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "bento_link.h"

#include "../sdk_examples_cm33.h"

/* ── A minimal backend ────────────────────────────────────────────────────
 *
 * It advertises itself honestly and refuses work it cannot do. That refusal is
 * the point: bento_link.h says callers adapt to the advertised capability and
 * never assume, so a backend that cannot carry the data plane returns false
 * rather than dropping frames silently. The shipped "ipc" backend does the
 * same for send_data in v1.
 */
static bool example_send_ctrl(bento_link_t *self, uint8_t cmd, uint32_t value)
{
    (void)self;
    /* A real backend would put the two bytes on its wire here. This one only
     * has a console, and the runner task is safe to printf from. */
    printf("    [example link] ctrl cmd=0x%02X value=%lu\r\n",
           (unsigned)cmd, (unsigned long)value);
    return true;
}

static bool example_send_data(bento_link_t *self, uint8_t stream_id,
                              const void *buf, uint16_t len)
{
    (void)self;
    (void)stream_id;
    (void)buf;
    (void)len;
    return false;               /* no data plane — advertised by mtu = 0 */
}

static void example_register_receive(bento_link_t *self,
                                     void (*ctrl_cb)(uint8_t cmd, uint32_t value),
                                     void (*data_cb)(uint8_t stream_id,
                                                     const void *buf,
                                                     uint16_t len, uint16_t seq))
{
    (void)self;
    (void)ctrl_cb;
    (void)data_cb;
    /* Nothing ever arrives on this backend, so there is nothing to remember.
     * A real one stores both callbacks and invokes them from TASK context —
     * bento_link.h requires the ISR -> queue -> task trampoline, and the
     * shipped ipc backend implements precisely that. */
}

/* File scope: the registry keeps this pointer for the life of the boot. */
static bento_link_t s_example_link = {
    .send_ctrl        = example_send_ctrl,
    .send_data        = example_send_data,
    .register_receive = example_register_receive,
    .name             = "example",
    .mtu              = 0u,          /* no data plane                        */
    .bandwidth_bps    = 0u,          /* honest: this carries no bulk traffic */
    .flags            = 0u,          /* not lossy, no bulk ring              */
};

int example_mpy_secure_link_registry(void);

int example_mpy_secure_link_registry(void)
{
    printf("\r\n--- mpy_secure/01_link_registry ---\r\n");

    /* 1. What is already there? The shipped firmware registers "ipc" when the
     *    MicroPython edge_ai / deepcraft module first initialises, so this list
     *    is empty until then. Both outcomes are normal. */
    printf("  registry before:\r\n");
    unsigned before = 0u;
    for (uint32_t i = 0u; ; i++) {
        bento_link_t *l = bento_link_at(i);
        if (l == NULL) {
            break;                   /* NULL past the end — the only terminator */
        }
        before++;
        printf("    [%lu] name=%-5s mtu=%u bw=%lu bps flags=0x%02X\r\n",
               (unsigned long)i, l->name, (unsigned)l->mtu,
               (unsigned long)l->bandwidth_bps, (unsigned)l->flags);
    }
    if (before == 0u) {
        printf("    (none registered yet)\r\n");
    }

    /* 2. Register, idempotently. bento_link_get() first, because a second
     *    register of the same name returns false and there is no way to tell
     *    that apart from a full table by the return value alone. */
    bento_link_t *mine = bento_link_get(s_example_link.name);
    if (mine == NULL) {
        if (!bento_link_register(&s_example_link)) {
            /* Only two causes remain: the table is full (4 backends), or the
             * struct was malformed (NULL name). Neither is recoverable here. */
            printf("  bento_link_register(\"%s\") = false — registry full "
                   "(max %u) or malformed link\r\n",
                   s_example_link.name, (unsigned)BENTO_LINK_MAX_BACKENDS);
            return SDK_EX_REFUSED;
        }
        printf("  bento_link_register(\"%s\") = true\r\n", s_example_link.name);
    } else {
        printf("  \"%s\" was already registered (this example has run before)\r\n",
               s_example_link.name);
    }

    /* 3. Look it back up. This is how every caller above the link layer gets a
     *    handle — by name, never by holding the pointer it registered. */
    mine = bento_link_get(s_example_link.name);
    if (mine == NULL) {
        printf("  bento_link_get(\"%s\") = NULL after a successful register — "
               "registry is inconsistent\r\n", s_example_link.name);
        return SDK_EX_UNAVAILABLE;
    }

    /* 4. Adapt to what it advertises, do not assume. mtu == 0 means there is no
     *    data plane on this transport, so do not even try to send one. */
    if (mine->mtu > 0u && mine->send_data != NULL) {
        static const uint8_t frame[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        printf("  send_data -> %s\r\n",
               mine->send_data(mine, 0u, frame, (uint16_t)sizeof(frame))
                   ? "true" : "false");
    } else {
        printf("  mtu=0: this backend advertises no data plane, so no "
               "send_data attempt is made\r\n");
    }
    printf("  send_ctrl -> %s\r\n",
           mine->send_ctrl(mine, 0x01u, 42u) ? "true" : "false");

    /* 5. A name nobody registered gives NULL. Not an error — it is how you ask
     *    "is BLE compiled into this board's persona?" */
    printf("  bento_link_get(\"ble\") = %s\r\n",
           (bento_link_get("ble") != NULL) ? "present" : "NULL (not on this board)");

    /* 6. Enumerate again, and prove index n is the last by getting NULL at n. */
    unsigned after = 0u;
    while (bento_link_at(after) != NULL) {
        after++;
    }
    printf("  registry now holds %u backend(s) (was %u)\r\n", after, before);

    return SDK_EX_OK;
}
