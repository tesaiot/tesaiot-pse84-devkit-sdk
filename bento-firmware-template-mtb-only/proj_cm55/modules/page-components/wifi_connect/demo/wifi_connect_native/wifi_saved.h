/*******************************************************************************
 * File: wifi_saved.h
 *
 * Description:
 *   Saved WiFi networks module — stores up to 6 network credentials in
 *   OPTIGA Trust M NVM via existing TESAIoT CRED_READ/WRITE/ERASE IPC.
 *
 *   Each saved network (106 bytes) is stored in one OPTIGA Type 3 data
 *   object (140 bytes max). Uses slots 5, 6, 8, 9, 10, 11 (skipping
 *   slot 4=reserved and slot 7=API_KEY).
 *
 *   LRU eviction: When all 6 slots are full, the entry with the
 *   smallest last_used timestamp is overwritten.
 *
 *   NVRAM fallback: When OPTIGA is not connected, entries are stored
 *   in a RAM cache (persists within session, lost on reboot).
 *
 ******************************************************************************/

#ifndef WIFI_SAVED_H
#define WIFI_SAVED_H

#include <stdint.h>
#include <stdbool.h>

#define WIFI_SAVED_MAX          6

/* Per-slot binary format: 106 bytes packed */
typedef struct __attribute__((packed)) {
    char     ssid[33];       /* null-terminated, max 32 chars */
    char     password[65];   /* null-terminated, max 63 chars (WPA2 standard) */
    uint8_t  security;       /* 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3 */
    uint8_t  flags;          /* bit0 = auto_connect */
    uint16_t reserved;       /* alignment padding */
    uint32_t last_used;      /* epoch seconds (from RTC/NTP) for LRU eviction */
} wifi_saved_entry_t;        /* exactly 106 bytes */

/*******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * @brief Load a saved network from OPTIGA slot.
 * @param index  Slot index 0..5
 * @param out    Output entry (filled on success)
 * @return true if a valid entry was read, false if empty/error
 */
bool wifi_saved_load(int index, wifi_saved_entry_t *out);

/**
 * @brief Store a network entry to OPTIGA slot.
 * @param index  Slot index 0..5
 * @param entry  Entry to write
 * @return true on success
 */
bool wifi_saved_store(int index, const wifi_saved_entry_t *entry);

/**
 * @brief Erase a saved network from OPTIGA slot.
 * @param index  Slot index 0..5
 * @return true on success
 */
bool wifi_saved_erase(int index);

/**
 * @brief Find a saved network by SSID.
 * @param ssid  SSID to search for
 * @return Slot index 0..5 if found, -1 if not found
 */
int wifi_saved_find(const char *ssid);

/**
 * @brief Add or update a network in saved list.
 *        If SSID already exists, updates in-place.
 *        If not, uses the first empty slot (or overwrites slot 0 if full).
 * @param ssid      Network SSID
 * @param password  Network password
 * @param security  Security type
 * @return true on success
 */
bool wifi_saved_add(const char *ssid, const char *password, uint8_t security);

/**
 * @brief Count number of valid saved networks.
 * @return Count 0..6
 */
int wifi_saved_count(void);

/**
 * @brief Load all saved networks into an array.
 * @param entries  Output array (must have WIFI_SAVED_MAX elements)
 * @return Number of valid entries loaded (0..6)
 */
int wifi_saved_load_all(wifi_saved_entry_t *entries);

/*******************************************************************************
 * Non-blocking OPTIGA probe API — use from LVGL callbacks to avoid blocking.
 *
 * Usage:
 *   1. wifi_saved_probe_start()   → send IPC probe, return immediately
 *   2. wifi_saved_probe_ready()   → poll periodically (100ms timer)
 *   3. wifi_saved_probe_finish()  → set OPTIGA state based on response
 *   4. After probe, wifi_saved_load() skips probe (already done)
 ******************************************************************************/
bool wifi_saved_probe_start(void);
bool wifi_saved_probe_ready(void);
void wifi_saved_probe_finish(void);

/*******************************************************************************
 * Non-blocking slot read API — async IPC send + poll for individual slots.
 *
 * Usage:
 *   1. wifi_saved_read_start(index)  → send IPC or instant (RAM fallback)
 *   2. wifi_saved_read_ready()       → poll periodically
 *   3. wifi_saved_read_result(out)   → get entry after ready==true
 ******************************************************************************/
bool wifi_saved_read_start(int index);
bool wifi_saved_read_ready(void);
bool wifi_saved_read_result(wifi_saved_entry_t *out);

#endif /* WIFI_SAVED_H */
