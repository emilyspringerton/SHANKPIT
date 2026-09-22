#ifndef NPC_ARCHETYPE_H
#define NPC_ARCHETYPE_H

/* npc_archetype.h -- BIG_O engine merge phase 3 (EMILY/BACKLOG.md SECTION 536). Ported in from
 * BIG_O's core/npc_archetype.h, unchanged in shape: a thin real integration layer over SHANKPIT's
 * OWN already-native humanness.c (BIG_O's own copy was vendored FROM this repo in the first
 * place -- no drift to reconcile, diffed clean except one build-toolchain-only M_PI literal that
 * doesn't apply to this repo's own Makefile build).
 *
 * Two archetypes share this one struct because they're both humans with the same MISHRI-shaped
 * mood vocabulary, differing only in personality bias. Zombies do NOT get a variant of this
 * struct -- see zombie_values.h's own doc comment for why they need a genuinely different
 * vocabulary, not just different tuning numbers.
 *
 * Citizens (ambient witness/gossip NPCs, packages/simulation/witness_sim.h's own real consumer
 * once a citizen spawn layer exists -- SECTION 536 phase 7) get a LOW base vigilance and a
 * curious/social/boredom-leaning mood bias. The Men (the cleanup crew that keeps the illusion
 * physically true) get a HIGH base vigilance and a focused/low-jitter bias.
 *
 * npc_brain_effective_vigilance's own formula moved to a real PARENA rules module
 * (PARENA/stdlib/shankpit/ai_brain_rules.prn, generated into ai_brain_rules.c) per "use parena
 * duh" -- it's a pure scalar decision formula, the same shape witness_rules.prn already holds.
 * Everything else here (mood ticking, RNG-jittered timers) stays host C, matching
 * humanness_tick_mood's own already-established precedent in this exact codebase. */

#include <stdint.h>
#include "humanness.h"

typedef enum {
    NPC_ARCHETYPE_CITIZEN = 0,
    NPC_ARCHETYPE_THE_MEN = 1
} NpcArchetype;

typedef struct {
    NpcArchetype archetype;
    HumannessState humanness;
    int base_vigilance; /* 0..100, witness_sim.h's own real vigilance scale -- the archetype's
                            baseline BEFORE npc_brain_effective_vigilance's own mood/energy
                            modulation (below). */
} NpcBrain;

/* Real, archetype-differentiated defaults. Citizen: base_vigilance 35, mood bias toward CURIOUS/
 * SOCIAL/BORED. The Men: base_vigilance 85, mood bias FOCUSED, near-baseline energy/fatigue. */
void npc_brain_init(NpcBrain *b, NpcArchetype archetype, uint32_t now_ms);

/* Thin wrapper around humanness_tick_mood; kept as its own call so a future per-archetype tick
 * hook has a real, obvious place to grow into. */
void npc_brain_tick(NpcBrain *b, uint32_t now_ms);

/* Real wrapper around humanness_get_startled -- call when this NPC's own witness_sim state
 * transitions into something alarming (SILENCING/PANIC/ENGAGE) or the NPC takes damage. */
void npc_brain_get_startled(NpcBrain *b, uint32_t now_ms);

/* THE real attention-mechanism integration point: base_vigilance modulated by this tick's real
 * mood/energy/fatigue, clamped to witness_sim.h's own real 0..100 scale (PARENA-computed, see
 * this header's own top doc comment). A tired, unfocused citizen misses things a startled one
 * wouldn't. */
int npc_brain_effective_vigilance(const NpcBrain *b);

/* Pure passthrough to humanness_reaction_delay_ms -- deliberately NOT archetype-scaled here on
 * top of that (archetype/role-tuned timing belongs in the CALLER's own base_ms, matching how
 * story_ai.h's per-role next_attack_ms already works). */
uint32_t npc_brain_reaction_delay_ms(const NpcBrain *b, uint32_t base_ms);

#endif
