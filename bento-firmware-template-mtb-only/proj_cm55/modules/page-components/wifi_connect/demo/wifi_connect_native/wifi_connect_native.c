/*******************************************************************************
 * File: wifi_connect_native.c
 *
 * Description:
 *   WiFi Connect page using the original AIC-EEC WiFi UI core (aic_wifi.*)
 *   with backend operations bridged to local wifi_manager.
 *
 ******************************************************************************/

#include "wifi_connect_native.h"
#include "aic_wifi.h"
#include "wifi_saved.h"
#include "page_manager.h"
#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    aic_wifi_ctx_t *ui;
    bool was_connected;
    uint32_t tick;
    /* Deferred connect parameters (stored before lv_timer fires) */
    char pending_ssid[WIFI_SSID_MAX_LEN];
    char pending_password[64];
    uint8_t pending_security;
} wifi_native_ctx_t;

static wifi_native_ctx_t s_ctx;

/* Forward declarations */
static void wifi_native_deferred_connect_cb(lv_timer_t *timer);
static void wifi_native_screen_loaded_cb(lv_event_t *e);

/* Persistent cache survives page create/destroy — instant restore on re-enter */
static struct {
    ipc_wifi_scan_t  scan;
    wifi_mgr_status_t status;
    bool valid;
} s_cache;

static void wifi_native_apply_details_from_status(const wifi_mgr_status_t *st,
                                                   uint8_t channel)
{
    if (s_ctx.ui == NULL || st == NULL) {
        return;
    }

    /* TCP/IP tab — populated from status response */
    ipc_wifi_tcpip_t tcpip;
    memset(&tcpip, 0, sizeof(tcpip));
    tcpip.dhcp_enabled = 1U;
    tcpip.subnet[0] = 255U;
    tcpip.subnet[1] = 255U;
    tcpip.subnet[2] = 255U;
    tcpip.subnet[3] = 0U;

    unsigned int ip0 = 0U, ip1 = 0U, ip2 = 0U, ip3 = 0U;
    if (st->ip_addr[0] != '\0' &&
        sscanf(st->ip_addr, "%u.%u.%u.%u", &ip0, &ip1, &ip2, &ip3) == 4) {
        tcpip.ip_addr[0] = (uint8_t)ip0;
        tcpip.ip_addr[1] = (uint8_t)ip1;
        tcpip.ip_addr[2] = (uint8_t)ip2;
        tcpip.ip_addr[3] = (uint8_t)ip3;
        tcpip.gateway[0] = (uint8_t)ip0;
        tcpip.gateway[1] = (uint8_t)ip1;
        tcpip.gateway[2] = (uint8_t)ip2;
        tcpip.gateway[3] = 1U;
        tcpip.dns1[0] = (uint8_t)ip0;
        tcpip.dns1[1] = (uint8_t)ip1;
        tcpip.dns1[2] = (uint8_t)ip2;
        tcpip.dns1[3] = 1U;
    }
    aic_wifi_update_tcpip(s_ctx.ui, &tcpip);

    /* Hardware tab — update only MAC from status; RSSI/Channel/Security
     * already populated from scan data in aic_wifi_update_networks(). */
    if (st->mac_addr[0] || st->mac_addr[1] || st->mac_addr[2] ||
        st->mac_addr[3] || st->mac_addr[4] || st->mac_addr[5]) {
        char mac_str[20];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 st->mac_addr[0], st->mac_addr[1], st->mac_addr[2],
                 st->mac_addr[3], st->mac_addr[4], st->mac_addr[5]);
        lv_label_set_text(s_ctx.ui->lbl_mac, mac_str);
    }
}

/*******************************************************************************
 * Saved Networks helpers
 ******************************************************************************/

//! [cm55_wifi_saved_load_all_sync]
static void wifi_native_refresh_saved(void)
{
    if (!s_ctx.ui) return;

    wifi_saved_entry_t entries[WIFI_SAVED_MAX];
    int count = wifi_saved_load_all(entries);
    aic_wifi_set_saved_networks(s_ctx.ui, entries, count);
}
//! [cm55_wifi_saved_load_all_sync]

