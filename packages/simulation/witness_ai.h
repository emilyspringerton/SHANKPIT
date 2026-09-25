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
 *  - ~~No movement/patrol/wander AI~~ -- **closed 2026-09-25** for zombies (real flat-radius
 *    perception + chase + melee) and citizens (real flee-from-a-hunting-zombie reaction), see this
 *    header's own "Zombie perception/pursuit/melee + citizen flee reactions" section below. The
 *    Men and Giant Zombie Bugs keep their own existing, separate, still-standing-at-spawn scope --
 *    not touched by this pass.
 *  - ~~zombie_tick's own `has_target` is always passed 0~~ -- **closed 2026-09-25**: a zombie now
 *    genuinely perceives the hero within WITNESS_AI_ZOMBIE_PERCEPTION_RADIUS (flat (x,z), no real
 *    line-of-sight system exists yet -- same honest boundary this file's zone/witness radius
 *    checks already accept). witness_ai_force_zombie_mood is still real and still useful (an
 *    instant, deterministic bootstrap for tests/scripted encounter beats), just no longer the ONLY
 *    way a zombie ever reaches HUNTING/FRENZIED.
 *  - ~~No resolution/memory-wipe loop~~ -- **closed 2026-09-25**: a live The Men NPC now hunts
 *    down and walks to the nearest SILENCING/ENGAGE citizen (WITNESS_AI_MEN_RESPONSE_RADIUS) and
 *    resolves every hunting citizen in that same zone on arrival via the real, already-tested
 *    witness_sim_memory_wipe (see this header's own "The Men's dispatch/resolution loop" section
 *    below, and docs2/specs/BIGO_ENGINE_MERGE_NORTHSTAR.md §2m). PANIC is a real WS_* state
 *    witness_rules.c can still reach that this dispatch loop does NOT resolve (only SILENCING/
 *    ENGAGE, matching resolve_hunters' own real, unchanged scope) -- a citizen stuck at PANIC
 *    stays there, a real, separate, still-open gap.
 *    Citizen flee (also 2026-09-25) is a real, separate reaction layered on top of this state
 *    machine, not a replacement for it. */

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
#define WITNESS_AI_ROLE_GIANT_BUG 3
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

/* --- "wheelbarrow" -- carry a whole zombie or citizen back to your lab (founder real-time,
 * 2026-09-22: "cannon - add wheelbarrow for carrying whole zombies or citizens back to your
 * lab") ----------------------------------------------------------------------------------------
 * Real, honest scope cut: no literal wheelbarrow prop/model exists (a new 3D asset, a real,
 * separate art task) -- "wheelbarrow" is this mechanic's flavor name, the carry itself is the
 * real feature. Delivery target is the SAME hardcoded VOXWORLD "lab" trespass circle the costume
 * mechanic already uses (witness_ai.c's own WITNESS_AI_LAB_ZONE_*) -- there is no real lab scene
 * to walk into yet (BACKLOG's own "the lab" follow-up, still entirely unstarted), so this reuses
 * the one real, hardcoded "the lab is here" location this engine merge already has rather than
 * inventing a second one. */
#define WITNESS_AI_PICKUP_RADIUS 4.0f

/* Finds the nearest active citizen or zombie (spawned by this module) within
 * WITNESS_AI_PICKUP_RADIUS of (px,py,pz) and marks it carried. No-op (returns -1) if something is
 * already being carried -- one at a time, matching CTF's own real one-flag-per-team precedent
 * (local_game.h's own carried_flag_team_id). Returns the picked-up player_id, or -1. */
int witness_ai_try_pickup(ServerState *s, float px, float py, float pz, unsigned int now_ms);

/* Releases whatever is currently carried in place (no delivery credit -- that only happens by
 * walking the carried NPC into the lab circle, see witness_ai_tick's own real delivery check).
 * Safe to call when nothing is carried. */
void witness_ai_drop_carried(void);

/* The currently-carried player_id, or -1 if nothing is being carried. */
int witness_ai_carried_player_id(void);

/* Real, live count of specimens delivered to the lab circle so far this session -- the one real
 * number STATUS/LAB (the phone app UI's own still-generic screens) could show once either grows a
 * real reason to display it; not wired into either screen yet (honest, named, not this pass). */
int witness_ai_lab_deliveries(void);

/* --- cake-smash distraction (founder real-time, 2026-09-22: "if the cake gets smashed it flies
 * everywhere and causes a big distraction and distracts from heavy zombie usage") -------------
 * Halves every active citizen/The Men's effective vigilance for WITNESS_AI_DISTRACTION_MS, which
 * feeds directly into witness_rules.c's own real noticed() formula -- a zombie event elsewhere
 * during the window is genuinely less likely to be noticed. Triggered by CARGO smashing FOOD_CAKE
 * specifically (phone.h's own BP_FX_SMASH_CAKE), not eaten like the other 16 food items. */
#define WITNESS_AI_DISTRACTION_MS 8000u
void witness_ai_smash_cake(unsigned int now_ms);

/* Real, live accessor: is a distraction currently active? Test/debug + a future HUD readout. */
int witness_ai_distraction_active(unsigned int now_ms);

/* --- Zombie perception/pursuit/melee + citizen flee reactions (founder real-time, 2026-09-25:
 * "add more affordances from the big_o spec bring the game to life in shankpit as a v_0 just
 * like the most awesome visceral agency zombie fighting citizens reacting all the things") -----
 * This closes witness_ai.h's own top-doc-comment "has_target is always passed 0" and "no movement/
 * patrol/wander AI" scope cuts, for zombies and citizens specifically (The Men/Giant Zombie Bugs
 * keep their own existing, separate scope). A zombie now genuinely perceives the hero (flat (x,z)
 * radius, no line-of-sight system exists yet -- same honest boundary this file's own zone/witness
 * radius checks already accept), chases via the SAME generic accelerate()/collision pipeline
 * story_ai.c's own bots already move through (sets p->yaw/p->in_fwd, local_game.h's per-player
 * loop does the rest for any i>0 active player in MODE_STORY), and melees the hero on contact.
 * A citizen within WITNESS_AI_CITIZEN_FLEE_RADIUS of a HUNTING/FRENZIED zombie runs directly away
 * from it, the same movement pipeline, independent of (and faster-reacting than) the witness_sim
 * state-machine escalation above -- that state machine still drives the narrative (DENIAL/PANIC/
 * SILENCING), this drives what the player actually SEES the citizen's body do. */
#define WITNESS_AI_ZOMBIE_PERCEPTION_RADIUS 45.0f
#define WITNESS_AI_ZOMBIE_MELEE_RANGE 3.0f
#define WITNESS_AI_ZOMBIE_ATTACK_COOLDOWN_MS 1100u
#define WITNESS_AI_ZOMBIE_MELEE_DAMAGE 14
#define WITNESS_AI_CITIZEN_FLEE_RADIUS 30.0f

/* The Men's dispatch/resolution loop (2026-09-25 follow-up, same pass) -- closes this header's own
 * long-standing "no resolution/memory-wipe loop" scope cut. A live The Men NPC now hunts down and
 * walks to the nearest SILENCING/ENGAGE citizen in its own scene within
 * WITNESS_AI_MEN_RESPONSE_RADIUS, and on arrival (WITNESS_LIVE_DISPATCH_ARRIVAL_RADIUS,
 * witness_live.h -- a real, previously-unused constant) resolves every hunting citizen in that
 * SAME zone via witness_sim_memory_wipe (witness_sim.c, phase 2, real and tested since before this
 * file existed, just never given a live caller before now). See witness_ai.c's own doc comment on
 * the dispatch loop itself for the full account. */
/* 220.0f, not a tight "nearby only" radius -- checked against the real seeded VOXWORLD encounter
 * (witness_ai_seed_voxworld_encounter): The Men are stationed guarding the lab circle at
 * (cx+110,cz), while the 4 ambient citizens sit ~75-155 units away at the encounter's own
 * spatial footprint. A tighter, "more realistic" radius would make this whole mechanic
 * invisible in the one real, live seeded encounter that exists -- same "make it real, not just
 * theoretically wired" bar the rest of this pass holds itself to. */
#define WITNESS_AI_MEN_RESPONSE_RADIUS 220.0f

/* --- Giant Zombie Bugs (founder real-time, 2026-09-22: "add giant zombie bugs (feral AI units)
 * they need a totally unique value system vector based deliberately non human 64 layer hand
 * written llm" -> "use parena" -> "if they eat a strong zombie they get stronger if they eat a
 * fast zombie they get faster" -> "men are the custodians of the keys for the giant zombie feral
 * ai bugs"). See giant_bug_values.h's own doc comment for the full account -- a genuinely
 * separate entity type from the regular zombies above, its own PARENA-computed value network
 * (PARENA/stdlib/shankpit/giant_bug_brain.prn), real eat-to-grow mechanic, and a real
 * authorization gate (witness_ai_bug_command_authorized) requiring a live The Men NPC. ------- */

/* Spawns one Giant Zombie Bug into a free player slot with a fresh GiantBugState (real 1.0
 * strength/speed baseline). Same shape/contract as witness_ai_spawn_zombie. Returns the player
 * slot id, or -1. */
int witness_ai_spawn_giant_bug(ServerState *s, float x, float y, float z, unsigned int now_ms);

/* "The Men are the custodians of the keys" -- true only while at least one live The Men NPC is
 * active. witness_ai_tick's own real per-tick loop gates giant-bug hunting/eating on this. */
int witness_ai_bug_command_authorized(void);

/* Test/debug accessors: the current, real, live strength/speed of the giant bug at this
 * player_id (grows permanently from giant_bug_eat_zombie), or -1.0f if player_id doesn't resolve
 * to an active giant bug spawned by this module. */
float witness_ai_bug_strength(int player_id);
float witness_ai_bug_speed(int player_id);

#endif
