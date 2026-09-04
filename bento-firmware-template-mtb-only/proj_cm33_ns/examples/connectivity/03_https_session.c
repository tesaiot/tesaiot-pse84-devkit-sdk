/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/03_https_session
 * title:   One HTTPS session, end to end
 * teaches: connect, GET, POST, disconnect against the TESAIoT gateway — and
 *          when the plaintext door is the right one and when it is a mistake
 * apis:    claw_https_connect, claw_https_connected, claw_https_get,
 *          claw_https_post, claw_https_disconnect, claw_http_connect_insecure
 * entry:   example_mpy_secure_https_session
 */

/*******************************************************************************
 * BEFORE THIS RUNS
 * ----------------
 * WiFi must already be associated and have an IP. This module is an HTTP
 * client and nothing more; it does not bring up the radio, and every call
 * below fails cleanly (false / -1) if the link is down.
 *
 * ONE CONNECTION AT A TIME
 * ------------------------
 * The module holds a single client handle in file-scope state. A second
 * claw_https_connect() silently disconnects the first — so "connect, use,
 * disconnect" is not a style preference here, it is the only correct shape.
 *
 * WHAT THE TWO DOORS ARE FOR
 * --------------------------
 * claw_https_connect(host, port, api_key)
 *      TLS, and the api_key is sent as an `apikey:` header on every POST.
 *      This is the door for anything carrying a credential or device data.
 *
 * claw_http_connect_insecure(host, port)
 *      PLAINTEXT. For keyless public endpoints whose content is not worth
 *      hiding — the weather and IP-geolocation services the watch face reads.
 *
 *      NEVER use it for: anything with an API key, anything with device
 *      identity, anything you would mind an observer reading or rewriting. A
 *      plaintext response is attacker-controlled data; treat it as untrusted
 *      input even when the service is honest.
 *
 *      It is a separate function on purpose. Plaintext is selected by an
 *      internal flag, never by the port number — because the port arrives from
 *      Python and from host-supplied "host:port:api_key" strings, so keying the
 *      downgrade on port 80 would let a peer turn the API-key header into
 *      cleartext while the UI still said "Online (HTTPS)".
 *
 * ONE MORE THING YOU SHOULD KNOW BEFORE SHIPPING
 * ----------------------------------------------
 * This build sets root_ca_verify_mode = CY_AWS_ROOTCA_VERIFY_NONE (confirmed by
 * reading BENTO-TESAIoT-libraries/claw/common/mpy/claw_transport.c). The TLS
 * session is encrypted but the server's certificate is NOT verified, so it
 * stops a passive listener and not an active man in the middle. Pin a root CA
 * before this carries anything you cannot afford to have redirected.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "claw_transport.h"

#include "../sdk_examples_cm33.h"

/* Edit these three for your deployment. The gateway rejects an empty key. */
#ifndef EXAMPLE_API_HOST
#define EXAMPLE_API_HOST     "api.tesaiot.com"
#endif
#ifndef EXAMPLE_API_PORT
#define EXAMPLE_API_PORT     (443u)
#endif
#ifndef EXAMPLE_API_KEY
#define EXAMPLE_API_KEY      ""          /* your device key for the key-auth plugin */
#endif

/* A keyless public endpoint, for the plaintext leg. */
#ifndef EXAMPLE_PLAIN_HOST
#define EXAMPLE_PLAIN_HOST   "api.open-meteo.com"
#endif
#ifndef EXAMPLE_PLAIN_PORT
#define EXAMPLE_PLAIN_PORT   (80u)
#endif
#ifndef EXAMPLE_PLAIN_PATH
#define EXAMPLE_PLAIN_PATH   "/v1/forecast?latitude=13.75&longitude=100.50&current=temperature_2m"
#endif

/* Static, not stack: the runner task's stack is not sized for this. */
static char s_body[512];

/* Print a body without trusting it to be short, terminated, or printable-only.
 * It came off the network. */
