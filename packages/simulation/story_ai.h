#ifndef STORY_AI_H
#define STORY_AI_H

#include "../common/protocol.h"
#include "humanness.h"
#include "ai_nav.h"
#include "empire_units.h"

/* 128, not 16: a dressed city authors a population on every FLOOR of every building (levels/lofts),
   and 16 was the story campaign's own cast size. The arrays are static and the level loader's own cap
   matches this number (LEVEL_BOXES_MAX_CHARACTERS), so the two must move together. */
#define STORY_AI_MAX 128
#define STORY_AI_PATROL_MAX_POINTS 8

#ifndef STORY_AI_DEBUG
#define STORY_AI_DEBUG 0
#endif

typedef enum {
    AI_MODE_DISABLED = 0,
    AI_MODE_PUPPET,
    AI_MODE_PATROL,
    AI_MODE_INVESTIGATE,
    AI_MODE_COMBAT,
    AI_MODE_SEARCH,
    AI_MODE_FLEE,
    AI_MODE_ALLY_FOLLOW,
    AI_MODE_SCRIPTED,
    /* S462 -- real Territorial Beast radius-anchor/leash retreat (founder real-time: "Radius
       Anchoring & Return Leashes... retreats if pulled too far away"). Forced unconditionally
       (even mid-combat) whenever distance-from-home exceeds leash_radius; exits only on real
       arrival back home, not on the radius re-check alone. See ai_run_leash_return. */
    AI_MODE_LEASH_RETURN,
    /* S470 -- real, ambient friendly behavior (founder real-time: "have them wave to the player
       when the player gets close and then dance before resuming patrol"). Locked, non-combat --
       AI_ROLE_WANDERING_BOT is the only role that ever enters it, and its own decision logic
       (story_ai_tick) never routes it through combat/investigate/flee at all, same class of
       "real behavior, deliberately excluded from the rest of the state machine" precedent
       AI_MODE_SCRIPTED's own locked-state comment already sets. See ai_run_greet. */
    AI_MODE_GREET,
    /* EMPIRE unit orders (packages/simulation/empire_units.h). These are not new brains: each one is a
       small, specific way of using the movement/attack primitives the modes above already have, chosen
       by eu_choose_order from the unit's own authored mask. A unit whose archetype is 0 never enters
       them -- every pre-existing AI's behaviour is untouched. */
    AI_MODE_EXPLORE, /* walk to points inside the explore radius, look around, pick another */
    AI_MODE_GATHER,  /* walk to the nearest resource node and take from it while standing on it */
    AI_MODE_CLAIM   /* walk to the nearest unowned cell and stand on it until it is ours */
} AIMode;

typedef enum {
    AI_ROLE_RIFT_HOUND = 0,
    AI_ROLE_SHAMBLER_TROOPER,
    AI_ROLE_GORE_BRUTE,
    AI_ROLE_STORY_ALLY,
    AI_ROLE_GUARD,
    /* S181-05, founder real-time: "advance the entities fought in story
     * mode... use redgarden squad ai and realistic abilities and
     * powerups... many enemies with different threats." Two new roles
     * for real roster variety beyond the original 3, each a genuinely
     * distinct threat archetype (long-range burst vs. heavy AOE spammer)
     * built on real, already-proven SHANKPIT weapon mechanics -- not
     * invented systems. */
    AI_ROLE_STORM_CALLER,  /* WPN_SNIPER -- long-range storm-charge burst threat */
    AI_ROLE_BOMBARDIER,    /* WPN_MISSILE -- heavy, slow, real splash-AOE spammer */
    /* S462 -- solo, non-squad enemy archetypes (founder real-time: "the auto-pilot AI shifts its
       focus away from tactical communication and onto territorial boundaries, distinct sensory
       profiles, and physiological drives"). Never joined to a squad (story_ai_form_squad is
       never called for them) -- squad_id stays -1, the same default every AI spawns with. */
    AI_ROLE_RELENTLESS_PURSUER, /* "Zombie" -- direct-line pursuit only, never kites, never flees */
    AI_ROLE_TERRITORIAL_BEAST,  /* radius-anchored home + leash retreat, see AI_MODE_LEASH_RETURN */
    AI_ROLE_BLIND_STALKER,      /* "Ambush Predator" + "Sightless Echo-Locator" combined -- near-
                                    zero vision, detects almost entirely by hearing */
    /* S470 -- real, non-hostile ambient archetype (founder real-time: "get all the robots in
       there and have them walking around different waypoints... wave to the player when the
       player gets close and then dance before resuming patrol"). Never enters combat/investigate/
       search/flee at all -- story_ai_tick's own decision loop special-cases this role the same
       way it already special-cases AI_ROLE_STORY_ALLY, just routing to AI_MODE_PATROL/
       AI_MODE_GREET only instead of ALLY_FOLLOW/COMBAT. Never joined to a squad, same as the
       three S462 solo archetypes. */
    AI_ROLE_WANDERING_BOT,
    /* The civic boss (founder: "add a civic boss"). Deliberately the only role that is not a
       reskin of a threat the roster already had: a magistrate holds the bench instead of charging,
       carries WPN_MAGNUM (the one weapon no other role uses) and its ability is a real sentence --
       see ai_combat_magistrate in story_ai.c. Authored as the second body of the municipal
       courthouse (tools/build_municipal_district.py's BUILDING_STAFF), i.e. the body behind the
       bailiff rather than a third body in a district that has no slot to spare. */
    AI_ROLE_MAGISTRATE
} AIRole;