static void wifi_native_on_saved_connect(const char *ssid, const char *password,
                                          uint8_t security)
{
    if (!s_ctx.ui || !ssid) return;
    (void)security;

    /* Stash parameters and trigger deferred connect */
    strncpy(s_ctx.pending_ssid, ssid, sizeof(s_ctx.pending_ssid) - 1);
    s_ctx.pending_ssid[sizeof(s_ctx.pending_ssid) - 1] = '\0';
    strncpy(s_ctx.pending_password, password ? password : "",
            sizeof(s_ctx.pending_password) - 1);
    s_ctx.pending_password[sizeof(s_ctx.pending_password) - 1] = '\0';
    s_ctx.pending_security = security;

    aic_wifi_set_state(s_ctx.ui, WIFI_STATE_CONNECTING);
    lv_timer_create(wifi_native_deferred_connect_cb, 50, NULL);
}

//! [cm55_wifi_saved_find_erase]
static void wifi_native_on_saved_delete(int saved_index)
{
    /* Find the OPTIGA slot index for this entry */
    if (!s_ctx.ui) return;

    if (saved_index < 0 || saved_index >= s_ctx.ui->saved_count) return;

    /* Find the matching slot by SSID (saved_index is UI order, need OPTIGA slot) */
    const char *ssid = s_ctx.ui->saved_entries[saved_index].ssid;
    int slot = wifi_saved_find(ssid);
    if (slot >= 0) {
        wifi_saved_erase(slot);
    }

    /* Refresh saved networks UI */
    wifi_native_refresh_saved();
}
//! [cm55_wifi_saved_find_erase]

/*******************************************************************************
 * Phase 3: Async saved-networks loading (OPTIGA probe + one slot per tick)
 *
 * Instead of blocking for 1.5s+ inside a single timer callback, we:
 *   3a. Start OPTIGA probe IPC (instant) → poll every 100ms
 *   3b. After probe: load one slot per timer tick (~50-100ms each)
 *   3c. After all slots: update saved networks UI
 ******************************************************************************/

static wifi_saved_entry_t s_saved_buf[WIFI_SAVED_MAX];
static int s_saved_count;
static int s_saved_load_idx;

/* State machine for fully async slot loading */
enum { SLOT_SEND, SLOT_POLL };
static int s_slot_state;

//! [cm55_wifi_saved_read_slot_machine]
/* Phase 3c: Async slot read — state machine: SEND (instant) → POLL (instant) */
static void wifi_native_saved_slot_cb(lv_timer_t *timer)
{
    if (s_ctx.ui == NULL) { lv_timer_delete(timer); return; }

    if (s_slot_state == SLOT_SEND) {
        if (s_saved_load_idx >= WIFI_SAVED_MAX) {
            lv_timer_delete(timer);
            aic_wifi_set_saved_networks(s_ctx.ui, s_saved_buf, s_saved_count);
            return;
        }
        /* Send IPC for this slot — returns instantly */
        if (wifi_saved_read_start(s_saved_load_idx)) {
            s_slot_state = SLOT_POLL;
        } else {
            /* Send failed — skip this slot */
            s_saved_load_idx++;
        }
        return;
    }

    /* SLOT_POLL: check if IPC response is ready */
    if (!wifi_saved_read_ready()) return;  /* Not ready — try next tick */

    wifi_saved_entry_t tmp;
    if (wifi_saved_read_result(&tmp)) {
        memcpy(&s_saved_buf[s_saved_count], &tmp, sizeof(tmp));
        s_saved_count++;
    }
    s_saved_load_idx++;
    s_slot_state = SLOT_SEND;  /* Next tick: send for next slot */
}
//! [cm55_wifi_saved_read_slot_machine]

//! [cm55_wifi_saved_probe_sequence]
/* Phase 3b: Probe poll — fires every 100ms until OPTIGA probe completes */
static void wifi_native_saved_probe_poll_cb(lv_timer_t *timer)
{
    if (!wifi_saved_probe_ready()) return;

    lv_timer_delete(timer);
    wifi_saved_probe_finish();

    /* Start async slot loading — state machine: send → poll → next */
    s_saved_count = 0;
    s_saved_load_idx = 0;
    s_slot_state = SLOT_SEND;
    lv_timer_create(wifi_native_saved_slot_cb, 50, NULL);
}

