/*******************************************************************************
 * File Name: sdk_examples.h
 *
 * Description: The CM55 SDK example registry.
 *
 *              Every example in this tree is ONE function with ONE signature,
 *              named in ONE table. The Examples page (PAGE_ID_EXAMPLES) renders
 *              that table as a scrollable list and calls the function pointer
 *              when a row is tapped. There is no page per example — 24 of the
 *              25 page_id_t slots are already spoken for, and a table row costs
 *              nothing.
 *
 *              Compiled only when ENABLE_PAGE_EXAMPLES=1. Default is 0, so the
 *              shipped product firmware is unchanged unless a developer opts in:
 *
 *                  make build ENABLE_PAGE_EXAMPLES=1
 *
 * WHAT AN EXAMPLE IS
 * ------------------
 * A complete, self-contained job a developer would actually do — every include,
 * the real init order, and honest error handling. Copy one file into your own
 * project and it works. Examples are grouped by TASK, not one file per
 * function: "read a sensor and put it on the screen" is a file; twelve getters
 * are not twelve files.
 *
 * The getters, status probes and readers that no realistic task exercises live
 * in ONE reference file per module (ref_<module>.c), each call carrying a short
 * comment saying when to use it and what it returns. That file is clearly a
 * reference list, not a working job — which is what keeps the task files honest
 * instead of padded to hit a coverage number.
 *
 * THE HEADER BLOCK EVERY EXAMPLE CARRIES
 * --------------------------------------
 * The table below is GENERATED from the example files by
 * tools/gen_examples_table.py, so it can never drift from what is on disk.
 * Each file states its own row in its first 25 lines:
 *
 *   / * sdk-example: core=cm55 variant=both module=edge_ai
 *    * id:      edge_ai/01_first_inference
 *    * title:   Run your first inference
 *    * teaches: pick a model, start the engine, read the verdict back
 *    * apis:    ai_engine_init, ai_engine_start, ai_engine_snapshot
 *    * entry:   example_edge_ai_first_inference
 *    * /
 *
 * THE CONTRACT
 * ------------
 *   int example_xxx(lv_obj_t *parent);
 *
 *   parent  The content container of the Examples page, already laid out and
 *           scrollable, in the GFX task. Draw into it with any LVGL call.
 *           NULL is never passed — an example that does not draw simply
 *           ignores it.
 *
 *   return  0 on success, or a negative SDK_EX_* code below. The page shows
 *           the code and sdk_example_strerror() beside the row.
 *
 * RULES EVERY EXAMPLE FOLLOWS
 * ---------------------------
 *  1. run() is called FROM THE GFX TASK, inside an LVGL event callback.
 *     Therefore: LVGL calls are legal here and ONLY here (workspace rule —
 *     all LVGL work in the GFX task). Never create an LVGL widget from any
 *     other task.
 *  2. run() MUST NOT BLOCK. The GFX task drives the display; a busy-wait here
 *     freezes the screen and simultaneously stops the busy overlay that would
 *     have explained the freeze. Start asynchronous work and return; report
 *     progress through sdk_example_logf() on a later tap or an lv_timer.
 *  3. Never printf(). CM55 has no console — proj_cm55/bento_cm55_stdio.c makes
 *     printf a weak no-op precisely so this mistake is silent rather than a
 *     hard fault. Use sdk_example_logf(), which reaches the screen.
 *  4. Report failure honestly. If the hardware is absent, say so and return
 *     SDK_EX_UNAVAILABLE. Do not fake a result.
 *
 *******************************************************************************/

#ifndef SDK_EXAMPLES_H
#define SDK_EXAMPLES_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Return codes
 *******************************************************************************/
#define SDK_EX_OK            (0)    /* the job completed                       */
#define SDK_EX_UNAVAILABLE  (-1)    /* hardware or peer absent on this board   */
#define SDK_EX_BUSY         (-2)    /* something else owns the resource        */
#define SDK_EX_REFUSED      (-3)    /* the SDK refused — see the log line      */
#define SDK_EX_NO_DATA      (-4)    /* nothing published yet; try again later  */
#define SDK_EX_STARTED      (-5)    /* asynchronous work began; not an error   */

/** Human text for a code above. Never NULL. */
const char *sdk_example_strerror(int rc);

/*******************************************************************************
 * Which shipped library the example belongs to. One per lib/<module>/api.txt.
 *******************************************************************************/
/*******************************************************************************
 * Capability groups — what the board DOES, not which archive it came from.
 *
 * A developer arrives with "I want to read a sensor", not with "I want
 * ipc_core". The groups are the first thing the menu sorts by for that reason.
 * Some groups exist on both cores (a sensor is read on CM33, drawn on CM55);
 * the row's `where` field says which, and a row that cannot run on this core
 * carries run == NULL.
 *******************************************************************************/
typedef enum {
    SDK_GRP_SENSORS = 0,    /* IMU, magnetometer, pressure, humidity, radar, mic */
    SDK_GRP_IO,             /* buttons, CapSense, GPIO, RGB matrix, pots, motor  */
    SDK_GRP_DISPLAY,        /* LVGL widgets, charts, pages                       */
    SDK_GRP_EDGE_AI,        /* on-device inference                               */
    SDK_GRP_SECURITY,       /* OPTIGA Trust M: keys, CSR, Protected Update, mTLS */
    SDK_GRP_CONNECTIVITY,   /* WiFi, MQTT, HTTP/HTTPS, NTP                       */
    SDK_GRP_STORAGE,        /* LittleFS, credential storage                      */
    SDK_GRP_BLE,            /* needs ENABLE_PAGE_BENTO_BUDDY=1 — see the README  */
    SDK_GRP_END_TO_END,     /* cross-module: sensor -> display -> MQTT, etc.     */
    SDK_GRP_COUNT
} sdk_group_t;

/** Short display name for a group ("sensors"). Never NULL. */
const char *sdk_group_name(sdk_group_t g);

/** Accent colour (0xRRGGBB) the menu paints that group's rows with. */
uint32_t sdk_group_color(sdk_group_t g);

/*******************************************************************************
 * One example: a table row plus a function pointer.
 *******************************************************************************/
typedef struct {
    const char  *id;        /* "edge_ai/01_first_inference" — matches the file */
    const char  *title;     /* menu row text                                   */
    const char  *teaches;   /* one line: what a developer learns here          */
    const char  *apis;      /* the SDK calls this example exercises            */
    sdk_group_t  group;

    /* NULL for a CM33-owned example: it cannot run on this core. The page
     * shows `where` instead of running anything. */
    int        (*run)(lv_obj_t *parent);

    /* For a CM33 row: how to run it. NULL for CM55 rows. */
    const char  *where;
} sdk_example_t;

/** The table. Defined in sdk_examples.c. */
extern const sdk_example_t g_sdk_examples[];
extern const unsigned      g_sdk_example_count;

/*******************************************************************************
 * Reporting
 *
 * Examples write results here instead of printf(). The Examples page shows the
 * buffer under the row that was tapped, so a developer sees the real numbers
 * the SDK returned on the real board.
 *******************************************************************************/

/** Clear the log. The page calls this before running an example. */
void sdk_example_log_clear(void);

/** Append one line. Truncates rather than overflowing; never blocks. */
void sdk_example_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** The accumulated text, NUL-terminated. Never NULL. */
const char *sdk_example_log_text(void);

#ifdef __cplusplus
}
#endif

#endif /* SDK_EXAMPLES_H */
