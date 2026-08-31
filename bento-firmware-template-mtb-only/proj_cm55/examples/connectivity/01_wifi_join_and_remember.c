/* sdk-example: core=cm55 variant=both group=connectivity
 * id:      cm55/connectivity/01_wifi_join_and_remember
 * title:   Join a network from CM55, and let the board remember it
 * teaches: CM55 owns no radio — every WiFi call is proxied to CM33_NS over IPC; the blocking and non-blocking forms and when each is wrong; where saved credentials actually live
 * apis:    wifi_manager_init, wifi_manager_scan_start, wifi_manager_scan_ready, wifi_manager_scan_result, wifi_manager_connect, wifi_manager_status_start, wifi_manager_status_ready, wifi_manager_status_result, wifi_manager_get_ip, wifi_manager_is_connected, wifi_manager_disconnect, wifi_manager_last_error, wifi_saved_add, wifi_saved_count, wifi_saved_load, wifi_saved_find
 * entry:   example_cm55_wifi_join_and_remember
 */
/*******************************************************************************
 * connectivity/01 — joining a network, from the core that has no radio.
 *
 * WHY THIS IS NOT A DRIVER CALL
 * -----------------------------
 * CM55 cannot reach the radio at all. The WiFi part sits behind SDHC0, and that
 * peripheral is protection-configured for CM33_NS; a CM55 access faults rather
 * than failing. So `wifi_manager` on this core is a proxy: it packs a request,
 * hands it to CM33_NS over the IPC pipe, and waits.
 *
 *   BENTO-TESAIoT-libraries/claw/common/modules/wifi_manager/wifi_manager.c:5-7
 *       "CM55 cannot access SDHC0/WHD directly, so all WiFi operations are
 *        proxied to CM33 via IPC"
 *   proj_cm55/Makefile:824-825   INCLUDES+ and SEARCH+ for the module
 *
 * Everything below therefore costs an IPC round trip, and the timeouts are
 * measured against a radio that has to initialise itself first.
 *
 * THE TIMEOUTS, AND WHY THEY ARE THAT LARGE
 * ------------------------------------------
 *   wifi_manager.c:18-26
 *     WIFI_IPC_RESPONSE_TIMEOUT_SCAN_MS  35000   WiFi init ~22 s + scan ~5 s
 *     WIFI_IPC_RESPONSE_TIMEOUT_CONN_MS  95000   init ~20 s + up to 6 attempts
 *     WIFI_IPC_RESPONSE_TIMEOUT_MS        5000   status, disconnect
 *
 * Those are not padding. WiFi is brought up LAZILY on CM33_NS — the SDIO and
 * WCM stack is not initialised at boot, only on the first scan or connect
 * (proj_cm33_ns/main.c:13-14, proj_cm33_ns/Makefile:91). The first call you
 * make pays for the whole bring-up. Later calls are fast.
 *
 * BLOCKING VERSUS NON-BLOCKING, AND WHERE THE BLOCKING FORM IS A BUG
 * ------------------------------------------------------------------
 * wifi_manager offers both. `wifi_manager_scan()` blocks until the answer
 * arrives, which can be 35 seconds. Called from the GFX task, that freezes the
 * display for 35 seconds and the user concludes the board has crashed.
 *
 * So: the blocking form is for a task of your own that is allowed to wait. The
 * start/ready/result form is for anything that shares a task with the UI. This
 * example shows the non-blocking form for the scan, because that is the one
 * that is easy to get wrong, and the blocking form for connect, because a
 * deliberate "join this network now" is an action the user is already waiting
 * on. The poll cadence documented for the ready-functions is 100 ms
 * (wifi_manager.h:80, :92) — polling faster only burns the IPC pipe.
 *
 * WHERE SAVED CREDENTIALS LIVE
 * -----------------------------
 * Two different stores, and they are easy to confuse.
 *
 *   wifi_saved_*     the CM55-side list this example uses. It is the list the
 *                    UI shows and the one auto-connect walks.
 *   lfs_save_wifi_creds / lfs_load_wifi_creds
 *                    the CM33_NS LittleFS copy, written by the MicroPython
 *                    side (proj_cm33_ns/wifi_init.h:64, :71). Not reachable
 *                    from here, and not the same records.
 *
 * Saving here does not write the MicroPython copy, and vice versa. If a board
 * joins from Python but this list is empty, that is the reason.
 *
 * WHAT THIS EXAMPLE DOES NOT DO
 * ------------------------------
 * It does not hard-code an SSID or a password, and neither should yours. The
 * configuration store holds them (ipc_tesaiot_defs.h:66-68 — wifi_ssid[33],
 * wifi_pass[65], wifi_security[16]) and the shipped defaults file contains a
 * live API key and a live MQTT password already
 * (tesaiot_config_defaults.h:22-24). Reproducing credentials in source is how
 * they end up in a public repository.
 ******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "wifi_manager.h"
#include "wifi_saved.h"

/* 100 ms is the cadence wifi_manager.h:80 and :92 document for the
   ready-functions. The ceilings come straight from wifi_manager.c:20-21 so a
   caller never waits longer than the transport itself will. */
