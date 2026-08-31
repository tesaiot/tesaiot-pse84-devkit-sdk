/*******************************************************************************
 * File: wifi_creds_types.h
 *
 * Description:
 *   WiFi credential entry type and constants shared between:
 *   - lfs_wifi_creds.c (LittleFS-based, primary boot store)
 *   - OPTIGA Trust M (secure backup, future)
 *
 *   Entry format: 100 bytes per network (SSID + password + security + flags)
 *   Max 6 saved networks.
 *
 ******************************************************************************/

#ifndef WIFI_CREDS_TYPES_H
#define WIFI_CREDS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* File format constants */
#define QSPI_WIFI_CREDS_MAGIC   0x57494649U  /* "WIFI" */
#define QSPI_WIFI_CREDS_VERSION 1
#define QSPI_WIFI_CREDS_MAX     6

/* Per-network credential entry (100 bytes, packed) */
typedef struct __attribute__((packed)) {
    char     ssid[33];       /* 32 chars + null terminator */
    char     password[65];   /* 64 chars + null terminator (WPA2 max=63) */
    uint8_t  security;       /* 0=open, 2=WPA, 6=WPA2_AES_PSK, etc. */
    uint8_t  flags;          /* bit0=auto_connect, bit1-7=reserved */
} qspi_wifi_entry_t;        /* sizeof = 100 */

#endif /* WIFI_CREDS_TYPES_H */
