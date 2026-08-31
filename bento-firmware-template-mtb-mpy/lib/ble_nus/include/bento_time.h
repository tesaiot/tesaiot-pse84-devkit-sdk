/*******************************************************************************
 * File Name: bento_time.h
 *
 * Description: Wall-clock for the BentoClaw firmware.
 *
 *              The PSE84 has no battery-backed RTC on the AI Kit / Eva Kit
 *              boards (no coin cell), so we cannot keep real wall-clock
 *              time across a power cycle. Instead the firmware accepts a
 *              one-shot "boot epoch" push from the desktop side at BLE
 *              connect time and extrapolates the current wall-clock from
 *              there using the FreeRTOS tick counter:
 *
 *                  wall_clock_ms = boot_epoch_ms + uptime_ms
 *
 *              `bento.time.sync` (Buddy → firmware)
 *                  body: {"cmd":"bento.time.sync","epoch_ms":1747085730000}
 *                  effect: stores `epoch_ms - current_uptime_ms` as the
 *                          boot epoch so subsequent now() returns are
 *                          monotonic relative to the host's clock.
 *                  ack: {"ack":"bento.time.sync","ok":true,
 *                        "boot_epoch_ms":1747085728766}
 *
 *              `bento.time.now` (LLM / desktop → firmware)
 *                  body: {"cmd":"bento.time.now"}
 *                  ack (synced):
 *                      {"ack":"bento.time.now","ok":true,"synced":true,
 *                       "epoch_ms":1747086000123,
 *                       "iso8601":"2026-05-11T20:20:00Z",
 *                       "year":2026,"month":5,"day":11,
 *                       "hour":20,"minute":20,"second":0,
 *                       "uptime_ms":271357}
 *                  ack (never synced):
 *                      {"ack":"bento.time.now","ok":true,"synced":false,
 *                       "epoch_ms":null,"iso8601":null,
 *                       "uptime_ms":271357}
 *
 *              Date formatting uses a self-contained gmtime equivalent
 *              (libc <time.h> is not pulled into this build) so the
 *              firmware can return year / month / day / hour / minute /
 *              second fields directly without the LLM having to compute
 *              them.
 *
 ******************************************************************************/

#ifndef BENTO_TIME_H
#define BENTO_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vendor/jsmn.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Handler for `bento.time.sync`. Parses `epoch_ms` from the JSON body
 * and stores the implied boot epoch. Emits an ack with the recorded
 * boot_epoch_ms so the desktop can verify the round trip. */
void bento_time_handle_sync(const char *json, const jsmntok_t *toks, int n_toks);

/* Handler for `bento.time.now`. Emits the current wall-clock view +
 * uptime + a `synced` flag. Always responds, even before the desktop
 * has pushed a sync. */
void bento_time_handle_now(const char *json, const jsmntok_t *toks, int n_toks);

#ifdef __cplusplus
}
#endif

#endif /* BENTO_TIME_H */
