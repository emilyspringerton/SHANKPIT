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
    AI_MODE_SCRIPTED
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
    AI_ROLE_BOMBARDIER     /* WPN_MISSILE -- heavy, slow, real splash-AOE spammer */
} AIRole;

typedef struct {
    float x, y, z;
    unsigned int wait_ms;
    int behavior_hint;
} AIPatrolPoint;

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
} AIController;

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

#endif