/* Phase 3a: Start async OPTIGA probe */
static void wifi_native_deferred_load_saved_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    if (s_ctx.ui == NULL) return;

    wifi_saved_probe_start();

    if (wifi_saved_probe_ready()) {
        /* Already probed (cached) — skip poll, go straight to slot loading */
        wifi_saved_probe_finish();
        s_saved_count = 0;
        s_saved_load_idx = 0;
        s_slot_state = SLOT_SEND;
        lv_timer_create(wifi_native_saved_slot_cb, 50, NULL);
    } else {
        /* Probe IPC in flight — poll until ready */
        lv_timer_create(wifi_native_saved_probe_poll_cb, 100, NULL);
    }
}
//! [cm55_wifi_saved_probe_sequence]

/*******************************************************************************
 * Scan — non-blocking async pattern
 *
 * Problem: wifi_manager_scan() blocks for 5-35s inside LVGL timer callback,
 * which prevents lv_timer_handler() from processing input events. Result:
 * buttons (including "< Home") don't respond during scan.
 *
 * Solution: Split into send (instant) + poll (100ms periodic timer).
 * Each timer callback returns in microseconds, keeping LVGL responsive.
 ******************************************************************************/

/*******************************************************************************
 * 3-phase async pipeline — each phase returns in microseconds so LVGL stays
 * responsive throughout the entire WiFi scan → status → saved-networks flow.
 *
 * Phase 1 (scan_poll_cb):  Scan ready → show SSID list → start async status
 * Phase 2 (status_poll_cb): Status ready → mark connected → defer saved load
 * Phase 3 (deferred_load_saved_cb): Load saved networks (OPTIGA probe ≤1.5s)
 ******************************************************************************/

/* Temporary scan data shared between phase 1 and phase 2 */
static ipc_wifi_scan_t s_pending_scan;

/* Phase 2: Status poll — fires every 100ms until status IPC completes */
static void wifi_native_status_poll_cb(lv_timer_t *timer)
{
    if (!wifi_manager_status_ready()) return;  /* Not ready — try next tick */

    lv_timer_delete(timer);

    if (s_ctx.ui == NULL) return;

    wifi_mgr_status_t st;
    memset(&st, 0, sizeof(st));
    wifi_manager_status_result(&st);

    /* Mark connected SSID in the scan list */
    for (uint8_t i = 0; i < s_pending_scan.count; i++) {
        if (st.connected && st.ssid[0] &&
            strncmp(st.ssid, s_pending_scan.networks[i].ssid, WIFI_SSID_MAX_LEN) == 0) {
            s_pending_scan.networks[i].flags |= 0x01U;
            s_pending_scan.connected_idx = (int8_t)i;
        }
    }

    /* Re-update networks with connected flag now set */
    aic_wifi_update_networks(s_ctx.ui, &s_pending_scan);
    aic_wifi_set_state(s_ctx.ui, st.connected ? WIFI_STATE_CONNECTED : WIFI_STATE_DISCONNECTED);
    if (st.connected) {
        uint8_t conn_ch = 0U;
        if (s_pending_scan.connected_idx >= 0 &&
            s_pending_scan.connected_idx < (int)s_pending_scan.count) {
            conn_ch = s_pending_scan.networks[s_pending_scan.connected_idx].channel;
        }
        wifi_native_apply_details_from_status(&st, conn_ch);
    }

    /* Cache for instant restore on re-enter */
    memcpy(&s_cache.scan, &s_pending_scan, sizeof(s_pending_scan));
    memcpy(&s_cache.status, &st, sizeof(st));
    s_cache.valid = true;

    /* Saved networks are NOT loaded on page entry — eliminates IPC pipe
     * conflict that causes UI hang.  Saved list populates after user
     * connects or deletes a network (user-initiated, blocking OK). */
}

