/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/03_parallel_set_run
 * title:   Watch several models at once
 * teaches: start a parallel set, wait out the window fill, then read EACH member's verdict instead of the last one published
 * apis:    ai_engine_start, ai_engine_set_name, ai_engine_set_members, ai_engine_mic_settling, ai_engine_mic_settle_pct, ai_engine_snapshot_model, ai_engine_active, ai_engine_stop, ai_engine_model
 * entry:   example_edge_ai_parallel_set_run
 */
/*
 * A parallel set feeds the same sensor data to several models on every pass and
 * lets each publish its own verdict. You start it exactly like a model -- the
 * set numbers are pseudo-indices that ride in on ai_engine_start() -- and then
 * everything about reading it is different.
 *
 * THE MISTAKE THIS FILE EXISTS TO PREVENT
 *
 *   ai_engine_snapshot()          the most recent verdict from ANY model
 *   ai_engine_snapshot_model(i)   model i's own last verdict
 *
 * In a set, four models write that one shared slot, so snapshot() is
 * last-writer-wins. A cough detected at 94% is overwritten a millisecond later
 * by three quiet models each publishing "unlabelled 0.99", and a UI polling
 * snapshot() shows silence. The detection was never lost; it was never read.
 * Iterate ai_engine_set_members() and call snapshot_model() per member.
 *
 * THE SETTLE WINDOW IS NOT A DELAY YOU CAN SKIP
 *
 * Every member needs a continuous run of samples before it can say anything --
 * a full window, which for the longest audio model is over three seconds. Until
 * then their outputs are computed over a partly empty buffer. The engine
 * withholds verdicts for that period and ai_engine_mic_settling() is true;
 * ai_engine_mic_settle_pct() moves 0..100 so the wait can read as progress
 * rather than as a hang. Show it. Four silent zero bars for four seconds is
 * long enough that people reach for the power switch.
 *
 * WHY 'mic' IS IN THOSE TWO NAMES
 *
 * Sets began as the microphone-only watch and the names outlived that. They now
 * report the settle state of WHATEVER set is active, including sets holding
 * radar and IMU members. A member's own sensor is in its descriptor -- test
 * desc->sensor before doing anything sensor-specific with an entry, because
 * ai_engine_set_members() hands back every member whatever it reads.
 *
 * PORTABILITY: the parallel path is compiled into images that carry microphone
 * models. On a motion-only or radar-only build ai_engine_start(252) returns
 * false, and this example reports that rather than pretending.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sdk_examples.h"
#include "ai_engine.h"

#define POLL_MS       (1000u)
#define SETTLE_MAX_MS (12000u)  /* the longest member window, with headroom */
#define MEMBERS_MAX   (8u)      /* the engine's own ceiling on set size     */

static struct {
    lv_timer_t *timer;
    uint32_t    elapsed_ms;
    uint32_t    set_index;
    bool        reported;
    bool        running;
} s;

static const char *sensor_name(ai_sensor_t id)
{
    switch (id) {
        case AI_SENSOR_IMU:   return "IMU";
        case AI_SENSOR_RADAR: return "radar";
        case AI_SENSOR_MIC:   return "mic";
        default:              return "?";
    }
}

static void session_end(void)
{
    ai_engine_stop();
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    s.running = false;
}

/* Read every member, not the shared slot. This is the whole point of the file. */
static void report_members(void)
{
    uint8_t        idx[MEMBERS_MAX];
    const uint32_t n = ai_engine_set_members(idx, MEMBERS_MAX);

    if (n == 0u) {
        sdk_example_logf("the set resolved to no members. A member whose init()"
                         " failed is pruned, so an empty set means every one of"
                         " them failed");
        return;
    }

    sdk_example_logf("%lu member(s), each read on its own:", (unsigned long)n);

    for (uint32_t i = 0u; i < n; i++) {
        const uint32_t         mi = idx[i];
        const ai_model_desc_t *d  = ai_engine_model(mi);
        const char            *nm = ((d != NULL) && (d->name != NULL))
                                        ? d->name : "?";
        ai_result_t r;

        if (!ai_engine_snapshot_model(mi, &r)) {
            /* Not an error: this member has not completed a window yet. */
            sdk_example_logf("  [%lu] %s (%s): nothing published yet",
                             (unsigned long)mi, nm,
                             (d != NULL) ? sensor_name(d->sensor) : "?");
            continue;
        }

        uint8_t   which = 1u;
        const int pct   = ai_result_top_positive(&r, &which);
        const char *label = "?";
        if ((d != NULL) && (which < d->class_count) &&
            (d->class_labels[which] != NULL)) {
            label = d->class_labels[which];
        }
        sdk_example_logf("  [%lu] %s: %s %d%% (%lu inf)",
                         (unsigned long)mi, nm, label, pct,
                         (unsigned long)r.inferences);
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    s.elapsed_ms += POLL_MS;

    const int active = ai_engine_active();
    if (active != (int)s.set_index) {
        /* active() answers with the set's own pseudo-index while a set runs.
         * Anything else means something took the engine away from us. */
        sdk_example_logf("engine left the set: active=%d, expected %lu",
                         active, (unsigned long)s.set_index);
        session_end();
        return;
    }

    if (ai_engine_mic_settling()) {
        if (s.elapsed_ms >= SETTLE_MAX_MS) {
            sdk_example_logf("still filling windows after %lums -- the feed is"
                             " not reaching the members (see 07_engine_health)",
                             (unsigned long)s.elapsed_ms);
            session_end();
            return;
        }
        sdk_example_logf("filling windows: %lu%%",
                         (unsigned long)ai_engine_mic_settle_pct());
        return;
    }

    if (!s.reported) {
        sdk_example_logf("settled (%lu%%) -- verdicts are trustworthy from here",
                         (unsigned long)ai_engine_mic_settle_pct());
        report_members();
        s.reported = true;
        return;             /* one more tick, so the numbers are seen moving */
    }

    report_members();
    sdk_example_logf("stopping the set");
    session_end();
}

int example_edge_ai_parallel_set_run(lv_obj_t *parent)
{
    (void)parent;   /* the per-member readings go to the log */

    if (s.running) {
        sdk_example_logf("a set session is already running");
        return SDK_EX_BUSY;
    }

    memset(&s, 0, sizeof(s));
    s.set_index = (uint32_t)AI_PARALLEL_ALL;   /* the widest watch: every row */

    /* NULL is how you tell a set index from a model index: the four sets are
     * 252..255 and nothing else answers with a name. */
    const char *name = ai_engine_set_name(s.set_index);
    if (name == NULL) {
        sdk_example_logf("%lu is not a set index", (unsigned long)s.set_index);
        return SDK_EX_REFUSED;
    }
    sdk_example_logf("starting set %lu '%s'", (unsigned long)s.set_index, name);

    if (!ai_engine_start(s.set_index)) {
        sdk_example_logf("start(%lu) refused. The parallel path is compiled"
                         " into images carrying microphone models; this one"
                         " does not", (unsigned long)s.set_index);
        return SDK_EX_UNAVAILABLE;
    }

    s.timer = lv_timer_create(tick_cb, POLL_MS, NULL);
    if (s.timer == NULL) {
        sdk_example_logf("no LVGL timer available -- unwinding");
        session_end();
        return SDK_EX_UNAVAILABLE;
    }
    s.running = true;

    sdk_example_logf("waiting out the window fill before reading anything");
    return SDK_EX_STARTED;
}
