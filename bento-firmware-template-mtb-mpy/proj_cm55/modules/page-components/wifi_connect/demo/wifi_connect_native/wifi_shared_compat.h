/*******************************************************************************
 * File: wifi_shared_compat.h
 *
 * Description:
 *   Compatibility WiFi shared types imported from AIC-EEC part5 stack.
 *   This header keeps the original UI contract of aic_wifi.* while the
 *   backend is mapped to wifi_manager in this firmware.
 *
 ******************************************************************************/

#ifndef WIFI_SHARED_COMPAT_H
#define WIFI_SHARED_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define WIFI_SSID_MAX_LEN       (33U)
#define WIFI_PASSWORD_MAX_LEN   (65U)
#define WIFI_SCAN_MAX_NETWORKS  (16U)
#define WIFI_MAC_ADDR_LEN       (6U)
#define WIFI_IP_ADDR_LEN        (4U)

typedef enum {
    WIFI_SECURITY_OPEN          = 0,
    WIFI_SECURITY_WEP           = 1,
    WIFI_SECURITY_WPA           = 2,
    WIFI_SECURITY_WPA2          = 3,
    WIFI_SECURITY_WPA3          = 4,
    WIFI_SECURITY_WPA_WPA2      = 5,
    WIFI_SECURITY_WPA2_WPA3     = 6,
    WIFI_SECURITY_ENTERPRISE    = 7,
    WIFI_SECURITY_UNKNOWN       = 0xFF
} wifi_security_t;

typedef enum {
    WIFI_BAND_2_4GHZ    = 0,
    WIFI_BAND_5GHZ      = 1,
    WIFI_BAND_6GHZ      = 2,
    WIFI_BAND_UNKNOWN   = 0xFF
} wifi_band_t;

typedef enum {
    WIFI_STATE_DISCONNECTED     = 0,
    WIFI_STATE_CONNECTING       = 1,
    WIFI_STATE_CONNECTED        = 2,
    WIFI_STATE_DISCONNECTING    = 3,
    WIFI_STATE_SCANNING         = 4,
    WIFI_STATE_ERROR            = 5
} wifi_state_t;

typedef struct __attribute__((packed)) {
    char        ssid[WIFI_SSID_MAX_LEN];
    int8_t      rssi;
    uint8_t     security;
    uint8_t     channel;
    uint8_t     band;
    uint8_t     flags; /* bit0=connected, bit1=saved */
    uint8_t     reserved[2];
} ipc_wifi_network_t;

typedef struct __attribute__((packed)) {
    uint8_t             count;
    int8_t              connected_idx;
    uint8_t             reserved[2];
    ipc_wifi_network_t  networks[WIFI_SCAN_MAX_NETWORKS];
} ipc_wifi_scan_t;

typedef struct __attribute__((packed)) {
    uint8_t     dhcp_enabled;
    uint8_t     reserved[3];
    uint8_t     ip_addr[WIFI_IP_ADDR_LEN];
    uint8_t     subnet[WIFI_IP_ADDR_LEN];
    uint8_t     gateway[WIFI_IP_ADDR_LEN];
    uint8_t     dns1[WIFI_IP_ADDR_LEN];
    uint8_t     dns2[WIFI_IP_ADDR_LEN];
    uint32_t    lease_time;
} ipc_wifi_tcpip_t;

typedef struct __attribute__((packed)) {
    uint8_t     mac_addr[WIFI_MAC_ADDR_LEN];
    uint8_t     band;
    uint8_t     channel;
    int8_t      rssi;
    int8_t      tx_power;
    uint16_t    mtu;
    uint32_t    link_speed;
    char        fw_version[16];
} ipc_wifi_hardware_t;

typedef enum {
    WIFI_ERR_NONE               = 0,
    WIFI_ERR_TIMEOUT            = 1,
    WIFI_ERR_AUTH_FAILED        = 2,
    WIFI_ERR_NO_AP              = 3,
    WIFI_ERR_CONNECTION_LOST    = 4,
    WIFI_ERR_DRIVER             = 5,
    WIFI_ERR_SCAN_FAILED        = 6,
    WIFI_ERR_DHCP_FAILED        = 7,
    WIFI_ERR_UNKNOWN            = 0xFF
} wifi_error_t;

#define WIFI_IS_CONNECTED(network)  (((network)->flags & 0x01U) != 0U)
#define WIFI_IS_SAVED(network)      (((network)->flags & 0x02U) != 0U)
#define WIFI_RSSI_TO_BARS(rssi) \
    ((rssi) >= -50 ? 4 : \
     (rssi) >= -60 ? 3 : \
     (rssi) >= -70 ? 2 : \
     (rssi) >= -80 ? 1 : 0)

static inline const char* wifi_security_to_str(uint8_t security)
{
    switch (security) {
        case WIFI_SECURITY_OPEN:        return "Open";
        case WIFI_SECURITY_WEP:         return "WEP";
        case WIFI_SECURITY_WPA:         return "WPA";
        case WIFI_SECURITY_WPA2:        return "WPA2";
        case WIFI_SECURITY_WPA3:        return "WPA3";
        case WIFI_SECURITY_WPA_WPA2:    return "WPA/WPA2";
        case WIFI_SECURITY_WPA2_WPA3:   return "WPA2/WPA3";
        case WIFI_SECURITY_ENTERPRISE:  return "Enterprise";
        default:                        return "Unknown";
    }
}

static inline const char* wifi_band_to_str(uint8_t band)
{
    switch (band) {
        case WIFI_BAND_2_4GHZ:  return "2.4 GHz";
        case WIFI_BAND_5GHZ:    return "5 GHz";
        case WIFI_BAND_6GHZ:    return "6 GHz";
        default:                return "Unknown";
    }
}

#endif /* WIFI_SHARED_COMPAT_H */
