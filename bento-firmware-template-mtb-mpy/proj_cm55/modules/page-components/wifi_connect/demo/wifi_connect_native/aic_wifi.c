/*******************************************************************************
 * File: aic_wifi.c
 * Description: WiFi UI Helper Functions Implementation
 *
 * Part of BiiL Course: Embedded C for IoT - Week 7
 ******************************************************************************/

#include "aic_wifi.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Local Variables
 ******************************************************************************/

static aic_wifi_ctx_t* g_wifi_ctx = NULL;

/*******************************************************************************
 * Forward Declarations
 ******************************************************************************/

static void create_sidebar(aic_wifi_ctx_t* ctx, lv_obj_t* parent);
static void create_details_panel(aic_wifi_ctx_t* ctx, lv_obj_t* parent);
static void network_item_click_cb(lv_event_t* e);
static void scan_btn_click_cb(lv_event_t* e);
static void connect_btn_click_cb(lv_event_t* e);
static void password_kb_cb(lv_event_t* e);
static void update_connect_button(aic_wifi_ctx_t* ctx);
static void saved_item_click_cb(lv_event_t* e);
static void saved_delete_click_cb(lv_event_t* e);

/*******************************************************************************
 * Initialization Functions
 ******************************************************************************/

aic_wifi_ctx_t* aic_wifi_init(lv_obj_t* parent)
{
    aic_wifi_ctx_t* ctx = lv_malloc(sizeof(aic_wifi_ctx_t));
    if (!ctx) return NULL;

    memset(ctx, 0, sizeof(aic_wifi_ctx_t));
    ctx->selected_index = -1;
    ctx->state = WIFI_STATE_DISCONNECTED;
    g_wifi_ctx = ctx;

    /* Create main screen/container */
    if (parent == NULL) {
        ctx->main_screen = lv_obj_create(lv_screen_active());
    } else {
        ctx->main_screen = lv_obj_create(parent);
    }

    lv_obj_set_size(ctx->main_screen, AIC_WIFI_SCREEN_WIDTH, AIC_WIFI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(ctx->main_screen, AIC_WIFI_COLOR_BG, 0);
    lv_obj_set_style_border_width(ctx->main_screen, 0, 0);
    lv_obj_set_style_radius(ctx->main_screen, 0, 0);
    lv_obj_set_style_pad_all(ctx->main_screen, 0, 0);
    lv_obj_center(ctx->main_screen);

    /* Create layout: sidebar (left) + details (right) */
    lv_obj_set_flex_flow(ctx->main_screen, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->main_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Create sidebar with network list */
    create_sidebar(ctx, ctx->main_screen);

    /* Create details panel */
    create_details_panel(ctx, ctx->main_screen);

    return ctx;
}

void aic_wifi_deinit(aic_wifi_ctx_t* ctx)
{
    if (!ctx) return;

    if (ctx->main_screen) {
        lv_obj_delete(ctx->main_screen);
    }

    lv_free(ctx);

    if (g_wifi_ctx == ctx) {
        g_wifi_ctx = NULL;
    }
}

/*******************************************************************************
 * Sidebar Creation (Network List)
 ******************************************************************************/

static void create_sidebar(aic_wifi_ctx_t* ctx, lv_obj_t* parent)
{
    /* Sidebar container - 280px width */
    lv_obj_t* sidebar = lv_obj_create(parent);
    lv_obj_set_size(sidebar, 280, AIC_WIFI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(sidebar, AIC_WIFI_COLOR_SIDEBAR, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 10, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(sidebar, 0);

    /* WiFi title */
    lv_obj_t* title = lv_label_create(sidebar);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, AIC_WIFI_COLOR_TEXT, 0);
    lv_obj_set_style_pad_bottom(title, 10, 0);

    /* Status line */
    ctx->status_bar = lv_label_create(sidebar);
    lv_label_set_text(ctx->status_bar, "Tap Scan to refresh");
    lv_obj_set_style_text_font(ctx->status_bar, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->status_bar, AIC_WIFI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_pad_bottom(ctx->status_bar, 10, 0);

    /* Network list container */
    ctx->network_list = lv_obj_create(sidebar);
    lv_obj_set_size(ctx->network_list, 260, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(ctx->network_list, 380, 0);
    lv_obj_set_style_bg_color(ctx->network_list, AIC_WIFI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(ctx->network_list, 0, 0);
    lv_obj_set_style_radius(ctx->network_list, 8, 0);
    lv_obj_set_style_pad_all(ctx->network_list, 5, 0);
    lv_obj_set_flex_flow(ctx->network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ctx->network_list, LV_DIR_VER);
    lv_obj_set_flex_grow(ctx->network_list, 1);

    /* Scan button */
    ctx->scan_btn = lv_button_create(sidebar);
    lv_obj_set_size(ctx->scan_btn, 260, 40);
    lv_obj_set_style_bg_color(ctx->scan_btn, AIC_WIFI_COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_radius(ctx->scan_btn, 8, 0);
    lv_obj_add_event_cb(ctx->scan_btn, scan_btn_click_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t* scan_lbl = lv_label_create(ctx->scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_center(scan_lbl);
    lv_obj_set_style_text_color(scan_lbl, AIC_WIFI_COLOR_TEXT, 0);

    /* Saved Networks section removed — OPTIGA IPC causes pipe conflict
     * that hangs LVGL.  Saved creds managed via connect/delete callbacks. */
    ctx->saved_header = NULL;
    ctx->saved_list = NULL;
}

/*******************************************************************************
 * Details Panel Creation
 ******************************************************************************/

/* Helper: create a non-interactive info row (label: value) */
static lv_obj_t* create_info_row(lv_obj_t* parent)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return row;
}

static void create_details_panel(aic_wifi_ctx_t* ctx, lv_obj_t* parent)
{
    /* Details panel - remaining width */
    ctx->details_panel = lv_obj_create(parent);
    lv_obj_set_size(ctx->details_panel, 520, AIC_WIFI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(ctx->details_panel, AIC_WIFI_COLOR_BG, 0);
    lv_obj_set_style_border_width(ctx->details_panel, 0, 0);
    lv_obj_set_style_radius(ctx->details_panel, 0, 0);
    lv_obj_set_style_pad_all(ctx->details_panel, 20, 0);
    lv_obj_set_flex_flow(ctx->details_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(ctx->details_panel, 1);

    /* Network name header */
    ctx->lbl_ssid = lv_label_create(ctx->details_panel);
    lv_label_set_text(ctx->lbl_ssid, "Select a network");
    lv_obj_set_style_text_font(ctx->lbl_ssid, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ctx->lbl_ssid, AIC_WIFI_COLOR_TEXT, 0);

    /* Status label */
    ctx->lbl_status = lv_label_create(ctx->details_panel);
    lv_label_set_text(ctx->lbl_status, "Not connected");
    lv_obj_set_style_text_font(ctx->lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->lbl_status, AIC_WIFI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_pad_bottom(ctx->lbl_status, 20, 0);

    /* Create tabview for TCP/IP and Hardware */
    lv_obj_t* tabview = lv_tabview_create(ctx->details_panel);
    lv_obj_set_size(tabview, 480, 320);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 40);
    lv_obj_set_style_bg_color(tabview, AIC_WIFI_COLOR_CARD, 0);
    lv_obj_set_style_radius(tabview, 8, 0);
    lv_obj_set_flex_grow(tabview, 1);

    /* TCP/IP Tab */
    lv_obj_t* tab_tcpip = lv_tabview_add_tab(tabview, "TCP/IP");
    lv_obj_set_style_pad_all(tab_tcpip, 15, 0);
    lv_obj_set_flex_flow(tab_tcpip, LV_FLEX_FLOW_COLUMN);

    /* IP Address */
    lv_obj_t* row1 = create_info_row(tab_tcpip);

    lv_obj_t* lbl = lv_label_create(row1);
    lv_label_set_text(lbl, "IP Address:");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_ip = lv_label_create(row1);
    lv_label_set_text(ctx->lbl_ip, "--");
    lv_obj_set_style_text_color(ctx->lbl_ip, AIC_WIFI_COLOR_TEXT, 0);

    /* Subnet Mask */
    lv_obj_t* row2 = create_info_row(tab_tcpip);

    lbl = lv_label_create(row2);
    lv_label_set_text(lbl, "Subnet Mask:");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_subnet = lv_label_create(row2);
    lv_label_set_text(ctx->lbl_subnet, "--");
    lv_obj_set_style_text_color(ctx->lbl_subnet, AIC_WIFI_COLOR_TEXT, 0);

    /* Router */
    lv_obj_t* row3 = create_info_row(tab_tcpip);

    lbl = lv_label_create(row3);
    lv_label_set_text(lbl, "Router:");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_router = lv_label_create(row3);
    lv_label_set_text(ctx->lbl_router, "--");
    lv_obj_set_style_text_color(ctx->lbl_router, AIC_WIFI_COLOR_TEXT, 0);

    /* DNS */
    lv_obj_t* row4 = create_info_row(tab_tcpip);

    lbl = lv_label_create(row4);
    lv_label_set_text(lbl, "DNS:");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_dns = lv_label_create(row4);
    lv_label_set_text(ctx->lbl_dns, "--");
    lv_obj_set_style_text_color(ctx->lbl_dns, AIC_WIFI_COLOR_TEXT, 0);

    /* Hardware Tab */
    lv_obj_t* tab_hw = lv_tabview_add_tab(tabview, "Hardware");
    lv_obj_set_style_pad_all(tab_hw, 15, 0);
    lv_obj_set_flex_flow(tab_hw, LV_FLEX_FLOW_COLUMN);

    /* MAC Address */
    lv_obj_t* hw_row1 = create_info_row(tab_hw);

    lbl = lv_label_create(hw_row1);
    lv_label_set_text(lbl, "MAC Address:");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_mac = lv_label_create(hw_row1);
    lv_label_set_text(ctx->lbl_mac, "--");
    lv_obj_set_style_text_color(ctx->lbl_mac, AIC_WIFI_COLOR_TEXT, 0);

    /* RSSI */
    lv_obj_t* hw_row2 = create_info_row(tab_hw);

    lbl = lv_label_create(hw_row2);
    lv_label_set_text(lbl, "Signal (RSSI):");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_rssi = lv_label_create(hw_row2);
    lv_label_set_text(ctx->lbl_rssi, "--");
    lv_obj_set_style_text_color(ctx->lbl_rssi, AIC_WIFI_COLOR_TEXT, 0);

    /* Channel */
    lv_obj_t* hw_row3 = create_info_row(tab_hw);

    lbl = lv_label_create(hw_row3);
    lv_label_set_text(lbl, "Channel:");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_channel = lv_label_create(hw_row3);
    lv_label_set_text(ctx->lbl_channel, "--");
    lv_obj_set_style_text_color(ctx->lbl_channel, AIC_WIFI_COLOR_TEXT, 0);

    /* Band */
    lv_obj_t* hw_row4 = create_info_row(tab_hw);

    lbl = lv_label_create(hw_row4);
    lv_label_set_text(lbl, "Band:");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_band = lv_label_create(hw_row4);
    lv_label_set_text(ctx->lbl_band, "--");
    lv_obj_set_style_text_color(ctx->lbl_band, AIC_WIFI_COLOR_TEXT, 0);

    /* Security */
    lv_obj_t* hw_row5 = create_info_row(tab_hw);

    lbl = lv_label_create(hw_row5);
    lv_label_set_text(lbl, "Security:");
    lv_obj_set_style_text_color(lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);

    ctx->lbl_security = lv_label_create(hw_row5);
    lv_label_set_text(ctx->lbl_security, "--");
    lv_obj_set_style_text_color(ctx->lbl_security, AIC_WIFI_COLOR_TEXT, 0);

    /* Connect/Disconnect button */
    ctx->connect_btn = lv_button_create(ctx->details_panel);
    lv_obj_set_size(ctx->connect_btn, 150, 45);
    lv_obj_set_style_bg_color(ctx->connect_btn, AIC_WIFI_COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_radius(ctx->connect_btn, 8, 0);
    lv_obj_add_event_cb(ctx->connect_btn, connect_btn_click_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_flag(ctx->connect_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* btn_lbl = lv_label_create(ctx->connect_btn);
    lv_label_set_text(btn_lbl, "Connect");
    lv_obj_center(btn_lbl);
    lv_obj_set_style_text_color(btn_lbl, AIC_WIFI_COLOR_TEXT, 0);
}

/*******************************************************************************
 * Event Callbacks
 ******************************************************************************/

static void network_item_click_cb(lv_event_t* e)
{
    aic_wifi_ctx_t* ctx = (aic_wifi_ctx_t*)lv_event_get_user_data(e);
    lv_obj_t* item = lv_event_get_current_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(item);

    if (index < 0 || index >= (int)ctx->scan_data.count) return;

    ctx->selected_index = index;
    const ipc_wifi_network_t* net = &ctx->scan_data.networks[index];
    strncpy(ctx->selected_ssid, net->ssid, WIFI_SSID_MAX_LEN - 1);
    ctx->selected_ssid[WIFI_SSID_MAX_LEN - 1] = '\0';

    /* Update details panel */
    lv_label_set_text(ctx->lbl_ssid, net->ssid);

    if (WIFI_IS_CONNECTED(net)) {
        lv_label_set_text(ctx->lbl_status, "Connected");
        lv_obj_set_style_text_color(ctx->lbl_status, AIC_WIFI_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(ctx->lbl_status, "Not connected");
        lv_obj_set_style_text_color(ctx->lbl_status, AIC_WIFI_COLOR_TEXT_DIM, 0);
    }

    /* Update RSSI in hardware tab */
    char rssi_str[32];
    snprintf(rssi_str, sizeof(rssi_str), "%d dBm", net->rssi);
    lv_label_set_text(ctx->lbl_rssi, rssi_str);

    /* Update channel */
    char ch_str[16];
    snprintf(ch_str, sizeof(ch_str), "%d", net->channel);
    lv_label_set_text(ctx->lbl_channel, ch_str);

    /* Update band */
    lv_label_set_text(ctx->lbl_band, wifi_band_to_str(net->band));

    /* Update security */
    lv_label_set_text(ctx->lbl_security, wifi_security_to_str(net->security));

    /* Update highlight on all items */
    lv_obj_t* child = lv_obj_get_child(ctx->network_list, 0);
    int i = 0;
    while (child) {
        if (i == index) {
            lv_obj_set_style_bg_color(child, AIC_WIFI_COLOR_HIGHLIGHT, 0);
        } else {
            lv_obj_set_style_bg_color(child, AIC_WIFI_COLOR_CARD, 0);
        }
        child = lv_obj_get_child(ctx->network_list, ++i);
    }

    /* Show connect button */
    update_connect_button(ctx);
    lv_obj_remove_flag(ctx->connect_btn, LV_OBJ_FLAG_HIDDEN);

    /* Callback */
    if (ctx->on_select) {
        ctx->on_select(index, net->ssid);
    }
}

static void scan_btn_click_cb(lv_event_t* e)
{
    aic_wifi_ctx_t* ctx = (aic_wifi_ctx_t*)lv_event_get_user_data(e);

    lv_label_set_text(ctx->status_bar, "Scanning...");

    if (ctx->on_scan) {
        ctx->on_scan();
    }
}

static void connect_btn_click_cb(lv_event_t* e)
{
    aic_wifi_ctx_t* ctx = (aic_wifi_ctx_t*)lv_event_get_user_data(e);

    if (ctx->selected_index < 0 || ctx->selected_index >= (int)ctx->scan_data.count) return;

    const ipc_wifi_network_t* net = &ctx->scan_data.networks[ctx->selected_index];

    if (WIFI_IS_CONNECTED(net)) {
        /* Disconnect */
        if (ctx->on_disconnect) {
            ctx->on_disconnect();
        }
    } else {
        /* Save pending connect info BEFORE showing dialog */
        strncpy(ctx->pending_ssid, net->ssid, WIFI_SSID_MAX_LEN - 1);
        ctx->pending_ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
        ctx->pending_security = net->security;

        /* Need password? */
        if (net->security != WIFI_SECURITY_OPEN) {
            aic_wifi_show_password_dialog(ctx, net->ssid);
        } else {
            /* Open network - connect directly */
            if (ctx->on_connect) {
                ctx->on_connect(net->ssid, "", net->security);
            }
        }
    }
}

static void password_kb_cb(lv_event_t* e)
{
    aic_wifi_ctx_t* ctx = (aic_wifi_ctx_t*)lv_event_get_user_data(e);
    if (!ctx || !ctx->password_ta) return;

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        /* OK pressed - connect using stored pending_ssid (not index!) */
        const char* password = lv_textarea_get_text(ctx->password_ta);

        if (ctx->pending_ssid[0] != '\0' && ctx->on_connect) {
            ctx->on_connect(ctx->pending_ssid, password, ctx->pending_security);
        }
    }

    /* Hide dialog (async delete to avoid crash inside callback) */
    aic_wifi_hide_password_dialog(ctx);
}

static void update_connect_button(aic_wifi_ctx_t* ctx)
{
    if (ctx->selected_index < 0 || ctx->selected_index >= (int)ctx->scan_data.count) return;

    const ipc_wifi_network_t* net = &ctx->scan_data.networks[ctx->selected_index];
    lv_obj_t* btn_lbl = lv_obj_get_child(ctx->connect_btn, 0);

    if (WIFI_IS_CONNECTED(net)) {
        lv_label_set_text(btn_lbl, "Disconnect");
        lv_obj_set_style_bg_color(ctx->connect_btn, AIC_WIFI_COLOR_ERROR, 0);
    } else {
        lv_label_set_text(btn_lbl, "Connect");
        lv_obj_set_style_bg_color(ctx->connect_btn, AIC_WIFI_COLOR_HIGHLIGHT, 0);
    }
}

/*******************************************************************************
 * Callback Registration
 ******************************************************************************/

void aic_wifi_set_scan_cb(aic_wifi_ctx_t* ctx, aic_wifi_scan_cb_t cb)
{
    if (ctx) ctx->on_scan = cb;
}

void aic_wifi_set_select_cb(aic_wifi_ctx_t* ctx, aic_wifi_select_cb_t cb)
{
    if (ctx) ctx->on_select = cb;
}

void aic_wifi_set_connect_cb(aic_wifi_ctx_t* ctx, aic_wifi_connect_cb_t cb)
{
    if (ctx) ctx->on_connect = cb;
}

void aic_wifi_set_disconnect_cb(aic_wifi_ctx_t* ctx, aic_wifi_disconnect_cb_t cb)
{
    if (ctx) ctx->on_disconnect = cb;
}

/*******************************************************************************
 * Data Update Functions
 ******************************************************************************/

void aic_wifi_update_networks(aic_wifi_ctx_t* ctx, const ipc_wifi_scan_t* scan_data)
{
    if (!ctx || !scan_data) return;

    /* Copy and sort by RSSI (strongest signal first) */
    ipc_wifi_scan_t sorted;
    memcpy(&sorted, scan_data, sizeof(ipc_wifi_scan_t));

    /* Track connected SSID before sorting */
    char connected_ssid[WIFI_SSID_MAX_LEN] = "";
    if (sorted.connected_idx >= 0 && sorted.connected_idx < (int)sorted.count) {
        strncpy(connected_ssid, sorted.networks[sorted.connected_idx].ssid,
                WIFI_SSID_MAX_LEN - 1);
    }

    /* Insertion sort by RSSI descending (strongest first) */
    for (uint8_t i = 1; i < sorted.count && i < WIFI_SCAN_MAX_NETWORKS; i++) {
        ipc_wifi_network_t temp = sorted.networks[i];
        int j = (int)i - 1;
        while (j >= 0 && sorted.networks[j].rssi < temp.rssi) {
            sorted.networks[j + 1] = sorted.networks[j];
            j--;
        }
        sorted.networks[j + 1] = temp;
    }

    /* Update connected_idx after sorting */
    sorted.connected_idx = -1;
    if (connected_ssid[0] != '\0') {
        for (uint8_t i = 0; i < sorted.count; i++) {
            if (strcmp(sorted.networks[i].ssid, connected_ssid) == 0) {
                sorted.connected_idx = (int8_t)i;
                break;
            }
        }
    }

    /* Store sorted scan data */
    memcpy(&ctx->scan_data, &sorted, sizeof(ipc_wifi_scan_t));

    /* Save scroll position before clearing */
    int32_t scroll_y = lv_obj_get_scroll_y(ctx->network_list);

    /* Clear existing list */
    lv_obj_clean(ctx->network_list);

    /* Update status */
    char status[64];
    snprintf(status, sizeof(status), "%d networks found", sorted.count);
    lv_label_set_text(ctx->status_bar, status);

    /* Create network items (already sorted by signal strength) */
    for (uint8_t i = 0; i < sorted.count && i < WIFI_SCAN_MAX_NETWORKS; i++) {
        aic_wifi_create_network_item(ctx->network_list, &sorted.networks[i], i);
    }

    /* Restore scroll position */
    if (scroll_y > 0) {
        lv_obj_scroll_to_y(ctx->network_list, scroll_y, LV_ANIM_OFF);
    }

    /* Re-highlight previously selected network (find by SSID after sort) */
    if (ctx->selected_ssid[0] != '\0') {
        for (uint8_t i = 0; i < sorted.count; i++) {
            if (strcmp(sorted.networks[i].ssid, ctx->selected_ssid) == 0) {
                ctx->selected_index = (int)i;
                lv_obj_t* child = lv_obj_get_child(ctx->network_list, i);
                if (child) {
                    lv_obj_set_style_bg_color(child, AIC_WIFI_COLOR_HIGHLIGHT, 0);
                }
                break;
            }
        }
    }

    /* Auto-select connected network (overrides previous selection) */
    if (sorted.connected_idx >= 0 && sorted.connected_idx < (int)sorted.count) {
        ctx->selected_index = sorted.connected_idx;
        const ipc_wifi_network_t* net = &sorted.networks[sorted.connected_idx];
        strncpy(ctx->selected_ssid, net->ssid, WIFI_SSID_MAX_LEN - 1);
        lv_label_set_text(ctx->lbl_ssid, net->ssid);
        lv_label_set_text(ctx->lbl_status, "Connected");
        lv_obj_set_style_text_color(ctx->lbl_status, AIC_WIFI_COLOR_SUCCESS, 0);

        /* Populate Hardware tab from scan entry data */
        {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%d dBm", net->rssi);
            lv_label_set_text(ctx->lbl_rssi, tmp);
            snprintf(tmp, sizeof(tmp), "%d", net->channel);
            lv_label_set_text(ctx->lbl_channel, tmp);
            lv_label_set_text(ctx->lbl_band, wifi_band_to_str(net->band));
            lv_label_set_text(ctx->lbl_security, wifi_security_to_str(net->security));
            lv_label_set_text(ctx->lbl_mac, "N/A");
        }

        update_connect_button(ctx);
        lv_obj_remove_flag(ctx->connect_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

void aic_wifi_update_tcpip(aic_wifi_ctx_t* ctx, const ipc_wifi_tcpip_t* tcpip)
{
    if (!ctx || !tcpip) return;

    memcpy(&ctx->tcpip_info, tcpip, sizeof(ipc_wifi_tcpip_t));

    char buf[20];

    /* IP Address */
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             tcpip->ip_addr[0], tcpip->ip_addr[1],
             tcpip->ip_addr[2], tcpip->ip_addr[3]);
    lv_label_set_text(ctx->lbl_ip, buf);

    /* Subnet */
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             tcpip->subnet[0], tcpip->subnet[1],
             tcpip->subnet[2], tcpip->subnet[3]);
    lv_label_set_text(ctx->lbl_subnet, buf);

    /* Router */
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             tcpip->gateway[0], tcpip->gateway[1],
             tcpip->gateway[2], tcpip->gateway[3]);
    lv_label_set_text(ctx->lbl_router, buf);

    /* DNS */
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             tcpip->dns1[0], tcpip->dns1[1],
             tcpip->dns1[2], tcpip->dns1[3]);
    lv_label_set_text(ctx->lbl_dns, buf);
}

void aic_wifi_update_hardware(aic_wifi_ctx_t* ctx, const ipc_wifi_hardware_t* hw_info)
{
    if (!ctx || !hw_info) return;

    memcpy(&ctx->hw_info, hw_info, sizeof(ipc_wifi_hardware_t));

    char buf[32];

    /* MAC Address */
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             hw_info->mac_addr[0], hw_info->mac_addr[1],
             hw_info->mac_addr[2], hw_info->mac_addr[3],
             hw_info->mac_addr[4], hw_info->mac_addr[5]);
    lv_label_set_text(ctx->lbl_mac, buf);

    /* RSSI */
    snprintf(buf, sizeof(buf), "%d dBm", hw_info->rssi);
    lv_label_set_text(ctx->lbl_rssi, buf);

    /* Channel */
    snprintf(buf, sizeof(buf), "%d", hw_info->channel);
    lv_label_set_text(ctx->lbl_channel, buf);

    /* Band */
    lv_label_set_text(ctx->lbl_band, wifi_band_to_str(hw_info->band));
}

void aic_wifi_set_state(aic_wifi_ctx_t* ctx, wifi_state_t state)
{
    if (!ctx) return;

    ctx->state = state;

    switch (state) {
        case WIFI_STATE_SCANNING:
            lv_label_set_text(ctx->status_bar, "Scanning...");
            break;
        case WIFI_STATE_CONNECTING:
            lv_label_set_text(ctx->lbl_status, "Connecting...");
            lv_obj_set_style_text_color(ctx->lbl_status, AIC_WIFI_COLOR_WARNING, 0);
            break;
        case WIFI_STATE_CONNECTED:
            lv_label_set_text(ctx->lbl_status, "Connected");
            lv_obj_set_style_text_color(ctx->lbl_status, AIC_WIFI_COLOR_SUCCESS, 0);
            update_connect_button(ctx);
            break;
        case WIFI_STATE_DISCONNECTED:
            lv_label_set_text(ctx->lbl_status, "Not connected");
            lv_obj_set_style_text_color(ctx->lbl_status, AIC_WIFI_COLOR_TEXT_DIM, 0);
            lv_label_set_text(ctx->lbl_ssid, "Select a network");
            /* Reset TCP/IP labels */
            lv_label_set_text(ctx->lbl_ip, "--");
            lv_label_set_text(ctx->lbl_subnet, "--");
            lv_label_set_text(ctx->lbl_router, "--");
            lv_label_set_text(ctx->lbl_dns, "--");
            /* Reset Hardware labels */
            lv_label_set_text(ctx->lbl_mac, "--");
            lv_label_set_text(ctx->lbl_rssi, "--");
            lv_label_set_text(ctx->lbl_channel, "--");
            lv_label_set_text(ctx->lbl_band, "--");
            lv_label_set_text(ctx->lbl_security, "--");
            /* Clear connected state in scan data */
            ctx->scan_data.connected_idx = -1;
            for (uint8_t k = 0; k < ctx->scan_data.count; k++) {
                ctx->scan_data.networks[k].flags &= (uint8_t)~0x01;
            }
            /* Clear selection and hide connect button */
            ctx->selected_index = -1;
            ctx->selected_ssid[0] = '\0';
            /* Don't clear pending_ssid here - it's used by password dialog flow.
             * Clearing it would break connect if auto-scan triggers while dialog is open. */
            if (!ctx->dialog_open) {
                ctx->pending_ssid[0] = '\0';
            }
            lv_obj_add_flag(ctx->connect_btn, LV_OBJ_FLAG_HIDDEN);
            /* Remove highlight from all network items */
            {
                int idx = 0;
                lv_obj_t* child = lv_obj_get_child(ctx->network_list, idx);
                while (child) {
                    lv_obj_set_style_bg_color(child, AIC_WIFI_COLOR_CARD, 0);
                    child = lv_obj_get_child(ctx->network_list, ++idx);
                }
            }
            break;
        case WIFI_STATE_ERROR:
            lv_label_set_text(ctx->lbl_status, "Connection failed");
            lv_obj_set_style_text_color(ctx->lbl_status, AIC_WIFI_COLOR_ERROR, 0);
            break;
        default:
            break;
    }
}

/* Error dialog button callbacks */
static void error_try_again_cb(lv_event_t* e)
{
    aic_wifi_ctx_t* ctx = (aic_wifi_ctx_t*)lv_event_get_user_data(e);
    /* Find and delete the overlay: button -> btn_row -> dialog -> overlay */
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* overlay = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(btn)));
    lv_obj_delete_async(overlay);

    /* Reopen password dialog with pending SSID */
    if (ctx) {
        ctx->dialog_open = false;  /* Will be set true again by show_password_dialog */
        if (ctx->pending_ssid[0] != '\0') {
            aic_wifi_show_password_dialog(ctx, ctx->pending_ssid);
        }
    }
}

static void error_close_cb(lv_event_t* e)
{
    aic_wifi_ctx_t* ctx = (aic_wifi_ctx_t*)lv_event_get_user_data(e);
    /* Find and delete the overlay: button -> btn_row -> dialog -> overlay */
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* overlay = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(btn)));
    lv_obj_delete_async(overlay);

    if (ctx) {
        ctx->dialog_open = false;
    }
}

void aic_wifi_show_error(aic_wifi_ctx_t* ctx, wifi_error_t error, const char* message)
{
    if (!ctx) return;
    ctx->dialog_open = true;  /* Block auto-scan while error dialog is open */

    /* Determine error message */
    const char* title = "Connection Failed";
    const char* detail = message ? message : "Unknown error";
    if (error == WIFI_ERR_AUTH_FAILED) {
        title = "Authentication Failed";
        detail = "Password is incorrect. Please try again.";
    } else if (error == WIFI_ERR_NO_AP) {
        title = "Network Not Found";
        detail = "The selected network is not available.";
    }

    /* Create overlay */
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, AIC_WIFI_SCREEN_WIDTH, AIC_WIFI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_center(overlay);

    /* Dialog box */
    lv_obj_t* dialog = lv_obj_create(overlay);
    lv_obj_set_size(dialog, 380, 200);
    lv_obj_set_style_bg_color(dialog, AIC_WIFI_COLOR_CARD, 0);
    lv_obj_set_style_radius(dialog, 12, 0);
    lv_obj_center(dialog);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(dialog, 20, 0);
    lv_obj_set_style_pad_row(dialog, 12, 0);

    /* Error icon + title */
    lv_obj_t* title_lbl = lv_label_create(dialog);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_lbl, AIC_WIFI_COLOR_ERROR, 0);

    /* Detail message */
    lv_obj_t* detail_lbl = lv_label_create(dialog);
    lv_label_set_text(detail_lbl, detail);
    lv_obj_set_style_text_font(detail_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(detail_lbl, AIC_WIFI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(detail_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(detail_lbl, 340);

    /* Button row */
    lv_obj_t* btn_row = lv_obj_create(dialog);
    lv_obj_set_size(btn_row, 340, 45);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 20, 0);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Try Again button */
    lv_obj_t* try_btn = lv_button_create(btn_row);
    lv_obj_set_size(try_btn, 140, 40);
    lv_obj_set_style_bg_color(try_btn, AIC_WIFI_COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_radius(try_btn, 8, 0);
    lv_obj_add_event_cb(try_btn, error_try_again_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* try_lbl = lv_label_create(try_btn);
    lv_label_set_text(try_lbl, "Try Again");
    lv_obj_set_style_text_color(try_lbl, lv_color_white(), 0);
    lv_obj_center(try_lbl);

    /* Close button */
    lv_obj_t* close_btn = lv_button_create(btn_row);
    lv_obj_set_size(close_btn, 140, 40);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, error_close_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);
}

/*******************************************************************************
 * UI Component Functions
 ******************************************************************************/

lv_obj_t* aic_wifi_create_signal_icon(lv_obj_t* parent, int8_t rssi)
{
    /* 5-level signal: >-50=5, >-60=4, >-70=3, >-80=2, >-90=1, else=0 */
    int bars = (rssi >= -50) ? 5 : (rssi >= -60) ? 4 : (rssi >= -70) ? 3
             : (rssi >= -80) ? 2 : (rssi >= -90) ? 1 : 0;

    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, 30, 24);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_column(container, 1, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Create 5 bars with increasing height */
    for (int i = 0; i < 5; i++) {
        lv_obj_t* bar = lv_obj_create(container);
        int height = 4 + i * 4;  /* 4, 8, 12, 16, 20 */
        lv_obj_set_size(bar, 4, height);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        if (i < bars) {
            lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x3A3A3C), 0);
        }
    }

    return container;
}

void aic_wifi_update_signal_icon(lv_obj_t* icon, int8_t rssi)
{
    int bars = (rssi >= -50) ? 5 : (rssi >= -60) ? 4 : (rssi >= -70) ? 3
             : (rssi >= -80) ? 2 : (rssi >= -90) ? 1 : 0;

    for (int i = 0; i < 5; i++) {
        lv_obj_t* bar = lv_obj_get_child(icon, i);
        if (bar) {
            if (i < bars) {
                lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
            } else {
                lv_obj_set_style_bg_color(bar, lv_color_hex(0x3A3A3C), 0);
            }
        }
    }
}

lv_obj_t* aic_wifi_create_network_item(lv_obj_t* parent, const ipc_wifi_network_t* network, int index)
{
    bool is_connected = WIFI_IS_CONNECTED(network);

    lv_obj_t* item = lv_obj_create(parent);
    lv_obj_set_size(item, 248, 50);
    lv_obj_set_style_bg_color(item, is_connected ? AIC_WIFI_COLOR_HIGHLIGHT : AIC_WIFI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 6, 0);
    lv_obj_set_style_pad_all(item, 6, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_user_data(item, (void*)(intptr_t)index);
    lv_obj_add_event_cb(item, network_item_click_cb, LV_EVENT_CLICKED, g_wifi_ctx);

    /* Left: Signal strength bars */
    aic_wifi_create_signal_icon(item, network->rssi);

    /* Middle: SSID and status */
    lv_obj_t* mid = lv_obj_create(item);
    lv_obj_set_size(mid, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(mid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mid, 0, 0);
    lv_obj_set_style_pad_all(mid, 0, 0);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(mid, 1);
    lv_obj_remove_flag(mid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* ssid_lbl = lv_label_create(mid);
    lv_label_set_text(ssid_lbl, network->ssid);
    lv_obj_set_style_text_color(ssid_lbl, AIC_WIFI_COLOR_TEXT, 0);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(ssid_lbl, 180);

    /* Status line: security + dBm */
    lv_obj_t* status_lbl = lv_label_create(mid);
    if (WIFI_IS_CONNECTED(network)) {
        lv_label_set_text_fmt(status_lbl, "Connected  %d dBm", network->rssi);
        lv_obj_set_style_text_color(status_lbl, AIC_WIFI_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text_fmt(status_lbl, "%s  %d dBm",
                              wifi_security_to_str(network->security), network->rssi);
        lv_obj_set_style_text_color(status_lbl, AIC_WIFI_COLOR_TEXT_DIM, 0);
    }
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);

    return item;
}

void aic_wifi_show_password_dialog(aic_wifi_ctx_t* ctx, const char* ssid)
{
    if (!ctx) return;
    ctx->dialog_open = true;

    /* Create overlay */
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, AIC_WIFI_SCREEN_WIDTH, AIC_WIFI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_center(overlay);

    /* Dialog box — positioned above keyboard (keyboard is 260px from bottom) */
    lv_obj_t* dialog = lv_obj_create(overlay);
    lv_obj_set_size(dialog, 400, 200);
    lv_obj_set_style_bg_color(dialog, AIC_WIFI_COLOR_CARD, 0);
    lv_obj_set_style_radius(dialog, 12, 0);
    lv_obj_align(dialog, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(dialog, 20, 0);

    /* Title */
    lv_obj_t* title = lv_label_create(dialog);
    lv_label_set_text_fmt(title, "Connect to \"%s\"", ssid);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, AIC_WIFI_COLOR_TEXT, 0);

    /* Password label */
    lv_obj_t* pwd_label = lv_label_create(dialog);
    lv_label_set_text(pwd_label, "Enter password:");
    lv_obj_set_style_text_color(pwd_label, AIC_WIFI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_pad_top(pwd_label, 8, 0);

    /* Password textarea */
    ctx->password_ta = lv_textarea_create(dialog);
    lv_obj_set_size(ctx->password_ta, 360, 40);
    lv_textarea_set_password_mode(ctx->password_ta, true);
    lv_textarea_set_one_line(ctx->password_ta, true);
    lv_textarea_set_placeholder_text(ctx->password_ta, "Password");
    lv_obj_set_style_bg_color(ctx->password_ta, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(ctx->password_ta, AIC_WIFI_COLOR_TEXT, 0);
    /* Cursor style: thin "|" bar (2px wide solid rectangle) */
    lv_obj_set_style_width(ctx->password_ta, 2, LV_PART_CURSOR);
    lv_obj_set_style_bg_color(ctx->password_ta, AIC_WIFI_COLOR_TEXT, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ctx->password_ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ctx->password_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ctx->password_ta, 500, LV_PART_CURSOR);

    /* Keyboard — taller for better touch */
    ctx->password_kb = lv_keyboard_create(overlay);
    lv_keyboard_set_textarea(ctx->password_kb, ctx->password_ta);
    lv_obj_set_size(ctx->password_kb, AIC_WIFI_SCREEN_WIDTH, 260);
    lv_obj_align(ctx->password_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(ctx->password_kb, password_kb_cb, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(ctx->password_kb, password_kb_cb, LV_EVENT_CANCEL, ctx);

    /* Store overlay reference for hiding */
    lv_obj_set_user_data(ctx->password_ta, overlay);
}

void aic_wifi_hide_password_dialog(aic_wifi_ctx_t* ctx)
{
    if (!ctx || !ctx->password_ta) return;

    lv_obj_t* overlay = (lv_obj_t*)lv_obj_get_user_data(ctx->password_ta);
    if (overlay) {
        lv_obj_delete_async(overlay);
    }

    ctx->password_ta = NULL;
    ctx->password_kb = NULL;
    ctx->dialog_open = false;
}

void aic_wifi_show_connecting(aic_wifi_ctx_t* ctx, const char* ssid)
{
    if (!ctx) return;

    aic_wifi_set_state(ctx, WIFI_STATE_CONNECTING);

    /* Could add a spinner animation here */
}

void aic_wifi_hide_connecting(aic_wifi_ctx_t* ctx)
{
    if (!ctx) return;

    /* Remove spinner if added */
}

/*******************************************************************************
 * Saved Networks Callbacks & API
 ******************************************************************************/

static void saved_item_click_cb(lv_event_t* e)
{
    aic_wifi_ctx_t* ctx = g_wifi_ctx;
    if (!ctx) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ctx->saved_count) return;

    const wifi_saved_entry_t* entry = &ctx->saved_entries[idx];
    if (ctx->on_saved_connect) {
        ctx->on_saved_connect(entry->ssid, entry->password, entry->security);
    }
}

static void saved_delete_click_cb(lv_event_t* e)
{
    aic_wifi_ctx_t* ctx = g_wifi_ctx;
    if (!ctx) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ctx->saved_count) return;

    if (ctx->on_saved_delete) {
        ctx->on_saved_delete(idx);
    }
}

void aic_wifi_set_saved_connect_cb(aic_wifi_ctx_t* ctx, aic_wifi_saved_connect_cb_t cb)
{
    if (ctx) ctx->on_saved_connect = cb;
}

void aic_wifi_set_saved_delete_cb(aic_wifi_ctx_t* ctx, aic_wifi_saved_delete_cb_t cb)
{
    if (ctx) ctx->on_saved_delete = cb;
}

void aic_wifi_set_saved_networks(aic_wifi_ctx_t* ctx,
                                  const wifi_saved_entry_t* entries, int count)
{
    if (!ctx) return;

    if (count > WIFI_SAVED_MAX) count = WIFI_SAVED_MAX;
    ctx->saved_count = count;
    if (entries && count > 0) {
        memcpy(ctx->saved_entries, entries, (size_t)count * sizeof(wifi_saved_entry_t));
    }
    aic_wifi_refresh_saved(ctx);
}

void aic_wifi_refresh_saved(aic_wifi_ctx_t* ctx)
{
    if (!ctx || !ctx->saved_list) return;

    /* Clear existing saved items */
    lv_obj_clean(ctx->saved_list);

    if (ctx->saved_count <= 0) {
        lv_obj_add_flag(ctx->saved_header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->saved_list, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Show section */
    lv_obj_remove_flag(ctx->saved_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ctx->saved_list, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < ctx->saved_count; i++) {
        const wifi_saved_entry_t* entry = &ctx->saved_entries[i];

        /* Row container */
        lv_obj_t* row = lv_obj_create(ctx->saved_list);
        lv_obj_set_size(row, 248, 36);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2F2F2F), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, saved_item_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        /* Star icon */
        lv_obj_t* star = lv_label_create(row);
        lv_label_set_text(star, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(star, AIC_WIFI_COLOR_SUCCESS, 0);
        lv_obj_set_style_text_font(star, &lv_font_montserrat_14, 0);

        /* SSID label (grows to fill) */
        lv_obj_t* ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, entry->ssid);
        lv_obj_set_style_text_color(ssid_lbl, AIC_WIFI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(ssid_lbl, 1);
        lv_obj_set_width(ssid_lbl, 0); /* flex-grow will size it */

        /* Delete button [x] */
        lv_obj_t* del_btn = lv_button_create(row);
        lv_obj_set_size(del_btn, 28, 28);
        lv_obj_set_style_bg_color(del_btn, lv_color_hex(0x4A4A4A), 0);
        lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(del_btn, 14, 0);
        lv_obj_set_style_border_color(del_btn, AIC_WIFI_COLOR_ERROR, 0);
        lv_obj_set_style_border_width(del_btn, 1, 0);
        lv_obj_set_style_pad_all(del_btn, 0, 0);
        lv_obj_add_event_cb(del_btn, saved_delete_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t* del_lbl = lv_label_create(del_btn);
        lv_label_set_text(del_lbl, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(del_lbl, AIC_WIFI_COLOR_ERROR, 0);
        lv_obj_set_style_text_font(del_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(del_lbl);
    }
}

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

lv_color_t aic_wifi_rssi_color(int8_t rssi)
{
    int bars = WIFI_RSSI_TO_BARS(rssi);
    switch (bars) {
        case 4: return AIC_WIFI_COLOR_SIGNAL_4;
        case 3: return AIC_WIFI_COLOR_SIGNAL_3;
        case 2: return AIC_WIFI_COLOR_SIGNAL_2;
        case 1: return AIC_WIFI_COLOR_SIGNAL_1;
        default: return AIC_WIFI_COLOR_SIGNAL_0;
    }
}

void aic_wifi_format_ip(lv_obj_t* label, const uint8_t* ip)
{
    if (!label || !ip) return;
    lv_label_set_text_fmt(label, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void aic_wifi_format_mac(lv_obj_t* label, const uint8_t* mac)
{
    if (!label || !mac) return;
    lv_label_set_text_fmt(label, "%02X:%02X:%02X:%02X:%02X:%02X",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
