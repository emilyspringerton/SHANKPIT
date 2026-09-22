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
#include "../world/level_boxes.h" /* CustomLevelData, level_boxes_zone_for_position -- phase 7c */
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

/* witness_ai_sync_zones -- BIG_O engine merge phase 7c. Real, live consumer of
 * level_boxes_zone_for_position (packages/world/level_boxes.h): for every active citizen,
 * resolves its CURRENT PlayerState position against the given level's own authored zone volumes
 * and writes the result into the owned WitnessSim's npc entry (WitnessNpc.zone -- public field,
 * same direct-write convention witness_ai_tick already uses for .vigilance/.state). A citizen
 * standing outside every authored zone (or `level` is NULL -- no level data loaded, or a level
 * with no "zones" array at all) keeps its LAST zone rather than being silently reset to
 * ZONE_PUBLIC -- a real, deliberate choice: an unauthored gap in a level's zone coverage should
 * not read as a meaningful "the player stepped into public" fact.
 *
 * Deliberately a separate function from witness_ai_tick, not folded into it -- level data is
 * per-level (reloaded on `story_ai_reset`-style level transitions), while witness_ai_tick runs
 * every real server frame regardless of whether a level is even loaded; keeping them separate
 * means a caller with no CustomLevelData yet (or a non-story mode with no zones authored) can
 * still call witness_ai_tick every frame without this function ever needing a NULL-safe no-op
 * path baked into the hot loop. Real, honest, not wired into any live tick loop yet -- no caller
 * exists in apps/server or apps/lobby (that's 7d/7e, once a real MODE_STORY-replacing game loop
 * calls both this and witness_ai_tick together each frame). */
void witness_ai_sync_zones(ServerState *s, const CustomLevelData *level);

/* witness_ai_seed_voxworld_encounter -- BIG_O engine merge phase 7d (MODE_STORY content
 * cutover, "replace outright" per founder direction). The real replacement for
 * story_ai_seed_voxworld_encounter (story_ai.c) as MODE_STORY's VOXWORLD content: 4 ambient
 * citizens and 2 zombies instead of the old AI_ROLE_* combat squad/patrol encounter. A no-op for
 * any other scene (mirrors story_ai_seed_voxworld_encounter's own SCENE_VOXWORLD guard exactly).
 *
 * Real, deliberate, narrowly-scoped cutover -- what this does NOT touch, on purpose:
 *  - story_ai.c's own AI_ROLE_* roster and its general LevelCharacter/NOCK-authoring spawn path
 *    (server_apply_custom_level) stay completely alive and untouched -- that's real, general,
 *    cross-mode level-editor infrastructure other levels may use (SHANKPIT/CLAUDE.md's own
 *    standing "levels are never story-mode-only" instruction), not "MODE_STORY's content."
 *  - The VOXWORLD boss fight (StoryBossState, local_game.h) is a separate, hand-rolled system
 *    with no relationship to story_ai.c's AIController roster -- out of scope, untouched.
 *
 * Bootstraps one zombie straight into HUNTING (via witness_ai_force_zombie_mood below) --
 * real, honest reason: zombie_tick's own has_target is always 0 (no perception system exists
 * yet, see this header's own top doc comment), so a freshly-spawned zombie would otherwise sit
 * DORMANT forever with nothing for any citizen to ever witness. This is the same test/debug hook
 * witness_ai_test.c already exercises, now given its first real gameplay call site. */
void witness_ai_seed_voxworld_encounter(ServerState *s, unsigned int now_ms);

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

/* Test/debug accessor: the citizen's CURRENT WitnessNpc zone (a ZONE_* value, witness_sim.h),
 * or -1 if player_id doesn't resolve to an active citizen spawned by this module. */
int witness_ai_citizen_zone(int player_id);

/* --- "full phone app parity, costume changes, add The Men" (EMILY/BACKLOG.md SECTION 536
 * follow-up, founder real-time, 2026-09-22) -------------------------------------------------- */

/* Same "int with a -1 sentinel" convention as witness_ai_citizen_state/vigilance/zone above. A
 * caller (main.c's own mannequin-kit selector) needs to tell citizen/The Men/zombie apart to give
 * each a real, distinct visual identity -- see witness_ai.c's own doc comment on why this reuses
 * npc_archetype.h's existing NPC_ARCHETYPE_* distinction rather than inventing a new one. */
#define WITNESS_AI_ROLE_NONE -1
#define WITNESS_AI_ROLE_CITIZEN 0
#define WITNESS_AI_ROLE_THE_MEN 1
#define WITNESS_AI_ROLE_ZOMBIE 2
int witness_ai_role_for_player(int player_id);

/* Spawns one member of The Men (npc_archetype.h's NPC_ARCHETYPE_THE_MEN -- high base_vigilance,
 * focused mood bias, already real and tested since phase 3, never actually spawned anywhere until
 * now) into a free player slot. Same shape/contract as witness_ai_spawn_citizen, sharing that
 * function's own internal spawn_human helper (witness_ai.c) -- not a duplicate implementation.
 * Returns the player slot id, or -1. */
int witness_ai_spawn_the_men(ServerState *s, int zone, int base_vigilance, int arrogance,
                              float x, float y, float z, unsigned int now_ms);

/* Wires the phone's own real WARDROBE selection (packages/common/phone.h, phase 6) into the
 * live human player's WitnessSim slot (g_sim.p[0] -- already allocated by witness_ai_reset's own
 * nplayers=1 call, previously never driven with real data; this is that hook). Idempotent, cheap
 * to call every frame the phone is open. */
void witness_ai_set_player_costume(int costume);

/* Real, live readout of the human player's own decorum meter -- witness_ai_tick's own hardcoded
 * VOXWORLD "lab" trespass circle (witness_ai.c, no LevelZone/JSON authoring exists for this scene
 * yet, same "hardcoded coordinates" precedent witness_ai_seed_voxworld_encounter's own citizen/
 * zombie placement already set) calls witness_sim_enter on the player's behalf when they cross
 * into it, which is what actually moves this number. Raw WitnessPlayer.decorum and its BAND_* per
 * witness_sim_decorum_band, respectively. */
int witness_ai_player_decorum(void);
int witness_ai_player_decorum_band(void);

#endif
