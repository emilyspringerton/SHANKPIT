/* wave_spawner.h -- the wave timer. Pure, headless, and deliberately stupid about bodies.
 *
 * story_ai.c owns the actual spawning; this decides WHEN a wave is due and HOW MANY it is. Splitting it
 * that way is what makes the one rule that matters testable without a server: a world that is at its live
 * cap must leave the wave DUE, not skip it. A host that advanced the wave number anyway would silently eat
 * every wave it could not place -- the mode would end early and nothing would say so.
 *
 * Ownership: the runner holds three numbers (which wave, when it is due, how many bodies it has emitted).
 * Time is passed IN as milliseconds, never read here, so a test drives a whole session in a loop and a
 * server with a 30 s hitch cannot mint waves.
 */
#ifndef SHANKPIT_WAVE_SPAWNER_H
#define SHANKPIT_WAVE_SPAWNER_H

#include "wave_rules.h"

typedef struct {
    int enabled;   /* off until a host arms it -- no existing level starts spawning waves by accident */
    int wave;      /* waves STARTED so far (0 = the first one is still pending) */
    int due_ms;    /* when the next wave may arrive */
    int spawned;   /* lifetime bodies emitted, for a HUD or a test */
} WaveRunner;

static inline void wave_runner_reset(WaveRunner *r, int now_ms) {
    if (!r) return;
    int on = r->enabled;
    r->enabled = on;
    r->wave = 0;
    r->spawned = 0;
    r->due_ms = now_ms + g_wr_rules.first_delay_ms();
}

/* wave_runner_tick -- how many bodies to spawn right now, 0 for none.
 *
 * Returns the size of a wave that has just come due and fits under the live cap, and advances the schedule
 * by the NEXT wave's interval. Clock going backwards is a no-op rather than a burst. */
static inline int wave_runner_tick(WaveRunner *r, int now_ms, int live) {
    if (!r || !r->enabled) return 0;
    if (g_wr_rules.should_spawn(r->wave, live, now_ms, r->due_ms) == 0) return 0;
    int size = g_wr_rules.size_for(r->wave);
    r->wave++;
    r->spawned += size;
    r->due_ms = now_ms + g_wr_rules.interval_for(r->wave);
    return size;
}

/* waves_remaining -- how many more waves this session has, for a HUD. */
static inline int wave_runner_remaining(const WaveRunner *r) {
    if (!r) return 0;
    int left = g_wr_rules.count() - r->wave;
    return left > 0 ? left : 0;
}

#endif /* SHANKPIT_WAVE_SPAWNER_H */
