/*******************************************************************************
 * File Name: bento_time.c
 *
 * Description: Implementation of bento.time.sync / bento.time.now (see
 *              bento_time.h for the contract + rationale).
 *
 ******************************************************************************/

#include "bento_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1)
#include "FreeRTOS.h"
#include "task.h"
#endif

#include "vendor/jsmn.h"

/* Single external dep — the NUS TX path. The two JSON helpers we need
 * (json_find_top, json_int_or) are `static` inside nus_commands.c so
 * we can't link against them; inline equivalents below keep this
 * translation unit standalone and avoid leaking the helpers' linkage.
 */
extern int ble_nus_send(const uint8_t *data, size_t len);

/* Compare a jsmn key token against a C-string. Same semantics as the
 * `jsonstr_eq` inside nus_commands.c. */
static bool bt_key_eq(const char *json, const jsmntok_t *t, const char *name)
{
    if (t->type != JSMN_STRING) return false;
    size_t n = (size_t)(t->end - t->start);
    return strlen(name) == n && memcmp(json + t->start, name, n) == 0;
}

/* Find the value token of a top-level object key. Mirrors
 * nus_commands.c::json_find_top but lives here so bento_time.c can be
 * linked without depending on a static symbol from nus_commands. */
static const jsmntok_t *bt_find_top(const char *json,
                                    const jsmntok_t *toks, int n,
                                    const char *name)
{
    if (n < 1 || toks[0].type != JSMN_OBJECT) return NULL;
    int i = 1;
    while (i < n) {
        const jsmntok_t *key = &toks[i];
        if (key->parent != 0) { i++; continue; }
        if (i + 1 >= n) break;
        if (bt_key_eq(json, key, name)) return &toks[i + 1];
        int depth = 1;
        i += 1;
        do {
            if (toks[i].type == JSMN_OBJECT) depth += toks[i].size * 2;
            else if (toks[i].type == JSMN_ARRAY) depth += toks[i].size;
            i++;
            depth--;
        } while (depth > 0 && i < n);
    }
    return NULL;
}

/* Decimal-format an int64 into `buf` (NUL-terminated). The ARM toolchain
 * we link against ships newlib-nano with NO 64-bit printf support; `%lld`
 * silently prints the literal characters "ld" and the corresponding
 * `va_arg` still consumes 8 stack bytes, scrambling every argument that
 * follows. We compose the int64 → decimal conversion manually and feed
 * the result back as `%s`, which the slim printf handles fine.
 * `buf` MUST be ≥ 24 bytes (20 digits + optional sign + NUL). */
static void bt_i64_to_str(int64_t v, char *buf, size_t bufsize)
{
    if (bufsize < 2) { if (bufsize) buf[0] = '\0'; return; }
    bool neg = (v < 0);
    /* Use unsigned to avoid INT64_MIN-negation UB. */
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    char tmp[24];
    size_t len = 0;
    do {
        tmp[len++] = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u != 0 && len < sizeof(tmp));
    size_t pos = 0;
    if (neg && pos < bufsize - 1) buf[pos++] = '-';
    while (len > 0 && pos < bufsize - 1) {
        buf[pos++] = tmp[--len];
    }
    buf[pos] = '\0';
}

/* Parse a JSMN primitive token as int64. Tolerates leading sign +
 * the JSON literals true/false/null (treated as fallback). */
static int64_t bt_int_or(const char *json, const jsmntok_t *t, int64_t fallback)
{
    if (t == NULL || t->type != JSMN_PRIMITIVE) return fallback;
    char buf[24];
    size_t n = (size_t)(t->end - t->start);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, json + t->start, n);
    buf[n] = '\0';
    if (buf[0] == 't' || buf[0] == 'f' || buf[0] == 'n') return fallback;
    return (int64_t)strtoll(buf, NULL, 10);
}

/*------ State ---------------------------------------------------------------*/

/* The boot moment expressed as Unix epoch ms. Computed at sync time as
 * `epoch_ms_from_desktop - current_uptime_ms` so that
 *   wall_clock_ms = s_boot_epoch_ms + uptime_ms
 * is monotonic for the rest of the boot regardless of how long after
 * boot the desktop pushed the sync. */
static int64_t s_boot_epoch_ms = 0;
static bool    s_synced        = false;

/*------ Helpers -------------------------------------------------------------*/

#if defined(BENTO_HAS_BLE_NUS) && (BENTO_HAS_BLE_NUS == 1)
static int64_t now_uptime_ms(void)
{
    return (int64_t)((uint64_t)xTaskGetTickCount() *
                     (1000U / configTICK_RATE_HZ));
}
#else
static int64_t now_uptime_ms(void) { return 0; }
#endif

static bool is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* Convert a Unix epoch in milliseconds to (UTC) Y/M/D h:m:s.ms. The
 * algorithm is the standard "days since 1970-01-01" iterative unwind —
 * for the use case (boot epoch range 2026–2100) the loop count is
 * O(year - 1970) so worst-case ~130 iterations on the year loop +
 * ≤12 on the month loop. Trivial compared to BLE round-trip. */
typedef struct {
    int year;
    int month;   /* 1-12 */
    int day;     /* 1-31 */
    int hour;    /* 0-23 */
    int minute;  /* 0-59 */
    int second;  /* 0-59 */
    int ms;      /* 0-999 */
} ymdhms_t;