/* story_ai_role_is_valid -- the one range check every role consumer should use, exactly the role
   story_ai_kit_is_valid below already plays for kits. This range was written out by hand in
   apps/server, apps/lobby and story_ai_spawn_unit_role (plus IDUNA's validateCharacters), and one of
   those copies already shipped a value short -- AI_ROLE_WANDERING_BOT was real natively and rejected
   at load. Adding the civic boss would have been a fourth copy to keep in step, so they become one
   function instead. */
static inline int story_ai_role_is_valid(int role) {
    return role >= AI_ROLE_RIFT_HOUND && role <= AI_ROLE_MAGISTRATE;
}

/* AIKit (S492) -- a NOCK-authored character's chosen visual model, completely decoupled from
   AIRole (behavior) above -- the same "role picks the brain, kit picks the body" split
   PlayerState.forced_kit's own doc comment (packages/common/protocol.h) describes. AI_KIT_AUTO
   preserves the pre-S492 behavior exactly: draw_player_skin_mannequin's own connection-order
   round-robin / witness_ai role-based pick, unchanged for any character that doesn't opt in.
   Kept in sync BY HAND with internal/shankpit.AIKit* (IDUNA) and apps/lobby/src/main.c's own
   kits[5] array order -- same manual cross-boundary sync AIRole's own values already require. */
typedef enum {
    AI_KIT_AUTO = 0,
    AI_KIT_MANNEQUIN,
    AI_KIT_STAN,
    AI_KIT_MIKE,
    AI_KIT_LEELA,
    AI_KIT_GEORGE,
    /* Imported skins. A set NOCK imported into assets/goldenband/clipsets/<name>/ becomes a kit at
       startup (skin_registry_scan + gband_skel_npc_load_clipset), and an authored character picks
       one with AI_KIT_IMPORTED_BASE + index -- the same number the lobby prints for each set when it
       loads, so a level author reads the index off the host's own startup line instead of guessing.
       forced_kit is an unsigned char on the wire (NetPlayer.forced_kit), so this range costs nothing
       and AI_KIT_IMPORTED_BASE + STORY_AI_IMPORTED_MAX still fits in a byte. */
    AI_KIT_IMPORTED_BASE = 6
} AIKit;

/* How many imported skins an authored character may address. The host loads at most this many (it is
   bounded by the GOLDENBAND kit table and the registry scan), and a value past what actually loaded
   is not an error: the drawing cascade simply falls through to its usual round-robin. Kept as a
   plain number so this header stays free of the filesystem-facing skin_registry.h. */
#define STORY_AI_IMPORTED_MAX 10

