/*******************************************************************************
 * File Name: tesaiot_config_store.h
 *
 * Description: TESAIoT connectivity config store — public API.
 *              Runtime-editable config backed by LittleFS key=value file.
 *              Thread-safe access via FreeRTOS mutex + copy-on-read.
 *
 *              Types (tesaiot_mode_t, tesaiot_config_t) are defined in
 *              ipc_tesaiot_defs.h (shared between CM33_NS and CM55).
 *
 * Author:      Wiroon Sriborrirux (BDH)
 *
 ******************************************************************************/

#ifndef TESAIOT_CONFIG_STORE_H
#define TESAIOT_CONFIG_STORE_H

#include "ipc_tesaiot_defs.h"   /* tesaiot_mode_t, tesaiot_config_t */
#include <stddef.h>

/*******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * Initialize config store: create mutex, load from LittleFS or set defaults.
 * Must be called after VFS mount, before wifi_creds_init().
 * @return true on success
 */
bool tesaiot_config_init(void);

/**
 * Reload config from LittleFS file.
 * @return true if file loaded, false if missing/corrupt (defaults kept)
 */
bool tesaiot_config_load(void);

/**
 * Save current config to LittleFS (atomic: .tmp + rename).
 * @return true on success
 */
bool tesaiot_config_save(void);

/**
 * Reset all fields to factory defaults and save.
 */
void tesaiot_config_reset(void);

/**
 * Thread-safe copy of entire config struct.
 * @param out  Destination buffer (caller-owned)
 */
void tesaiot_config_get(tesaiot_config_t *out);

/**
 * Set a single field by key name and persist to file.
 * @param key    Field name (e.g., "tls_mode", "broker", "device_id")
 * @param value  String value (parsed to appropriate type internally)
 * @return true on success, false if key unknown or save failed
 */
bool tesaiot_config_set_field(const char *key, const char *value);

/**
 * Set a single field in memory only (no file save).
 * Safe to call from any FreeRTOS task (no Python I/O).
 * @return true if key was recognized and value parsed
 */
bool tesaiot_config_set_field_nosave(const char *key, const char *value);

/**
 * Get pointer to the active config (read-only, for same-core access).
 * NOT thread-safe — use tesaiot_config_get() for cross-task access.
 * @return Pointer to static config struct
 */
const tesaiot_config_t *tesaiot_config_get_ptr(void);

/**
 * Get the mode name string for display.
 * @param mode  Mode enum value
 * @return Static string (e.g., "mTLS + OPTIGA")
 */
const char *tesaiot_mode_name(tesaiot_mode_t mode);

#endif /* TESAIOT_CONFIG_STORE_H */
