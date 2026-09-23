#ifndef GIANT_BUG_VALUES_H
#define GIANT_BUG_VALUES_H

/* giant_bug_values.h -- Giant Zombie Bug's real value system (BIG_O engine merge SECTION 536
 * follow-up, founder real-time, 2026-09-22: "add giant zombie bugs (feral AI units) they need a
 * totally unique value system vector based deliberately non human 64 layer hand written llm" ->
 * "use parena"). See PARENA/stdlib/shankpit/giant_bug_brain.prn's own doc comment for the full,
 * honest "64 layer hand written llm" scope account -- this header is the C-side state/tick half,
 * matching zombie_values.h/humanness.c's own established "stateful stuff stays host C, pure
 * decision formulas move to PARENA" split.
 *
 * "Neurobiological interface" / "cloning based" (founder real-time follow-up): real fictional
 * framing, not a new mechanical system -- these units are lab-grown/cloned (tying into
 * lab_sim.c's own real cloning-facility logic, phase 4) and their brain is a bio-engineered
 * pass-through of the giant_bug_brain.prn network, not a literal separate neural-interface
 * subsystem. Actually spawning one FROM the lab stays the same named, honest, unstarted gap this
 * whole engine merge keeps naming -- "the lab" has zero UI/interaction model.
 *
 * "Deliberately non human": every field here is alien on purpose, sharing nothing with
 * humanness.h's mood enum or zombie_values.h's own hunger/aggression/decay -- hive-signal and
 * ground-vibration in particular have no equivalent in either of those. */

#include <stdint.h>
#include "zombie_values.h" /* ZombieState -- giant_bug_eat_zombie's own real prey type */

typedef struct {
    /* Alien sense inputs, all 0.0-1.0. */
    float hunger;
    float swarm_density;
    float heat_scent;
    float molt_pressure;    /* ONE-WAY, like zombie_values.h's own decay -- never decreases */
    float ground_vibration;
    float pain;
    float hive_signal;
    float light_aversion;

    /* "if they eat a strong zombie they get stronger if they eat a fast zombie they get faster"
     * -- founder real-time follow-up. Real, permanent, one-way stat growth from
     * giant_bug_eat_zombie, starting at a real baseline of 1.0. */
    float strength;
    float speed;

    uint32_t mood_change_at_ms;
} GiantBugState;

/* Fresh spawn: baseline hunger, everything else 0.0, strength/speed at their real 1.0 baseline. */
void giant_bug_state_init(GiantBugState *b, uint32_t now_ms);

/* Real, per-tick drift. has_target lowers hunger (it's eating/hunting); nearby_bug_count (other
 * live giant bugs within sensing range, the caller's own real spatial concern) drives
 * swarm_density. molt_pressure always climbs, same one-way shape zombie decay already uses. */
void giant_bug_tick(GiantBugState *b, uint32_t now_ms, int has_target, int nearby_bug_count);

/* Real consumption: absorbs a real fraction of the eaten zombie's own strength/speed. zombie_
 * values.h has no explicit "strength"/"speed" field (hunger/aggression/decay instead) --
 * aggression is the real, closest published proxy for "strong" (it drives lunge power/frenzy);
 * the inverse of zombie_reaction_delay_ms at a real 1000ms baseline is the real, closest proxy
 * for "fast" (a short reaction window IS the fast zombie). Both derived from real, already-live
 * ZombieState fields, not invented for this. Resets pain to 0 (a good meal). */
void giant_bug_eat_zombie(GiantBugState *b, const ZombieState *prey, uint32_t now_ms);

/* Real decision outputs, PARENA-computed (giant_bug_brain.c) -- each 0..100. */
int giant_bug_attack_drive(const GiantBugState *b);
int giant_bug_flee_drive(const GiantBugState *b);
int giant_bug_swarm_drive(const GiantBugState *b);

#endif