/* story_ai_kit_is_valid -- the one range check every kit consumer should use. Before imported skins
   existed this test was written out by hand as `kit >= AI_KIT_AUTO && kit <= AI_KIT_GEORGE` in both
   apps/server and apps/lobby; two copies of a range that has now grown is exactly how an imported
   skin silently stops being authorable. */
static inline int story_ai_kit_is_valid(int kit) {
    return kit >= AI_KIT_AUTO && kit < AI_KIT_IMPORTED_BASE + STORY_AI_IMPORTED_MAX;
}

typedef struct {
    float x, y, z;
    unsigned int wait_ms;
    int behavior_hint;
} AIPatrolPoint;

/* S461-03 -- real Squad Leader roles, layered on top of AIWorldBlackboard rather than replacing
   it: the blackboard is shared alert-state ANY AI can read, a squad is a real, persistent
   sub-group of specific AIs (hand-authored together at spawn, same convention as patrol points/
   the S461-01 nav graph) with an explicit leader and per-member combat roles that get
   re-evaluated when a member dies. See story_ai_form_squad / ai_squad_reevaluate in story_ai.c. */
typedef enum {
    SQUAD_ROLE_NONE = 0,
    SQUAD_ROLE_LEADER,
    SQUAD_ROLE_FLANK_LEFT,
    SQUAD_ROLE_FLANK_RIGHT,
    SQUAD_ROLE_SUPPRESS
} AISquadRole;

#define STORY_AI_SQUAD_MAX 8
#define STORY_AI_SQUAD_MAX_MEMBERS 6

typedef struct {
    int active;
    int player_id;
    AIRole role;
    AIMode mode;
    AIMode previous_mode;

    AIPatrolPoint patrol[STORY_AI_PATROL_MAX_POINTS];
    int patrol_count;
    int patrol_index;
    unsigned int wait_until_ms;

    int target_player_id;
    float last_known_x, last_known_y, last_known_z;
    unsigned int last_seen_ms;
    unsigned int last_heard_ms;
    unsigned int mode_entered_ms;

    float vision_range;
    float vision_fov_deg;
    float hearing_range;
    float attack_range;
    float preferred_range;
    float courage;
    float aggression;
    float aim_error_deg;
    float move_speed_scale;

    unsigned int next_attack_ms;
    unsigned int next_decision_ms;
    /* AI_ROLE_MAGISTRATE's own ability clock (ai_combat_magistrate). Zeroed by spawn's own memset,
       so a fresh magistrate's first sentence is available immediately -- the same "0 means ready"
       convention next_attack_ms above already uses. Dead data on every other role, which never
       reads it (the same honesty the WANDERING_BOT role defaults keep about unused fields). */
    unsigned int next_verdict_ms;
    /* The verdict's TELEGRAPH, not its result: when the cooldown is up and the player is in sight
       the bench arms this deadline instead of stunning on the spot, and the sentence is only served
       once the window elapses with the player still in sight. 0 means no sentence is in progress,
       the same "0 means ready" convention next_verdict_ms uses. Read by the client through
       story_ai_verdict_tell_fill below, so the ring and the boss bar cannot show a tell the AI is
       not actually running. Dead data on every other role. */
    unsigned int verdict_tell_until_ms;

    /* Humanness Phase 2 (docs/HUMANNESS_NORTHSTAR.md) -- real per-instance jitter/mood state,
       fed into ai_turn_towards' own real overshoot-then-settle turning and aim noise, and into
       each role's own attack-cooldown scheduling. See humanness.h's own doc comments for what
       each primitive actually does. */
    HumannessState humanness;
    int turn_overshooting;

    /* S461-01 -- real tactical pathing/cover state for AI_MODE_FLEE, computed once on first
       entry (see ai_run_flee's own comments for why this is intentionally terminal, not
       re-computed every tick). flee_target_node == -1 means either not yet computed or no
       reachable cover was found (falls back to a plain repulsion vector). */
    int flee_target_node;
    int flee_path[AI_NAV_MAX_PATH];
    int flee_path_len;
    int flee_path_index;

    /* S461-03 -- squad membership, set by story_ai_form_squad at spawn time and kept current by
       ai_squad_reevaluate every tick. squad_id == -1 means "not in a squad" (e.g. the story ally,
       which never joins a hostile squad). */
    int squad_id;
    AISquadRole squad_role;

    /* S461-04 -- real movement-hook + locked-hold + on-end handoff for AI_MODE_SCRIPTED
       (previously a dead enum value with zero behavior, same class of gap AI_MODE_FLEE was
       before S461-01). Server-authoritative timing/positioning only -- real client-side clip
       selection during the hold is a named, not-yet-built follow-up, see
       docs2/specs/AI_SCRIPTED_ANIMATION_NORTHSTAR.md. wait_until_ms (above) is reused as the
       "arrived, now holding" timer -- SCRIPTED and PATROL are mutually exclusive per AI. */
    float scripted_marker_x, scripted_marker_y, scripted_marker_z;
    unsigned int scripted_hold_ms;

    /* S462 -- home anchor, set to this AI's own real spawn position for EVERY role
       (story_ai_spawn_enemy), but only load-bearing when leash_radius > 0 (AI_ROLE_TERRITORIAL_
       BEAST's own per-role default; every other role keeps leash_radius == 0, a real no-op --
       the story_ai_tick leash check never fires for them). */
    float home_x, home_y, home_z;
    float leash_radius;

    /* S470 -- real AI_MODE_GREET phase state. greet_phase: 0 = wave, 1 = dance. wait_until_ms
       (above) is reused as the "this phase's own hold timer" sentinel, same convention
       AI_MODE_SCRIPTED's own wait_until_ms reuse already establishes -- GREET and SCRIPTED/PATROL
       never run concurrently on the same AI so there's no real conflict over the field. */
    int greet_phase;
    unsigned int last_greet_ms;

    /* EMPIRE unit layer -- see packages/simulation/empire_units.h. `archetype` is EU_ARCH_NONE (0) on
       every AI that was not authored as a citizen/soldier/ally, which is what keeps this additive:
       the order block in story_ai_tick is skipped entirely for archetype 0. `eu` carries the inherited
       traits, the accepted order mask and the live order; `last_order_ms` is the delta the gather/claim
       runs use (a claim is 30 real seconds of standing still, so it needs a clock). */
    EUUnit eu;
    int archetype;
    int faction;
    unsigned int last_order_ms;
} AIController;