#define POLL_MS          (100U)
#define SCAN_CEILING_MS  (35000U)
#define CONN_CEILING_MS  (95000U)

static const char *security_name(uint8_t sec)
{
    /* wifi_manager.h:25 — 0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA3 */
    switch (sec) {
        case 0:  return "open";
        case 1:  return "WEP";
        case 2:  return "WPA";
        case 3:  return "WPA2";
        case 4:  return "WPA3";
        default: return "?";
    }
}

/* Report a failure the way the module actually reports it. wifi_manager
   synthesises its own codes for transport problems rather than inventing a
   cy_rslt_t that means something else — wifi_manager.c:29-33. Naming them is
   the difference between "it failed" and knowing which half failed. */
static void explain_last_error(const char *what)
{
    cy_rslt_t e = wifi_manager_last_error();

    switch ((unsigned long)e) {
        case 0xDEAD5501UL:
            printf("%s: could not hand the request to CM33 (pipe full)\n", what);
            break;
        case 0xDEAD5502UL:
            printf("%s: CM33 did not answer in time\n", what);
            break;
        case 0xDEAD5503UL:
            printf("%s: CM33 answered with something unparseable\n", what);
            break;
        case 0xDEAD5504UL:
            printf("%s: bad argument\n", what);
            break;
        default:
            if (((unsigned long)e & 0xFFFFFF00UL) == 0xDEAD5600UL) {
                printf("%s: CM33 refused it, status 0x%02lX\n",
                       what, (unsigned long)e & 0xFFUL);
            } else {
                printf("%s: cy_rslt_t 0x%08lX\n", what, (unsigned long)e);
            }
            break;
    }
}

/* Non-blocking scan. Returns the number of entries, or -1.
   This is the form to use from any task that also draws. */
