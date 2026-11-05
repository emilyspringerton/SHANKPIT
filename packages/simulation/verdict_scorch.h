#ifndef SHANKPIT_VERDICT_SCORCH_H
#define SHANKPIT_VERDICT_SCORCH_H

/* verdict_scorch.h -- the mark a served verdict leaves on the floor, and the hazard it is.
 *
 * Founder: "give scorches a hazard". That has to be a RULE, not a drawing: a decal that only exists in
 * apps/lobby's render pool can never hurt anyone, and a hazard that only exists on the client is not a
 * hazard at all the moment two people are in the room. So the scorch pool lives here, in simulation
 * (story_ai.c owns the instance and ticks it), and the renderer reads it through an accessor -- one
 * pool, one set of positions, one lifetime, no chance of the fire on screen and the fire that burns you
 * being two different fires.
 *
 * What a scorch is:
 *   - a mark pushed when the magistrate SERVES a sentence (the same moment the stamp plays), at the
 *     bench's feet, tagged with the scene it was earned in;
 *   - a hazard for VERDICT_SCORCH_LIFE_MS (45 s): anyone standing inside VERDICT_SCORCH_RADIUS takes
 *     VERDICT_SCORCH_DAMAGE every VERDICT_SCORCH_TICK_MS (2 hp / 250 ms = 8 hp/s -- visible pressure
 *     that a player can walk out of, not a one-shot), and only in the scene that earned it;
 *   - a bounded ring of VERDICT_SCORCH_MAX (8), so a long fight chars the gallery instead of growing.
 *
 * Everything here is a pure function of (pool, now_ms, position, scene) so the hazard AND the scene
 * filter are both testable headless (make test-verdict-fx): a client that draws a scorch the sim does
 * not have, or a sim that burns someone through a wall into another map, is exactly the class of bug
 * that used to need two windows open to notice.
 *
 * Damage is applied by the caller (story_ai.c), through the same katana_apply_damage path every other
 * hit in this engine goes through, so a scorch can kill and the death/respawn handling is not
 * re-implemented here. This header only decides WHEN and HOW MUCH.
 */

#include <math.h>
#include <string.h>

#define VERDICT_SCORCH_MAX      8
#define VERDICT_SCORCH_RADIUS  15.0f   /* smaller than the ring that made it (26) */
#define VERDICT_SCORCH_LIFE_MS 45000U  /* how long a served sentence stays burned into the floor */
#define VERDICT_SCORCH_TICK_MS   250U  /* damage cadence while standing in it */
#define VERDICT_SCORCH_DAMAGE      2   /* hp per cadence: 8 hp/s */

typedef struct {
    int active;
    float x, y, z;
    int scene_id;
    unsigned int t0;
    unsigned int next_damage_ms;
} VerdictScorch;

typedef struct {
    VerdictScorch pool[VERDICT_SCORCH_MAX];
    unsigned int next;
} VerdictScorches;

static inline void verdict_scorches_reset(VerdictScorches *scorches) {
    memset(scorches, 0, sizeof(*scorches));
}

/* 1.0 at the moment of the sentence, falling to 0.0 at VERDICT_SCORCH_LIFE_MS. Squared so the mark
   stays dark while the fight is still on and only thins out late -- a scorch that visibly fades in the
   first five seconds would read as a rendering bug rather than as damage. */
static inline float verdict_scorch_alpha(const VerdictScorch *s, unsigned int now_ms) {
    if (!s || !s->active) return 0.0f;
    unsigned int dt = (now_ms > s->t0) ? (now_ms - s->t0) : 0u;
    if (dt >= VERDICT_SCORCH_LIFE_MS) return 0.0f;
    float t = (float)dt / (float)VERDICT_SCORCH_LIFE_MS;
    return (1.0f - t) * (1.0f - t);
}

/* The scene filter, in one place: a mark earned in the district never follows the player into another
   map, and a mark too faint to see is not drawn (or felt) either. */
static inline int verdict_scorch_present(const VerdictScorch *s, int scene_id, unsigned int now_ms) {
    return s && s->active && s->scene_id == scene_id && verdict_scorch_alpha(s, now_ms) > 0.0f;
}

/* Is a body at (x,z) standing in this scorch? */
static inline int verdict_scorch_covers(const VerdictScorch *s, int scene_id, float x, float z,
                                        unsigned int now_ms) {
    if (!verdict_scorch_present(s, scene_id, now_ms)) return 0;
    float dx = x - s->x, dz = z - s->z;
    return dx * dx + dz * dz <= VERDICT_SCORCH_RADIUS * VERDICT_SCORCH_RADIUS;
}

/* Damage cadence: due every VERDICT_SCORCH_TICK_MS from birth. The caller applies VERDICT_SCORCH_DAMAGE
   to every body it finds covered (one pass per scorch per tick, so several players standing in the same
   mark all get hit), then advances the clock with verdict_scorch_advance. */
static inline int verdict_scorch_due(const VerdictScorch *s, unsigned int now_ms) {
    return s && s->active && now_ms >= s->next_damage_ms;
}

static inline void verdict_scorch_advance(VerdictScorch *s, unsigned int now_ms) {
    if (!s) return;
    s->next_damage_ms += VERDICT_SCORCH_TICK_MS;
    /* A long stall must not turn into a burst of catch-up damage: skip whole periods instead of
       paying them back all at once, which is how a hitch becomes a one-frame kill. */
    if (s->next_damage_ms <= now_ms) s->next_damage_ms = now_ms + VERDICT_SCORCH_TICK_MS;
}

/* Ring buffer: the ninth scorch replaces the first, so a long fight chars the floor rather than
   growing a decal list without a ceiling. */
static inline VerdictScorch *verdict_scorch_push(VerdictScorches *scorches, float x, float y, float z,
                                                 int scene_id, unsigned int now_ms) {
    VerdictScorch *s = &scorches->pool[scorches->next % VERDICT_SCORCH_MAX];
    scorches->next = (scorches->next + 1u) % VERDICT_SCORCH_MAX;
    s->active = 1;
    s->x = x; s->y = y; s->z = z;
    s->scene_id = scene_id;
    s->t0 = now_ms;
    s->next_damage_ms = now_ms + VERDICT_SCORCH_TICK_MS;
    return s;
}

static inline void verdict_scorches_age(VerdictScorches *scorches, unsigned int now_ms) {
    for (int i = 0; i < VERDICT_SCORCH_MAX; i++) {
        VerdictScorch *s = &scorches->pool[i];
        if (s->active && verdict_scorch_alpha(s, now_ms) <= 0.0f) s->active = 0;
    }
}

static inline int verdict_scorches_live(const VerdictScorches *scorches) {
    int n = 0;
    for (int i = 0; i < VERDICT_SCORCH_MAX; i++) if (scorches->pool[i].active) n++;
    return n;
}

#endif /* SHANKPIT_VERDICT_SCORCH_H */