/* S461-03 -- a real, persistent group of specific AIController slots (by index into the
   module-private g_story_ai array in story_ai.c), not a per-tick proximity cluster -- formed
   once at encounter-spawn time via story_ai_form_squad, same hand-authoring convention as patrol
   points. member_ai_index[0] is always the current leader; ai_squad_reevaluate promotes the next
   living member when the leader dies, and re-derives the remaining members' flank/suppress roles
   from their new order, matching the founder's own real-time spec: "If Member 3 dies, the Squad
   class instantly re-evaluates and promotes ... to change their auto-pilot state dynamically." */
typedef struct {
    int active;
    int member_ai_index[STORY_AI_SQUAD_MAX_MEMBERS];
    int member_count;
} AISquad;

typedef struct {
    int alert_level;
    int player_visible_count;
    int active_attackers;
    int active_flankers;
    float last_known_player_x;
    float last_known_player_y;
    float last_known_player_z;
    unsigned int last_global_alert_ms;
} AIWorldBlackboard;

void story_ai_reset(ServerState *s);
// story_ai_despawn_all_characters -- S480, founder real-time: "can we make the characters stuff
// work outside of story mode?" Real, safe alternative to story_ai_reset for a non-MODE_STORY
// level load: story_ai_reset deactivates EVERY player slot 1..MAX_CLIENTS-1 unconditionally --
// exactly right for MODE_STORY's own real single-hero-plus-NPCs world, but a genuine regression
// risk anywhere real connected humans (or the QUEUE bot pool) might occupy those same slots. This
// walks g_story_ai's own active entries and deactivates ONLY the specific player slots they
// themselves spawned into (via each AIController's own player_id), leaving every other slot --
// human or otherwise-bot -- completely untouched. Does not touch g_story_bb (a real, global,
// per-tick-recomputed-from-scratch blackboard, see story_ai_tick's own top-of-tick memset --
// self-heals within one tick regardless) or g_story_nav (reloaded unconditionally by
// story_ai_load_nav_graph right after this runs at every real call site, mode-agnostic already).
void story_ai_despawn_all_characters(ServerState *s);
// kit: AI_KIT_AUTO or an explicit AIKit (S492) -- sets the spawned PlayerState's own forced_kit
// once, unchanged for the rest of that NPC's life (see forced_kit's own doc comment).
int story_ai_spawn_enemy(ServerState *s, AIRole role, int kit, float x, float y, float z);

