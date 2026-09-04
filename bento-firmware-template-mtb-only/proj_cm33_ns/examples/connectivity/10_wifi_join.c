/* sdk-example: core=cm33 variant=both group=connectivity
 * id:      cm33/connectivity/10_wifi_join
 * title:   Join a Wi-Fi network
 * teaches: the whole join on CM33_NS — bring the radio up, take credentials
 *          from a store instead of a #define, connect, read the IP and the
 *          associated AP back, and put the link down again
 * apis:    app_wifi_is_ready, app_wifi_init, app_wifi_connect_direct,
 *          app_wifi_get_ipv4, app_wifi_disconnect, cy_wcm_is_connected_to_ap,
 *          cy_wcm_get_ip_addr, cy_wcm_get_associated_ap_info,
 *          lfs_load_wifi_creds, tesaiot_config_get
 * entry:   example_net_wifi_join
 */

/*******************************************************************************
 * WHICH API THIS USES, AND WHY — read this before copying the file
 * ----------------------------------------------------------------
 * There are two Wi-Fi APIs in this SDK and they are not interchangeable.
 *
 *   wifi_manager_*   bento_libs/claw/common/modules/wifi_manager/wifi_manager.h
 *                    A CM55 module. Every call in it packs an IPC message and
 *                    waits for CM33_NS to answer — CM55 cannot reach SDHC0,
 *                    which is PPC-protected for CM33_NS. It is compiled into
 *                    proj_cm55 only (proj_cm55/Makefile adds
 *                    $(BENTO_COMMON)/modules/wifi_manager to SEARCH; the CM33
 *                    makefile does not, and there is no wifi_manager.o under
 *                    proj_cm33_ns/build). Calling it from here would not fail
 *                    at run time — it would fail at LINK time, with one
 *                    undefined reference per call.
 *
 *   app_wifi_* / cy_wcm_*   proj_cm33_ns/wifi_init.h and cy_wcm.h
 *                    The native path on THIS core. wifi_manager's IPC handler
 *                    on the CM33 side ends up calling exactly these.
 *
 * So: this file uses the native path, and the wifi_manager entry points are
 * catalogued in ref_net.c instead of called.
 *
 * BLOCKING, OR THE start/ready/result TRIO?
 * -----------------------------------------
 * The question the header raises is real, and the answer here is BLOCKING.
 *
 * wifi_manager offers wifi_manager_scan_start / _scan_ready / _scan_result and
 * the matching status trio for one reason, stated in its own header: "use from
 * LVGL callbacks to avoid blocking GFX task". The blocking twin can sit in an
 * IPC wait for up to 95 seconds (WIFI_IPC_RESPONSE_TIMEOUT_CONN_MS), and the
 * GFX task holding still for 95 seconds is a frozen screen.
 *
 * This runner task is not the GFX task. It is tskIDLE_PRIORITY+1 — below every
 * printf-using task in the system — so while it blocks, nothing else waits on
 * it. app_wifi_connect_direct() blocks for up to three cy_wcm_connect_ap()
 * attempts with a 2 s backoff between them, and that is the correct shape for
 * a task whose whole job is this one operation.
 *
 * A polling trio here would be strictly worse: more code, the same wall-clock
 * time, and a state machine that has nothing to do while it waits.
 *
 * THIS EXAMPLE STARTS A RADIO — SO IT IS OFF BY DEFAULT
 * ----------------------------------------------------
 * app_wifi_init() drives WL_REG_ON, brings up SDIO, and starts WHD's threads;
 * app_wifi_connect_direct() then associates. That draws current, occupies the
 * single RF front end, and puts the device on somebody's network. None of that
 * should happen because a developer ran the example list. Rebuild with:
 *
 *     make build ENABLE_PAGE_EXAMPLES=1 \
 *          SDK_EXAMPLE_CM33=cm33/connectivity/10_wifi_join \
 *          DEFINES+=EXAMPLE_WIFI_JOIN=1
 *
 * The link is taken back down before returning, unless you also set
 * EXAMPLE_WIFI_KEEP_LINK=1 — which is what you want if the next thing you run
 * is 13_mqtt_publish or 15_ntp_time.
 *
 * WHERE THE CREDENTIALS COME FROM
 * -------------------------------
 * Not from this file. In order:
 *
 *   1. the LittleFS credential store (lfs_load_wifi_creds) — the same store the
 *      board's own auto-connect reads, and the one that survives a reflash.
 *      It is backed by the MicroPython VFS, so it exists only in the mtb-mpy
 *      variant; the guard below is why.
 *   2. tesaiot_config_store's wifi_ssid / wifi_pass / wifi_security — present
 *      in both variants.
 *   3. nothing. The example refuses and says how to provision the board.
 *
 * There is no fourth option and there is no default SSID. A credential
 * compiled into an example is a credential in a public repository.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "cy_wcm.h"

#include "wifi_init.h"
#include "tesaiot_config_store.h"

#include "../sdk_examples_cm33.h"

/* Off by default: this joins a network. volatile so the branch survives to the
 * object file and the calls below are really linked. */
