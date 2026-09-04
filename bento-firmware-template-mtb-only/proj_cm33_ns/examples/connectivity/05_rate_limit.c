/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/05_rate_limit
 * title:   Cap how often one tool may be called
 * teaches: check-then-record around every tool call, per-tool budgets, and the
 *          two ways the limiter lets calls through that you must design for
 * apis:    claw_rate_init, claw_rate_set, claw_rate_check, claw_rate_record
 * entry:   example_mpy_secure_rate_limit
 */

/*******************************************************************************
 * THE PATTERN. Two calls, and both are required:
 *
 *     if (!claw_rate_check("gpio_write")) { refuse; }
 *     do_the_tool();
 *     claw_rate_record("gpio_write");        <- without this, nothing counts
 *
 * check() is a question, not a reservation. It does not increment. Two tasks
 * that both check before either records will both be allowed — this limiter is
 * an abuse brake, not a concurrency semaphore.
 *
 * THREE FACTS THAT CHANGE HOW YOU CALL IT
 *
 *  1. THE NAME IS STORED BY POINTER, NOT COPIED. The entry keeps the `const
 *     char *` you passed. Pass a string literal or a static buffer. Pass a
 *     stack buffer and the table ends up holding a dangling pointer that
 *     strcmp() will read on the next call. Every name in this file is a
 *     literal, deliberately.
 *
 *  2. IT FAILS OPEN. The table holds CLAW_RATE_MAX_TOOLS (16) names, and there
 *     is no eviction and no removal. Tool seventeen gets no entry, and
 *     claw_rate_check() then returns TRUE for it — untracked means allowed. If
 *     your tool set can exceed 16 names, the limiter silently stops limiting
 *     the tail of it. Count your names.
 *
 *  3. THE WINDOW IS A TUMBLING 60 s, not a rolling one. The first check or
 *     record after 60 s have elapsed resets the counter and starts a fresh
 *     window from that moment. So a budget of 10/min permits 20 calls across a
 *     window boundary. Size the budget for the burst you can survive, not for
 *     an average.
 *
 * DEFAULT: any name the limiter meets for the first time gets 10 calls per
 * minute. claw_rate_set() overrides it — and also creates the entry, so calling
 * it at start-up for every tool you know about is the way to keep the 16 slots
 * under your control instead of first-come-first-served.
 ******************************************************************************/

#include <stdio.h>

#include "claw_safety.h"

#include "../sdk_examples_cm33.h"

/* Static storage duration, because the limiter keeps the pointer. */
static const char TOOL_CHEAP[]  = "example_cheap_tool";
static const char TOOL_COSTLY[] = "example_costly_tool";

#define COSTLY_BUDGET  (3u)

int example_mpy_secure_rate_limit(void);

int example_mpy_secure_rate_limit(void)
{
    printf("\r\n--- mpy_secure/05_rate_limit ---\r\n");

    /* Known start. init() wipes every entry, including budgets other modules
     * set with claw_rate_set(). It belongs at agent start-up, once — not in a
     * per-request path. */
    claw_rate_init();
    printf("  claw_rate_init(): %u tool slots, all free\r\n",
           (unsigned)CLAW_RATE_MAX_TOOLS);

    /* Declare the budget up front. This both sets the limit and claims the
     * slot, so a later flood of unknown tool names cannot crowd it out. */
    claw_rate_set(TOOL_COSTLY, (uint16_t)COSTLY_BUDGET);
    printf("  claw_rate_set(\"%s\", %u)\r\n", TOOL_COSTLY, (unsigned)COSTLY_BUDGET);

    /* A name never passed to claw_rate_set() gets the 10/min default the first
     * time it is seen. Nothing has to declare it. */
    printf("  \"%s\" was never configured; check() = %s (default budget)\r\n",
           TOOL_CHEAP, claw_rate_check(TOOL_CHEAP) ? "true" : "false");
    claw_rate_record(TOOL_CHEAP);

    /* Burn the costly tool's budget one call at a time, exactly as a dispatcher
     * would: ask, act, count. */
    unsigned allowed = 0u, refused = 0u;
    for (unsigned attempt = 1u; attempt <= COSTLY_BUDGET + 2u; attempt++) {
        if (!claw_rate_check(TOOL_COSTLY)) {
            refused++;
            printf("  attempt %u: check() = false — refused, tool not run\r\n",
                   attempt);
            continue;                    /* do NOT record a call you refused */
        }
        /* The tool would run here. */
        claw_rate_record(TOOL_COSTLY);
        allowed++;
        printf("  attempt %u: check() = true  — ran, recorded\r\n", attempt);
    }

    printf("  budget %u/min: %u allowed, %u refused\r\n",
           (unsigned)COSTLY_BUDGET, allowed, refused);

    if (allowed != COSTLY_BUDGET || refused == 0u) {
        /* Only two ways to get here: the 60 s window rolled over mid-loop (the
         * loop is microseconds long, so this would be a clock fault), or the
         * limiter is not enforcing. Either way, do not report success. */
        printf("  expected %u allowed then refusals — the limiter is not "
               "enforcing this budget\r\n", (unsigned)COSTLY_BUDGET);
        return SDK_EX_REFUSED;
    }

    /* The refusal is per name. A different tool has its own counter and is
     * unaffected — which is the whole reason the budget is per-tool. */
    printf("  meanwhile \"%s\" check() = %s (separate counter)\r\n",
           TOOL_CHEAP, claw_rate_check(TOOL_CHEAP) ? "true" : "false");

    /* Leave the table clean for whatever runs next. */
    claw_rate_init();
    return SDK_EX_OK;
}