/* story_ai_spawn_unit -- spawn an EMPIRE archetype (packages/simulation/empire_units.h): citizen,
   zombie, soldier, ally, guard, worker. The archetype picks the brain role, the default skin, the
   faction, the accepted orders and the health pool; every one of them is overridable here, which is
 * the level editor's own affordance ("spawn a soldier", "spawn Mike as an ally on my side").
 *
 *   kit:     > 0 = that AIKit, 0 = the archetype's default. An authored skin always wins.
 *   faction: >= 0 = that faction, < 0 = the archetype's default.
 *   orders:  >= 0 = that EU_ORDER_* mask, < 0 = the archetype's default mask.
 *
 * Returns the player slot id (>= 1), or -1 when there is no free slot/controller. Spawning through
 * this path is what puts the unit's orders into the real tick -- story_ai_spawn_enemy alone still
 * works and still means exactly what it meant before. */
int story_ai_spawn_unit(ServerState *s, int archetype, int kit, int faction, int orders,
                         float x, float y, float z);

/* story_ai_spawn_unit_role -- the same spawn with an authored brain role: the level editor's own
 * `role` field (level_boxes.h) laid over the archetype's. `role` < 0 means "the archetype's own",
 * which is exactly what story_ai_spawn_unit calls this with, so that entry point is unchanged.
 *
 * Why the override exists: the EMPIRE layer's "archetype picks the brain" rule is a good default and
 * a bad ceiling. A civic district staffing six buildings through it alone gets six identical pursuers
 * (or, at best, two brains shared six ways); with the override each building can have its own threat
 * -- a shotgun bailiff, a sniper archivist, a knife orderly -- while the archetype still supplies the
 * health pool, the default skin and the order priorities. An out-of-range role is ignored, never
 * clamped, so a stale reference cannot turn into a valid but different brain. */
int story_ai_spawn_unit_role(ServerState *s, int archetype, int role, int kit, int faction, int orders,
                             float x, float y, float z);

/* The shared EMPIRE world (resource nodes + claimable cells + per-faction stockpiles). One per process,
 * owned here because this is the module that ticks units. The lobby's local sim and the server each have
 * their own, the same way each already owns its own ServerState. */
EUWorld *story_ai_empire_world(void);

/* Seeds the default contested ground: a ring of resource nodes and a handful of claim cells around the
 * origin. Called from story_ai_reset so a level with authored units always has something to gather and
 * something to take; idempotent, and a caller that wants a different layout can clear/refill the world
 * before spawning. Named honestly: this is a default, not level-authored terrain (the level schema does
 * not carry nodes/cells yet). */
void story_ai_empire_seed_default(void);

/* story_ai_empire_load_level -- the level-authored world (level_boxes.h's resource_nodes/claim_cells),
 * unpacked into flat arrays by the caller so this header keeps its own "no packages/world dependency"
 * boundary. Replaces the whole world; a level that authors nothing (both counts 0) falls back to
 * story_ai_empire_seed_default's ring, which is exactly what every level authored before this did.
 * Returns the number of nodes loaded. */
int story_ai_empire_load_level(int node_count, const float *node_x, const float *node_z,
                               const int *node_amount, int cell_count, const float *cell_x,
                               const float *cell_z, const int *cell_radius);

/* story_ai_empire_unit_count -- how many live EMPIRE units this process is ticking (archetype > 0).
 * The HUD reads it to decide whether to draw anything at all, and the replication broadcaster uses it as
 * its own gate; 0 means "this world has no EMPIRE in it". */
