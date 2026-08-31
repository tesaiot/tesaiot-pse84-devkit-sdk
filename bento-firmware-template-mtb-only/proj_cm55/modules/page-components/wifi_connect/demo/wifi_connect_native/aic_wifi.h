/*******************************************************************************
 * File: aic_wifi.h
 * Description: WiFi UI Helper Functions for LVGL
 *
 * Provides macOS-style WiFi UI components:
 * - WiFi network list with signal strength bars
 * - Network details panel (TCP/IP and Hardware tabs)
 * - Connection/Disconnection dialogs
 * - Password entry keyboard
 *
 * Part of BiiL Course: Embedded C for IoT - Week 7
 ******************************************************************************/

#ifndef AIC_WIFI_H
#define AIC_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "wifi_shared_compat.h"
#include "wifi_saved.h"

/*******************************************************************************
 * Display Configuration
 ******************************************************************************/

#define AIC_WIFI_SCREEN_WIDTH   800
#define AIC_WIFI_SCREEN_HEIGHT  480

/*******************************************************************************
 * Color Palette (macOS-style dark theme)
 ******************************************************************************/

#define AIC_WIFI_COLOR_BG           lv_color_hex(0x1E1E1E)  /* Main background */
#define AIC_WIFI_COLOR_SIDEBAR      lv_color_hex(0x2D2D2D)  /* Sidebar background */
#define AIC_WIFI_COLOR_CARD         lv_color_hex(0x3A3A3A)  /* Card background */
#define AIC_WIFI_COLOR_HIGHLIGHT    lv_color_hex(0x0A84FF)  /* Selected item */
#define AIC_WIFI_COLOR_TEXT         lv_color_hex(0xFFFFFF)  /* Primary text */
#define AIC_WIFI_COLOR_TEXT_DIM     lv_color_hex(0x8E8E93)  /* Secondary text */
#define AIC_WIFI_COLOR_SUCCESS      lv_color_hex(0x30D158)  /* Connected status */
#define AIC_WIFI_COLOR_WARNING      lv_color_hex(0xFFD60A)  /* Warning status */
#define AIC_WIFI_COLOR_ERROR        lv_color_hex(0xFF453A)  /* Error status */
#define AIC_WIFI_COLOR_SIGNAL_4     lv_color_hex(0x30D158)  /* Excellent signal */
#define AIC_WIFI_COLOR_SIGNAL_3     lv_color_hex(0x63DA38)  /* Good signal */
#define AIC_WIFI_COLOR_SIGNAL_2     lv_color_hex(0xFFD60A)  /* Fair signal */
#define AIC_WIFI_COLOR_SIGNAL_1     lv_color_hex(0xFF9F0A)  /* Weak signal */
#define AIC_WIFI_COLOR_SIGNAL_0     lv_color_hex(0xFF453A)  /* No signal */

/*******************************************************************************
 * Callback Types
 ******************************************************************************/

/* Called when user requests WiFi scan */
typedef void (*aic_wifi_scan_cb_t)(void);

/* Called when user selects a network */
typedef void (*aic_wifi_select_cb_t)(int index, const char* ssid);

/* Called when user requests connect */
typedef void (*aic_wifi_connect_cb_t)(const char* ssid, const char* password, uint8_t security);

/* Called when user requests disconnect */
typedef void (*aic_wifi_disconnect_cb_t)(void);

/* Called when user taps a saved network to auto-connect */
typedef void (*aic_wifi_saved_connect_cb_t)(const char* ssid, const char* password, uint8_t security);

/* Called when user taps delete on a saved network */
typedef void (*aic_wifi_saved_delete_cb_t)(int saved_index);

/*******************************************************************************
 * WiFi Manager Context
 ******************************************************************************/

