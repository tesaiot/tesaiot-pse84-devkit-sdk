/* sdk-example: core=cm55 variant=both group=edge_ai
 * id:      cm55/edge_ai/07_engine_health
 * title:   Is the model actually running?
 * teaches: sample the pipeline counters twice and read the DELTAS, which is the only way a cumulative counter answers a question
 * apis:    ai_engine_active, ai_engine_feeds, ai_engine_dq_calls, ai_engine_dq_ok, ai_engine_stale_drops, ai_engine_npu_cycles, ai_engine_stack_words, ai_engine_stack_free_words
 * entry:   example_edge_ai_engine_health
 */
/*
 * "The screen shows 0% and nothing is happening" has four different causes and
 * they need four different fixes. These counters separate them -- but only as
 * DELTAS. Every one of them is cumulative for the session, so a single reading
 * tells you what happened since the model started and nothing at all about
 * whether it is running NOW. A big number is not health; a big number that is
 * not growing is a stall.
 *
 * So: sample, wait a known interval, sample again, subtract. That is the whole
 * technique, and it is the reason this file needs a timer at all.
 *
 * READING THE DELTAS, IN THE ORDER THAT NARROWS IT FASTEST
 *
 *   feeds == 0        Nothing reached the model. The pipeline never started.
 *                     For an IMU model that is CM33 not pushing -- either the
 *                     rate request never landed or the sensor is not there. For
 *                     radar or mic it is the on-core source.
 *   dq_calls == 0     The inference task did not complete a pass. It is not
 *                     running: no model selected, or it is stuck inside one.
 *   dq_ok == 0 with dq_calls climbing
 *                     Passes happen, verdicts do not. Either the model's window
 *                     is not full yet (normal, briefly) or the NPU has stalled
 *                     -- this is the signature of a stalled NPU, and it is what
 *                     to key an alarm on.
 *   stale_drops rising
 *                     Verdicts were WITHHELD because the dequeue ran past the
 *                     Ethos-U wait bound. Read it as "how often a verdict was
 *                     withheld", NOT as an NPU health meter: the measurement is
 *                     wall clock, and the inference task sits below the GFX
 *                     task, so a long render frame or an XIP stall on the
 *                     shared flash pushes a perfectly good dequeue past the
 *                     threshold. It also stops counting once a stall wedges the
 *                     driver, because dequeue then fails before reaching the
 *                     check. Unlike the others it is cumulative for the BOOT
 *                     and survives a model switch.
 *   npu_cycles flat
 *                     Coarse only. The middleware adds to it on the timeout
 *                     path too, so a rising count does not prove any particular
 *                     inference completed -- and a flat count does not prove
 *                     the NPU is dead, because the cycle counter is switched
 *                     off by a teardown and only turned back on deliberately.
 *                     Corroborate with dq_ok before blaming silicon.
 *
 * THE STACK PAIR IS NOT A DELTA
 *
 *   stack_words       what the inference task actually got, in words. The
 *                     engine asks for a generous stack and steps DOWN until one
 *                     fits rather than failing outright, so this is a measured
 *                     value, not a constant -- a smaller number than you expect
 *                     means the CM55 heap was squeezed at boot.
 *   stack_free_words  unused words remaining, as an ALL-TIME MINIMUM. It only
 *                     ever falls. A delta of zero means no new low-water mark
 *                     in the interval, which is good news, not a stall. The
 *                     call scans the stack, so read it at about 1 Hz, not in a
 *                     render loop.
 *
 * This file starts nothing and stops nothing. Point it at whatever session is
 * already running -- 01_first_inference, 03_parallel_set_run, the Edge AI page.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sdk_examples.h"
#include "ai_engine.h"

#define SAMPLE_MS  (1000u)   /* the interval the deltas are reported over */

typedef struct {
    uint32_t feeds;
    uint32_t dq_calls;
    uint32_t dq_ok;
    uint32_t stale;
    uint64_t npu;
    uint32_t stack_free;
} health_t;

static struct {
    lv_timer_t *timer;
    health_t    first;
    int         model;
    bool        running;
} s;

static void sample(health_t *h)
{
    h->feeds      = ai_engine_feeds();
    h->dq_calls   = ai_engine_dq_calls();
    h->dq_ok      = ai_engine_dq_ok();
    h->stale      = ai_engine_stale_drops();
    h->npu        = ai_engine_npu_cycles();
    h->stack_free = ai_engine_stack_free_words();
}

/* Saturating, because a counter is cleared on a model switch and a switch
 * between the two samples would otherwise print a delta near 2^32. */
static uint32_t delta32(uint32_t now, uint32_t was)
{
    return (now >= was) ? (now - was) : 0u;
}