int story_ai_empire_unit_count(void);

/* Test/debug readouts, -1 when player_id is not an EMPIRE unit: the order it is executing (EU_ORDER_*)
 * and its archetype (EU_ARCH_*). */
int story_ai_unit_order(int player_id);

/* story_ai_bank_handins -- empty every unit of `faction` that has walked its cargo back to its own spawn
 * point, and return how many units were handed in (0 when nobody is home with anything). Deliberately
 * returns UNITS, not influence: `empire_bank_credit` (PARENA, via empire_rules_host.h) says what a banked
 * unit is worth and how much fits under the pool's cap, and converting here would drag empire_rules.c into
 * the server's source list for no gain. Called by the lobby's own empire sync, once per frame. */
int story_ai_bank_handins(ServerState *s, int faction);

/* ---- survival waves (packages/simulation/wave_spawner.h + PARENA wave_rules.prn) --------------------
 * story_ai_waves_enable arms or disarms the schedule and resets it to the first delay from `now_ms`;
 * calling it with 1 is how a survival mode turns waves on. It is OFF by default and nothing in this file
 * arms it, so an ordinary level still has exactly the population it authored.
 *
 * story_ai_waves_tick emits one wave when the timer says so and the live cap allows, spawning the PARENA
 * archetype on a ring around the world origin, and returns how many bodies actually took a slot. Call it
 * once per host tick; time is passed in, never read here, so a hitch cannot mint waves. */
void story_ai_waves_enable(int on, unsigned int now_ms);
int story_ai_waves_tick(ServerState *s, unsigned int now_ms);
int story_ai_waves_remaining(void);
int story_ai_unit_archetype(int player_id);
/* story_ai_role_slot -- the PlayerState slot of the first active unit playing `role`, or -1. The
   client needs this because PlayerState itself carries no role field (only story_ai.c's own
   AIController does): the civic boss's health bar and its verdict tell both live on the body, and
   they have to find that body without inventing a second copy of the role table in main.c. */
int story_ai_role_slot(int role);
/* story_ai_verdict_tell_fill -- the AI_ROLE_MAGISTRATE verdict's own telegraph, as a real 0..1
   fraction of the tell window that has elapsed, or -1.0f when no sentence is in progress (the
   magistrate's role, the wrong slot, or between sentences all read -1.0f). The sim is the
   authority here: the ring and the boss bar in apps/lobby draw from this value, so breaking the
   player's line of sight clears the telegraph on screen at the same tick it cancels the sentence,
   and nothing can be telegraphed that is not really coming. */
float story_ai_verdict_tell_fill(int player_id, unsigned int now_ms);
/* story_ai_magistrate_event_counts -- two monotonic counters (tells armed, sentences served) so a
   presentation layer can EDGE-trigger on the verdict instead of polling a state that goes -1 -> 0.x
   -> -1: the client's spatial gavel cue and its ember/burst particles fire once per event because
   they compare these against the values they last saw, not because they guessed from a fill value.
   Reset to 0 by story_ai_reset, so a level change reads as a reset, not as a burst of a thousand
   events; a caller that sees the count go backwards treats it the same way. */
void story_ai_magistrate_event_counts(unsigned int *tells, unsigned int *served);
/* story_ai_magistrate_scorches -- the sim's own scorch pool, read-only for the renderer.
   verdict_scorch.h is the pool AND the hazard (radius, cadence, lifetime, scene filter); story_ai.c owns
   the instance and ticks the damage, and apps/lobby draws exactly this pool, so the fire that burns the
   player and the fire on the floor are the same fire instead of two parallel copies. Reset by
   story_ai_reset with the rest of the civic boss's state. */
#include "verdict_scorch.h"
#include "sight.h"
const VerdictScorches *story_ai_magistrate_scorches(void);

/* LINE OF SIGHT (founder: "also require line of site"). Hands the host's solid level boxes to the AI
 * brains, which copy them: perception (a body that is behind a wall is not seen, whatever the cone says)
 * and the verdict's floor hazard (a mark does not burn through a bench) both query this. Call once per
 * level load, from the host that owns the geometry -- apps/lobby does it inside its own
 * level_boxes_apply_to_physics. NULL / count 0 clears the world back to "everything is visible", which is
 * what a server or a test that has not federated its boxes keeps getting. Deliberately independent of
 * story_ai_reset: the geometry outlives the encounter, and re-installing it every reset would let a level
 * load and a respawn disagree about the same wall. */