typedef struct {
    lv_obj_t* main_screen;          /* Main screen object */
    lv_obj_t* network_list;         /* List of networks */
    lv_obj_t* details_panel;        /* Network details panel */
    lv_obj_t* status_bar;           /* Top status bar */
    lv_obj_t* connect_btn;          /* Connect/Disconnect button */
    lv_obj_t* scan_btn;             /* Scan button */
    lv_obj_t* password_kb;          /* Password keyboard (hidden by default) */
    lv_obj_t* password_ta;          /* Password text area */
    lv_obj_t* connecting_spinner;   /* Connection spinner */

    /* Detail labels */
    lv_obj_t* lbl_ssid;
    lv_obj_t* lbl_status;
    lv_obj_t* lbl_ip;
    lv_obj_t* lbl_subnet;
    lv_obj_t* lbl_router;
    lv_obj_t* lbl_dns;
    lv_obj_t* lbl_mac;
    lv_obj_t* lbl_rssi;
    lv_obj_t* lbl_channel;
    lv_obj_t* lbl_band;
    lv_obj_t* lbl_security;

    /* State */
    int selected_index;
    wifi_state_t state;
    ipc_wifi_scan_t scan_data;
    ipc_wifi_tcpip_t tcpip_info;
    ipc_wifi_hardware_t hw_info;
    char selected_ssid[WIFI_SSID_MAX_LEN];

    /* Pending connect info (saved when password dialog opens) */
    char pending_ssid[WIFI_SSID_MAX_LEN];
    uint8_t pending_security;
    bool dialog_open;

    /* Saved networks section */
    lv_obj_t* saved_header;             /* "Saved Networks" label */
    lv_obj_t* saved_list;               /* Container for saved items */
    wifi_saved_entry_t saved_entries[WIFI_SAVED_MAX];
    int saved_count;

    /* Callbacks */
    aic_wifi_scan_cb_t on_scan;
    aic_wifi_select_cb_t on_select;
    aic_wifi_connect_cb_t on_connect;
    aic_wifi_disconnect_cb_t on_disconnect;
    aic_wifi_saved_connect_cb_t on_saved_connect;
    aic_wifi_saved_delete_cb_t on_saved_delete;
} aic_wifi_ctx_t;

/*******************************************************************************
 * Initialization Functions
 ******************************************************************************/

/**
 * @brief Initialize the WiFi manager UI
 * @param parent Parent object (or NULL for screen)
 * @return Pointer to WiFi context
 */
aic_wifi_ctx_t* aic_wifi_init(lv_obj_t* parent);

/**
 * @brief Deinitialize and free resources
 * @param ctx WiFi context
 */
void aic_wifi_deinit(aic_wifi_ctx_t* ctx);

/*******************************************************************************
 * Callback Registration
 ******************************************************************************/

/**
 * @brief Set scan request callback
 */
void aic_wifi_set_scan_cb(aic_wifi_ctx_t* ctx, aic_wifi_scan_cb_t cb);

/**
 * @brief Set network select callback
 */
void aic_wifi_set_select_cb(aic_wifi_ctx_t* ctx, aic_wifi_select_cb_t cb);

/**
 * @brief Set connect request callback
 */
void aic_wifi_set_connect_cb(aic_wifi_ctx_t* ctx, aic_wifi_connect_cb_t cb);

/**
 * @brief Set disconnect request callback
 */
void aic_wifi_set_disconnect_cb(aic_wifi_ctx_t* ctx, aic_wifi_disconnect_cb_t cb);

/*******************************************************************************
 * Data Update Functions
 ******************************************************************************/

/**
 * @brief Update network list with scan results
 * @param ctx WiFi context
 * @param scan_data Pointer to scan result data
 */
void aic_wifi_update_networks(aic_wifi_ctx_t* ctx, const ipc_wifi_scan_t* scan_data);

/**
 * @brief Update TCP/IP info display
 * @param ctx WiFi context
 * @param tcpip TCP/IP info structure
 */
void aic_wifi_update_tcpip(aic_wifi_ctx_t* ctx, const ipc_wifi_tcpip_t* tcpip);