static void epoch_ms_to_ymdhms(int64_t epoch_ms, ymdhms_t *out)
{
    if (epoch_ms < 0) {
        memset(out, 0, sizeof(*out));
        return;
    }
    int64_t total_s = epoch_ms / 1000;
    int     ms      = (int)(epoch_ms % 1000);
    int64_t days    = total_s / 86400;
    int     sod     = (int)(total_s - (int64_t)days * 86400); /* second-of-day */

    int year = 1970;
    while (1) {
        int dy = is_leap(year) ? 366 : 365;
        if (days < dy) break;
        days -= dy;
        year++;
    }

    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = 0;
    int feb   = is_leap(year) ? 29 : 28;
    while (month < 12) {
        int dim = (month == 1) ? feb : dm[month];
        if (days < dim) break;
        days -= dim;
        month++;
    }

    out->year   = year;
    out->month  = month + 1;
    out->day    = (int)days + 1;
    out->hour   = sod / 3600;
    out->minute = (sod % 3600) / 60;
    out->second = sod % 60;
    out->ms     = ms;
}

/*------ NUS tx helper -------------------------------------------------------*/

/* bento.time.now's `_diag`-less ack is bounded; 256 bytes is plenty for
 * the full year + iso + uptime payload. */
#define TIME_TX_BUF 256

/*------ Handlers ------------------------------------------------------------*/

void bento_time_handle_sync(const char *json, const jsmntok_t *toks, int n_toks)
{
    /* Desktop pushes `epoch_ms` (Unix epoch ms). Negative or zero is
     * treated as "clear" and clears the sync state. */
    int64_t epoch_ms = bt_int_or(
        json, bt_find_top(json, toks, n_toks, "epoch_ms"), 0);

    if (epoch_ms <= 0) {
        s_synced = false;
        s_boot_epoch_ms = 0;
        char tx[TIME_TX_BUF];
        int w = snprintf(tx, sizeof(tx),
            "{\"ack\":\"bento.time.sync\",\"ok\":true,\"n\":0,"
            "\"synced\":false,\"boot_epoch_ms\":0}\n");
        if (w > 0 && w < (int)sizeof(tx)) {
            (void)ble_nus_send((const uint8_t *)tx, (size_t)w);
        }
        return;
    }

    int64_t up = now_uptime_ms();
    s_boot_epoch_ms = epoch_ms - up;
    s_synced = true;

    char tx[TIME_TX_BUF];
    char boot_buf[24];
    char up_buf[24];
    bt_i64_to_str(s_boot_epoch_ms, boot_buf, sizeof(boot_buf));
    bt_i64_to_str(up, up_buf, sizeof(up_buf));
    //! [ble_ble_nus_send_bounded_frame]
    /* ...context: inside the bento.time.sync ack emitter ... */
    int w = snprintf(tx, sizeof(tx),
        "{\"ack\":\"bento.time.sync\",\"ok\":true,\"n\":0,"
        "\"synced\":true,\"boot_epoch_ms\":%s,\"uptime_ms_at_sync\":%s}\n",
        boot_buf, up_buf);
    if (w > 0 && w < (int)sizeof(tx)) {
        (void)ble_nus_send((const uint8_t *)tx, (size_t)w);
    }
    //! [ble_ble_nus_send_bounded_frame]
}

void bento_time_handle_now(const char *json,
                           const jsmntok_t *toks, int n_toks)
{
    (void)json; (void)toks; (void)n_toks;

    int64_t up = now_uptime_ms();
    char tx[TIME_TX_BUF];
    char up_buf[24];
    bt_i64_to_str(up, up_buf, sizeof(up_buf));

    if (!s_synced) {
        int w = snprintf(tx, sizeof(tx),
            "{\"ack\":\"bento.time.now\",\"ok\":true,\"n\":0,"
            "\"synced\":false,\"epoch_ms\":null,\"iso8601\":null,"
            "\"uptime_ms\":%s}\n",
            up_buf);
        if (w > 0 && w < (int)sizeof(tx)) {
            (void)ble_nus_send((const uint8_t *)tx, (size_t)w);
        }
        return;
    }

    int64_t wall = s_boot_epoch_ms + up;
    ymdhms_t t;
    epoch_ms_to_ymdhms(wall, &t);
    char wall_buf[24];
    bt_i64_to_str(wall, wall_buf, sizeof(wall_buf));

    int w = snprintf(tx, sizeof(tx),
        "{\"ack\":\"bento.time.now\",\"ok\":true,\"n\":0,"
        "\"synced\":true,\"epoch_ms\":%s,"
        "\"iso8601\":\"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ\","
        "\"year\":%d,\"month\":%d,\"day\":%d,"
        "\"hour\":%d,\"minute\":%d,\"second\":%d,"
        "\"uptime_ms\":%s}\n",
        wall_buf,
        t.year, t.month, t.day, t.hour, t.minute, t.second, t.ms,
        t.year, t.month, t.day,
        t.hour, t.minute, t.second,
        up_buf);
    if (w > 0 && w < (int)sizeof(tx)) {
        (void)ble_nus_send((const uint8_t *)tx, (size_t)w);
    }
}
