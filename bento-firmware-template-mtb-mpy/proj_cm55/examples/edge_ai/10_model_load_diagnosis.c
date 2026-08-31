/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/10_model_load_diagnosis
 * title:   The model never produced a verdict — which of the four reasons is it?
 * teaches: init_calls, init_returns, inits and last_init_rc separate "the task never ran", "init hung", "init failed" and "init was never called"
 * apis:    ai_engine_init_calls, ai_engine_init_returns, ai_engine_inits, ai_engine_last_init_rc
 * entry:   example_edge_ai_model_load_diagnosis
 */
/*
 * 07_engine_health answers "is it running now". This one answers the question
 * that comes BEFORE it: did the model ever load at all.
 *
 * The two are different failures with different fixes, and the counters that
 * separate them are cumulative — unlike the health counters, these are read
 * ONCE, not as deltas, because a load either happened or it did not.
 *
 * WHAT EACH COUNTER MEANS  (ai_engine.h:220-232, :256)
 * ----------------------------------------------------
 *   ai_engine_init_calls()    how many times a model's init() was ENTERED
 *   ai_engine_init_returns()  how many of those RETURNED
 *   ai_engine_inits()         how many completed the whole cold-load
 *   ai_engine_last_init_rc()  the return code of the last init(), or
 *                             0x7FFFFFFF when init() was never called at all
 *
 * The sentinel matters: 0 means "init ran and succeeded", and 0x7FFFFFFF means
 * "init never ran". Reading last_init_rc alone cannot tell those apart from a
 * quick glance, which is why init_calls is read first.
 */

#include <stdio.h>
#include <stdint.h>

#include "ai_engine.h"

/* ai_engine.h:256 — the value last_init_rc carries when init() never ran. */
#define AI_INIT_RC_NEVER_CALLED  (0x7FFFFFFF)

void example_edge_ai_model_load_diagnosis(void)
{
    uint32_t calls   = ai_engine_init_calls();
    uint32_t returns = ai_engine_init_returns();
    uint32_t inits   = ai_engine_inits();
    int32_t  rc      = ai_engine_last_init_rc();

    printf("init_calls=%lu init_returns=%lu inits=%lu last_rc=%ld\n",
           (unsigned long)calls, (unsigned long)returns,
           (unsigned long)inits, (long)rc);

    /* Read in this order. Each test is only meaningful once the one above it
       has been ruled out. */

    if (calls == 0) {
        /* Nothing ever entered a model's init(). The inference task is absent,
           or no select()/start() ever landed on it. Look at whether the task
           was created and whether the UI actually sent a selection — not at
           the model, which was never asked to do anything. */
        printf("the inference task never reached the cold load\n");
        return;
    }

    if (returns < calls) {
        /* Entered more times than it came back. One init() is still inside the
           model's own code, or faulted there. This is the only case where the
           counters point INSIDE the model rather than around it. */
        printf("init() was entered %lu time(s) and returned %lu — one is stuck\n",
               (unsigned long)calls, (unsigned long)returns);
        return;
    }

    if (rc == AI_INIT_RC_NEVER_CALLED) {
        /* Reachable only if the engine recorded calls without recording a
           result — a bookkeeping inconsistency worth reporting rather than
           quietly treating as success. */
        printf("init() counted %lu call(s) but recorded no return code\n",
               (unsigned long)calls);
        return;
    }

    if (rc != 0) {
        /* The model's own init() refused, and rc is ITS code, not the engine's.
           Look it up in that model's header; the engine only carries it. */
        printf("the model refused to initialise, rc=%ld\n", (long)rc);
        return;
    }

    if (inits == 0) {
        /* init() succeeded but no cold load completed — the load was abandoned
           after init, typically because the selection changed underneath it. */
        printf("init() succeeded but no cold load completed\n");
        return;
    }

    printf("%lu model load(s) completed — look at 07_engine_health for what "
           "happens after the load\n", (unsigned long)inits);
}