static int scan_without_blocking(wifi_mgr_scan_entry_t *out, size_t max_entries)
{
    if (!wifi_manager_scan_start()) {
        explain_last_error("scan_start");
        return -1;
    }

    for (uint32_t waited = 0; waited < SCAN_CEILING_MS; waited += POLL_MS) {
        if (wifi_manager_scan_ready()) {
            return wifi_manager_scan_result(out, max_entries);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        /* Draw a frame here. That is the entire point of this form. */
    }

    /* The ceiling passed. The request is not cancelled — CM33 may still answer
       into the shared response slot after we stop looking, so do not start
       another scan immediately or the two answers interleave. */
    printf("scan: no answer within %u ms\n", (unsigned)SCAN_CEILING_MS);
    return -1;
}

void example_cm55_wifi_join_and_remember(void)
{
    /* wifi_manager_init() does NOT create a task, despite what the header
       comment at wifi_manager.h:47 says — it issues one status request and
       reports whether that round trip worked (wifi_manager.c:183). Treat it as
       "is CM33 answering", not "the radio is up". */
    if (!wifi_manager_init()) {
        explain_last_error("wifi_manager_init");
        return;
    }

    /* ---- 1. what is in range ------------------------------------------- */
    wifi_mgr_scan_entry_t found[WIFI_MGR_SCAN_MAX_ENTRIES];  /* = 6, wifi_manager.h:19 */
    int n = scan_without_blocking(found, WIFI_MGR_SCAN_MAX_ENTRIES);

    if (n < 0) {
        return;
    }
    printf("scan: %d network%s\n", n, (n == 1) ? "" : "s");
    for (int i = 0; i < n; i++) {
        printf("  %-32s %4d dBm  ch%-3u %s\n",
               found[i].ssid, (int)found[i].rssi, (unsigned)found[i].channel,
               security_name(found[i].security));
    }

    /* ---- 2. join ------------------------------------------------------- */
    /* Credentials come from the configuration store, never from source.
       ipc_tesaiot_defs.h:66-68 holds wifi_ssid[33] / wifi_pass[65]. Read them
       through the store rather than pasting them here. */
    /* wifi_saved_load() fills one 106-byte record — ssid, password, security,
       flags and last_used together (wifi_saved.h:28-36). It does not take
       separate buffers; the record IS the buffer. Slots are 0..5
       (WIFI_SAVED_MAX = 6, wifi_saved.h:25). */
    wifi_saved_entry_t saved;
    memset(&saved, 0, sizeof(saved));

    if (!wifi_saved_load(0, &saved)) {
        printf("no saved network yet — add one from the Wi-Fi page first\n");
        return;
    }

    printf("joining %s ...\n", saved.ssid);
    if (!wifi_manager_connect(saved.ssid, saved.password)) {  /* blocks, up to 95 s cold */
        explain_last_error("connect");
        return;
    }

    /* ---- 3. confirm, without trusting the connect return alone ---------- */
    /* wifi_manager_is_connected() blocks (wifi_manager.c:432). Use the
       start/ready/result form so this stays usable from a drawing task. */
    if (!wifi_manager_status_start()) {
        explain_last_error("status_start");
        return;
    }

    wifi_mgr_status_t st;
    bool have_status = false;

    for (uint32_t waited = 0; waited < CONN_CEILING_MS; waited += POLL_MS) {
        if (wifi_manager_status_ready()) {
            have_status = wifi_manager_status_result(&st);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }

    if (!have_status) {
        printf("joined, but CM33 never reported the resulting state\n");
        return;
    }

    if (!st.connected) {
        printf("connect returned success but the link is down — retry\n");
        return;
    }

    printf("connected: %s  %d dBm  ip %s\n", st.ssid, (int)st.rssi, st.ip_addr);
    printf("  mac %02X:%02X:%02X:%02X:%02X:%02X\n",
           st.mac_addr[0], st.mac_addr[1], st.mac_addr[2],
           st.mac_addr[3], st.mac_addr[4], st.mac_addr[5]);

    /* wifi_manager_get_ip() returns the cached string and reads "0.0.0.0"
       while the link is down (wifi_manager.c:45). It does not block, which
       makes it the right call for a status line that redraws every frame. */
    printf("  cached ip: %s\n", wifi_manager_get_ip());

    /* ---- 4. remember it ------------------------------------------------ */
    /* wifi_saved_find() first: adding a duplicate grows the list without
       changing behaviour, and the list is small. */
    /* wifi_saved_add() takes the security type as well — the store keeps it so
       a later auto-connect does not have to scan to find out (wifi_saved.h:81). */
    if (wifi_saved_find(saved.ssid) < 0) {
        if (wifi_saved_add(saved.ssid, saved.password, saved.security)) {
            printf("saved — %d network%s remembered\n",
                   wifi_saved_count(), (wifi_saved_count() == 1) ? "" : "s");
        } else {
            printf("could not save — the list may be full\n");
        }
    }

    /* Leaving the network is cheap and uses the short timeout, because no
       radio bring-up is involved (wifi_manager.c:410, :26). */
    /* wifi_manager_disconnect(); */
}
