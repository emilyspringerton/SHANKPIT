#ifndef STORY_AI_H
#define STORY_AI_H

#include "../common/protocol.h"
#include "humanness.h"
#include "ai_nav.h"

#define STORY_AI_MAX 16
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
    AI_MODE_GREET
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
    AI_ROLE_WANDERING_BOT
} AIRole;

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
int story_ai_spawn_enemy(ServerState *s, AIRole role, float x, float y, float z);
void story_ai_tick(ServerState *s, unsigned int now_ms);
void story_ai_seed_voxworld_encounter(ServerState *s);

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