#ifndef EXAMPLE_WIFI_JOIN
#define EXAMPLE_WIFI_JOIN       0
#endif
#ifndef EXAMPLE_WIFI_KEEP_LINK
#define EXAMPLE_WIFI_KEEP_LINK  0    /* leave the link up for the next example */
#endif

static volatile int s_join_allowed = EXAMPLE_WIFI_JOIN;
static volatile int s_keep_link    = EXAMPLE_WIFI_KEEP_LINK;

/* Static, not stack. The runner task's stack is not sized for 130 bytes of
 * credential plus a scan of formatting buffers. */
static char s_ssid[33];
static char s_pass[65];
static char s_sec[16];

/* Overwrite the passphrase through a volatile pointer so the compiler may not
 * decide the store is dead and drop it. It sits in .bss for the life of the
 * image otherwise. */
static void wipe(char *p, size_t n)
{
    volatile char *v = (volatile char *)p;
    while (n-- > 0u) {
        *v++ = '\0';
    }
}

/* cy_wcm hands back IPv4 in network byte order packed into a uint32_t; the
 * first octet is the low byte. Same unpacking wifi_init.c uses. */
static void ipv4_str(uint32_t v4, char *out, size_t cap)
{
    (void)snprintf(out, cap, "%u.%u.%u.%u",
                   (unsigned)(v4 & 0xFFu),
                   (unsigned)((v4 >> 8) & 0xFFu),
                   (unsigned)((v4 >> 16) & 0xFFu),
                   (unsigned)((v4 >> 24) & 0xFFu));
}

static const char *security_name(cy_wcm_security_t s)
{
    switch (s) {
        case CY_WCM_SECURITY_OPEN:            return "open";
        case CY_WCM_SECURITY_WEP_PSK:         return "WEP";
        case CY_WCM_SECURITY_WPA_AES_PSK:     return "WPA-AES";
        case CY_WCM_SECURITY_WPA_TKIP_PSK:    return "WPA-TKIP";
        case CY_WCM_SECURITY_WPA2_AES_PSK:    return "WPA2-AES";
        case CY_WCM_SECURITY_WPA2_MIXED_PSK:  return "WPA2-mixed";
        case CY_WCM_SECURITY_WPA3_SAE:        return "WPA3-SAE";
        case CY_WCM_SECURITY_WPA3_WPA2_PSK:   return "WPA3/WPA2";
        case CY_WCM_SECURITY_UNKNOWN:         return "unknown";
        default:                              return "other";
    }
}

/* Fill s_ssid / s_pass / s_sec. Returns the name of the store it used, or NULL
 * if the board has no credentials at all. */
static const char *load_credentials(void)
{
    memset(s_ssid, 0, sizeof(s_ssid));
    wipe(s_pass, sizeof(s_pass));
    memset(s_sec, 0, sizeof(s_sec));

#if BENTO_HAS_MPY
    /* The primary store. Returns false when the MicroPython VFS is not mounted
     * (lfs_wifi_creds_ready() is false) or when the file holds no usable entry
     * — it does not distinguish those. It hands back the most recently saved
     * network, and "WPA2" or "OPEN" as the security string, never WPA3: the
     * on-disk security field is one byte and WPA3's low byte collides with
     * WPA2-AES. A WPA3-only AP therefore needs the config store below. */
    if (lfs_load_wifi_creds(s_ssid, sizeof(s_ssid),
                            s_pass, sizeof(s_pass),
                            s_sec,  sizeof(s_sec))
        && s_ssid[0] != '\0') {
        return "LittleFS credential store";
    }
#endif

    {
        /* ~660 bytes. Static for the same reason as the buffers above. */
        static tesaiot_config_t cfg;
        tesaiot_config_get(&cfg);
        if (cfg.wifi_ssid[0] != '\0') {
            (void)snprintf(s_ssid, sizeof(s_ssid), "%s", cfg.wifi_ssid);
            (void)snprintf(s_pass, sizeof(s_pass), "%s", cfg.wifi_pass);
            (void)snprintf(s_sec,  sizeof(s_sec),  "%s", cfg.wifi_security);
            wipe(cfg.wifi_pass, sizeof(cfg.wifi_pass));
            return "tesaiot_config_store";
        }
        wipe(cfg.wifi_pass, sizeof(cfg.wifi_pass));
    }

    return NULL;
}