void story_ai_set_sight(const SightBox *boxes, int count);
void story_ai_tick(ServerState *s, unsigned int now_ms);
/* story_ai_seed_voxworld_encounter (the old MODE_STORY VOXWORLD combat-squad encounter) was
   removed in the BIG_O engine merge phase 7d content cutover -- see
   packages/simulation/witness_ai.h's own witness_ai_seed_voxworld_encounter, its real
   replacement. story_ai.c's own AI_ROLE_* roster, story_ai_spawn_enemy, and the general
   LevelCharacter/NOCK-authoring spawn path above are untouched by this and stay fully live. */

/* story_ai_add_patrol_point -- appends one waypoint to the given AI's (by player_id, as returned
   from story_ai_spawn_enemy) patrol loop. Returns 1 on success, 0 if player_id doesn't resolve.
   Exposes ai_set_patrol (story_ai.c-private) to callers seeding encounters from outside this
   file, same pattern story_ai_trigger_scripted already establishes for player_id-addressed AI. */
int story_ai_add_patrol_point(int player_id, float x, float y, float z, unsigned int wait_ms, int hint);

/* story_ai_seed_voxworld_robots -- founder real-time (2026-09-25): bring the S470 wandering
   robots (AI_ROLE_WANDERING_BOT, wave-then-dance AI_MODE_GREET) back into VOXWORLD alongside the
   BIG_O-merge citizen/zombie roster, rather than restoring the deleted combat-squad encounter
   either replaced. Real, no-op outside SCENE_VOXWORLD -- see story_ai.c for the exact placement
   and why it doesn't overlap witness_ai_seed_voxworld_encounter's own footprint. */
void story_ai_seed_voxworld_robots(ServerState *s, unsigned int now_ms);

/* S461-03 -- groups already-spawned enemies (by the player_id story_ai_spawn_enemy returned)
   into one persistent squad. Assigns member_ai_index[0] as leader immediately. Returns the new
   squad's id, or -1 if the squad table is full, count is out of range, or any player_id doesn't
   resolve to an active AI. */
int story_ai_form_squad(const int *player_ids, int count);

/* S461-04 -- sends player_id into AI_MODE_SCRIPTED: walks to (x,y,z), holds there for hold_ms
   once arrived, then hands back to whatever mode it was in before the trigger (or AI_MODE_PATROL
   if that would be AI_MODE_SCRIPTED itself, e.g. a second trigger landing before the first
   resolved). Returns 1 on success, 0 if player_id doesn't resolve to an active AI. */
int story_ai_trigger_scripted(int player_id, float x, float y, float z, unsigned int hold_ms, unsigned int now_ms);

/* S461-01/S464 -- loads a real, author-placed waypoint/cover graph (NOCK level-editor authored,
   founder real-time: "we are going to need a waypoint system in the levels and maps northstar
   it" / "continue filling in the gaps in our level editor"), replacing whatever graph was there
   before. Flat parallel arrays, not a struct pointer -- story_ai.c/.h has zero dependency on
   packages/world/level_boxes.h's CustomLevelData by design, same established pattern
   server_apply_custom_level's own spawner unpacking already uses for
   phys_set_custom_level_spawners. neighbors_flat is count * STORY_AI_NAV_NEIGHBORS_STRIDE ints,
   node i's own neighbor list at neighbors_flat[i*STORY_AI_NAV_NEIGHBORS_STRIDE .. +stride) --
   only the first neighbor_counts[i] of that stride are read. */
#define STORY_AI_NAV_NEIGHBORS_STRIDE 4
void story_ai_load_nav_graph(int count, const float *x, const float *y, const float *z,
                              const int *is_cover, const float *cover_dir_x, const float *cover_dir_z,
                              const int *neighbor_counts, const int *neighbors_flat);

#endif
