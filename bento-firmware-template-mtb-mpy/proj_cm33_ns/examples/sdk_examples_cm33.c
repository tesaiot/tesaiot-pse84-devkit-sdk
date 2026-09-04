/*******************************************************************************
 * File Name: sdk_examples_cm33.c
 *
 * Description: The CM33_NS SDK example runner.
 *
 *              CM33_NS owns the UART console, so these examples report with
 *              printf and are driven from the console rather than the screen.
 *              The three libraries they exercise — ble_nus, mpy_secure and
 *              tesaiot_hsm — are v8-M soft-float archives and cannot be linked
 *              into the CM55 image, so they cannot be run from the Examples
 *              page. The page still LISTS them, with the command below.
 *
 *              The example TABLE is not here. It is generated into
 *              sdk_examples_cm33_table.c from the header block each example
 *              file carries.
 *
 *              Compiled only when ENABLE_PAGE_EXAMPLES=1 — the same single
 *              master flag that adds the CM55 Examples page, so one switch
 *              turns the whole feature on or off across both cores.
 *
 * RUNNING THEM
 *     make build ENABLE_PAGE_EXAMPLES=1
 *         -> at boot the runner prints every example, its module, what it
 *            teaches and the APIs it exercises, and runs nothing.
 *
 *     make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=ble_nus/01_advertise
 *         -> the same listing, then that one example runs and prints its result.
 *
 * WHY THE LISTING IS THE DEFAULT
 * A developer's first question is "what can I call?", not "run something". The
 * listing answers it on a board with no debugger attached, and running nothing
 * by default keeps an opt-in build from starting a radio or touching the secure
 * element because somebody set one flag.
 *
 *******************************************************************************/

#include "sdk_examples_cm33.h"

#include <stdio.h>
#include <string.h>

#include "task.h"

#ifndef ENABLE_PAGE_EXAMPLES
#define ENABLE_PAGE_EXAMPLES 0
#endif

/* The id to run at boot, as a string. Empty means "list only". */
#ifndef SDK_EXAMPLE_CM33
#define SDK_EXAMPLE_CM33 ""
#endif

/*******************************************************************************
 * Return codes — the same values as the CM55 side (sdk_examples.h).
 *******************************************************************************/
const char *sdk_example_strerror(int rc)
{
    switch (rc) {
    case SDK_EX_OK:          return "ok";
    case SDK_EX_UNAVAILABLE: return "unavailable on this board";
    case SDK_EX_BUSY:        return "busy - another owner holds it";
    case SDK_EX_REFUSED:     return "refused by the SDK";
    case SDK_EX_NO_DATA:     return "no data published yet";
    case SDK_EX_STARTED:     return "started - result arrives asynchronously";
    default:                 return "unknown result";
    }
}

const char *sdk33_group_name(sdk33_group_t g)
{
    switch (g) {
    case SDK33_GRP_SENSORS:      return "sensors";
    case SDK33_GRP_IO:           return "io";
    case SDK33_GRP_CONNECTIVITY: return "connectivity";
    case SDK33_GRP_SECURITY:     return "security";
    case SDK33_GRP_STORAGE:      return "storage";
    case SDK33_GRP_BLE:          return "ble";
    case SDK33_GRP_END_TO_END:   return "end_to_end";
    default:                     return "?";
    }
}

/*******************************************************************************
 * Listing and dispatch
 *******************************************************************************/
void sdk_examples_cm33_list(void)
{
    printf("\r\n");
    printf("=== TESAIoT SDK examples on CM33_NS (%u) ===\r\n",
           g_sdk_example_cm33_count);
    printf("Run one with: make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=<id>\r\n");
    printf("\r\n");

    const char *mod = NULL;
    for (unsigned i = 0u; i < g_sdk_example_cm33_count; i++) {
        const sdk33_example_t *e = &g_sdk_examples_cm33[i];
        const char *m = sdk33_group_name(e->group);
        if ((mod == NULL) || (strcmp(mod, m) != 0)) {
            mod = m;
            printf("-- %s ------------------------------------------\r\n", m);
        }
        printf("  %-38s %s\r\n", e->id, e->title);
        printf("  %-38s   teaches: %s\r\n", "", e->teaches);
        printf("  %-38s   apis:    %s\r\n", "", e->apis);
    }
    printf("\r\n");
}

int sdk_examples_cm33_run(const char *id)
{
    if ((id == NULL) || (id[0] == '\0')) {
        return SDK_EX_UNAVAILABLE;
    }
    for (unsigned i = 0u; i < g_sdk_example_cm33_count; i++) {
        const sdk33_example_t *e = &g_sdk_examples_cm33[i];
        if (strcmp(e->id, id) == 0) {
            printf("[sdk-example] running %s -- %s\r\n", e->id, e->title);
            int rc = (e->run != NULL) ? e->run() : SDK_EX_UNAVAILABLE;
            printf("[sdk-example] %s -> %d (%s)\r\n",
                   e->id, rc, sdk_example_strerror(rc));
            return rc;
        }
    }
    printf("[sdk-example] no example with id '%s'\r\n", id);
    return SDK_EX_UNAVAILABLE;
}

/*******************************************************************************
 * The runner task
 *
 * Priority tskIDLE_PRIORITY + 1 is deliberate and load-bearing. retarget-io's
 * printf takes a FreeRTOS mutex with no priority inheritance, so a task that
 * prints while a lower-priority task holds that mutex can invert. At priority 1
 * this task is below every printf-using task in the system, so it can only ever
 * be the waiter, never the holder that blocks somebody more important. The same
 * reasoning is what makes the priority-1 heartbeat task safe.
 *
 * The delay lets the normal boot owners — BLE, MicroPython, the HSM handler —
 * finish registering before an example asks them anything.
 *******************************************************************************/
#if ENABLE_PAGE_EXAMPLES
static void sdk_examples_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(3000));

    sdk_examples_cm33_list();

    const char *id = SDK_EXAMPLE_CM33;
    if (id[0] != '\0') {
        (void)sdk_examples_cm33_run(id);
    } else {
        printf("[sdk-example] listing only; set SDK_EXAMPLE_CM33=<id> to run one\r\n");
    }

    vTaskDelete(NULL);
}
#endif

BaseType_t sdk_examples_cm33_start(void)
{
#if ENABLE_PAGE_EXAMPLES
    return xTaskCreate(sdk_examples_task, "SDK_Ex", 1024u, NULL,
                       tskIDLE_PRIORITY + 1u, NULL);
#else
    /* Nothing compiled in. pdPASS so callers need no #if of their own. */
    return pdPASS;
#endif
}