/* Phase 1: Scan poll — fires every 100ms until scan IPC completes */
static void wifi_native_scan_poll_cb(lv_timer_t *timer)
{
    if (!wifi_manager_scan_ready()) return;  /* Not ready yet — try next tick */

    lv_timer_delete(timer);

    if (s_ctx.ui == NULL) return;

    wifi_mgr_scan_entry_t entries[WIFI_MGR_SCAN_MAX_ENTRIES];
    memset(entries, 0, sizeof(entries));

    const int n = wifi_manager_scan_result(entries, WIFI_MGR_SCAN_MAX_ENTRIES);
    if (n < 0) {
        aic_wifi_set_state(s_ctx.ui, WIFI_STATE_ERROR);
        aic_wifi_show_error(s_ctx.ui, WIFI_ERR_SCAN_FAILED, "Scan failed");
        return;
    }

    /* Build scan data and show SSID list IMMEDIATELY (no status wait) */
    memset(&s_pending_scan, 0, sizeof(s_pending_scan));
    s_pending_scan.connected_idx = -1;
    s_pending_scan.count = (uint8_t)((n > (int)WIFI_SCAN_MAX_NETWORKS) ? WIFI_SCAN_MAX_NETWORKS : n);

    for (uint8_t i = 0; i < s_pending_scan.count; i++) {
        strncpy(s_pending_scan.networks[i].ssid, entries[i].ssid, WIFI_SSID_MAX_LEN - 1U);
        s_pending_scan.networks[i].ssid[WIFI_SSID_MAX_LEN - 1U] = '\0';
        s_pending_scan.networks[i].rssi = entries[i].rssi;
        s_pending_scan.networks[i].security = entries[i].security;
        s_pending_scan.networks[i].channel = entries[i].channel;
        s_pending_scan.networks[i].band = WIFI_BAND_2_4GHZ;
        s_pending_scan.networks[i].flags = 0U;
    }

    /* Show SSID list NOW — connected flag will be updated in phase 2 */
    aic_wifi_update_networks(s_ctx.ui, &s_pending_scan);
    aic_wifi_set_state(s_ctx.ui, WIFI_STATE_DISCONNECTED);  /* Assume until status confirms */

    /* Start async status check → phase 2 */
    if (wifi_manager_status_start()) {
        lv_timer_create(wifi_native_status_poll_cb, 100, NULL);
    } else {
        /* Status send failed — saved networks skipped on page entry */
    }
}

/* Blocking scan — used after connect/disconnect where blocking is acceptable */
static void wifi_native_scan_and_update(void)
{
    if (s_ctx.ui == NULL) return;

    wifi_mgr_scan_entry_t entries[WIFI_MGR_SCAN_MAX_ENTRIES];
    memset(entries, 0, sizeof(entries));

    ipc_wifi_scan_t scan_data;
    memset(&scan_data, 0, sizeof(scan_data));
    scan_data.connected_idx = -1;

    const int n = wifi_manager_scan(entries, WIFI_MGR_SCAN_MAX_ENTRIES);
    if (n < 0) {
        aic_wifi_set_state(s_ctx.ui, WIFI_STATE_ERROR);
        aic_wifi_show_error(s_ctx.ui, WIFI_ERR_SCAN_FAILED, "Scan failed");
        return;
    }

    wifi_mgr_status_t st;
    memset(&st, 0, sizeof(st));
    wifi_manager_get_status(&st);

    scan_data.count = (uint8_t)((n > (int)WIFI_SCAN_MAX_NETWORKS) ? WIFI_SCAN_MAX_NETWORKS : n);
    for (uint8_t i = 0; i < scan_data.count; i++) {
        strncpy(scan_data.networks[i].ssid, entries[i].ssid, WIFI_SSID_MAX_LEN - 1U);
        scan_data.networks[i].ssid[WIFI_SSID_MAX_LEN - 1U] = '\0';
        scan_data.networks[i].rssi = entries[i].rssi;
        scan_data.networks[i].security = entries[i].security;
        scan_data.networks[i].channel = entries[i].channel;
        scan_data.networks[i].band = WIFI_BAND_2_4GHZ;
        scan_data.networks[i].flags = 0U;

        if (st.connected && st.ssid[0] && strncmp(st.ssid, entries[i].ssid, WIFI_SSID_MAX_LEN) == 0) {
            scan_data.networks[i].flags |= 0x01U;
            scan_data.connected_idx = (int8_t)i;
        }
    }

    aic_wifi_update_networks(s_ctx.ui, &scan_data);
    aic_wifi_set_state(s_ctx.ui, st.connected ? WIFI_STATE_CONNECTED : WIFI_STATE_DISCONNECTED);
    if (st.connected) {
        uint8_t conn_ch = 0U;
        if (scan_data.connected_idx >= 0 && scan_data.connected_idx < (int)scan_data.count) {
            conn_ch = scan_data.networks[scan_data.connected_idx].channel;
        }
        wifi_native_apply_details_from_status(&st, conn_ch);
    }

    memcpy(&s_cache.scan, &scan_data, sizeof(scan_data));
    memcpy(&s_cache.status, &st, sizeof(st));
    s_cache.valid = true;
}

