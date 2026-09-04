/* sdk-example: core=cm33 variant=mtb-mpy group=connectivity
 * id:      cm33/connectivity/07_trust_policy
 * title:   Let the transport decide which tools may run
 * teaches: set the trust level when a session opens, gate every tool on its own
 *          risk class, and put the level back when the session closes
 * apis:    claw_trust_set, claw_trust_get, claw_trust_allows
 * entry:   example_mpy_secure_trust_policy
 */

/*******************************************************************************
 * A tool that erases flash should be reachable from a USB cable somebody is
 * holding, and not from an HTTPS request that arrived from the internet. That
 * is the whole idea: the SAME tool table, gated by where the request came from.
 *
 * THE LEVELS
 *   CLAW_TRUST_USB   (2)  a cable. Every risk class runs.
 *   CLAW_TRUST_HTTPS (1)  a remote caller. risk 0 and 1 only.
 *   CLAW_TRUST_NONE  (0)  nothing runs. The safe state.
 *
 * THE RISK CLASSES, as claw_trust_allows() reads them
 *   0  low     read a sensor, read a status
 *   1  medium  change a setting, drive an LED
 *   2  high    write flash, change a credential, anything irreversible
 *
 * WHO CALLS set(): the transport, once, when a session opens — the TACP handler
 * sets USB, the HTTPS handler sets HTTPS. Never a tool, and never the tool
 * dispatcher: a level chosen by the code being gated is not a gate.
 *
 * IT IS ONE GLOBAL. There is no per-session level and no stack of levels. If
 * two transports can be live at once, the last set() wins for both — so raise
 * it for the duration of a request and lower it again, exactly as this example
 * does, or you leave a USB-grade window open to the network.
 *
 * ANYTHING NOT one of the three enumerators is treated as NONE, so a
 * zero-initialised or corrupted level denies rather than permits.
 ******************************************************************************/

#include <stdio.h>

#include "claw_safety.h"

#include "../sdk_examples_cm33.h"

static const char *trust_str(claw_trust_level_t t)
{
    switch (t) {
        case CLAW_TRUST_USB:   return "USB (full)";
        case CLAW_TRUST_HTTPS: return "HTTPS (restricted)";
        case CLAW_TRUST_NONE:  return "NONE (blocked)";
        default:               return "unknown -> treated as NONE";
    }
}

/* What a dispatcher actually does with the answer. */
static void gate(const char *tool, uint8_t risk)
{
    printf("      risk=%u %-18s %s\r\n", (unsigned)risk, tool,
           claw_trust_allows(risk) ? "ALLOWED" : "refused");
}

static void survey(claw_trust_level_t level)
{
    claw_trust_set(level);
    printf("    level = %s\r\n", trust_str(claw_trust_get()));
    gate("read_sensor",   0u);
    gate("set_led",       1u);
    gate("write_creds",   2u);
}

int example_mpy_secure_trust_policy(void);

int example_mpy_secure_trust_policy(void)
{
    printf("\r\n--- mpy_secure/07_trust_policy ---\r\n");

    /* Read the level BEFORE changing it, and restore it at the end. This is a
     * process-wide setting; an example that leaves it somewhere else has
     * reconfigured the running agent. */
    const claw_trust_level_t saved = claw_trust_get();
    printf("  on entry: %s\r\n", trust_str(saved));

    survey(CLAW_TRUST_USB);
    survey(CLAW_TRUST_HTTPS);
    survey(CLAW_TRUST_NONE);

    /* Prove the two facts the table above is claiming, rather than trusting the
     * printout to have been read. */
    claw_trust_set(CLAW_TRUST_HTTPS);
    const bool https_blocks_high = !claw_trust_allows(2u);
    const bool https_allows_med  =  claw_trust_allows(1u);
    claw_trust_set(CLAW_TRUST_NONE);
    const bool none_blocks_low   = !claw_trust_allows(0u);

    claw_trust_set(saved);
    printf("  restored: %s\r\n", trust_str(claw_trust_get()));

    if (!https_blocks_high || !https_allows_med || !none_blocks_low) {
        printf("  policy did not behave as documented "
               "(https_blocks_high=%d https_allows_med=%d none_blocks_low=%d)\r\n",
               (int)https_blocks_high, (int)https_allows_med, (int)none_blocks_low);
        return SDK_EX_REFUSED;
    }
    return SDK_EX_OK;
}