int example_net_wifi_join(void);

int example_net_wifi_join(void)
{
    char ipbuf[16];
    int rc = SDK_EX_OK;

    printf("\r\n--- connectivity/10_wifi_join ---\r\n");

    /* Safe from here, always: a plain flag read of a static bool in
     * wifi_init.c. Ask it FIRST. cy_wcm_is_connected_to_ap() dereferences WCM
     * state that does not exist before cy_wcm_init(), and calling it early
     * hard-faults the board — mqtt_task.c carries the same warning above its
     * own wait loop, and that is where the rule was learned. */
    const bool ready = app_wifi_is_ready();
    printf("  app_wifi_is_ready()          = %s\r\n", ready ? "true" : "false");

    if (ready) {
        printf("  cy_wcm_is_connected_to_ap()  = %u\r\n",
               (unsigned)cy_wcm_is_connected_to_ap());
        ipv4_str(app_wifi_get_ipv4(), ipbuf, sizeof(ipbuf));
        printf("  app_wifi_get_ipv4()          = %s\r\n", ipbuf);
    } else {
        printf("  (WCM is not up, so nothing else may be asked yet)\r\n");
    }

    if (s_join_allowed == 0) {
        printf("  SKIPPED the join. This example powers the radio and\r\n"
               "    associates with an access point. Rebuild with\r\n"
               "    DEFINES+=EXAMPLE_WIFI_JOIN=1 to let it run. Nothing was\r\n"
               "    started and nothing was changed.\r\n");
        return SDK_EX_REFUSED;
    }

    /* ── credentials ─────────────────────────────────────────────────────── */
    {
        const char *from = load_credentials();
        if (from == NULL) {
            printf("  no credentials on this board. Save some first — from the\r\n"
                   "    REPL: wifi.connect(\"<ssid>\", \"<password>\") — or set\r\n"
                   "    wifi_ssid / wifi_pass / wifi_security in the TESAIoT\r\n"
                   "    config store. This example will not carry an SSID.\r\n");
            return SDK_EX_NO_DATA;
        }
        /* SSID and security only. The passphrase is never printed: the console
         * is not the radio, and this console is often a shared screen. */
        printf("  credentials from %s\r\n", from);
        printf("    ssid=\"%s\" security=\"%s\" passphrase=%u byte(s), not shown\r\n",
               s_ssid, s_sec[0] ? s_sec : "(default: WPA2)",
               (unsigned)strlen(s_pass));
    }

    /* ── bring the radio up ──────────────────────────────────────────────── */
    {
        /* Idempotent: returns CY_RSLT_SUCCESS immediately if already up. Must
         * run from a task, never before the scheduler — WCM creates threads. */
        cy_rslt_t r = app_wifi_init();
        printf("  app_wifi_init()              = 0x%08lX%s\r\n",
               (unsigned long)r, (r == CY_RSLT_SUCCESS) ? " (ok)" : " (FAILED)");
        if (r != CY_RSLT_SUCCESS) {
            wipe(s_pass, sizeof(s_pass));
            printf("    SDIO, the WL_REG_ON GPIO or cy_wcm_init() failed. There\r\n"
                   "    is no link to report and none was attempted.\r\n");
            return SDK_EX_UNAVAILABLE;
        }
    }

    /* ── join ────────────────────────────────────────────────────────────── */
    {
        /* Blocking, up to three attempts, 2 s apart. Maps "WPA3"/"OPEN" and an
         * empty passphrase itself; anything else is treated as WPA2-AES.
         * Returns 0, or the cy_rslt_t of the last attempt. */
        int join = app_wifi_connect_direct(s_ssid, s_pass,
                                           s_sec[0] ? s_sec : NULL);
        wipe(s_pass, sizeof(s_pass));

        printf("  app_wifi_connect_direct()    = %d (0x%08lX)\r\n",
               join, (unsigned long)(uint32_t)join);
        if (join != 0) {
            /* Do not dress this up. Three attempts failed; the AP was out of
             * range, the passphrase was wrong, or the cold-join RX buffer
             * shortfall bit (0x020003FF = WHD_JOIN_IN_PROGRESS). */
            printf("    not associated. Wrong passphrase, AP out of range, or a\r\n"
                   "    cold first join (0x020003FF). Nothing to clean up —\r\n"
                   "    cy_wcm_connect_ap tears its own half-join down.\r\n");
            return SDK_EX_UNAVAILABLE;
        }
    }

    /* ── what the link actually is ───────────────────────────────────────── */
    printf("  cy_wcm_is_connected_to_ap()  = %u\r\n",
           (unsigned)cy_wcm_is_connected_to_ap());

    ipv4_str(app_wifi_get_ipv4(), ipbuf, sizeof(ipbuf));
    printf("  app_wifi_get_ipv4()          = %s  (cached by the last connect)\r\n",
           ipbuf);

    {
        /* The live answer, straight from the network interface. It differs from
         * the cached one after a DHCP renewal. */
        cy_wcm_ip_address_t ip;
        memset(&ip, 0, sizeof(ip));
        cy_rslt_t r = cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_STA, &ip);
        if (r == CY_RSLT_SUCCESS) {
            ipv4_str(ip.ip.v4, ipbuf, sizeof(ipbuf));
            printf("  cy_wcm_get_ip_addr(STA)      = %s (version=%d)\r\n",
                   ipbuf, (int)ip.version);
        } else {
            printf("  cy_wcm_get_ip_addr(STA)      = 0x%08lX (no address)\r\n",
                   (unsigned long)r);
        }
    }

    {
        cy_wcm_ip_address_t gw;
        memset(&gw, 0, sizeof(gw));
        cy_rslt_t r = cy_wcm_get_gateway_ip_address(CY_WCM_INTERFACE_TYPE_STA, &gw);
        if (r == CY_RSLT_SUCCESS) {
            ipv4_str(gw.ip.v4, ipbuf, sizeof(ipbuf));
            printf("  gateway                      = %s\r\n", ipbuf);
        } else {
            printf("  gateway                      = 0x%08lX (not available)\r\n",
                   (unsigned long)r);
        }
    }

    {
        cy_wcm_associated_ap_info_t ap;
        memset(&ap, 0, sizeof(ap));
        cy_rslt_t r = cy_wcm_get_associated_ap_info(&ap);
        if (r == CY_RSLT_SUCCESS) {
            /* SSID is uint8_t[33] and a corrupt beacon is exactly the case
               where the terminator is missing — bound the print. */
            printf("  associated AP: ssid=\"%.*s\" rssi=%d dBm ch=%u "
                   "security=%s bssid=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   (int)sizeof(ap.SSID), (const char *)ap.SSID,
                   (int)ap.signal_strength, (unsigned)ap.channel,
                   security_name(ap.security),
                   ap.BSSID[0], ap.BSSID[1], ap.BSSID[2],
                   ap.BSSID[3], ap.BSSID[4], ap.BSSID[5]);
        } else {
            printf("  cy_wcm_get_associated_ap_info() = 0x%08lX\r\n",
                   (unsigned long)r);
        }
    }

    /* ── put it back the way we found it ─────────────────────────────────── */
    if (s_keep_link != 0) {
        printf("  LEAVING THE LINK UP (EXAMPLE_WIFI_KEEP_LINK=1). 13_mqtt_publish\r\n"
               "    and 15_ntp_time both need it. Nothing else will take it down.\r\n");
        return rc;
    }

    {
        /* Safe when not connected: returns 0 without touching WCM if the radio
         * was never brought up. */
        int d = app_wifi_disconnect();
        printf("  app_wifi_disconnect()        = %d\r\n", d);
        if (d != 0) {
            rc = SDK_EX_UNAVAILABLE;
        }
        printf("  cy_wcm_is_connected_to_ap()  = %u after disconnect\r\n",
               (unsigned)cy_wcm_is_connected_to_ap());
    }

    /* WCM itself is deliberately left initialised. app_wifi_deinit() exists —
     * ref_net.c has it — but tearing WCM down under a firmware whose MQTT task
     * and MicroPython wifi module both assume it is up is a bigger change than
     * an example should make on its own. */
    return rc;
}
