/*******************************************************************************
 * File Name: sdk_examples.c
 *
 * Description: Support code for the CM55 SDK examples — the report log the
 *              examples write into, and the small lookups the Examples page
 *              uses to label rows.
 *
 *              The example TABLE is not here. It is generated into
 *              sdk_examples_table.c from the header block each example file
 *              carries, so it cannot drift from what is on disk.
 *
 *              Compiled only when ENABLE_PAGE_EXAMPLES=1.
 *
 * WHY THERE IS A LOG AT ALL
 * -------------------------
 * CM55 has no console. proj_cm55/bento_cm55_stdio.c makes printf a weak no-op
 * on this core deliberately: retarget-io's _write takes a mutex that is never
 * initialised here and calls abort() when it cannot get it, so a printf left in
 * CM55 code is a hard fault waiting for the first person who reaches that line.
 *
 * An example that cannot report is not an example, so the examples write here
 * and the Examples page draws the buffer. The developer sees the real values
 * the SDK returned on the real board, which is the whole point.
 *
 *******************************************************************************/

#include "sdk_examples.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Return codes
 *******************************************************************************/
const char *sdk_example_strerror(int rc)
{
    switch (rc) {
    case SDK_EX_OK:          return "ok";
    case SDK_EX_UNAVAILABLE: return "unavailable on this board";
    case SDK_EX_BUSY:        return "busy — another owner holds it";
    case SDK_EX_REFUSED:     return "refused by the SDK";
    case SDK_EX_NO_DATA:     return "no data published yet";
    case SDK_EX_STARTED:     return "started — result arrives asynchronously";
    default:                 return "unknown result";
    }
}

/*******************************************************************************
 * Module labels
 *******************************************************************************/
const char *sdk_group_name(sdk_group_t g)
{
    switch (g) {
    case SDK_GRP_SENSORS:      return "sensors";
    case SDK_GRP_IO:           return "io";
    case SDK_GRP_DISPLAY:      return "display";
    case SDK_GRP_EDGE_AI:      return "edge_ai";
    case SDK_GRP_SECURITY:     return "security";
    case SDK_GRP_CONNECTIVITY: return "connectivity";
    case SDK_GRP_STORAGE:      return "storage";
    case SDK_GRP_BLE:          return "ble";
    case SDK_GRP_END_TO_END:   return "end_to_end";
    default:                   return "?";
    }
}

uint32_t sdk_group_color(sdk_group_t g)
{
    /* One colour per capability, so a developer scanning the list sees the
     * shape of the SDK rather than a wall of identical rows. */
    switch (g) {
    case SDK_GRP_SENSORS:      return 0x4CAF50;  /* green  */
    case SDK_GRP_IO:           return 0x8BC34A;  /* lime   */
    case SDK_GRP_DISPLAY:      return 0x00BCD4;  /* cyan   */
    case SDK_GRP_EDGE_AI:      return 0x9C27B0;  /* purple */
    case SDK_GRP_SECURITY:     return 0xF44336;  /* red    */
    case SDK_GRP_CONNECTIVITY: return 0x2196F3;  /* blue   */
    case SDK_GRP_STORAGE:      return 0xFF9800;  /* orange */
    case SDK_GRP_BLE:          return 0x7E57C2;  /* violet */
    case SDK_GRP_END_TO_END:   return 0xE1482F;  /* accent */
    default:                   return 0x9E9E9E;
    }
}

/*******************************************************************************
 * The report log
 *
 * A fixed buffer, not a heap allocation: this is written from the GFX task
 * while LVGL is drawing from the same heap, and an example that reports a
 * failure is exactly the moment you do not want a second failure mode.
 * Overflow truncates and says so, rather than dropping the tail silently.
 *******************************************************************************/
#define SDK_LOG_BYTES  (2048u)

static char     s_log[SDK_LOG_BYTES];
static uint32_t s_used;
static bool     s_truncated;

void sdk_example_log_clear(void)
{
    s_log[0]    = '\0';
    s_used      = 0u;
    s_truncated = false;
}

void sdk_example_logf(const char *fmt, ...)
{
    if (s_truncated) {
        return;
    }
    /* Leave room for the truncation notice, so it can always be appended. */
    static const char notice[] = "\n... (log full)";
    const uint32_t    reserve  = (uint32_t)sizeof(notice);

    if (s_used + reserve >= SDK_LOG_BYTES) {
        goto full;
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(&s_log[s_used], (size_t)(SDK_LOG_BYTES - reserve - s_used),
                      fmt, ap);
    va_end(ap);

    if (n < 0) {
        return;                     /* encoding error — drop this line only */
    }
    if ((uint32_t)n >= (SDK_LOG_BYTES - reserve - s_used)) {
        s_used = SDK_LOG_BYTES - reserve;
        goto full;
    }
    s_used += (uint32_t)n;

    /* One line per call: callers pass the text, the newline is ours, so no
     * example can forget it and run two results together. */
    if (s_used + 1u < SDK_LOG_BYTES - reserve) {
        s_log[s_used++] = '\n';
        s_log[s_used]   = '\0';
    }
    return;

full:
    s_truncated = true;
    (void)memcpy(&s_log[s_used], notice, sizeof(notice));
}

const char *sdk_example_log_text(void)
{
    return s_log;
}