static void second_sample_cb(lv_timer_t *t)
{
    (void)t;

    const int now_model = ai_engine_active();
    health_t  b;
    sample(&b);

    if (now_model != s.model) {
        sdk_example_logf("the active model changed under the measurement"
                         " (%d -> %d). The counters were cleared; deltas would"
                         " be meaningless", s.model, now_model);
        goto done;
    }

    {
        const uint32_t d_feeds = delta32(b.feeds,    s.first.feeds);
        const uint32_t d_calls = delta32(b.dq_calls, s.first.dq_calls);
        const uint32_t d_ok    = delta32(b.dq_ok,    s.first.dq_ok);
        const uint32_t d_stale = delta32(b.stale,    s.first.stale);
        const uint64_t d_npu   = (b.npu >= s.first.npu)
                                     ? (b.npu - s.first.npu) : 0u;

        sdk_example_logf("over %lums: feeds +%lu, dq_calls +%lu, dq_ok +%lu",
                         (unsigned long)SAMPLE_MS, (unsigned long)d_feeds,
                         (unsigned long)d_calls, (unsigned long)d_ok);
        sdk_example_logf("stale_drops +%lu, npu_cycles +%llu",
                         (unsigned long)d_stale, (unsigned long long)d_npu);

        /* One verdict, narrowest cause first. */
        if (d_feeds == 0u) {
            sdk_example_logf("VERDICT: nothing is reaching the model. For an"
                             " IMU model CM33 is not pushing -- the rate"
                             " request never landed, or the sensor is absent");
        } else if (d_calls == 0u) {
            sdk_example_logf("VERDICT: samples arrive but no inference pass"
                             " completes. The inference task is not running or"
                             " is stuck inside a model call");
        } else if (d_ok == 0u) {
            sdk_example_logf("VERDICT: passes happen, verdicts do not. Either"
                             " the window is still filling, or the NPU has"
                             " stalled -- this is that signature");
        } else {
            sdk_example_logf("VERDICT: healthy. %lu verdict(s) of %lu pass(es)"
                             " in %lums", (unsigned long)d_ok,
                             (unsigned long)d_calls, (unsigned long)SAMPLE_MS);
        }

        if (d_stale > 0u) {
            sdk_example_logf("note: %lu verdict(s) withheld. Wall-clock, so a"
                             " long render frame or an XIP stall counts here"
                             " too -- not proof the NPU misbehaved",
                             (unsigned long)d_stale);
        }
        if ((d_npu == 0u) && (d_ok > 0u)) {
            sdk_example_logf("note: verdicts completed with the NPU cycle"
                             " counter flat. The counter is disabled by a"
                             " teardown; that is the usual reason, not a dead"
                             " NPU");
        }

        /* An all-time minimum: it falls or stays put, never rises. */
        if (b.stack_free < s.first.stack_free) {
            sdk_example_logf("stack low-water fell %lu -> %lu words: a deeper"
                             " path ran during the interval",
                             (unsigned long)s.first.stack_free,
                             (unsigned long)b.stack_free);
        }
    }

done:
    if (s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    s.running = false;
}

int example_edge_ai_engine_health(lv_obj_t *parent)
{
    (void)parent;   /* counters and a verdict; both go to the log */

    if (s.running) {
        sdk_example_logf("a measurement is already in flight");
        return SDK_EX_BUSY;
    }
    memset(&s, 0, sizeof(s));

    /* Read before anything else: these two are meaningful even while idle, and
     * a task that was never created reports 0 words, which is itself the
     * answer to "why does nothing infer". */
    const uint32_t stack_total = ai_engine_stack_words();
    const uint32_t stack_free  = ai_engine_stack_free_words();

    if (stack_total == 0u) {
        sdk_example_logf("the inference task was never created -- CM55 heap"
                         " was exhausted at boot. Nothing can infer");
        return SDK_EX_UNAVAILABLE;
    }
    sdk_example_logf("inference task stack: %lu words granted, %lu still free"
                     " at its worst (%lu%% used)",
                     (unsigned long)stack_total, (unsigned long)stack_free,
                     (unsigned long)(((stack_total - stack_free) * 100u)
                                     / stack_total));

    s.model = ai_engine_active();
    if (s.model < 0) {
        sdk_example_logf("no model is active, so every delta would be zero and"
                         " would say nothing. Start one (01_first_inference)"
                         " and run this again");
        return SDK_EX_NO_DATA;
    }

    {
        const ai_model_desc_t *d = ai_engine_model((uint32_t)s.model);
        sdk_example_logf("measuring model %d '%s' over %lums", s.model,
                         ((d != NULL) && (d->name != NULL)) ? d->name : "set",
                         (unsigned long)SAMPLE_MS);
    }

    sample(&s.first);

    /* The wait is a timer, never a delay: run() is on the GFX task, and a
     * second of busy-wait here freezes the display and the overlay that would
     * have explained the freeze. */
    s.timer = lv_timer_create(second_sample_cb, SAMPLE_MS, NULL);
    if (s.timer == NULL) {
        sdk_example_logf("no LVGL timer available -- cannot take the second"
                         " sample, and one sample answers nothing");
        return SDK_EX_UNAVAILABLE;
    }
    s.running = true;

    return SDK_EX_STARTED;
}
