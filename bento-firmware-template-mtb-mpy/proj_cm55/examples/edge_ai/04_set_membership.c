/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/04_set_membership
 * title:   Redefine what a set contains
 * teaches: read a set's compiled membership, replace it at run time, read the override back, and put the original back
 * apis:    ai_engine_set_name, ai_engine_set_models, ai_engine_set_define, ai_engine_set_members_defined, ai_engine_model_count, ai_engine_model
 * entry:   example_edge_ai_set_membership
 */
/*
 * A set ships with a membership chosen at build time -- "Room Presence" is a
 * particular handful of models. ai_engine_set_define() replaces that list for
 * the rest of the boot, which is how a run-time model (05_register_model,
 * 06_staged_load) joins a watch it could not have been compiled into.
 *
 * THREE CALLS, AND THEY ANSWER DIFFERENT QUESTIONS
 *
 *   ai_engine_set_models(set,..)          the EFFECTIVE membership: the
 *                                         override if one exists, otherwise
 *                                         the compiled list. This is what the
 *                                         engine will actually run.
 *   ai_engine_set_members_defined(set,..) the override ALONE. 0 means "no
 *                                         override" -- it is the only way to
 *                                         tell a set that was never touched
 *                                         from one redefined to the same list.
 *   ai_engine_set_members(..)             the ACTIVE set's members, whatever
 *                                         set that is. A different question
 *                                         again; see 03_parallel_set_run.
 *
 * WHAT THE ENGINE WILL AND WILL NOT ACCEPT
 *
 *   - Only the three definable sets: INTRUDER (253), ROOM (254), MIC (255).
 *     ALL (252) is "every registered row" by construction and has no override
 *     slot; set_define() on it is refused.
 *   - 1 to 8 members. Zero is refused, which is why there is no "undefine".
 *   - Every index must be a live registry row. One bad entry refuses the WHOLE
 *     definition -- the set is never left half-changed.
 *   - The new list is built complete and then published, so a reader mid-call
 *     sees either the old membership or the new one, never a splice of both.
 *
 * THERE IS NO UNDO. A defined set stays defined for the boot. "Putting the
 * original back", below, means defining the original list a second time: the
 * membership is restored exactly, the override flag is not cleared. That is
 * enough to restore behaviour, and it is why this example captures the list
 * BEFORE it changes anything -- read first, then write, or the original is
 * gone.
 *
 * Task context, and quick: it validates, copies at most eight bytes under a
 * critical section, and returns. Nothing is started and no model is disturbed.
 */

#include <stdbool.h>
#include <stdint.h>

#include "../sdk_examples.h"
#include "ai_engine.h"

#define MEMBERS_MAX  (8u)   /* the engine's ceiling on a set definition */

static void log_membership(const char *what, const uint8_t *idx, uint32_t n)
{
    if (n == 0u) {
        sdk_example_logf("%s: (none)", what);
        return;
    }
    for (uint32_t i = 0u; i < n; i++) {
        const ai_model_desc_t *d = ai_engine_model(idx[i]);
        sdk_example_logf("%s: [%u] %s", what, (unsigned)idx[i],
                         ((d != NULL) && (d->name != NULL)) ? d->name : "?");
    }
}

int example_edge_ai_set_membership(lv_obj_t *parent)
{
    (void)parent;   /* membership is a list of numbers; the log shows them */

    const uint32_t set   = (uint32_t)AI_PARALLEL_ROOM;   /* 254, definable */
    const uint32_t count = ai_engine_model_count();

    if (count == 0u) {
        sdk_example_logf("registry empty -- a set can only hold live rows");
        return SDK_EX_UNAVAILABLE;
    }

    const char *name = ai_engine_set_name(set);
    if (name == NULL) {
        sdk_example_logf("%lu is not a set index", (unsigned long)set);
        return SDK_EX_REFUSED;
    }
    sdk_example_logf("set %lu '%s', registry holds %lu model(s)",
                     (unsigned long)set, name, (unsigned long)count);

    /* --- 1. read the override state, then the effective membership -------
     * In this order, because the second call cannot tell you which of the two
     * it just returned. */
    uint8_t        ovr[MEMBERS_MAX];
    const uint32_t ovr_n = ai_engine_set_members_defined(set, ovr, MEMBERS_MAX);
    sdk_example_logf("override before: %s (%lu entries)",
                     (ovr_n == 0u) ? "none, this is the compiled list"
                                   : "already defined this boot",
                     (unsigned long)ovr_n);

    uint8_t        original[MEMBERS_MAX];
    const uint32_t original_n = ai_engine_set_models(set, original, MEMBERS_MAX);
    log_membership("effective", original, original_n);

    if (original_n == 0u) {
        /* Nothing to put back afterwards, and set_define(0) is refused, so
         * there would be no way home. Refuse to start rather than strand it. */
        sdk_example_logf("this set is empty in this image. Defining one would"
                         " be irreversible for the boot -- not doing it");
        return SDK_EX_UNAVAILABLE;
    }

    /* --- 2. build a different membership ------------------------------- */
    uint8_t  mine[MEMBERS_MAX];
    uint32_t mine_n = 1u;
    mine[0] = 0u;
    if ((original_n == 1u) && (original[0] == 0u) && (count > 1u)) {
        mine[0] = (uint8_t)(count - 1u);   /* make the change observable */
    }
    if ((original_n == 1u) && (original[0] == mine[0])) {
        sdk_example_logf("only one model in this image, so the 'new' list is"
                         " the old one -- the calls below still show the flow");
    }
    log_membership("proposed", mine, mine_n);

    /* --- 3. define, then PROVE it by reading back ----------------------- */
    const int rc = ai_engine_set_define(set, mine, mine_n);
    sdk_example_logf("set_define returned %d", rc);
    if (rc < 0) {
        sdk_example_logf("refused: not a definable set, count outside 1..%u, or"
                         " an index that is not a live row. Nothing changed",
                         (unsigned)MEMBERS_MAX);
        return SDK_EX_REFUSED;
    }

    /* The return value is a promise; these two are the evidence. Check the
     * state, not the status code -- they are different things, and only one of
     * them is what the engine will run. */
    uint8_t        now[MEMBERS_MAX];
    const uint32_t now_n = ai_engine_set_members_defined(set, now, MEMBERS_MAX);
    log_membership("override now", now, now_n);

    uint8_t        eff[MEMBERS_MAX];
    const uint32_t eff_n = ai_engine_set_models(set, eff, MEMBERS_MAX);
    sdk_example_logf("effective is now %lu entr%s -- the override wins outright",
                     (unsigned long)eff_n, (eff_n == 1u) ? "y" : "ies");
    log_membership("effective", eff, eff_n);

    /* --- 4. put the original list back ---------------------------------- */
    const int back = ai_engine_set_define(set, original, original_n);
    if (back < 0) {
        /* Only reachable if a row vanished, which cannot happen -- rows are
         * never removed. Report it loudly rather than claiming a clean exit. */
        sdk_example_logf("RESTORE FAILED (%d). '%s' is left holding the test"
                         " membership for the rest of this boot", back, name);
        return SDK_EX_REFUSED;
    }

    const uint32_t final_n = ai_engine_set_models(set, eff, MEMBERS_MAX);
    log_membership("restored", eff, final_n);
    sdk_example_logf("membership is back. The override FLAG stays set for the"
                     " boot -- there is no call that clears it");

    return SDK_EX_OK;
}