/* Deferred scan — sends IPC and starts poll timer (non-blocking) */
//! [cm55_wifi_connect_ui_flow]
static void wifi_native_deferred_scan_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    if (s_ctx.ui == NULL) return;

    if (!wifi_manager_scan_start()) {
        aic_wifi_set_state(s_ctx.ui, WIFI_STATE_ERROR);
        aic_wifi_show_error(s_ctx.ui, WIFI_ERR_SCAN_FAILED, "Scan failed");
        return;
    }

    /* Poll every 100ms — each callback returns instantly until scan completes.
     * LVGL processes input events between polls → buttons stay responsive. */
    lv_timer_create(wifi_native_scan_poll_cb, 100, NULL);
}

static void wifi_native_on_scan(void)
{
    if (s_ctx.ui == NULL) return;
    aic_wifi_set_state(s_ctx.ui, WIFI_STATE_SCANNING);
    /* Non-blocking: send IPC + start poll timer */
    lv_timer_create(wifi_native_deferred_scan_cb, 50, NULL);
}

static void wifi_native_on_select(int index, const char *ssid)
{
    /* No IPC call here — details already populated by scan_and_update.
     * Calling get_status blocks LVGL thread for 5s per tap. */
    (void)index;
    (void)ssid;
}

static void wifi_native_deferred_connect_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    if (s_ctx.ui == NULL) {
        return;
    }

    //! [cm55_wifi_saved_add_after_connect]
    /* ...context: inside the deferred connect callback, after a confirmed association ... */
    /* Auto-retry: cy_wcm_connect_ap() often fails on the first attempt
     * after a scan due to internal WCM state transition. Retry once. */
    bool ok = wifi_manager_connect(s_ctx.pending_ssid, s_ctx.pending_password);
    if (!ok) {
        ok = wifi_manager_connect(s_ctx.pending_ssid, s_ctx.pending_password);
    }
    if (!ok) {
        aic_wifi_set_state(s_ctx.ui, WIFI_STATE_ERROR);
        aic_wifi_show_error(s_ctx.ui, WIFI_ERR_AUTH_FAILED, "Connect failed");
        return;
    }

    /* Auto-save credentials on successful connect */
    wifi_saved_add(s_ctx.pending_ssid, s_ctx.pending_password, s_ctx.pending_security);
    wifi_native_refresh_saved();

    wifi_native_scan_and_update();
}
//! [cm55_wifi_saved_add_after_connect]

static void wifi_native_on_connect(const char *ssid, const char *password, uint8_t security)
{
    (void)security;
    if (s_ctx.ui == NULL) {
        return;
    }

    /* Stash parameters for deferred callback */
    strncpy(s_ctx.pending_ssid, ssid, sizeof(s_ctx.pending_ssid) - 1);
    s_ctx.pending_ssid[sizeof(s_ctx.pending_ssid) - 1] = '\0';
    strncpy(s_ctx.pending_password, password, sizeof(s_ctx.pending_password) - 1);
    s_ctx.pending_password[sizeof(s_ctx.pending_password) - 1] = '\0';

    aic_wifi_set_state(s_ctx.ui, WIFI_STATE_CONNECTING);
    /* Defer connect so LVGL can render "Connecting..." before blocking IPC */
    lv_timer_create(wifi_native_deferred_connect_cb, 50, NULL);
}
//! [cm55_wifi_connect_ui_flow]

