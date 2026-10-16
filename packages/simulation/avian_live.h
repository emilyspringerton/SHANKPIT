#ifndef AVIAN_LIVE_H
#define AVIAN_LIVE_H

/* avian_live.h -- pure glue wiring core/avian_values.h's coalition into the OTHER AI actors
 * already ticking on the live server: core/npc_archetype.h (Citizens/The Men), core/
 * zombie_values.h (zombies), core/witness_rules.h (the human witness-state machine). Founder
 * real-time follow-up to NORTHSTAR.md §31 ("the birds"): "integrate them deeply into the other AI
 * interactions in terms of observing the observer."
 *
 * "Observing the observer" is a specific, real design choice, not a slogan: this coalition does
 * NOT get its own copy of witness_rules.c's noticed()/conspicuousness() to watch the player
 * directly (that would just be a second, redundant witness system with bird flavor). Instead the
 * coalition watches the OTHER watchers -- a citizen's or The Men's own npc_brain_effective_
 * vigilance spiking, a human's own witness_state escalating past mere DENIAL, a zombie crossing
 * into a witnessable HUNTING/FRENZIED state (core/witness_live.h's own bigo_zombie_is_
 * witnessable_event). Each of those IS already one AI system noticing something -- the coalition's
 * real job, matching TYLER/README.md's Eastwind Owls ("their inability to destroy records is a
 * feature, not a bug"), is to archive the fact that a witnessing already happened and broadcast
 * it onward (avian_values.h's own avian_beacon_strength), one meta-layer above the event itself.
 * A bird never needs its own line of sight to the player to become alarmed -- it only needs to
 * see a citizen flinch, a witness go SILENCING, or a zombie go FRENZIED. This is genuinely
 * different information than what witness_rules.c/zombie_values.c already produce for their own
 * consumers -- it is a second-order signal (a signal ABOUT another AI's own signal), not a
 * restatement of the first-order one, which is the actual mechanical meaning of "observing the
 * observer" here rather than just a re-skinned witness check.
 *
 * Deliberately header-only and pure, same "extract testable logic, zero live-entity dependency"
 * discipline as witness_live.h/bigo_pheromone.h -- every function here takes plain scalars a
 * future ServerAvian tick loop would read off the SAME ServerNpc/ServerZombie arrays apps/server/
 * src/main.c already populates (§8d), not a new entity type of its own. */

#include "avian_values.h"

#include "witness_sim.h"   /* WS_* witness-state enum (BIG_O had it in witness_rules.h) */
#include "zombie_values.h"
#include "witness_live.h"

/* A human NPC (Citizen or The Men) whose OWN effective vigilance has climbed to this point is,
 * itself, a real "something worth archiving" signal -- a tired contractor going sharp, or a
 * veteran guard going sharper still, both cross this the same way, matching witness_rules.h's own
 * flat 0..100 vigilance scale rather than re-deriving an archetype-specific threshold. */
#define AVIAN_OBSERVED_VIGILANCE_THRESHOLD 60

/* A human witness_state past mere catatonic DENIAL is a real escalation worth the coalition's
 * attention -- COMPROMISED and beyond mean the record is no longer just "one denying witness", it
 * is an active thread the corporate night-society is now working (silencing, panicking, or
 * engaging). Compared directly against the raw WS_* ordinal (UNAWARE < DENIAL < COMPROMISED <
 * SILENCING < PANIC < ENGAGE, exactly the enum's own declared order in witness_rules.h) --
 * deliberately NOT witness_rules.c's own escalation_rank(), which is a different, non-monotonic
 * grouping used for decorum-penalty banding (rank 3 = {SILENCING, ENGAGE}, rank 1 = {DENIAL,
 * PANIC}, rank 0 = {UNAWARE, COMPROMISED} -- COMPROMISED and UNAWARE share a rank there, which
 * would make this threshold meaningless if reused for "has this become a real thread yet"). */
#define AVIAN_WITNESS_ESCALATION_MIN WS_COMPROMISED

