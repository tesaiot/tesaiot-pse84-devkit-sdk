/*******************************************************************************
 * File Name: sdk_examples_cm33.h
 *
 * Description: The CM33_NS SDK example registry.
 *
 *              Three of the six shipped libraries are Cortex-M33 soft-float
 *              archives — ble_nus (libbento_secure.a), mpy_secure
 *              (libbento_mpy.a) and tesaiot_hsm (libbento_hsm.a). Verified from
 *              the archives themselves:
 *
 *                arm-none-eabi-readelf -A  ->  Tag_CPU_arch: v8-M.mainline
 *                                              (no Tag_ABI_VFP_args)
 *
 *              while edge_ai / cm55_core / ipc_core report cortex-m55 with
 *              "Tag_ABI_VFP_args: VFP registers". The two cores are not
 *              ABI-compatible, so these examples cannot live in proj_cm55/ and
 *              cannot be called from the CM55 Examples page. They run HERE.
 *
 * HOW TO RUN THEM
 * ---------------
 * CM33_NS owns the UART console (driver/retarget_io_init.c calls
 * cy_retarget_io_init(); CM55's printf is a deliberate weak no-op). So these
 * examples report with printf, and are selected at build time:
 *
 *     make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=<id>
 *
 * where <id> is the `id` field of a row in g_sdk_examples_cm33[]. With
 * ENABLE_PAGE_EXAMPLES=1 and no id, the runner lists every row and its
 * description on the console at boot and runs nothing — which is itself the
 * answer to "which functions can I call".
 *
 * THE CONTRACT
 * ------------
 *     int example_xxx(void);   0 = ok, negative = SDK_EX_* (sdk_examples.h)
 *
 * RULES
 *  1. The runner task is priority tskIDLE_PRIORITY+1 — below every printf-using
 *     task in the system. printf is therefore safe here: this task can only
 *     ever be the waiter on the UART mutex, never the holder that blocks a
 *     higher-priority task. (The priority-inversion hazard documented in
 *     ipc_tesaiot_handler.c applies to tasks at priority 3+, not to this one.)
 *  2. NEVER printf from an IPC callback — that is ISR context.
 *  3. No example here starts a radio, writes a credential, or changes OPTIGA
 *     life-cycle state unless its name says so and its comment explains the
 *     consequence. OPTIGA LcsO moves one way only and no reflash undoes it.
 *
 *******************************************************************************/

#ifndef SDK_EXAMPLES_CM33_H
#define SDK_EXAMPLES_CM33_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Same codes as the CM55 side, repeated here so this core needs no CM55
 * header. Kept in lock-step deliberately; both are quoted in examples/README. */
#define SDK_EX_OK            (0)
#define SDK_EX_UNAVAILABLE  (-1)
#define SDK_EX_BUSY         (-2)
#define SDK_EX_REFUSED      (-3)
#define SDK_EX_NO_DATA      (-4)
#define SDK_EX_STARTED      (-5)

const char *sdk_example_strerror(int rc);

typedef enum {
    SDK33_GRP_SENSORS = 0,
    SDK33_GRP_IO,
    SDK33_GRP_CONNECTIVITY,
    SDK33_GRP_SECURITY,
    SDK33_GRP_STORAGE,
    SDK33_GRP_BLE,
    SDK33_GRP_END_TO_END,
    SDK33_GRP_COUNT
} sdk33_group_t;

const char *sdk33_group_name(sdk33_group_t g);

typedef struct {
    const char    *id;       /* "ble_nus/01_advertise" — matches the file  */
    const char    *title;
    const char    *teaches;
    const char    *apis;     /* the SDK calls this example exercises       */
    sdk33_group_t  group;
    int          (*run)(void);
} sdk33_example_t;

extern const sdk33_example_t g_sdk_examples_cm33[];
extern const unsigned        g_sdk_example_cm33_count;

/** Create the runner task. Safe to call before the scheduler starts.
 *  Does nothing unless ENABLE_PAGE_EXAMPLES=1. */
BaseType_t sdk_examples_cm33_start(void);

/** Run one row by id. Returns SDK_EX_UNAVAILABLE for an unknown id. */
int sdk_examples_cm33_run(const char *id);

/** Print every row — id, module, title, APIs — to the console. */
void sdk_examples_cm33_list(void);

#ifdef __cplusplus
}
#endif

#endif /* SDK_EXAMPLES_CM33_H */