static void wifi_native_deferred_disconnect_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    wifi_manager_disconnect();
    if (s_ctx.ui) {
        aic_wifi_set_state(s_ctx.ui, WIFI_STATE_DISCONNECTED);
    }
    wifi_native_scan_and_update();
}

static void wifi_native_on_disconnect(void)
{
    /* Defer disconnect so LVGL can render state change first */
    lv_timer_create(wifi_native_deferred_disconnect_cb, 50, NULL);
}

static void wifi_native_back_cb(lv_event_t *e)
{
    page_manager_t *pm = (page_manager_t *)lv_event_get_user_data(e);
    if (pm) {
        pm_back(pm);
    }
}

/*******************************************************************************
 * Screen loaded callback — fires AFTER page transition animation completes
 ******************************************************************************/
static void wifi_native_screen_loaded_cb(lv_event_t *e)
{
    (void)e;
    if (s_ctx.ui == NULL) return;

    /* WiFi scan — deferred 200ms to allow page to fully render.
     * Saved networks are loaded AFTER scan completes (in scan_poll_cb)
     * so OPTIGA probe doesn't block page entry. */
    lv_timer_create(wifi_native_deferred_scan_cb, 200, NULL);
}

lv_obj_t *wifi_connect_native_create(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_ctx.ui = aic_wifi_init(scr);
    if (s_ctx.ui == NULL) {
        return scr;
    }

    aic_wifi_set_scan_cb(s_ctx.ui, wifi_native_on_scan);
    aic_wifi_set_select_cb(s_ctx.ui, wifi_native_on_select);
    aic_wifi_set_connect_cb(s_ctx.ui, wifi_native_on_connect);
    aic_wifi_set_disconnect_cb(s_ctx.ui, wifi_native_on_disconnect);
    aic_wifi_set_saved_connect_cb(s_ctx.ui, wifi_native_on_saved_connect);
    aic_wifi_set_saved_delete_cb(s_ctx.ui, wifi_native_on_saved_delete);

    lv_obj_t *home_btn = lv_button_create(scr);
    lv_obj_set_size(home_btn, 92, 34);
    lv_obj_set_pos(home_btn, 10, 8);
    lv_obj_set_style_bg_color(home_btn, lv_color_hex(0x2F2F2F), 0);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_btn, 0, 0);
    lv_obj_set_style_radius(home_btn, 16, 0);
    lv_obj_move_foreground(home_btn);

    lv_obj_t *home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, LV_SYMBOL_LEFT " Home");
    lv_obj_set_style_text_color(home_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(home_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(home_lbl);
    lv_obj_add_event_cb(home_btn, wifi_native_back_cb, LV_EVENT_CLICKED, pm_get_instance());

    /* Restore cached state instantly (no IPC wait on re-enter) */
    if (s_cache.valid) {
        aic_wifi_update_networks(s_ctx.ui, &s_cache.scan);
        aic_wifi_set_state(s_ctx.ui, s_cache.status.connected
                            ? WIFI_STATE_CONNECTED : WIFI_STATE_DISCONNECTED);
        if (s_cache.status.connected) {
            uint8_t conn_ch = 0U;
            if (s_cache.scan.connected_idx >= 0 &&
                s_cache.scan.connected_idx < (int)s_cache.scan.count) {
                conn_ch = s_cache.scan.networks[s_cache.scan.connected_idx].channel;
            }
            wifi_native_apply_details_from_status(&s_cache.status, conn_ch);
        }
    } else {
        aic_wifi_set_state(s_ctx.ui, WIFI_STATE_SCANNING);
    }

    /* Defer heavy IPC work to AFTER page transition animation completes.
     * LV_EVENT_SCREEN_LOADED fires when lv_screen_load_anim() finishes
     * (300ms slide-left). This prevents blocking the GFX task during
     * animation, eliminating the stutter/freeze visual. */
    lv_obj_add_event_cb(scr, wifi_native_screen_loaded_cb,
                        LV_EVENT_SCREEN_LOADED, NULL);
    return scr;
}

void wifi_connect_native_render(sensorhub_snapshot_t *snap)
{
    (void)snap;
    /* All WiFi state updates happen via scan/connect/disconnect callbacks.
     * No periodic IPC polling here — it would block the LVGL render thread. */
}

void wifi_connect_native_destroy(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
}
