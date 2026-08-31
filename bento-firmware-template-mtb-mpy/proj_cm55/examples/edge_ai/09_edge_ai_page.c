/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/09_edge_ai_page
 * title:   Wire the Edge AI page into your page manager
 * teaches: the create/render/destroy trio is a page-manager callback set -- what each one owes the manager, and why calling them by hand corrupts the header
 * apis:    page_edge_ai_create, page_edge_ai_render, page_edge_ai_destroy, ipc_sensorhub_snapshot, ai_engine_model_count
 * needs:   BENTO_HAS_EDGE_AI
 * entry:   example_cm55_edge_ai_page
 */
/*
 * ONE page shows every compiled-in model: a list on the left, live class
 * scores, inference latency and engine state on the right. One page and not
 * one per model is a hard requirement, not a preference -- PM_MAX_PAGES is 20
 * and the product already registers 18, so per-model pages would fail to
 * register silently and leave dead cards on Home.
 *
 * THE THREE FUNCTIONS ARE PAGE-MANAGER CALLBACKS, and the table at the bottom
 * of this file is the artifact you copy:
 *
 *   page_edge_ai_create()   -> lv_obj_t *   builds a whole SCREEN (not a
 *       widget you can drop into a container) and runs it through the page
 *       manager's header helper, which takes the shared status-bar and
 *       connectivity slots for itself.
 *   page_edge_ai_render(snap)              called on every UI tick with a
 *       FRESH sensorhub snapshot. It reads snap->bmi270.sequence to compute
 *       the push rate, so handing it the same snapshot twice reports a stalled
 *       sensor that is not stalled.
 *   page_edge_ai_destroy()                 called on page exit. It deliberately
 *       leaves the model RUNNING -- stopping on exit would kill the session the
 *       moment a visitor navigated away, and the engine is idle-cheap.
 *
 * DO NOT CALL THEM BY HAND. Two mechanisms make it unsafe, and both are
 * silent:
 *
 *  1. The page header's status and connectivity labels are SINGLE fields on
 *     the shared page_manager_t, rewritten by whichever page was created last.
 *     The manager closes that window by creating the next page before the old
 *     screen goes away. Build this page out of band, delete its screen, and
 *     those fields point into freed memory -- which the status tick then
 *     writes to. LV_USE_ASSERT_OBJ is 0 in this build, so nothing catches it.
 *  2. Navigation loads the next screen with lv_screen_load_anim(..., auto_del)
 *     and the manager destroys the page IT thinks is current. A screen you
 *     created yourself is deleted by LVGL while your own destroy is never
 *     called, and the page the manager tore down instead is the one still on
 *     screen.
 *
 * So: register the table, let the manager drive. This example does what is
 * safe on its own -- it checks the prerequisites the page depends on and shows
 * the snapshot render() would have received.
 */

/* This file guards ITSELF, and must.
 *
 * page_edge_ai.h wraps its three declarations in
 *   #if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)
 * so with Edge AI compiled out the names below do not exist and this file is a
 * compile error — in a DEFAULT build, on a board nobody thought to test.
 *
 * The real build compiles every file in an examples subdirectory: it does not
 * read the `variant` tag, and CY_IGNORE only subtracts whole directories. So a
 * file needing an opt-in feature guards its own body, and a default build
 * compiles it to nothing. tools/examples_check.sh proves that with its
 * "default build (all opt-ins OFF)" pass, which is what caught this.
 */
#if defined(BENTO_HAS_EDGE_AI) && (BENTO_HAS_EDGE_AI == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sdk_examples.h"
#include "ai_engine.h"
#include "ipc_sensorhub.h"
#include "page_edge_ai.h"

/* The callback set a page manager wants. Give this to pm_register() as the
 * create/render/destroy of PAGE_ID_EDGE_AI and the page is wired -- that is
 * the whole integration, and these three symbols are the whole reason the
 * archive exports them. */
typedef struct {
    lv_obj_t *(*create)(void);
    void      (*render)(sensorhub_snapshot_t *snap);
    void      (*destroy)(void);
    const char *title;
} example_page_hooks_t;

const example_page_hooks_t example_edge_ai_page_hooks = {
    .create  = page_edge_ai_create,
    .render  = page_edge_ai_render,
    .destroy = page_edge_ai_destroy,
    .title   = "Edge AI",
};

int example_cm55_edge_ai_page(lv_obj_t *parent)
{
    (void)parent;   /* the page builds its own screen; nothing is drawn here */

    sdk_example_logf("hooks: create=0x%08lX render=0x%08lX destroy=0x%08lX",
                     (unsigned long)(uintptr_t)example_edge_ai_page_hooks.create,
                     (unsigned long)(uintptr_t)example_edge_ai_page_hooks.render,
                     (unsigned long)(uintptr_t)example_edge_ai_page_hooks.destroy);
    sdk_example_logf("register these as PAGE_ID_EDGE_AI, then add the matching"
                     " Home card -- a page with no card cannot be reached, and a"
                     " card with no page faults on tap");

    /* --- prerequisite 1: the page renders the registry, so it needs one --- */
    const uint32_t models = ai_engine_model_count();
    if (models == 0u) {
        sdk_example_logf("registry is empty. The page would build, show an empty"
                         " dropdown and never leave 'Press Load'");
        sdk_example_logf("build with BENTO_HAS_EDGE_AI=1 and at least one model");
        return SDK_EX_UNAVAILABLE;
    }
    sdk_example_logf("registry: %lu model(s) for the dropdown",
                     (unsigned long)models);

    /* --- prerequisite 2: the snapshot render() is fed on every tick ------ */
    sensorhub_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    ipc_sensorhub_snapshot(&snap);

    if (!snap.has_bmi270) {
        sdk_example_logf("no BMI270 in the snapshot. render() would draw, but its"
                         " push-rate line stays 0 Hz and no IMU model can run");
        sdk_example_logf("CM33 has not pushed a sample yet -- check the IPC link"
                         " before blaming the page");
        return SDK_EX_NO_DATA;
    }

    sdk_example_logf("snapshot ready: bmi270 seq=%u, changed=%s",
                     (unsigned)snap.bmi270.sequence,
                     snap.bmi270_changed ? "yes" : "no");
    sdk_example_logf("render() derives the push rate from that sequence number,"
                     " so give it a FRESH snapshot every tick -- reusing one"
                     " reads as a stalled sensor");
    sdk_example_logf("the manager's loop is exactly: snapshot -> render, then"
                     " destroy on exit (which leaves the model running)");

    return SDK_EX_OK;
}

#else  /* !BENTO_HAS_EDGE_AI */

/* Edge AI is compiled out: the page this example registers does not exist.
 * The translation unit is deliberately empty. The row is guarded to match in
 * the generated table, so the menu never offers a row with no function. */

#endif /* BENTO_HAS_EDGE_AI */
