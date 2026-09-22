#ifndef WITNESS_LIVE_H
#define WITNESS_LIVE_H

/* witness_live.h -- BIG_O engine merge phase 7a (EMILY/BACKLOG.md SECTION 536, "MODE_STORY content
 * cutover"). Ported from BIG_O's core/witness_live.h, bigo_* -> witness_live_* (a genuinely
 * BIG_O-branded prefix, unlike the WS_/ZONE_/COS_ enums phase 2 deliberately left alone). Logic
 * unchanged.
 *
 * Pure glue wiring a zombie's live mood (zombie_values.h, phase 3) into witness_rules.c's own pure
 * decision function (npc_next_state, phase 2) -- the actual real answer to the "no live NPC entity
 * array exists yet" blocker phases 2/3/5 each named. Confirmed while scoping this phase: even
 * BIG_O's own original version deliberately stayed a pure, zero-ServerNpc-dependency header (see
 * this header's own doc comment there) -- so porting it here is exactly the same real amount of
 * "live" as BIG_O itself ever built. The actual ServerNpc-shaped population loop that calls these
 * functions per-tick against real spawned NPCs is still real, not-yet-built follow-up work -- see
 * docs2/specs/BIGO_ENGINE_MERGE_NORTHSTAR.md phase 7 for the honest breakdown.
 *
 * Real, deliberate scope cut (inherited from BIG_O unchanged): this wires the LOUD-event half of
 * witness_rules.c only -- a HUNTING/FRENZIED zombie is a "loud event," unconditionally witnessed
 * by every human NPC within range, matching BIG_O/docs/B1_WITNESS_RULES.md Section 7's "a loud
 * event... is witnessed by every non-accomplice NPC in the same zone" rule (scaled from zone-wide
 * to radius-based, since SHANKPIT has no per-NPC zone concept yet either). The QUIET-observation
 * path (costume/gear noticing, witness_sim.c's own witness_sim_observe) is a real, separate, still
 * open gap this header does not wire. */

#include "zombie_values.h"

/* witness_rules.c (PARENA-generated, do-not-edit-by-hand) has no shared header -- same convention
 * day_night_clock.c/witness_sim.c/ai_brain_rules callers all already follow, redeclared directly. */
int npc_next_state(int prev, int count, int arrogance, int compromised, int zombie_event, int resolved);
int is_legal_transition(int from, int to);

#define WITNESS_LIVE_DETECTION_RADIUS 25.0f  /* how far a human NPC can witness a loud zombie event */
#define WITNESS_LIVE_DISPATCH_ARRIVAL_RADIUS 2.5f /* how close The Men must get to resolve a hunt */

/* witness_live_zombie_is_witnessable_event -- true if this zombie's CURRENT mood counts as a real,
 * noticeable "loud event". DORMANT/AGITATED zombies are just shambling -- not yet a witnessed
 * event, matching the digest's own framing that the zombies aren't the horror, being SEEN acting
 * monstrously is. */
static inline int witness_live_zombie_is_witnessable_event(ZombieMood mood) {
    return mood == ZOMBIE_MOOD_HUNTING || mood == ZOMBIE_MOOD_FRENZIED;
}

/* witness_live_in_range -- pure flat (x,z)-plane distance check, matching SHANKPIT's own real
 * movement model (y is cosmetic for this check, same convention pheromone.h already uses). */
static inline int witness_live_in_range(float ax, float az, float bx, float bz, float radius) {
    float dx = ax - bx, dz = az - bz;
    return (dx * dx + dz * dz) <= radius * radius;
}

/* witness_live_next_state_for_event -- one human NPC's own real witness-state transition to a loud
 * zombie event, a thin, explicit wrapper around witness_rules.c's own real npc_next_state
 * (zombie_event=1 always, compromised=0 always -- no forced-compromise mechanic live here, that
 * stays witness_sim.c's own scenario-only feature). `count` is the caller's own real count of every
 * human currently in range of THIS SAME event -- computed once per event, shared across every
 * human witnessing it. `resolved` is 0 for a fresh witnessing tick, 1 when The Men's own dispatch
 * loop resolves the hunt (memory-wipe -> DENIAL). */
static inline int witness_live_next_state_for_event(int prev_state, int count, int arrogance, int resolved) {
    return npc_next_state(prev_state, count, arrogance, 0, 1, resolved);
}

#endif /* WITNESS_LIVE_H */