/**
 * @brief Update hardware info display
 * @param ctx WiFi context
 * @param hw_info Hardware info structure
 */
void aic_wifi_update_hardware(aic_wifi_ctx_t* ctx, const ipc_wifi_hardware_t* hw_info);

/**
 * @brief Update connection state
 * @param ctx WiFi context
 * @param state New connection state
 */
void aic_wifi_set_state(aic_wifi_ctx_t* ctx, wifi_state_t state);

/**
 * @brief Show error message
 * @param ctx WiFi context
 * @param error Error code
 * @param message Error message string
 */
void aic_wifi_show_error(aic_wifi_ctx_t* ctx, wifi_error_t error, const char* message);

/*******************************************************************************
 * Saved Networks Functions
 ******************************************************************************/

/**
 * @brief Set saved networks data and refresh the UI section
 */
void aic_wifi_set_saved_networks(aic_wifi_ctx_t* ctx,
                                  const wifi_saved_entry_t* entries, int count);

/**
 * @brief Refresh saved networks UI (re-render items)
 */
void aic_wifi_refresh_saved(aic_wifi_ctx_t* ctx);

/**
 * @brief Set callback for saved network tap-to-connect
 */
void aic_wifi_set_saved_connect_cb(aic_wifi_ctx_t* ctx, aic_wifi_saved_connect_cb_t cb);

/**
 * @brief Set callback for saved network delete
 */
void aic_wifi_set_saved_delete_cb(aic_wifi_ctx_t* ctx, aic_wifi_saved_delete_cb_t cb);

/*******************************************************************************
 * UI Component Functions
 ******************************************************************************/

/**
 * @brief Create signal strength indicator (WiFi bars)
 * @param parent Parent object
 * @param rssi RSSI value in dBm
 * @return Signal indicator object
 */
lv_obj_t* aic_wifi_create_signal_icon(lv_obj_t* parent, int8_t rssi);

/**
 * @brief Update signal indicator
 * @param icon Signal icon object
 * @param rssi New RSSI value
 */
void aic_wifi_update_signal_icon(lv_obj_t* icon, int8_t rssi);

/**
 * @brief Create a network list item
 * @param parent Parent list object
 * @param network Network info
 * @param index Item index
 * @return List item object
 */
lv_obj_t* aic_wifi_create_network_item(lv_obj_t* parent, const ipc_wifi_network_t* network, int index);

/**
 * @brief Show password entry dialog
 * @param ctx WiFi context
 * @param ssid Network SSID to connect
 */
void aic_wifi_show_password_dialog(aic_wifi_ctx_t* ctx, const char* ssid);

/**
 * @brief Hide password dialog
 * @param ctx WiFi context
 */
void aic_wifi_hide_password_dialog(aic_wifi_ctx_t* ctx);

/**
 * @brief Show connecting spinner
 * @param ctx WiFi context
 * @param ssid Network being connected
 */
void aic_wifi_show_connecting(aic_wifi_ctx_t* ctx, const char* ssid);

/**
 * @brief Hide connecting spinner
 * @param ctx WiFi context
 */
void aic_wifi_hide_connecting(aic_wifi_ctx_t* ctx);

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * @brief Get color for RSSI value
 * @param rssi RSSI value in dBm
 * @return LV color for signal strength
 */
lv_color_t aic_wifi_rssi_color(int8_t rssi);

/**
 * @brief Format IP address to label
 * @param label Label object
 * @param ip IP address array (4 bytes)
 */
void aic_wifi_format_ip(lv_obj_t* label, const uint8_t* ip);

/**
 * @brief Format MAC address to label
 * @param label Label object
 * @param mac MAC address array (6 bytes)
 */
void aic_wifi_format_mac(lv_obj_t* label, const uint8_t* mac);

#ifdef __cplusplus
}
#endif

#endif /* AIC_WIFI_H */