static void print_body(const char *what, int len)
{
    if (len < 0) {
        printf("  %s: failed (-1)\r\n", what);
        return;
    }
    int shown = (len > 200) ? 200 : len;
    printf("  %s: %d byte(s)%s\r\n    %.*s\r\n",
           what, len, (shown < len) ? " (first 200 shown)" : "",
           shown, s_body);
}

int example_mpy_secure_https_session(void);

int example_mpy_secure_https_session(void)
{
    int rc = SDK_EX_OK;

    printf("\r\n--- mpy_secure/03_https_session ---\r\n");

    /* Leave nothing of a previous run behind. Cheap, and it makes the state
     * report below mean something. */
    if (claw_https_connected()) {
        printf("  a connection was already open — closing it first\r\n");
        claw_https_disconnect();
    }
    printf("  claw_https_connected() = %s\r\n",
           claw_https_connected() ? "true" : "false");

    /* ── Leg 1: TLS to the gateway ──────────────────────────────────────── */
    printf("  connecting (TLS) to %s:%u ...\r\n",
           EXAMPLE_API_HOST, (unsigned)EXAMPLE_API_PORT);

    if (!claw_https_connect(EXAMPLE_API_HOST, (uint16_t)EXAMPLE_API_PORT,
                            EXAMPLE_API_KEY)) {
        /* One of: no IP, DNS failure, TCP refused, TLS handshake failed. The
         * module collapses them into one false and there is no error detail to
         * report — so report the fact, not a guess at the cause. */
        printf("  claw_https_connect() = false. WiFi down, DNS/TCP refused, or "
               "the TLS handshake failed — the module does not distinguish.\r\n");
        rc = SDK_EX_UNAVAILABLE;
    } else {
        printf("  claw_https_connect() = true, connected = %s\r\n",
               claw_https_connected() ? "true" : "false");

        /* GET. Returns the body length, or -1 for any transport error AND for
         * any non-2xx status — a 404 and a dropped socket look the same here. */
        memset(s_body, 0, sizeof(s_body));
        print_body("GET /health", claw_https_get("/health", s_body, sizeof(s_body)));

        /* POST. The api_key given to connect() rides as the `apikey:` header;
         * Content-Type is set to application/json for you. json_len is the
         * byte count, so a body with embedded NULs is fine. */
        static const char req[] = "{\"ping\":1}";
        memset(s_body, 0, sizeof(s_body));
        print_body("POST /ai/v1/chat",
                   claw_https_post("/ai/v1/chat", req, sizeof(req) - 1u,
                                   s_body, sizeof(s_body)));

        claw_https_disconnect();
        printf("  after disconnect, connected = %s\r\n",
               claw_https_connected() ? "true" : "false");
    }

    /* ── Leg 2: plaintext, for a keyless public service ──────────────────
     *
     * Note what is NOT passed here: there is no api_key parameter, because
     * there must not be one. If the endpoint you are reaching for needs a key,
     * you are on the wrong function.
     */
    printf("  connecting (PLAINTEXT) to %s:%u ...\r\n",
           EXAMPLE_PLAIN_HOST, (unsigned)EXAMPLE_PLAIN_PORT);

    if (!claw_http_connect_insecure(EXAMPLE_PLAIN_HOST,
                                    (uint16_t)EXAMPLE_PLAIN_PORT)) {
        printf("  claw_http_connect_insecure() = false\r\n");
        if (rc == SDK_EX_OK) {
            rc = SDK_EX_UNAVAILABLE;
        }
        return rc;
    }

    memset(s_body, 0, sizeof(s_body));
    print_body("GET (plaintext) " EXAMPLE_PLAIN_PATH,
               claw_https_get(EXAMPLE_PLAIN_PATH, s_body, sizeof(s_body)));

    /* Always close. The insecure flag is consumed by the connect that follows
     * it, so it cannot leak into a later session — but the handle would. */
    claw_https_disconnect();
    printf("  closed; connected = %s\r\n",
           claw_https_connected() ? "true" : "false");

    return rc;
}
