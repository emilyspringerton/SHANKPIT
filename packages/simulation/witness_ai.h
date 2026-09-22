#ifndef WITNESS_AI_H
#define WITNESS_AI_H

/* witness_ai.h -- BIG_O engine merge phase 7b (EMILY/BACKLOG.md SECTION 536, "MODE_STORY content
 * cutover"). The real live NPC population/tick loop phase 7a's own header named as its still-open
 * follow-up: this is the actual consumer of witness_live.h, and it composes FOUR already-shipped,
 * previously-standalone pieces into one genuinely working pipeline (same "compose, don't just
 * stand up another standalone primitive" discipline phase 6's world_alert_bridge already used),
 * without modifying any of their own files:
 *
 *   witness_sim.h (phase 2, ambient citizens' zone/decorum/witness-state)
 *     + npc_archetype.h (phase 3, per-citizen mood-modulated vigilance)
 *     + zombie_values.h (phase 3, per-zombie hunger/aggression/mood arc)
 *     + witness_live.h (phase 7a, the pure "loud zombie event -> witness-state transition" glue)
 *       --tick loop below composes all four-->  live citizen/zombie state, tested end to end.
 *
 * Checked before writing this: SHANKPIT already has the right shape for a "live NPC entity" --
 * it's not a new concept, it's story_ai.c's own real "bot occupies an actual PlayerState slot"
 * convention (story_ai_spawn_enemy, MAX_CLIENTS=70 slots), which every connected client already
 * renders/networks for free. This module reuses that exact convention for citizens and zombies,
 * completely independent of story_ai.c's own g_story_ai array (a citizen/zombie spawned here never
 * touches story_ai.c's own bookkeeping, and vice versa -- they just both allocate from the same
 * shared s->players[] slot pool).
 *
 * Real, deliberate scope cuts, named plainly, not papered over:
 *  - No movement/patrol/wander AI. Citizens and zombies stand at their spawn position. Real,
 *    separate follow-up (could reuse story_ai.c's own nav-graph/pheromone.h, phase 5's own
 *    steering primitive -- pheromone.h's real first live consumer is a further follow-up, not
 *    this pass).
 *  - zombie_tick's own `has_target` is always passed 0 here -- no player-perception/line-of-sight
 *    system exists yet to organically drive a zombie into HUNTING/FRENZIED. A zombie only reaches
 *    those states via witness_ai_force_zombie_mood (test/debug + a future trigger hook), same
 *    honest boundary zombie_values.h's own header already draws around zombie_effective_alertness.
 *  - No resolution/memory-wipe loop. Once a citizen's witness state escalates (SILENCING/PANIC/
 *    ENGAGE), witness_live_next_state_for_event's own persistence rule means it stays there until
 *    something calls the resolved=1 path -- that's The Men's own dispatch loop, real, separate,
 *    not-yet-built follow-up work (see phase 7d in docs2/specs/BIGO_ENGINE_MERGE_NORTHSTAR.md). */

#include "../common/protocol.h"
#include "witness_sim.h" /* WITNESS_SIM_MAX_NPCS */

#define WITNESS_AI_MAX_CITIZENS WITNESS_SIM_MAX_NPCS /* bounded by WitnessSim's own npc array */
#define WITNESS_AI_MAX_ZOMBIES 16

/* Resets all population state (citizens, zombies, the owned WitnessSim instance) and deactivates
 * any player slot this module itself previously spawned into -- safe to call on any story-mode
 * level load, same "only touch what I spawned" discipline story_ai_despawn_all_characters (S480)
 * already established. seed feeds the owned WitnessSim's RNG. */
void witness_ai_reset(unsigned int seed, unsigned int now_ms);

/* Spawns one ambient citizen into a free player slot (story_ai_spawn_enemy's own convention) and
 * registers it with the owned WitnessSim. base_vigilance/arrogance are WitnessNpc's own real 0-100
 * scale (witness_sim_add_npc's own params). Returns the player slot id, or -1 if either the player
 * slot table or the WitnessSim npc table (WITNESS_SIM_MAX_NPCS) is full. */
int witness_ai_spawn_citizen(ServerState *s, int zone, int base_vigilance, int arrogance,
                              float x, float y, float z, unsigned int now_ms);

/* Spawns one zombie into a free player slot. Fresh ZombieState (DORMANT, all-zero), same
 * zombie_state_init contract as zombie_values.h itself. Returns the player slot id, or -1. */
int witness_ai_spawn_zombie(ServerState *s, float x, float y, float z, unsigned int now_ms);

/* Real per-server-tick update: ticks every active citizen's NpcBrain (writing its real,
 * mood-modulated effective vigilance back into the owned WitnessSim's own npc entry) and every
 * active zombie's ZombieState, then runs the actual witness_live.h integration pass -- for every
 * zombie whose CURRENT mood counts as a loud event, counts every citizen within
 * WITNESS_LIVE_DETECTION_RADIUS (flat x,z) of it and updates each in-range citizen's own witness
 * state via witness_live_next_state_for_event. Safe to call every real tick, self-throttles the
 * owned WitnessSim's own ambient decorum decay to roughly once per second. */
void witness_ai_tick(ServerState *s, unsigned int now_ms);

/* Test/debug hook: directly forces a zombie's mood, bypassing the real has_target-gated tick path
 * -- the same real, named boundary this header's own top doc comment already states (no
 * perception system exists yet to earn HUNTING/FRENZIED organically). */
void witness_ai_force_zombie_mood(int player_id, int mood);

/* Test/debug accessor: the current WS_* witness state of the citizen at this player_id, or -1 if
 * player_id doesn't resolve to an active citizen spawned by this module. */
int witness_ai_citizen_state(int player_id);

/* Test/debug accessor: the citizen's CURRENT WitnessNpc vigilance (npc_archetype's own real
 * mood-modulated value, written back by witness_ai_tick -- see witness_ai.c's own doc comment),
 * or -1 if player_id doesn't resolve to an active citizen spawned by this module. */
int witness_ai_citizen_vigilance(int player_id);

#endif