/* avian_live_observe_npc_vigilance -- the coalition watching a human observer's OWN attention
 * spike, not the thing that spiked it. Real, discrete alert only when effective_vigilance crosses
 * the threshold above -- a merely-average tired citizen never alerts a bird, matching this
 * header's own "second-order signal" framing (ordinary background vigilance carries no
 * information worth archiving). Returns 1 if this call actually alerted the bird (a real, testable
 * "did the meta-witness fire" signal for a caller's own logging/scenario asserts), 0 otherwise. */
static inline int avian_live_observe_npc_vigilance(AvianState *a, int npc_effective_vigilance, uint32_t now_ms) {
    if (npc_effective_vigilance < AVIAN_OBSERVED_VIGILANCE_THRESHOLD) return 0;
    avian_get_alerted(a, now_ms);
    return 1;
}

/* avian_live_observe_witness_state -- the coalition witnessing that a human has ITSELF become an
 * active witness (raw witness_state >= AVIAN_WITNESS_ESCALATION_MIN) -- the direct, literal
 * "Eastwind Owl archives the archivist" mechanic this module's own top doc comment names. A human
 * still in UNAWARE/DENIAL is not yet a signal. Compares the raw WS_* ordinal directly (see this
 * header's own #define comment on why escalation_rank() is the wrong tool here). */
static inline int avian_live_observe_witness_state(AvianState *a, int witness_state, uint32_t now_ms) {
    if (witness_state < AVIAN_WITNESS_ESCALATION_MIN) return 0;
    avian_get_alerted(a, now_ms);
    return 1;
}

/* avian_live_observe_zombie_event -- the coalition watching the SAME loud-zombie-event trigger
 * witness_live.h's own witness_live_zombie_is_witnessable_event already gates human witnessing on --
 * reused directly rather than re-implementing the HUNTING/FRENZIED check, so the two systems can
 * never silently drift apart on what counts as "loud". This is the one call in this header that
 * is arguably first-order (a zombie mood, not another AI's reaction to one) -- kept anyway because
 * the coalition's own in-fiction framing (NORTHSTAR.md §31) is that it watches for a record ABOUT
 * TO BE witnessed, not only after a human already has, which matters for beacon timing (see this
 * header's own top doc comment on broadcasting the fact onward before/alongside the human side
 * catching up). */
static inline int avian_live_observe_zombie_event(AvianState *a, ZombieMood mood, uint32_t now_ms) {
    if (!witness_live_zombie_is_witnessable_event(mood)) return 0;
    avian_get_alerted(a, now_ms);
    return 1;
}

/* avian_live_meta_witness_rank -- a single, real, comparable 0..3 severity rank distilling all
 * three observation channels above into one number a future dispatch/priority system can sort on,
 * matching escalation_rank()'s own "one comparable number over several real states" shape:
 *   0 = nothing worth archiving this tick
 *   1 = one channel fired (an NPC spiked, OR a witness escalated, OR a zombie went loud)
 *   2 = two channels fired at once -- a real corroborating signal, not just one twitchy NPC
 *   3 = all three fired at once -- the coalition's own "clean audit trail" case: an NPC noticed,
 *       a human is actively a witness, AND the zombie event itself is loud. This is the concrete
 *       signal that should, at a future live-wiring pass, drive avian_beacon_strength's own
 *       urgency rather than plain SIGNALING/MOBBING mood alone -- named here, not yet consumed
 *       anywhere (see this header's own top doc comment on staying header-only glue). */
static inline int avian_live_meta_witness_rank(int npc_effective_vigilance, int witness_state, ZombieMood zombie_mood) {
    int rank = 0;
    if (npc_effective_vigilance >= AVIAN_OBSERVED_VIGILANCE_THRESHOLD) rank++;
    if (witness_state >= AVIAN_WITNESS_ESCALATION_MIN) rank++;
    if (witness_live_zombie_is_witnessable_event(zombie_mood)) rank++;
    return rank;
}

#endif /* AVIAN_LIVE_H */
