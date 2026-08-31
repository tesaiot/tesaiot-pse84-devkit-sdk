/*******************************************************************************
 * wifi_config.h — WiFi Configuration for PSoC Edge MicroPython AI
 *
 * Default: STA mode (connect to existing AP, configurable from MicroPython)
 * Supports both STA and SoftAP modes
 *******************************************************************************/

#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include "cy_wcm.h"

/* WiFi Mode: SOFTAP or STA */
#define WIFI_MODE_SOFTAP    (0)
#define WIFI_MODE_STA       (1)
#define WIFI_DEFAULT_MODE   WIFI_MODE_STA

/* SoftAP Configuration */
#define SOFTAP_SSID             "PSoC-Edge-AI"
#define SOFTAP_PASSWORD         "tesaiot2026"
#define SOFTAP_SECURITY         CY_WCM_SECURITY_WPA2_AES_PSK
#define SOFTAP_CHANNEL          (1)
#define SOFTAP_MAX_CLIENTS      (4)

/* SoftAP IP Configuration */
#define SOFTAP_IP_ADDRESS       "192.168.4.1"
#define SOFTAP_NETMASK          "255.255.255.0"
#define SOFTAP_GATEWAY          "192.168.4.1"

/* STA Configuration (default, overridable from Python)
 *
 * Placeholders on purpose. Fill these in with your own network before you
 * build if you want the board to join it without being told to; leaving them
 * as they are costs nothing, because nothing in this tree reads them — WiFi
 * credentials normally arrive at run time from Python (wifi.connect), from the
 * WiFi Connect screen, or from the credentials saved in flash.
 *
 * tools/check_no_credentials.sh fails the build if a real passphrase is left
 * here, so keep the placeholders in anything you commit or ship.
 */
#define STA_SSID                "YOUR_WIFI_SSID"
#define STA_PASSWORD            "YOUR_WIFI_PASSWORD"
#define STA_SECURITY            CY_WCM_SECURITY_WPA2_AES_PSK
#define STA_MAX_RETRIES         (10)
#define STA_RETRY_INTERVAL_MS   (2000)

#endif /* WIFI_CONFIG_H */
