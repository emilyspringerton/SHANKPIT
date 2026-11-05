#include "wave_spawner.h"   /* the wave timer: numbers are PARENA wave_rules.prn */
#include "story_ai.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define STORY_AI_MAX_ATTACKERS 3
#define STORY_AI_HEARING_MEMORY_MS 2200U
#define STORY_AI_SEEN_MEMORY_MS 3200U
#define STORY_AI_SEARCH_MIN_MS 5000U
#define STORY_AI_SEARCH_VAR_MS 3000U
/* S461-01: real health-gated flee trigger. Threshold matches bot_client's own existing
   w_retreat convention ("health-aware retreat multiplier (activated when hp < 30%)",
   packages/common/protocol.h) rather than inventing a new number. Courage gate keeps the
   tank-identity roles (Brute 1.0, Bombardier 0.9, Hound 0.8) fighting through low health --
   only the roles whose own combat comments already lean on "keeps distance"/"backs away"
   (Trooper 0.55, Storm Caller 0.35, Guard's 0.5 default) actually flee. */
#define STORY_AI_FLEE_HEALTH_PCT 30
#define STORY_AI_FLEE_COURAGE_MAX 0.7f

/* S470 -- real, tuned-by-eye ambient-greet constants (founder real-time: "wave to the player when
   the player gets close and then dance before resuming patrol"). Radius comfortably inside
   ai_gather_perception's own vision_range values so a wandering bot notices the player at a
   normal walking approach, not at point-blank range. Cooldown prevents an immediate re-trigger
   the instant the greet ends while the player is still standing right there -- the bot resumes a
   real patrol leg first. Hold durations are picked against the real clip lengths exported for
   this pass (George/Stan/Mike/Leela's own Hello gesture runs ~1.5-1.9s at 30 ticks/sec, the
   mannequin's real Dance_Loop ~1.0s) so each phase plays at least one full loop, not a clipped
   fraction. */
#define STORY_AI_GREET_RADIUS 24.0f
#define STORY_AI_GREET_COOLDOWN_MS 9000U
#define STORY_AI_GREET_WAVE_MS 1800U
#define STORY_AI_GREET_DANCE_MS 2800U

/* AI_ROLE_MAGISTRATE -- the civic boss (see ai_combat_magistrate). These live here rather than in
   PARENA because they are AI behaviour numbers (sightline, cadence), the same vocabulary as the role
   table below; the unit's health, faction and orders still come from its EMPIRE archetype. The
   verdict range is longer than every clerk's attack range -- the bench sees the whole gallery -- and
   700 ms is a punctuation mark, not a death sentence. */
#define STORY_AI_MAGISTRATE_VERDICT_RANGE 130.0f
#define STORY_AI_MAGISTRATE_VERDICT_STUN_MS 700U
#define STORY_AI_MAGISTRATE_VERDICT_COOLDOWN_MS 5000U
/* The telegraph (founder: "telegraph verdict"). A sentence that simply appears on the tick the bench
   decides is unstoppable by reading the screen -- it can only be learned by being hit. So the
   verdict is announced and then served: 600 ms is longer than a human reaction plus a step (roughly
   250 ms plus the distance a body covers in a third of a second) and shorter than the fight's own
   rhythm, and breaking the line of sight inside it drops the sentence outright -- which turns the
   courthouse's single long sightline into the counter-play instead of decoration. */
#define STORY_AI_MAGISTRATE_VERDICT_TELL_MS 600U
/* A dropped sentence is not re-read instantly: the bench takes a moment to consider again, so
   stepping out and back in does not simply roll the same sentence at the next tick. */
#define STORY_AI_MAGISTRATE_VERDICT_RETRY_MS 800U

/* Presentation events for the verdict (see story_ai_magistrate_event_counts): monotonically counted
   here because the transitions happen inside this file, in one place, and re-deriving them from
   verdict_tell_until_ms on the client would mean guessing which edge a sampled frame landed on.
   Reset only by story_ai_reset -- a level load is a real reset, a respawn is not. */
static unsigned int g_magistrate_tells;
static unsigned int g_magistrate_served;

/* The served sentence's floor hazard (verdict_scorch.h). Sim-owned because a hazard that only exists
   in the renderer cannot hurt anyone, and because the damage has to go through the engine's own death
   and respawn path rather than a second one written in a draw call. */
static VerdictScorches g_magistrate_scorches;


static float ai_angle_diff(float a, float b) {
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

typedef struct {
    int visible;
    int heard;
    float dist;
    float to_player_x;
    float to_player_z;
} AIPerception;

static AIController g_story_ai[STORY_AI_MAX];

/* EMPIRE world -- resource nodes, claimable cells and the per-faction stockpiles, owned here because this
   is the module that ticks the units. See story_ai.h's own doc comments. */
static EUWorld g_empire_world;
static int ai_index_by_player_id(int player_id);
static AIWorldBlackboard g_story_bb;
static AIPerception g_story_perception[STORY_AI_MAX];
static unsigned int g_story_debug_next_log_ms = 0;
static AINavGraph g_story_nav; /* S461-01 -- real, hand-authored waypoint/cover graph, see
                                   docs2/specs/AI_WAYPOINT_NAV_NORTHSTAR.md. Author-placed by
                                   story_ai_load_nav_graph, consumed by any AI_ROLE_* NPC a NOCK
                                   level places (general infrastructure -- the old MODE_STORY
                                   VOXWORLD encounter that used to reference this here was removed
                                   in the BIG_O engine merge phase 7d content cutover). */
static AISquad g_story_squads[STORY_AI_SQUAD_MAX]; /* S461-03 -- real squad leader system */

/* LINE OF SIGHT (packages/simulation/sight.h, founder: "also require line of site"). The host hands over
   its solid level boxes once per level load; the COPY is deliberate -- a level can be freed or reloaded
   while the sim still holds queries, and 2048 boxes at load is cheaper than a dangling pointer at 60 Hz.
   No boxes installed means an open world, which is exactly the behaviour every existing test and the
   server have today. Eyes are at chest height on both bodies so a waist-high bench is cover for the
   floor and not for a person. */
#define STORY_AI_SIGHT_MAX      4096
#define STORY_AI_SIGHT_EYE_Y     1.25f
static SightBox g_sight_boxes[STORY_AI_SIGHT_MAX];
static int      g_sight_box_count;

void story_ai_set_sight(const SightBox *boxes, int count) {
    g_sight_box_count = 0;
    if (!boxes || count <= 0) return;
    if (count > STORY_AI_SIGHT_MAX) count = STORY_AI_SIGHT_MAX;
    memcpy(g_sight_boxes, boxes, sizeof(SightBox) * (size_t)count);
    g_sight_box_count = count;
}

static int ai_sight_clear(float x0, float y0, float z0, float x1, float y1, float z1) {
    return sight_segment_clear(g_sight_boxes, g_sight_box_count, x0, y0, z0, x1, y1, z1);
}

static float ai_len2(float x, float z) { return sqrtf(x * x + z * z); }
/* Yaw (degrees) that makes an entity FACE/MOVE toward (dx, dz) in the simulation's own yaw
 * convention -- the one shared_movement.h's shankpit_yaw_basis_from_deg and physics.h's hitscan /
 * melee code both use: forward = (-sin(yaw), -cos(yaw)), so yaw 0 looks down -Z.
 *
 * This used to be atan2f(dx, dz), i.e. forward = (+sin, +cos) -- exactly 180 degrees opposite. Every
 * story_ai NPC therefore walked, turned and aimed directly AWAY from whatever it was steering at
 * (found 2026-10-01 by MODE_TYLER's tests/tyler/test_tyler_vo.c: a scripted actor sent to z=-30
 * walked to z=+50 and pinned itself against the far wall; perception's FOV test compared two
 * values in the same wrong convention so it never noticed). Nothing in story_ai.c turns a yaw back
 * into a vector except through the sim's movement basis and physics.h, so this is the one place the
 * convention lives. */
static float ai_angle_to(float dx, float dz) { return atan2f(-dx, -dz) * (180.0f / 3.14159265f); }

static float ai_clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void ai_set_mode(AIController *ai, AIMode next_mode, unsigned int now_ms) {
    if (ai->mode == next_mode) return;
    /* A verdict is only read while the bench is still engaged with the player. Found live by this
       role's own telegraph test: losing sight drops the AI out of COMBAT, which means
       ai_combat_magistrate stops running and an armed tell would sit frozen at its last value and
       land whenever the player next walked back in -- a sentence served minutes after it was read.
       Leaving combat drops it here, at the one funnel every mode change already goes through, so no
       exit path can forget. Harmless dead data on every other role (always 0). */
    if (next_mode != AI_MODE_COMBAT) ai->verdict_tell_until_ms = 0;
    ai->previous_mode = ai->mode;
    ai->mode = next_mode;
    ai->mode_entered_ms = now_ms;
}


#if STORY_AI_DEBUG
static const char *ai_mode_name(AIMode mode) {
    switch (mode) {
        case AI_MODE_PATROL: return "PATROL";
        case AI_MODE_INVESTIGATE: return "INVESTIGATE";
        case AI_MODE_COMBAT: return "COMBAT";
        case AI_MODE_SEARCH: return "SEARCH";
        case AI_MODE_ALLY_FOLLOW: return "ALLY";
        case AI_MODE_FLEE: return "FLEE";
        case AI_MODE_LEASH_RETURN: return "LEASH_RETURN";
        case AI_MODE_GREET: return "GREET";
        case AI_MODE_EXPLORE: return "EXPLORE";
        case AI_MODE_GATHER: return "GATHER";
        case AI_MODE_CLAIM: return "CLAIM";
        default: return "DISABLED";
    }
}

static const char *ai_role_name(AIRole role) {
    switch (role) {
        case AI_ROLE_RIFT_HOUND: return "RIFT_HOUND";
        case AI_ROLE_SHAMBLER_TROOPER: return "SHAMBLER";
        case AI_ROLE_GORE_BRUTE: return "GORE_BRUTE";
        case AI_ROLE_STORY_ALLY: return "ALLY";
        case AI_ROLE_GUARD: return "GUARD";
        case AI_ROLE_STORM_CALLER: return "STORM_CALLER";
        case AI_ROLE_BOMBARDIER: return "BOMBARDIER";
        case AI_ROLE_RELENTLESS_PURSUER: return "PURSUER";
        case AI_ROLE_TERRITORIAL_BEAST: return "TERRITORIAL_BEAST";
        case AI_ROLE_BLIND_STALKER: return "BLIND_STALKER";
        case AI_ROLE_WANDERING_BOT: return "WANDERING_BOT";
        case AI_ROLE_MAGISTRATE: return "MAGISTRATE";
        default: return "UNKNOWN";
    }
}
#endif

static void ai_reset_input(PlayerState *p) {
    p->in_fwd = 0.0f;
    p->in_strafe = 0.0f;
    p->in_shoot = 0;
    p->in_reload = 0;
    p->in_jump = 0;
    p->in_use = 0;
    p->in_ability = 0;
    p->crouching = 0;
    p->anim_override = 0; /* S470 -- real per-tick reset; only ai_run_greet ever sets this non-zero */
}

/* Humanness Phase 2: ai_turn_towards now routes through humanness_smooth_turn_step
   (docs/HUMANNESS_NORTHSTAR.md) instead of a flat clamp-lerp -- real, mood-scaled turn speed
   and a real, occasional overshoot-then-settle, matching MISHRI's own real smoothTurn()
   behavior. max_turn_deg keeps its own existing real meaning (the per-call turn budget every
   caller already tunes -- 10.0f for the hound, 4.0f for the brute, etc.) by treating it as a
   real turn_speed_deg_per_sec with dt_seconds=1.0 -- so an unmodified (neutral-mood,
   not-mid-overshoot) call behaves identically to the old clamp, and humanness only ever adds
   real behavior on top, never silently changes the tuned baseline. ai may be NULL (a caller with
   no real AIController context, e.g. none in this file today, but kept honest rather than
   assumed) -- falls back to the old, exact clamp-lerp with no jitter in that case. */
static void ai_turn_towards(AIController *ai, PlayerState *p, float target_yaw, float max_turn_deg) {
    if (!ai) {
        float diff = ai_angle_diff(target_yaw, p->yaw);
        diff = ai_clamp(diff, -max_turn_deg, max_turn_deg);
        p->yaw += diff;
        return;
    }
    humanness_smooth_turn_step(&p->yaw, target_yaw, max_turn_deg, 1.0f, &ai->humanness, &ai->turn_overshooting);
}

/* S461-02 real arrival steering: when slow_radius > 0, speed scales down linearly as the AI
   closes inside it (classic seek+arrival, not a flat speed until collision with the stop
   check) -- floored at 0.15 rather than 0 so it still visibly closes the last few units instead
   of asymptotically crawling to a halt. slow_radius == 0 keeps the old constant-speed approach
   for callers where the target is a continuously-moving point (e.g. ai_run_search's orbit) or a
   repulsion vector rather than a real destination, where "arrival" doesn't mean anything. */
static void ai_move_towards(AIController *ai, PlayerState *p, float tx, float tz, float speed_scale, float turn_speed, float slow_radius) {
    float dx = tx - p->x;
    float dz = tz - p->z;
    float dist = ai_len2(dx, dz);
    float yaw = ai_angle_to(dx, dz);
    float arrival_scale = (slow_radius > 0.0f && dist < slow_radius) ? ai_clamp(dist / slow_radius, 0.15f, 1.0f) : 1.0f;
    ai_turn_towards(ai, p, yaw, turn_speed);
    p->in_fwd = ai_clamp(speed_scale * arrival_scale, -1.0f, 1.0f);
}

static void ai_assign_role_defaults(AIController *ai, PlayerState *p) {
    p->current_weapon = WPN_AR;
    ai->vision_range = 180.0f;
    ai->vision_fov_deg = 100.0f;
    ai->hearing_range = 64.0f;
    ai->attack_range = 26.0f;
    ai->preferred_range = 28.0f;
    ai->courage = 0.5f;
    ai->aggression = 0.6f;
    ai->aim_error_deg = 5.0f;
    ai->move_speed_scale = 0.7f;
    ai->leash_radius = 0.0f; /* S462 -- per-role default; only AI_ROLE_TERRITORIAL_BEAST sets one */

    switch (ai->role) {
        case AI_ROLE_RIFT_HOUND:
            p->current_weapon = WPN_KATANA;
            ai->vision_range = 165.0f;
            ai->vision_fov_deg = 120.0f;
            ai->hearing_range = 90.0f;
            ai->attack_range = 20.0f;
            ai->preferred_range = 12.0f;
            ai->courage = 0.8f;
            ai->aggression = 0.9f;
            ai->aim_error_deg = 8.0f;
            ai->move_speed_scale = 1.0f;
            break;
        case AI_ROLE_SHAMBLER_TROOPER:
            p->current_weapon = WPN_AR;
            ai->vision_range = 220.0f;
            ai->vision_fov_deg = 95.0f;
            ai->hearing_range = 100.0f;
            ai->attack_range = 72.0f;
            ai->preferred_range = 44.0f;
            ai->courage = 0.55f;
            ai->aggression = 0.65f;
            ai->aim_error_deg = 4.0f;
            ai->move_speed_scale = 0.72f;
            break;
        case AI_ROLE_GORE_BRUTE:
            p->current_weapon = WPN_SHOTGUN;
            ai->vision_range = 165.0f;
            ai->vision_fov_deg = 85.0f;
            ai->hearing_range = 90.0f;
            ai->attack_range = 24.0f;
            ai->preferred_range = 18.0f;
            ai->courage = 1.0f;
            ai->aggression = 0.85f;
            ai->aim_error_deg = 10.0f;
            ai->move_speed_scale = 0.5f;
            break;
        case AI_ROLE_STORY_ALLY:
            p->current_weapon = WPN_AR;
            ai->vision_range = 210.0f;
            ai->vision_fov_deg = 110.0f;
            ai->hearing_range = 120.0f;
            ai->attack_range = 65.0f;
            ai->preferred_range = 42.0f;
            ai->courage = 0.7f;
            ai->aggression = 0.55f;
            ai->aim_error_deg = 6.0f;
            ai->move_speed_scale = 0.78f;
            break;
        case AI_ROLE_GUARD:
            p->current_weapon = WPN_AR;
            ai->vision_range = 200.0f;
            ai->vision_fov_deg = 100.0f;
            ai->hearing_range = 90.0f;
            ai->attack_range = 52.0f;
            ai->preferred_range = 36.0f;
            ai->move_speed_scale = 0.65f;
            break;
        case AI_ROLE_STORM_CALLER:
            /* Long-range sniper threat -- keeps far back, real storm-charge
             * burst ability (100% the existing WPN_SNIPER mechanic, see
             * ai_combat_storm_caller). Lowest courage/aggression of the
             * roster on purpose: a storm caller that closes distance loses
             * its entire identity, so it should actively avoid doing that. */
            p->current_weapon = WPN_SNIPER;
            ai->vision_range = 320.0f;
            ai->vision_fov_deg = 90.0f;
            ai->hearing_range = 70.0f;
            ai->attack_range = 260.0f;
            ai->preferred_range = 200.0f;
            ai->courage = 0.35f;
            ai->aggression = 0.45f;
            ai->aim_error_deg = 2.0f;
            ai->move_speed_scale = 0.55f;
            break;
        case AI_ROLE_BOMBARDIER:
            /* Heavy AOE spammer -- WPN_MISSILE as the BASE weapon (not an
             * ability), so every shot is already a real splash-damage hit
             * via the same spawn_projectile/explode_splash pipeline Frag
             * Toss/Ground Slam reuse. Slow and low-FOV on purpose (a heavy
             * unit that also tracks/moves like a Rift Hound would be
             * unfair, not "challenging" in an interesting way) -- the real
             * threat is area denial, not precision or speed. */
            p->current_weapon = WPN_MISSILE;
            ai->vision_range = 200.0f;
            ai->vision_fov_deg = 80.0f;
            ai->hearing_range = 80.0f;
            ai->attack_range = 140.0f;
            ai->preferred_range = 100.0f;
            ai->courage = 0.9f;
            ai->aggression = 0.5f;
            ai->aim_error_deg = 9.0f;
            ai->move_speed_scale = 0.42f;
            break;
        case AI_ROLE_RELENTLESS_PURSUER:
            /* "Zombie / Relentless Pursuer" -- founder real-time: "Distance Thresholds & Direct
             * Lines... Paths straight toward the player's last known position. Ignores personal
             * safety or tactical positioning." Melee-only, attack_range == preferred_range (the
             * combat function never backs off, no kiting), maximum courage/aggression -- courage
             * 1.0 also clears S461-01's flee gate (courage < 0.7) outright, which is the whole
             * point of this archetype: it does not retreat, ever. */
            p->current_weapon = WPN_KNIFE;
            ai->vision_range = 150.0f;
            ai->vision_fov_deg = 110.0f;
            ai->hearing_range = 110.0f;
            ai->attack_range = 16.0f;
            ai->preferred_range = 16.0f;
            ai->courage = 1.0f;
            ai->aggression = 1.0f;
            ai->aim_error_deg = 12.0f;
            ai->move_speed_scale = 0.62f;
            break;
        case AI_ROLE_TERRITORIAL_BEAST:
            /* "Territorial Beast" -- founder real-time: "Radius Anchoring & Return Leashes...
             * Patrols a fixed vector coordinate. Aggros if the player enters their sphere, but
             * retreats if pulled too far away." leash_radius is this role's own real default;
             * story_ai_spawn_enemy sets home_x/y/z to the real spawn position for every role
             * (a no-op for the other two new roles below, whose leash_radius stays 0). */
            p->current_weapon = WPN_SHOTGUN;
            ai->vision_range = 170.0f;
            ai->vision_fov_deg = 130.0f;
            ai->hearing_range = 95.0f;
            ai->attack_range = 22.0f;
            ai->preferred_range = 16.0f;
            ai->courage = 0.75f;
            ai->aggression = 0.8f;
            ai->aim_error_deg = 9.0f;
            ai->move_speed_scale = 0.68f;
            ai->leash_radius = 120.0f;
            break;
        case AI_ROLE_BLIND_STALKER:
            /* "Ambush Predator" + "Sightless Echo-Locator" combined -- the founder's own two
             * sections both center on the same real behavior (charge off non-visual detection),
             * treated here as one archetype rather than two. Near-zero vision_range means
             * ai_gather_perception's own existing dist<=vision_range check almost never passes --
             * this role is functionally blind and detects almost entirely through the existing
             * hearing_range/recently-fired path, zero new perception code needed for that part. */
            p->current_weapon = WPN_KATANA;
            ai->vision_range = 8.0f;
            ai->vision_fov_deg = 360.0f; /* irrelevant at this range -- left wide rather than tuned */
            ai->hearing_range = 150.0f;
            ai->attack_range = 20.0f;
            ai->preferred_range = 20.0f;
            ai->courage = 0.9f;
            ai->aggression = 0.95f;
            ai->aim_error_deg = 7.0f;
            ai->move_speed_scale = 0.9f;
            break;
        case AI_ROLE_MAGISTRATE:
            /* The bench. WPN_MAGNUM is the one weapon no other role in the roster carries (45 a
               round, eight in the cylinder), so the boss reads differently the moment it fires; the
               long attack_range and the high preferred_range make it hold the far end of the gallery
               instead of wading in. courage 1.0 keeps S461-01's flee gate shut -- a boss that runs at
               29 health is a clerk with a title. aim_error_deg is the lowest in the roster on
               purpose: the verdict below does the missing, not the spray. */
            p->current_weapon = WPN_MAGNUM;
            ai->vision_range = 240.0f;
            ai->vision_fov_deg = 110.0f;
            ai->hearing_range = 100.0f;
            ai->attack_range = 120.0f;
            ai->preferred_range = 90.0f;
            ai->courage = 1.0f;
            ai->aggression = 0.7f;
            ai->aim_error_deg = 3.0f;
            ai->move_speed_scale = 0.45f;
            break;
        case AI_ROLE_WANDERING_BOT:
            /* S470 -- real, deliberately harmless: never fires (no combat function is ever
               dispatched for this role, see story_ai_tick's own decision-loop bypass), so
               weapon/attack_range/aim are dead data here, kept only because
               ai_assign_role_defaults sets every field unconditionally before this switch and
               leaving them at their generic defaults is honest rather than a fabricated
               "0 means something" convention. vision/hearing_range stay at the generic default
               too -- proximity-to-player detection for AI_MODE_GREET is a plain distance check
               against STORY_AI_GREET_RADIUS, not perception-based, so these values are never
               actually read for this role either. */
            ai->move_speed_scale = 0.55f;
            break;
        default:
            break;
    }
}

void story_ai_reset(ServerState *s) {
    int i;
    memset(g_story_ai, 0, sizeof(g_story_ai));
    memset(&g_story_bb, 0, sizeof(g_story_bb));
    memset(g_story_perception, 0, sizeof(g_story_perception));
    g_story_debug_next_log_ms = 0;
    ai_nav_reset(&g_story_nav);
    memset(g_story_squads, 0, sizeof(g_story_squads));
    g_magistrate_tells = 0;
    g_magistrate_served = 0;
    verdict_scorches_reset(&g_magistrate_scorches);
    if (!s) return;
    for (i = 1; i < MAX_CLIENTS; i++) {
        s->players[i].active = 0;
        s->players[i].is_bot = 1;
        s->players[i].team_id = -1;
        s->players[i].scene_id = s->scene_id;
        s->players[i].carried_flag_team_id = -1;
        s->players[i].state = STATE_ALIVE;
    }
    /* A level's authored EMPIRE units need ground to take and something to gather, so a reset seeds the
       default contested ring. Deliberately at reset and not at spawn: two units spawned a second apart must
       see the same world, and re-seeding per spawn would reset faction stockpiles under them. */
    story_ai_empire_seed_default();
}

// story_ai_despawn_all_characters -- see story_ai.h's own doc comment for the full rationale.
void story_ai_despawn_all_characters(ServerState *s) {
    int i;
    if (!s) return;
    for (i = 0; i < STORY_AI_MAX; i++) {
        if (!g_story_ai[i].active) continue;
        int pid = g_story_ai[i].player_id;
        if (pid >= 1 && pid < MAX_CLIENTS) {
            s->players[pid].active = 0;
            s->players[pid].is_bot = 1;
            s->players[pid].team_id = -1;
            s->players[pid].carried_flag_team_id = -1;
            s->players[pid].state = STATE_ALIVE;
        }
        memset(&g_story_ai[i], 0, sizeof(g_story_ai[i]));
        memset(&g_story_perception[i], 0, sizeof(g_story_perception[i]));
    }
}

int story_ai_spawn_enemy(ServerState *s, AIRole role, int kit, float x, float y, float z) {
    int slot = -1;
    int i;
    AIController *ai = NULL;
    PlayerState *p;
    if (!s) return -1;

    for (i = 1; i < MAX_CLIENTS; i++) {
        if (!s->players[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    for (i = 0; i < STORY_AI_MAX; i++) {
        if (!g_story_ai[i].active) { ai = &g_story_ai[i]; break; }
    }
    if (!ai) return -1;

    p = &s->players[slot];
    memset(ai, 0, sizeof(*ai));
    p->active = 1;
    p->is_bot = 1;
    p->scene_id = s->scene_id;
    p->team_id = -1;
    p->state = STATE_ALIVE;
    p->health = 100;
    p->shield = 0;
    p->x = x;
    p->z = z;
    p->y = y;
    p->vx = p->vy = p->vz = 0.0f;
    p->yaw = 180.0f;
    p->pitch = 0.0f;
    p->carried_flag_team_id = -1;
    p->forced_kit = kit;

    ai->active = 1;
    ai->player_id = slot;
    ai->role = role;
    ai->mode = (role == AI_ROLE_STORY_ALLY) ? AI_MODE_ALLY_FOLLOW : AI_MODE_PATROL;
    ai->previous_mode = AI_MODE_DISABLED;
    ai->target_player_id = 0;
    ai->last_known_x = s->players[0].x;
    ai->last_known_y = s->players[0].y;
    ai->last_known_z = s->players[0].z;
    ai->next_decision_ms = 0;
    ai->next_attack_ms = 0;
    humanness_state_init(&ai->humanness, 0); /* Humanness Phase 2 -- real, fresh per-instance state */
    ai->turn_overshooting = 0;
    ai->flee_target_node = -1; /* S461-01 -- not yet computed */
    ai->flee_path_len = 0;
    ai->flee_path_index = 0;
    ai->squad_id = -1; /* S461-03 -- not in a squad until story_ai_form_squad joins one */
    ai->squad_role = SQUAD_ROLE_NONE;
    ai->home_x = x; /* S462 -- every role's real spawn position; only load-bearing when */
    ai->home_y = y; /* leash_radius > 0 (AI_ROLE_TERRITORIAL_BEAST's own default) */
    ai->home_z = z;
    ai_assign_role_defaults(ai, p);

    return slot;
}

/* ---- EMPIRE units ----------------------------------------------------------------------------- */

EUWorld *story_ai_empire_world(void) { return &g_empire_world; }

void story_ai_empire_seed_default(void) {
    eu_world_clear(&g_empire_world);
    int cap = g_eu_rules.node_capacity();
    /* Six nodes on a 40-unit ring, twelve claim cells on a 70-unit ring: close enough that a unit walks to
       one in a few seconds, far enough apart that two factions do not silently share a node. */
    for (int i = 0; i < 6; i++) {
        float a = (float)i * 1.0471976f; /* 60 degrees */
        eu_world_add_node(&g_empire_world, cosf(a) * 40.0f, sinf(a) * 40.0f, cap);
    }
    for (int i = 0; i < 12; i++) {
        float a = (float)i * 0.5235988f; /* 30 degrees */
        eu_world_add_claim_cell(&g_empire_world, cosf(a) * 70.0f, sinf(a) * 70.0f);
    }
}

int story_ai_empire_load_level(int node_count, const float *node_x, const float *node_z,
                               const int *node_amount, int cell_count, const float *cell_x,
                               const float *cell_z, const int *cell_radius) {
    /* Absent keys stay absent: a level with no authored ground keeps the default ring rather than
       silently becoming an EMPIRE-free world (every order would then fall back to GUARD). */
    if (node_count <= 0 && cell_count <= 0) {
        story_ai_empire_seed_default();
        return 0;
    }
    return eu_world_load(&g_empire_world, node_count, node_x, node_z, node_amount,
                         cell_count, cell_x, cell_z, cell_radius);
}

int story_ai_empire_unit_count(void) {
    int n = 0;
    for (int i = 0; i < STORY_AI_MAX; i++)
        if (g_story_ai[i].active && g_story_ai[i].archetype != EU_ARCH_NONE) n++;
    return n;
}

/* story_ai_bank_handins -- a unit that walked a full node home hands it in, and the caller gets the units.
 *
 * The influence economy's own numbers live in PARENA (`empire-bank-credit`), but this file is linked into
 * the SERVER, whose source list does not carry empire_rules.c -- so what this returns is UNITS BANKED, not
 * influence, and the host that owns the pool converts it (apps/lobby's lobby_empire_sync calls
 * empire_bank_credit with its own balance and cap). Splitting it there is also what keeps the hand-in
 * testable without a display: who is carrying, and when they stop, is this function.
 *
 * Hand-in happens at the unit's OWN spawn point, because the return trip is the whole point of gathering --
 * crediting at pickup would make the walk home cosmetic. A unit still walking is left alone, and the
 * faction is a parameter because two sides can be hauling at once. */
int story_ai_bank_handins(ServerState *s, int faction) {
    if (!s) return 0;
    int banked = 0;
    float hand_in = (float)g_eu_rules.follow_distance();
    int slots = (int)(sizeof(s->players) / sizeof(s->players[0]));
    for (int i = 0; i < STORY_AI_MAX; i++) {
        AIController *ai = &g_story_ai[i];
        if (!ai->active || ai->archetype == EU_ARCH_NONE) continue;
        if (ai->eu.faction != faction || ai->eu.carrying <= 0) continue;
        if (ai->player_id < 0 || ai->player_id >= slots) continue;
        PlayerState *p = &s->players[ai->player_id];
        if (eu_dist2(p->x, p->z, ai->home_x, ai->home_z) > hand_in * hand_in) continue;
        banked += ai->eu.carrying;
        ai->eu.carrying = 0;
    }
    return banked;
}

/* ---- survival waves ---------------------------------------------------------------------------------
 * The timer is wave_spawner.h (headless, `make test-wave-spawner`); the numbers are PARENA's
 * (wave_rules.prn); the bodies are this file's. Disarmed until a host arms it, because a level that never
 * had waves must not grow them.
 *
 * A wave appears on a ring around the world origin, not around the player: a wave that arrives on top of
 * you is a trap rather than a fight, and the ring is where a survival mode's approach is legible. */
static WaveRunner g_wave_runner;

void story_ai_waves_enable(int on, unsigned int now_ms) {
    g_wave_runner.enabled = on ? 1 : 0;
    wave_runner_reset(&g_wave_runner, (int)now_ms);
}

int story_ai_waves_tick(ServerState *s, unsigned int now_ms) {
    if (!s) return 0;
    int size = wave_runner_tick(&g_wave_runner, (int)now_ms, story_ai_empire_unit_count());
    if (size <= 0) return 0;
    int arch = g_wr_rules.archetype();
    float radius = (float)g_wr_rules.spawn_radius_milli() / 1000.0f;
    int spawned = 0;
    for (int i = 0; i < size; i++) {
        /* Offset by half a step so an even-sized wave does not put two bodies on the same bearing. */
        float ang = 6.28318531f * ((float)i + 0.5f) / (float)size;
        if (story_ai_spawn_unit(s, arch, 0, -1, -1, cosf(ang) * radius, 8.0f, sinf(ang) * radius) >= 0)
            spawned++;
    }
    return spawned;
}

int story_ai_waves_remaining(void) { return wave_runner_remaining(&g_wave_runner); }

/* ---- downed allies -----------------------------------------------------------------------------------
 * A dead ALLY is not gone. physics puts it in STATE_DEAD with a respawn timer; this holds that timer off
 * while the body is on the ground, so a companion can reach it, and lets go when the bleed-out window
 * expires. Reviving is a HOLD, not a touch (g_eu_rules.revive_hold_ms), and it restores a fraction of the
 * archetype's pool rather than healing it -- the numbers are PARENA's empire_rules.prn, reached through
 * empire_units_rules.h so the server keeps linking.
 *
 * ALLIES ONLY, on purpose: an enemy that lingered would be a corpse with a countdown, and every existing
 * level's zombies must keep dying and respawning exactly as they did. */
static unsigned int g_downed_last_ms = 0;

static void story_ai_downed_tick(ServerState *s, unsigned int now_ms) {
    if (!s) return;
    int slots = (int)(sizeof(s->players) / sizeof(s->players[0]));
    PlayerState *hero = &s->players[0];
    unsigned int elapsed = now_ms - g_downed_last_ms;
    int dt = elapsed > 1000u ? 1000 : (int)elapsed;   /* unsigned subtraction: wrap-safe, hitch-capped */
    g_downed_last_ms = now_ms;
    for (int i = 0; i < STORY_AI_MAX; i++) {
        AIController *ai = &g_story_ai[i];
        if (!ai->active || ai->archetype == EU_ARCH_NONE) continue;
        if (ai->faction != EU_FACTION_ALLY) continue;
        if (ai->player_id < 0 || ai->player_id >= slots) continue;
        PlayerState *p = &s->players[ai->player_id];
        if (p->state != STATE_DEAD) {
            /* Up again -- by a revive below, or by physics' own respawn after a bleed-out. */
            ai->eu.downed_ms = 0;
            ai->eu.revive_ms = 0;
            continue;
        }
        if (!g_eu_rules.downable(ai->archetype)) continue;
        if (!eu_unit_is_downed(&ai->eu)) eu_unit_start_bleed(&ai->eu);
        else eu_unit_bleed_advance(&ai->eu, dt);
        if (ai->eu.downed_ms >= g_eu_rules.bleedout_ms()) {
            /* Written off: stop holding the respawn and let the body go. */
            eu_unit_revive(&ai->eu);
            continue;
        }
        /* Hold the body's own respawn so it is still there for a companion to reach. */
        p->respawn_time = now_ms + 1000;
        float reach = (float)g_eu_rules.revive_radius_milli() / 1000.0f;
        if (ai_len2(hero->x - p->x, hero->z - p->z) > reach) {
            ai->eu.revive_ms = 0;   /* left the body: the hold starts over */
            continue;
        }
        ai->eu.revive_ms += dt;
        if (ai->eu.revive_ms >= g_eu_rules.revive_hold_ms()) {
            int hp = eu_unit_revive_health(&ai->eu, g_eu_rules.revive_health_milli());
            /* Stand up WHERE THEY FELL. This deliberately does not go through phys_respawn: that is the
               respawn path, and it moves the body to a spawn point -- a revive that teleports the ally
               across the level is not a rescue. (This call was also compiled with no declaration in this
               TU, which -Wimplicit-function-declaration reports and which is why it was reachable at all
               without anyone noticing the teleport.) */
            p->state = STATE_ALIVE;
            p->health = hp;
            p->vx = p->vy = p->vz = 0.0f;
            p->respawn_time = 0;
            ai->eu.order = EU_ORDER_NONE;
            eu_unit_revive(&ai->eu);
            printf("[EMPIRE] ally %d revived at %d hp\n", ai->player_id, hp);
        }
    }
}

int story_ai_spawn_unit(ServerState *s, int archetype, int kit, int faction, int orders,
                        float x, float y, float z) {
    return story_ai_spawn_unit_role(s, archetype, -1, kit, faction, orders, x, y, z);
}

/* story_ai_spawn_unit_role -- the same spawn with an AUTHORED brain role laid over the archetype's.
 *
 * EMPIRE's rule is "archetype picks the brain role", and it stays the default; this is the level
 * editor's other half. A level character already carries a `role` (level_boxes.h) and both hosts
 * validate it -- but an archetype-authored body threw it away, so every clerk in a civic district was
 * the same pursuer whatever the level said. With the override one EMPIRE layer can staff a sniper
 * archivist and a knife orderly, while the archetype still owns the health pool, the skin default and
 * the orders the unit will accept.
 *
 *   role: a real AIRole (story_ai_role_is_valid -- the range now includes the civic boss,
 *         AI_ROLE_MAGISTRATE), or < 0 for "the archetype's own".
 *         An out-of-range number is IGNORED rather than clamped: a stale reference must not silently
 *         become RIFT_HOUND, which is exactly what a clamp would do.
 */
int story_ai_spawn_unit_role(ServerState *s, int archetype, int role, int kit, int faction, int orders,
                             float x, float y, float z) {
    if (!s) return -1;
    EUUnit probe;
    eu_unit_init(&probe, archetype, kit, faction, orders);
    if (probe.archetype == EU_ARCH_NONE) return -1;
    if (story_ai_role_is_valid(role)) probe.role = role;
    int pid = story_ai_spawn_enemy(s, (AIRole)probe.role, probe.kit, x, y, z);
    if (pid < 0) return -1;
    int idx = ai_index_by_player_id(pid);
    if (idx < 0) return -1;
    AIController *ai = &g_story_ai[idx];
    ai->archetype = probe.archetype;
    ai->faction = probe.faction;
    ai->eu = probe;
    ai->last_order_ms = 0;
    /* The archetype's health pool is the point of an archetype (an ally outlasts a zombie). The brain
       defaults above already set the weapon/senses for the role it picked. */
    s->players[pid].health = probe.health;
    /* A spawned body arrives with a LOADED weapon. Every other path that puts a gun in a hand fills the
       magazine -- phys_respawn does it for every respawn, the lobby does it for a joining player -- and
       this one did not: spawn_enemy memsets the slot, so ammo[] stayed zero. The brain then pulled the
       trigger on a rifle it had no rounds for, and update_weapons (physics.h) answered the only way it
       can: "empty, begin a full reload", every time, forever, because the same zero ammo starts the
       next one. No brain-level test could see that -- in_shoot is intent, and the intent was always
       correct -- which is exactly why the district's end-to-end test drives the weapon tick. */
    for (int wp = 0; wp < MAX_WEAPONS; wp++) s->players[pid].ammo[wp] = WPN_STATS[wp].ammo_max;
    return pid;
}

int story_ai_unit_order(int player_id) {
    int idx = ai_index_by_player_id(player_id);
    if (idx < 0 || g_story_ai[idx].archetype == EU_ARCH_NONE) return -1;
    return g_story_ai[idx].eu.order;
}

int story_ai_unit_archetype(int player_id) {
    int idx = ai_index_by_player_id(player_id);
    return idx < 0 ? -1 : g_story_ai[idx].archetype;
}

// story_ai_role_slot -- see story_ai.h's own doc comment. A plain scan rather than
// ai_index_by_player_id because the caller starts from the role, not from a slot.
int story_ai_role_slot(int role) {
    int i;
    for (i = 0; i < STORY_AI_MAX; i++) {
        if (g_story_ai[i].active && (int)g_story_ai[i].role == role) return g_story_ai[i].player_id;
    }
    return -1;
}

// story_ai_magistrate_scorches -- see story_ai.h's own doc comment.
const VerdictScorches *story_ai_magistrate_scorches(void) {
    return &g_magistrate_scorches;
}

// story_ai_magistrate_event_counts -- see story_ai.h's own doc comment.
void story_ai_magistrate_event_counts(unsigned int *tells, unsigned int *served) {
    if (tells) *tells = g_magistrate_tells;
    if (served) *served = g_magistrate_served;
}

// story_ai_verdict_tell_fill -- see story_ai.h's own doc comment.
float story_ai_verdict_tell_fill(int player_id, unsigned int now_ms) {
    int i;
    for (i = 0; i < STORY_AI_MAX; i++) {
        if (!g_story_ai[i].active || g_story_ai[i].player_id != player_id) continue;
        if (g_story_ai[i].role != AI_ROLE_MAGISTRATE) return -1.0f;
        if (!g_story_ai[i].verdict_tell_until_ms) return -1.0f;
        if (now_ms >= g_story_ai[i].verdict_tell_until_ms) return 1.0f;
        return ai_clamp(1.0f - (float)(g_story_ai[i].verdict_tell_until_ms - now_ms) /
                                 (float)STORY_AI_MAGISTRATE_VERDICT_TELL_MS, 0.0f, 1.0f);
    }
    return -1.0f;
}

static void ai_set_patrol(AIController *ai, int idx, float x, float y, float z, unsigned int wait_ms, int hint) {
    if (!ai || idx < 0 || idx >= STORY_AI_PATROL_MAX_POINTS) return;
    ai->patrol[idx].x = x;
    ai->patrol[idx].y = y;
    ai->patrol[idx].z = z;
    ai->patrol[idx].wait_ms = wait_ms;
    ai->patrol[idx].behavior_hint = hint;
    if (ai->patrol_count < idx + 1) ai->patrol_count = idx + 1;
}

static int ai_index_by_player_id(int player_id) {
    int i;
    for (i = 0; i < STORY_AI_MAX; i++) {
        if (g_story_ai[i].active && g_story_ai[i].player_id == player_id) return i;
    }
    return -1;
}

/* S461-03: position 0 in a squad's member list is always the leader; positions 1+ cycle through
   the three real combat-role biases ai_squad_apply_role_bias actually implements. */
static AISquadRole ai_squad_role_for_position(int pos) {
    if (pos == 0) return SQUAD_ROLE_LEADER;
    switch ((pos - 1) % 3) {
        case 0: return SQUAD_ROLE_FLANK_LEFT;
        case 1: return SQUAD_ROLE_FLANK_RIGHT;
        default: return SQUAD_ROLE_SUPPRESS;
    }
}

/* story_ai_seed_voxworld_robots -- brings back the S470 wandering-robot ambient encounter
   (AI_ROLE_WANDERING_BOT / AI_MODE_GREET) alongside the BIG_O-merge citizen/zombie roster
   (witness_ai_seed_voxworld_encounter) rather than restoring the deleted combat-squad encounter
   they replaced. Placed in VOXWORLD's northeast quadrant, clear of witness_ai's citizen/zombie/
   Men/lab/food-pickup footprint (all south of cz=-260) so the two systems never overlap. Five
   robots, one per real GOLDENBAND kit (George/Leela/Mike/Stan + mannequin), each on a small
   patrol loop so AI_MODE_GREET's wave-then-dance still fires when the player walks up. */
void story_ai_seed_voxworld_robots(ServerState *s, unsigned int now_ms) {
    static const float bots[5][2] = {
        { -30.0f,  40.0f },
        {  10.0f,  55.0f },
        {  45.0f,  30.0f },
        {   0.0f,  15.0f },
        { -50.0f,  15.0f }
    };
    int i;
    (void)now_ms;
    if (!s || s->scene_id != SCENE_VOXWORLD) return;

    for (i = 0; i < 5; i++) {
        float x = bots[i][0];
        float z = bots[i][1];
        int pid = story_ai_spawn_enemy(s, AI_ROLE_WANDERING_BOT, AI_KIT_AUTO, x, 8.0f, z);
        if (pid < 0) continue;
        story_ai_add_patrol_point(pid, x - 8.0f, 8.0f, z, 2000, 0);
        story_ai_add_patrol_point(pid, x + 8.0f, 8.0f, z + 6.0f, 2000, 0);
        story_ai_add_patrol_point(pid, x, 8.0f, z - 6.0f, 2000, 0);
    }
    printf("[STORY_AI] voxworld robots seeded: 5 wandering bots\n");
}

int story_ai_add_patrol_point(int player_id, float x, float y, float z, unsigned int wait_ms, int hint) {
    int idx = ai_index_by_player_id(player_id);
    if (idx < 0) return 0;
    ai_set_patrol(&g_story_ai[idx], g_story_ai[idx].patrol_count, x, y, z, wait_ms, hint);
    return 1;
}

int story_ai_form_squad(const int *player_ids, int count) {
    int slot = -1;
    int i;
    AISquad *sq;
    if (!player_ids || count <= 0 || count > STORY_AI_SQUAD_MAX_MEMBERS) return -1;
    for (i = 0; i < STORY_AI_SQUAD_MAX; i++) {
        if (!g_story_squads[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    sq = &g_story_squads[slot];
    memset(sq, 0, sizeof(*sq));
    for (i = 0; i < count; i++) {
        int idx = ai_index_by_player_id(player_ids[i]);
        if (idx < 0) return -1;
        sq->member_ai_index[sq->member_count++] = idx;
    }
    sq->active = 1;
    for (i = 0; i < sq->member_count; i++) {
        AIController *ai = &g_story_ai[sq->member_ai_index[i]];
        ai->squad_id = slot;
        ai->squad_role = ai_squad_role_for_position(i);
    }
    return slot;
}

/* S461-03: real, live re-evaluation, called once per tick before combat dispatch. Compacting
   member_ai_index in place (dropping dead/inactive members, keeping the survivors' relative
   order) means position 0 is automatically whichever survivor was highest-priority before the
   leader died -- promotion falls out of the same reassignment loop that recycles flank/suppress
   roles, not a separate special case. Matches the founder's own real-time spec: "the Squad class
   instantly re-evaluates and promotes ... to change their auto-pilot state dynamically." */
static void ai_squad_reevaluate(ServerState *s) {
    int qi, i, w;
    for (qi = 0; qi < STORY_AI_SQUAD_MAX; qi++) {
        AISquad *sq = &g_story_squads[qi];
        if (!sq->active) continue;
        w = 0;
        for (i = 0; i < sq->member_count; i++) {
            int idx = sq->member_ai_index[i];
            AIController *ai = &g_story_ai[idx];
            PlayerState *p = &s->players[ai->player_id];
            if (!ai->active || !p->active || p->state == STATE_DEAD) {
                ai->squad_id = -1;
                ai->squad_role = SQUAD_ROLE_NONE;
                continue;
            }
            sq->member_ai_index[w++] = idx;
        }
        sq->member_count = w;
        if (sq->member_count == 0) {
            sq->active = 0;
            continue;
        }
        for (i = 0; i < sq->member_count; i++) {
            g_story_ai[sq->member_ai_index[i]].squad_role = ai_squad_role_for_position(i);
        }
    }
}

/* S461-03: layered ON TOP of whatever the per-role combat function (ai_combat_hound/trooper/
   etc.) already set, same "real behavior added on top, baseline unchanged for non-squad AIs"
   pattern humanness.c's own turn/aim jitter already uses. Squad role only biases positioning,
   never overrides the per-role weapon/ability logic those functions already own. */
static void ai_squad_apply_role_bias(const AIController *ai, PlayerState *p) {
    if (ai->squad_id < 0) return;
    switch (ai->squad_role) {
        case SQUAD_ROLE_FLANK_LEFT:
            p->in_strafe = ai_clamp(-fabsf(p->in_strafe) - 0.25f, -1.0f, 1.0f);
            break;
        case SQUAD_ROLE_FLANK_RIGHT:
            p->in_strafe = ai_clamp(fabsf(p->in_strafe) + 0.25f, -1.0f, 1.0f);
            break;
        case SQUAD_ROLE_SUPPRESS:
            /* Real suppressing fire: hold ground and keep shooting (in_shoot is already set by
               the per-role combat function above) rather than closing distance. */
            p->in_fwd *= 0.35f;
            p->in_strafe *= 0.4f;
            break;
        default:
            break;
    }
}

static int ai_player_recently_fired(const PlayerState *player) {
    return player->is_shooting > 0 || player->in_shoot;
}

static void ai_gather_perception(ServerState *s, AIController *ai, AIPerception *out, unsigned int now_ms) {
    PlayerState *bot = &s->players[ai->player_id];
    PlayerState *hero = &s->players[0];
    float dx = hero->x - bot->x;
    float dz = hero->z - bot->z;
    float dist = ai_len2(dx, dz);
    float to_yaw = ai_angle_to(dx, dz);
    float fov_half = ai->vision_fov_deg * 0.5f;
    float diff = fabsf(ai_angle_diff(to_yaw, bot->yaw));
    int in_fov = diff <= fov_half;

    memset(out, 0, sizeof(*out));
    out->dist = dist;
    out->to_player_x = dx;
    out->to_player_z = dz;

    /* SIGHT IS NOT RANGE + CONE. The cheap FOV/range rejection stays first, then the geometry is asked:
       a clerk does not see through the courthouse wall just because the hero is inside its cone. */
    if (dist <= ai->vision_range && in_fov && hero->state != STATE_DEAD &&
        ai_sight_clear(bot->x, bot->y + STORY_AI_SIGHT_EYE_Y, bot->z,
                       hero->x, hero->y + STORY_AI_SIGHT_EYE_Y, hero->z)) {
        out->visible = 1;
        ai->last_seen_ms = now_ms;
        ai->last_known_x = hero->x;
        ai->last_known_y = hero->y;
        ai->last_known_z = hero->z;
    }

    if (!out->visible && hero->state != STATE_DEAD) {
        if (dist <= ai->hearing_range || (dist <= ai->hearing_range * 1.6f && ai_player_recently_fired(hero))) {
            out->heard = 1;
            ai->last_heard_ms = now_ms;
            ai->last_known_x = hero->x;
            ai->last_known_y = hero->y;
            ai->last_known_z = hero->z;
        }
    }
}

static void ai_run_patrol(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    AIPatrolPoint *pt;
    float dx, dz, dist;
    if (ai->patrol_count <= 0) {
        p->in_fwd = 0.2f;
        p->in_strafe = 0.2f;
        p->yaw += 0.35f;
        return;
    }

    if (ai->patrol_index >= ai->patrol_count) ai->patrol_index = 0;
    pt = &ai->patrol[ai->patrol_index];
    dx = pt->x - p->x;
    dz = pt->z - p->z;
    dist = ai_len2(dx, dz);

    if (dist < 4.0f) {
        if (ai->wait_until_ms == 0) {
            ai->wait_until_ms = now_ms + pt->wait_ms;
        }
        if (now_ms >= ai->wait_until_ms) {
            ai->wait_until_ms = 0;
            ai->patrol_index = (ai->patrol_index + 1) % ai->patrol_count;
        } else {
            p->in_fwd = 0.0f;
            p->in_strafe = 0.12f;
            p->yaw += 0.55f;
            return;
        }
    }

    ai_move_towards(ai, p, pt->x, pt->z, 0.45f * ai->move_speed_scale, 4.0f, 8.0f);
}

static void ai_run_investigate(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    float dx = ai->last_known_x - p->x;
    float dz = ai->last_known_z - p->z;
    float dist = ai_len2(dx, dz);
    (void)now_ms;
    ai_move_towards(ai, p, ai->last_known_x, ai->last_known_z, 0.65f * ai->move_speed_scale, 6.0f, 10.0f);
    if (dist < 7.0f) ai_set_mode(ai, AI_MODE_SEARCH, now_ms);
}

static void ai_run_search(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    float t = (float)(now_ms - ai->mode_entered_ms) * 0.001f;
    float orbit_x = ai->last_known_x + cosf(t + (float)ai->player_id) * 10.0f;
    float orbit_z = ai->last_known_z + sinf(t + (float)ai->player_id) * 10.0f;
    ai_move_towards(ai, p, orbit_x, orbit_z, 0.42f * ai->move_speed_scale, 3.0f, 0.0f);
    p->in_strafe = sinf(t * 1.3f) * 0.55f;
}

int story_ai_trigger_scripted(int player_id, float x, float y, float z, unsigned int hold_ms, unsigned int now_ms) {
    int idx = ai_index_by_player_id(player_id);
    AIController *ai;
    if (idx < 0) return 0;
    ai = &g_story_ai[idx];
    ai->scripted_marker_x = x;
    ai->scripted_marker_y = y;
    ai->scripted_marker_z = z;
    ai->scripted_hold_ms = hold_ms;
    ai->wait_until_ms = 0; /* reused as "not yet arrived" sentinel, same field patrol uses */
    ai_set_mode(ai, AI_MODE_SCRIPTED, now_ms);
    return 1;
}

void story_ai_load_nav_graph(int count, const float *x, const float *y, const float *z,
                              const int *is_cover, const float *cover_dir_x, const float *cover_dir_z,
                              const int *neighbor_counts, const int *neighbors_flat) {
    int i, k;
    ai_nav_reset(&g_story_nav);
    if (count > AI_NAV_MAX_NODES) count = AI_NAV_MAX_NODES;
    for (i = 0; i < count; i++) {
        ai_nav_add_node(&g_story_nav, x[i], y[i], z[i], is_cover[i], cover_dir_x[i], cover_dir_z[i]);
    }
    for (i = 0; i < count; i++) {
        int nc = neighbor_counts[i];
        if (nc > STORY_AI_NAV_NEIGHBORS_STRIDE) nc = STORY_AI_NAV_NEIGHBORS_STRIDE;
        for (k = 0; k < nc; k++) {
            int j = neighbors_flat[i * STORY_AI_NAV_NEIGHBORS_STRIDE + k];
            if (j >= 0 && j < count) ai_nav_link(&g_story_nav, i, j);
        }
    }
}

/* S461-04: the movement-hook -> locked-animation -> on-end-handoff chain from the founder's own
   real-time scripted_sequence breakdown, at the AI/logic layer. What's real here: server-
   authoritative walk-to-marker (arrival steering, same as every other mode), a real hold timer,
   and a real handoff back to whatever mode preceded the trigger. What's NOT yet real, named
   honestly rather than assumed: which animation clip actually PLAYS during the hold -- checked
   directly, story_ai's bots render through the existing draw_player_3rd/tyler_body path (the
   same one every other bot uses), not through gband_skel_npc.c's separate, currently-unconnected
   single-clip demo NPC (apps/lobby/src/main.c's one hardcoded mannequin draw call, fixed
   position, scene-gated, no link to any story_ai AI at all). A real client-side "SCRIPTED mode
   selects a specific clip" wire-up is scoped, not built, in docs2/specs/
   AI_SCRIPTED_ANIMATION_NORTHSTAR.md. */
static void ai_run_scripted(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    float dx = ai->scripted_marker_x - p->x;
    float dz = ai->scripted_marker_z - p->z;
    float dist = ai_len2(dx, dz);

    if (dist >= 4.0f) {
        ai_move_towards(ai, p, ai->scripted_marker_x, ai->scripted_marker_z, 0.55f * ai->move_speed_scale, 5.0f, 8.0f);
        return;
    }

    p->in_fwd = 0.0f;
    p->in_strafe = 0.0f;
    if (ai->wait_until_ms == 0) {
        ai->wait_until_ms = now_ms + ai->scripted_hold_ms;
        return;
    }
    if (now_ms >= ai->wait_until_ms) {
        AIMode back_to = (ai->previous_mode == AI_MODE_SCRIPTED) ? AI_MODE_PATROL : ai->previous_mode;
        ai->wait_until_ms = 0;
        ai_set_mode(ai, back_to, now_ms);
    }
}

/* S470 -- real ambient greet: stop, face the player, wave (phase 0), then dance (phase 1), then
   hand back to AI_MODE_PATROL, matching ai_run_scripted's own hold-timer/handoff pattern above.
   Faces the player continuously through both phases (not just once on entry) so a player who
   walks a slow circle around the bot mid-greet sees it keep turning to follow, same real
   ai_turn_towards call every other mode already leans on. What actually PLAYS during each phase
   is a client-side render decision (p->anim_override, see PlayerState's own doc comment) -- this
   function only owns the real, server-authoritative timing/facing/handoff, same division of
   responsibility AI_MODE_SCRIPTED's own doc comment already names. */
static void ai_run_greet(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    PlayerState *hero = &s->players[0];
    float dx = hero->x - p->x;
    float dz = hero->z - p->z;
    float target_yaw = ai_angle_to(dx, dz);

    p->in_fwd = 0.0f;
    p->in_strafe = 0.0f;
    ai_turn_towards(ai, p, target_yaw, 8.0f);

    /* Phase transition (if any) is applied BEFORE anim_override is read below, so the tick a
       phase actually flips on shows the NEW gesture immediately rather than lagging one tick
       behind -- a real, found-by-test bug in an earlier draft of this function (set
       anim_override from the OLD phase before checking the timer). */
    if (ai->wait_until_ms == 0) {
        ai->wait_until_ms = now_ms + ((ai->greet_phase == 0) ? STORY_AI_GREET_WAVE_MS : STORY_AI_GREET_DANCE_MS);
    } else if (now_ms >= ai->wait_until_ms) {
        ai->wait_until_ms = 0;
        if (ai->greet_phase == 0) {
            ai->greet_phase = 1; /* wave done -- move to dance, sampled below this same tick */
        } else {
            /* dance done -- resume patrol from wherever patrol_index already was. Returns
               without touching anim_override -- it stays 0 (ai_reset_input's own per-tick reset,
               already applied before this function ran), correct since this AI is no longer in
               AI_MODE_GREET as of this tick. */
            ai->greet_phase = 0;
            ai->last_greet_ms = now_ms;
            ai_set_mode(ai, AI_MODE_PATROL, now_ms);
            return;
        }
    }
    p->anim_override = (ai->greet_phase == 0) ? 1 /* GBAND_SKEL_NPC_ANIM_GREET */ : 2 /* GBAND_SKEL_NPC_ANIM_DANCE */;
}

/* EMPIRE order runs -- deliberately thin. They use the same ai_move_towards primitive every other mode
 * uses; the world mutation itself lives in empire_units.h so a test drives it without a server. */
static void ai_run_explore(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    if (!ai->eu.exploring) {
        /* Deterministic per unit and per leg: no RNG, so two hosts running this build explore the same
           way, and the point stays put for the few seconds it takes to walk to it. */
        int spin = (ai->player_id * 7 + (int)(now_ms / 2500U)) & 7;
        float ang = (float)spin * 0.7853982f;
        float radius = (float)g_eu_rules.explore_radius() * 0.5f + (float)(spin & 3) * 6.0f;
        ai->eu.explore_x = ai->home_x + cosf(ang) * radius;
        ai->eu.explore_z = ai->home_z + sinf(ang) * radius;
        ai->eu.exploring = 1;
    }
    float dist = ai_len2(ai->eu.explore_x - p->x, ai->eu.explore_z - p->z);
    if (dist < 4.0f) { ai->eu.exploring = 0; p->in_fwd = 0.0f; return; }
    ai_move_towards(ai, p, ai->eu.explore_x, ai->eu.explore_z, 0.55f * ai->move_speed_scale, 6.0f, 6.0f);
}

static void ai_run_gather(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    EUWorld *w = &g_empire_world;
    int node = ai->eu.target_node;
    if (node < 0 || node >= w->node_count || w->nodes[node].amount <= 0) {
        node = eu_nearest_node(w, p->x, p->z);
        ai->eu.target_node = node;
    }
    if (node < 0) { ai->eu.order = EU_ORDER_NONE; p->in_fwd = 0.0f; return; }
    float dist = ai_len2(w->nodes[node].x - p->x, w->nodes[node].z - p->z);
    if (dist > (float)g_eu_rules.gather_radius()) {
        ai_move_towards(ai, p, w->nodes[node].x, w->nodes[node].z,
                        0.6f * ai->move_speed_scale, 6.0f, 4.0f);
        return;
    }
    p->in_fwd = 0.0f;
    /* One unit every 250 ms, not one per tick: a node is 25 units, so emptying one is a real ~6 second
       job a player can interrupt rather than an instant pickup. */
    if (now_ms - ai->last_order_ms >= 250U) {
        ai->last_order_ms = now_ms;
        eu_world_gather(w, &ai->eu, node);
    }
}

static void ai_run_claim(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    EUWorld *w = &g_empire_world;
    int cell = ai->eu.target_cell;
    if (cell < 0 || cell >= w->cell_count || w->cells[cell].owner == ai->faction) {
        cell = eu_nearest_free_cell(w, ai->faction, p->x, p->z);
        ai->eu.target_cell = cell;
    }
    if (cell < 0) { ai->eu.order = EU_ORDER_NONE; p->in_fwd = 0.0f; return; }
    float dist = ai_len2(w->cells[cell].x - p->x, w->cells[cell].z - p->z);
    if (dist > (float)w->cells[cell].radius) {
        ai_move_towards(ai, p, w->cells[cell].x, w->cells[cell].z,
                        0.6f * ai->move_speed_scale, 6.0f, 4.0f);
        return;
    }
    p->in_fwd = 0.0f;
    int dt = (int)(now_ms - ai->last_order_ms);
    /* One second at most per tick: a real hitch must not hand out a free claim, but the cap cannot be
       smaller than the rules' own millisecond unit or a low-tick-rate host could never finish a claim. */
    if (dt > 1000) dt = 1000;
    if (dt < 0) dt = 0;
    ai->last_order_ms = now_ms;
    eu_world_claim_tick(w, &ai->eu, cell, dt);
}

static void ai_run_ally_follow(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    PlayerState *hero = &s->players[0];
    float dx = hero->x - p->x;
    float dz = hero->z - p->z;
    float dist = ai_len2(dx, dz);
    (void)now_ms;
    if (dist > 20.0f) ai_move_towards(ai, p, hero->x, hero->z, 0.70f * ai->move_speed_scale, 7.0f, 15.0f);
    else if (dist < 9.0f) ai_move_towards(ai, p, p->x - dx, p->z - dz, 0.40f * ai->move_speed_scale, 6.0f, 0.0f);
    else p->in_fwd = 0.0f;
}

/* S461-01: real tactical flee -- queries the hand-authored cover graph once (on first entry into
   AI_MODE_FLEE), A*s to the nearest cover node that actually faces away from the current threat,
   and holds there once arrived. Deliberately terminal: this codebase has no health-regen system
   anywhere, so re-triggering combat after fleeing would mean charging back out at the same low
   health that caused the flee in the first place -- "wounded, hides, stays hidden" is the
   honest v0 behavior, not a bug. A regen-driven re-engage is real, separate follow-up work. */
static void ai_run_flee(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    (void)now_ms;

    if (ai->flee_target_node < 0 && ai->flee_path_len == 0) {
        ai->flee_target_node = ai_nav_find_cover(&g_story_nav, p->x, p->z, ai->last_known_x, ai->last_known_z);
        if (ai->flee_target_node >= 0) {
            int len = ai_nav_find_path(&g_story_nav, p->x, p->z, ai->flee_target_node, ai->flee_path, AI_NAV_MAX_PATH);
            ai->flee_path_len = (len > 0) ? len : 0;
            ai->flee_path_index = 0;
        }
    }

    if (ai->flee_target_node >= 0 && ai->flee_path_index < ai->flee_path_len) {
        int node = ai->flee_path[ai->flee_path_index];
        const AINavNode *n = &g_story_nav.nodes[node];
        float dx = n->x - p->x;
        float dz = n->z - p->z;
        if (ai_len2(dx, dz) < 4.0f) {
            ai->flee_path_index++;
        } else {
            ai_move_towards(ai, p, n->x, n->z, 0.85f * ai->move_speed_scale, 8.0f, 8.0f);
            return;
        }
    }

    if (ai->flee_target_node >= 0) {
        const AINavNode *n = &g_story_nav.nodes[ai->flee_target_node];
        ai_move_towards(ai, p, n->x, n->z, 0.0f, 5.0f, 4.0f); /* hold at cover */
        return;
    }

    /* Real, honest fallback: no cover graph authored for this scene (or nothing in it faces away
       from the current threat) -- move directly away from the last-known threat position rather
       than doing nothing. */
    {
        float away_x = p->x - ai->last_known_x;
        float away_z = p->z - ai->last_known_z;
        float mag = ai_len2(away_x, away_z);
        if (mag < 0.0001f) {
            away_x = 1.0f;
            away_z = 0.0f;
            mag = 1.0f;
        }
        ai_move_towards(ai, p, p->x + (away_x / mag) * 40.0f, p->z + (away_z / mag) * 40.0f,
                         0.85f * ai->move_speed_scale, 8.0f, 0.0f);
    }
}

/* S462: founder real-time: "layer Perlin Noise or Sine Overrides onto your steering engine...
   inject a perpendicular offset using a time-based sine wave: offset = LeftVector *
   sin(Time.time * frequency) * amplitude." SHANKPIT's movement is 2D top-down (x/z), so the
   "LeftVector" perpendicular offset collapses to a straight sine value on in_strafe -- no
   separate vector math needed, same real simplification ai_run_search's own orbit math already
   makes for its circular motion. player_id phase-shifts each instance so a pack of these doesn't
   weave in lockstep. */
static float ai_weave_strafe(const AIController *ai, unsigned int now_ms, float freq, float amplitude) {
    return sinf((float)now_ms * 0.001f * freq + (float)ai->player_id * 1.7f) * amplitude;
}

/* S462 "Zombie / Relentless Pursuer": direct-line pursuit only. Unlike every other role's combat
   function, this one has no preferred_range kiting/backing-off branch at all -- it always closes,
   matching the founder's own "ignores personal safety or tactical positioning" exactly. */
static void ai_combat_pursuer(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 8.0f);
    p->in_fwd = 1.0f * ai->move_speed_scale;
    p->in_strafe = ai_weave_strafe(ai, now_ms, 2.2f, 0.5f);
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 340U);
    }
    (void)s;
}

/* S462 "Territorial Beast": ordinary preferred_range combat, no different from Trooper's own
   shape here -- the real, distinctive behavior for this role is AI_MODE_LEASH_RETURN, checked
   unconditionally in story_ai_tick before this function ever runs. */
static void ai_combat_territorial_beast(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 6.0f);
    if (per->dist > ai->preferred_range + 10.0f) p->in_fwd = 0.9f * ai->move_speed_scale;
    else if (per->dist < ai->preferred_range - 10.0f) p->in_fwd = -0.3f * ai->move_speed_scale;
    else p->in_fwd = 0.0f;
    p->in_strafe = ai_weave_strafe(ai, now_ms, 1.6f, 0.6f);
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 480U);
    }
    (void)s;
}

/* S462 "Ambush Predator": once it has a target at all -- which, given this role's near-zero
   vision_range, is almost always via hearing rather than sight -- it commits hard, no kiting, no
   hesitation, the widest/fastest weave of the three (the least predictable charge once
   triggered). Reuses WPN_KATANA's own existing real dash ability as the literal "charge," same
   real precedent ai_combat_hound already established for that mechanic. */
static void ai_combat_blind_stalker(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 9.0f);
    p->in_fwd = 1.0f * ai->move_speed_scale;
    p->in_strafe = ai_weave_strafe(ai, now_ms, 3.0f, 0.7f);
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 300U);
    }
    if (p->ability_cooldown == 0 && per->dist > 25.0f && per->dist < 90.0f) {
        p->in_ability = 1;
    }
    (void)s;
}

/* S462: founder real-time pseudocode (TerritorialAutoPilot::Update), followed closely --
   real arrival back home (dist < 2.0) is the only exit, matching "Arrived back home" exactly.
   Real stun immunity while returning, using the actual existing stunned_until_ms/
   stun_immune_until_ms fields (packages/common/protocol.h) rather than inventing a new flag --
   matches the founder's own SetInvulnerableToStun(true) intent with a real, already-wired
   mechanic. Re-armed every tick (not just once on entry) so a player repeatedly re-engaging
   mid-retreat can't outlast a single window. */
static void ai_run_leash_return(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    float dx = ai->home_x - p->x;
    float dz = ai->home_z - p->z;
    float dist = ai_len2(dx, dz);

    p->stunned_until_ms = 0;
    p->stun_immune_until_ms = now_ms + 500U;

    if (dist < 2.0f) {
        ai_set_mode(ai, AI_MODE_PATROL, now_ms);
        return;
    }
    ai_move_towards(ai, p, ai->home_x, ai->home_z, 0.85f * ai->move_speed_scale, 6.0f, 10.0f);
}

static void ai_combat_hound(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    PlayerState *hero = &s->players[0];
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    /* Humanness Phase 2: aim_error_deg was set per role (ai_assign_role_defaults) but
       never actually consumed anywhere -- a real, found dead-data gap. Now real: skill
       derived from it drives a real Gaussian aim-noise term on the actual firing angle. */
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 10.0f);
    p->in_fwd = 1.0f * ai->move_speed_scale;
    p->in_strafe = ((now_ms / 260U + (unsigned int)ai->player_id) % 2U) ? 0.85f : -0.85f;
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 380U); /* Humanness Phase 2: real jitter on the fixed per-role cooldown */
    }
    if (p->on_ground && ((now_ms / 900U + (unsigned int)ai->player_id) % 3U == 0U) && per->dist < 24.0f) {
        p->in_jump = 1;
    }
    /* S181-05 real ability: gap-closer dash (WPN_KATANA's own existing
     * dash mechanic, physics.h katana_try_start_dash -- triggered here,
     * not invented here). Only worth firing while there's real distance
     * to close; dashing at point-blank range wastes the cooldown for no
     * gain, dashing from very far overshoots past a reasonable engage. */
    if (p->ability_cooldown == 0 && per->dist > 30.0f && per->dist < 70.0f) {
        p->in_ability = 1;
    }
    ai->last_known_x = hero->x;
    ai->last_known_z = hero->z;
}

static void ai_combat_trooper(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    /* Humanness Phase 2: aim_error_deg was set per role (ai_assign_role_defaults) but
       never actually consumed anywhere -- a real, found dead-data gap. Now real: skill
       derived from it drives a real Gaussian aim-noise term on the actual firing angle. */
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 7.0f);
    if (per->dist > ai->preferred_range + 8.0f) p->in_fwd = 0.72f * ai->move_speed_scale;
    else if (per->dist < ai->preferred_range - 12.0f) p->in_fwd = -0.48f * ai->move_speed_scale;
    else p->in_fwd = 0.08f;

    p->in_strafe = ((now_ms / 680U + (unsigned int)ai->player_id) % 2U) ? 0.35f : -0.35f;
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        unsigned int burst_gate = (now_ms / 170U) % 5U;
        p->in_shoot = (burst_gate < 3U) ? 1 : 0;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 110U); /* Humanness Phase 2: real jitter on the fixed per-role cooldown */
    }
    /* S181-05 real ability: Frag Toss (physics.h's new WPN_AR ability
     * branch -- real splash-damage projectile, not a stub). Fired while
     * the player is in real engagement range, same window normal shots
     * already use, so it reads as "a periodic special mixed into a
     * kiting gunfight," not a separate disconnected behavior. */
    if (p->ability_cooldown == 0 && per->dist <= ai->attack_range) {
        p->in_ability = 1;
    }
    (void)s;
}

static void ai_combat_brute(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    /* Humanness Phase 2: aim_error_deg was set per role (ai_assign_role_defaults) but
       never actually consumed anywhere -- a real, found dead-data gap. Now real: skill
       derived from it drives a real Gaussian aim-noise term on the actual firing angle. */
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 4.0f);
    p->in_fwd = 0.55f * ai->move_speed_scale;
    p->in_strafe = 0.0f;
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 520U); /* Humanness Phase 2: real jitter on the fixed per-role cooldown */
    }
    /* S181-05 real ability: Ground Slam (physics.h's new WPN_SHOTGUN
     * ability branch -- real splash+stun, not a stub). Only once the
     * Brute has actually closed to melee range -- "gets in your face,
     * then slams" is the whole tank identity; firing it at range would
     * just waste the cooldown on nothing since the splash radius (6.0)
     * is tuned for close quarters. */
    if (p->ability_cooldown == 0 && per->dist < 18.0f) {
        p->in_ability = 1;
    }
    (void)s;
}

static void ai_combat_storm_caller(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    /* Humanness Phase 2: aim_error_deg was set per role (ai_assign_role_defaults) but
       never actually consumed anywhere -- a real, found dead-data gap. Now real: skill
       derived from it drives a real Gaussian aim-noise term on the actual firing angle. */
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 5.0f);
    /* Actively maintains distance -- backs away if the player closes
     * inside preferred_range, unlike Trooper which just stops advancing.
     * A storm caller that lets itself get out-ranged into melee has lost
     * its entire reason to exist as a distinct threat. */
    if (per->dist > ai->preferred_range + 20.0f) p->in_fwd = 0.5f * ai->move_speed_scale;
    else if (per->dist < ai->preferred_range - 15.0f) p->in_fwd = -0.6f * ai->move_speed_scale;
    else p->in_fwd = 0.0f;
    p->in_strafe = ((now_ms / 900U + (unsigned int)ai->player_id) % 2U) ? 0.3f : -0.3f;

    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 260U); /* Humanness Phase 2: real jitter on the fixed per-role cooldown */
    }
    /* Real ability: the 5-round storm-charge burst (physics.h's existing
     * WPN_SNIPER branch, unmodified -- this role is the first AI to
     * actually use it as a deliberate tactical choice instead of it being
     * silently unreachable). Only pops it once real engagement range is
     * confirmed -- popping it while the player is still out of vision
     * would burn the cooldown on nothing. */
    if (p->ability_cooldown == 0 && per->dist <= ai->attack_range) {
        p->in_ability = 1;
    }
    (void)s;
}

static void ai_combat_bombardier(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    /* Humanness Phase 2: aim_error_deg was set per role (ai_assign_role_defaults) but
       never actually consumed anywhere -- a real, found dead-data gap. Now real: skill
       derived from it drives a real Gaussian aim-noise term on the actual firing angle. */
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 4.0f);
    if (per->dist > ai->preferred_range + 15.0f) p->in_fwd = 0.5f * ai->move_speed_scale;
    else if (per->dist < ai->preferred_range - 20.0f) p->in_fwd = -0.35f * ai->move_speed_scale;
    else p->in_fwd = 0.0f;
    /* No dedicated in_ability trigger -- WPN_MISSILE is this role's BASE
     * weapon, so every normal shot is already a real splash-damage hit
     * via the same spawn_projectile/explode_splash pipeline Frag Toss and
     * Ground Slam borrow for their own abilities. The threat here is
     * sustained area denial from a slow-moving heavy, not a special move. */
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 950U); /* Humanness Phase 2: real jitter on the fixed per-role cooldown */
    }
    (void)s;
}

static void ai_combat_ally(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    PlayerState *hero = &s->players[0];
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    /* Humanness Phase 2: aim_error_deg was set per role (ai_assign_role_defaults) but
       never actually consumed anywhere -- a real, found dead-data gap. Now real: skill
       derived from it drives a real Gaussian aim-noise term on the actual firing angle. */
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 6.0f);
    if (per->dist > ai->preferred_range) p->in_fwd = 0.4f;
    else if (per->dist < 15.0f) p->in_fwd = -0.25f;
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = ((now_ms / 190U + (unsigned int)ai->player_id) % 4U) < 2U;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 180U); /* Humanness Phase 2: real jitter on the fixed per-role cooldown */
    }
    if (ai_len2(hero->x - p->x, hero->z - p->z) < 7.0f) p->in_strafe = 0.6f;
}

/* AI_ROLE_MAGISTRATE -- the civic boss. Two things make it a boss rather than a louder clerk, and
   both rest on mechanics that already exist below the AI layer:
     (1) it does not charge. The bench holds its own range and fires a magnum down the gallery
         (WPN_MAGNUM, the one weapon no other role in the roster carries), so the fight is a long
         sightline with something heavy standing at the end of it.
     (2) its ability is a sentence, and it is READ before it is SERVED. Every
         STORY_AI_MAGISTRATE_VERDICT_COOLDOWN_MS, with the player actually in sight, the bench arms
         a STORY_AI_MAGISTRATE_VERDICT_TELL_MS window (verdict_tell_until_ms, the value the client's
         ring and boss bar draw from). Only when that window elapses with the player STILL in sight
         does it hold them in contempt: p->stunned_until_ms is pushed forward -- the
         same real field local_game.h's own input gate reads (`cmd_time < p->stunned_until_ms`) -- so
         the player loses their inputs for that window while the bench keeps firing. Nothing here
         invents a stun system: the field, its gate and its respawn clearing all pre-date this role.
         What is new is an AI that writes it on purpose instead of a splash radius doing it by
         accident.
   It is deliberately not ranged-only: attack_range 120 against preferred_range 90 means backing off
   down the hall works, and the boss follows at a walk (move_speed_scale 0.45, the slowest body in
   the district) rather than teleporting the fight back to its opening distance. */
static void ai_combat_magistrate(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    PlayerState *hero = &s->players[0];
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    target_yaw += humanness_aim_noise(&ai->humanness, ai_clamp(1.0f - ai->aim_error_deg / 10.0f, 0.0f, 1.0f));
    ai_turn_towards(ai, p, target_yaw, 4.0f);
    if (per->dist > ai->preferred_range + 12.0f) p->in_fwd = 0.6f * ai->move_speed_scale;
    else if (per->dist < ai->preferred_range - 20.0f) p->in_fwd = -0.5f * ai->move_speed_scale;
    else p->in_fwd = 0.0f;
    p->in_strafe = 0.0f; /* it walks the aisle, it does not weave */
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + humanness_reaction_delay_ms(&ai->humanness, 620U);
    }
    /* Only a player actually IN SIGHT is sentenced: break the line down the gallery and nothing is
       read at all, even with the doors still shut. The sight test is re-run when the tell expires
       precisely because the window is meant to be played against -- leaving it is the counter. */
    int in_reach = per->visible && per->dist <= STORY_AI_MAGISTRATE_VERDICT_RANGE;
    if (ai->verdict_tell_until_ms) {
        if (!in_reach) {
            ai->verdict_tell_until_ms = 0;                         /* line broken: sentence dropped */
            ai->next_verdict_ms = now_ms + STORY_AI_MAGISTRATE_VERDICT_RETRY_MS;
        } else if (now_ms >= ai->verdict_tell_until_ms) {
            unsigned int until = now_ms + STORY_AI_MAGISTRATE_VERDICT_STUN_MS;
            if (until > hero->stunned_until_ms) hero->stunned_until_ms = until;
            ai->verdict_tell_until_ms = 0;
            ai->next_verdict_ms = now_ms + STORY_AI_MAGISTRATE_VERDICT_COOLDOWN_MS;
            g_magistrate_served++;
            /* The sentence leaves its mark where the bench is standing -- and that mark burns. */
            verdict_scorch_push(&g_magistrate_scorches, p->x, p->y, p->z, p->scene_id, now_ms);
        }
    } else if (in_reach && now_ms >= ai->next_verdict_ms) {
        ai->verdict_tell_until_ms = now_ms + STORY_AI_MAGISTRATE_VERDICT_TELL_MS;
        g_magistrate_tells++;
    }
}

static void ai_run_combat(ServerState *s, AIController *ai, const AIPerception *per, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    if (ai->role == AI_ROLE_RIFT_HOUND) ai_combat_hound(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_GORE_BRUTE) ai_combat_brute(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_STORY_ALLY) ai_combat_ally(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_STORM_CALLER) ai_combat_storm_caller(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_BOMBARDIER) ai_combat_bombardier(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_RELENTLESS_PURSUER) ai_combat_pursuer(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_TERRITORIAL_BEAST) ai_combat_territorial_beast(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_BLIND_STALKER) ai_combat_blind_stalker(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_MAGISTRATE) ai_combat_magistrate(s, ai, p, per, now_ms);
    else ai_combat_trooper(s, ai, p, per, now_ms);
    ai_squad_apply_role_bias(ai, p); /* S461-03 -- a real no-op for these roles, squad_id stays -1 */
}

/* ai_scorch_burn -- the floor mark's damage. Shield-then-health, the same order story_boss_tick and
   witness_ai.c's own melee path use, and deliberately NOT physics.h's katana_apply_damage: physics.h
   defines its functions non-static, so a second translation unit including it collides at link time
   (confirmed live here -- `make lobby` failed with "multiple definition of phys_resolve_rewind"
   against main.c's own copy, the exact collision witness_ai.c's hero-melee helper documents). The
   essential death fields are reimplemented instead, minus attacker reward bookkeeping: a burn is not a
   kill, and once the bench is dead this mark has no owner left to credit. Respawn delay 2000 ms
   matches mode_respawn_delay_ms's MODE_STORY default that witness_ai.c's own copy names. */
static void ai_scorch_burn(PlayerState *p, float from_x, float from_z, unsigned int now_ms) {
    int damage = VERDICT_SCORCH_DAMAGE;
    if (!p->active || p->state == STATE_DEAD) return;
    if (p->shield > 0) {
        if (p->shield >= damage) { p->shield -= damage; damage = 0; }
        else { damage -= p->shield; p->shield = 0; }
    }
    p->health -= damage;
    if (p->hit_feedback < 15) p->hit_feedback = 15;   /* defender knows it was hit, same floor as a shot */
    if (p->health > 0) return;

    p->health = 0;
    p->state = STATE_DEAD;
    p->in_shoot = 0;
    p->in_reload = 0;
    p->in_use = 0;
    p->in_jump = 0;
    p->in_ability = 0;
    p->is_shooting = 0;
    p->dash_timer = 0;
    p->death_time_ms = now_ms;
    p->death_duration_ms = 2000u;
    p->respawn_time = now_ms + 2000u;
    {
        float away_x = p->x - from_x, away_z = p->z - from_z;
        float len = ai_len2(away_x, away_z);
        if (len > 0.0001f) {
            p->death_dir_x = away_x / len;
            p->death_dir_z = away_z / len;
        } else {
            p->death_dir_x = 0.0f;
            p->death_dir_z = 1.0f;
        }
    }
    p->vx = p->death_dir_x * 0.35f;
    p->vz = p->death_dir_z * 0.35f;
    p->vy = 0.14f;
}

/* ai_magistrate_scorches_tick -- the hazard half of a served verdict. One pass per scorch per tick,
   so several players standing in the same mark all get burnt, and the cadence clock advances once
   instead of once per body. AI bodies are excluded on purpose: the bench stands in its own scorch for
   the rest of the fight, and a boss that immolates itself -- and its own bailiff -- would be a rule,
   not a hazard. The mark outlives the boss: a scorch keeps burning for its full life after the
   magistrate is dead, which is exactly what the death state above does not touch. */
static void ai_magistrate_scorches_tick(ServerState *s, unsigned int now_ms) {
    int i, pid;
    if (verdict_scorches_live(&g_magistrate_scorches) == 0) return;
    for (i = 0; i < VERDICT_SCORCH_MAX; i++) {
        VerdictScorch *sc = &g_magistrate_scorches.pool[i];
        if (!sc->active) continue;
        int due = verdict_scorch_due(sc, now_ms);
        int hit_any = 0;
        for (pid = 0; due && pid < MAX_CLIENTS; pid++) {
            PlayerState *p = &s->players[pid];
            if (!p->active || p->state == STATE_DEAD || p->is_bot) continue;
            if (!verdict_scorch_covers(sc, p->scene_id, p->x, p->z, now_ms)) continue;
            /* standing in the mark is not enough if a bench, a wall or a pew is between you and it: the
               burn is a fire on the floor, and fire does not go through cover. Same query the eyes use. */
            if (!ai_sight_clear(sc->x, sc->y + 0.2f, sc->z, p->x, p->y + STORY_AI_SIGHT_EYE_Y, p->z)) continue;
            ai_scorch_burn(p, sc->x, sc->z, now_ms);
            hit_any = 1;
        }
        if (hit_any) verdict_scorch_advance(sc, now_ms);
    }
    verdict_scorches_age(&g_magistrate_scorches, now_ms);
}

void story_ai_tick(ServerState *s, unsigned int now_ms) {
    /* Survival waves, if a host armed them (story_ai_waves_enable): a disarmed runner is a no-op. */
    if (g_wave_runner.enabled) (void)story_ai_waves_tick(s, now_ms);
    /* Downed allies: holds an ally's respawn so a companion can reach it (allies only). */
    story_ai_downed_tick(s, now_ms);
    int i;
    int attackers = 0;
    if (!s) return;
    /* MODE_TYLER (TYLER VALHANNA cold open) drives its two AI_MODE_SCRIPTED actors through this
       same tick: before this gate accepted it, story_ai_tick was a silent no-op in MODE_TYLER, so
       Tyler and Hana never got any movement input on the server OR locally (verified by
       tests/tyler/test_tyler_vo.c, T6). Deliberately an explicit allow-list entry, not a blanket
       "any mode" -- QUEUE/deathmatch levels that author characters keep their existing behavior. */
    int tick_allowed = (s->game_mode == MODE_STORY || s->game_mode == MODE_TYLER) &&
                       s->story_phase == STORY_PHASE_PLAYING;
    if (!tick_allowed) {
        /* EMPIRE units are level-authored, which means any mode: a NOCK character with an archetype can be
           spawned in ZOMBIES, a custom map or anything else, and the founder's own ask was to be able to
           put citizens/zombies/soldiers/allies into the world. Gating their tick on MODE_STORY would spawn
           them and leave them standing still forever, so a live EMPIRE unit makes the tick legal on its
           own. Characters without an archetype keep the old gate exactly. */
        for (i = 0; i < STORY_AI_MAX; i++) {
            if (g_story_ai[i].active && g_story_ai[i].archetype != EU_ARCH_NONE) { tick_allowed = 1; break; }
        }
    }
    if (!tick_allowed) return;

    memset(&g_story_bb, 0, sizeof(g_story_bb));

    /* the served sentence's floor hazard burns independently of what any AI is doing this tick */
    ai_magistrate_scorches_tick(s, now_ms);

    for (i = 0; i < STORY_AI_MAX; i++) {
        AIController *ai = &g_story_ai[i];
        if (!ai->active) continue;
        if (ai->player_id <= 0 || ai->player_id >= MAX_CLIENTS) continue;
        if (!s->players[ai->player_id].active || s->players[ai->player_id].state == STATE_DEAD) continue;
        humanness_tick_mood(&ai->humanness, now_ms); /* Humanness Phase 2 -- real, once per AI per tick */
        ai_gather_perception(s, ai, &g_story_perception[i], now_ms);
        if (g_story_perception[i].visible) {
            g_story_bb.player_visible_count++;
            g_story_bb.last_known_player_x = ai->last_known_x;
            g_story_bb.last_known_player_y = ai->last_known_y;
            g_story_bb.last_known_player_z = ai->last_known_z;
            g_story_bb.last_global_alert_ms = now_ms;
        }
    }

    g_story_bb.alert_level = (g_story_bb.player_visible_count > 0) ? 2 : ((now_ms - g_story_bb.last_global_alert_ms) < 2500U ? 1 : 0);

    ai_squad_reevaluate(s); /* S461-03 -- before combat dispatch so this tick's roles are current */

    for (i = 0; i < STORY_AI_MAX; i++) {
        AIController *ai = &g_story_ai[i];
        AIPerception *per = &g_story_perception[i];
        unsigned int search_timeout;
        int wants_combat;
        if (!ai->active) continue;
        if (!s->players[ai->player_id].active || s->players[ai->player_id].state == STATE_DEAD) continue;

        /* S461-04: a real locked scripted state -- combat/investigate/flee perception must not
           interrupt it (the founder's own spec: "forced the NPC into a locked 'scripted state'").
           ai_run_scripted below owns its own exit via ai_set_mode. */
        if (ai->mode == AI_MODE_SCRIPTED) continue;

        /* S470: real, deliberate bypass -- a wandering bot never enters combat/investigate/
           search/flee at all (checked BEFORE the leash check below, same ordering reason S462's
           own leash override sits before combat: this role's real behavior takes over
           unconditionally, not layered on top of the general roster state machine). GREET is a
           locked state, same "owns its own exit" pattern as SCRIPTED above -- ai_run_greet is the
           only thing that ever leaves it. */
        if (ai->role == AI_ROLE_WANDERING_BOT) {
            if (ai->mode == AI_MODE_GREET) continue;
            {
                PlayerState *hero = &s->players[0];
                PlayerState *bot = &s->players[ai->player_id];
                float gdist = ai_len2(hero->x - bot->x, hero->z - bot->z);
                /* last_greet_ms == 0 is the real "never greeted yet" sentinel (same convention
                   wait_until_ms == 0 already uses elsewhere in this file) -- without it, a bot
                   that hasn't greeted at all would have its first-ever greet incorrectly blocked
                   by the cooldown math whenever now_ms itself is still under
                   STORY_AI_GREET_COOLDOWN_MS since process start (a real bug, caught by a real
                   test before this ever shipped, not assumed safe). */
                if (hero->active && hero->state != STATE_DEAD && gdist < STORY_AI_GREET_RADIUS &&
                    (ai->last_greet_ms == 0 || (now_ms - ai->last_greet_ms) >= STORY_AI_GREET_COOLDOWN_MS)) {
                    ai->greet_phase = 0;
                    ai->wait_until_ms = 0;
                    ai_set_mode(ai, AI_MODE_GREET, now_ms);
                } else {
                    ai_set_mode(ai, AI_MODE_PATROL, now_ms);
                }
            }
            continue;
        }

        /* S462: real leash override, checked before anything else (including combat/flee) so it
           forces unconditionally, matching the founder's own pseudocode exactly ("If the monster
           is dragged too far from its zone, force a retreat"). leash_radius == 0 for every role
           except AI_ROLE_TERRITORIAL_BEAST makes this a real no-op for the rest of the roster.
           Once in AI_MODE_LEASH_RETURN, stays there until ai_run_leash_return's own real arrival
           check exits it -- re-entering the radius alone does not cancel the return early. */
        if (ai->leash_radius > 0.0f) {
            float home_dist = ai_len2(s->players[ai->player_id].x - ai->home_x, s->players[ai->player_id].z - ai->home_z);
            if (home_dist > ai->leash_radius) {
                ai_set_mode(ai, AI_MODE_LEASH_RETURN, now_ms);
                continue;
            }
            if (ai->mode == AI_MODE_LEASH_RETURN) continue;
        }

        wants_combat = per->visible;
        search_timeout = STORY_AI_SEARCH_MIN_MS + (((unsigned int)ai->player_id * 317U) % STORY_AI_SEARCH_VAR_MS);

        /* EMPIRE orders (empire_units.h) -- the one place an authored mask turns into behaviour. A unit
           with archetype 0 never reaches this, so every pre-existing AI's path below is untouched. NONE
           deliberately falls through to the role's own brain instead of freezing the unit: "no order
           applies right now" is not "stand still forever", which is what keeps a soldier on guard and a
           citizen wandering without either of those being special-cased here. */
        if (ai->archetype != EU_ARCH_NONE) {
            EUView view;
            /* ai_gather_perception has exactly one target: players[0], the hero. An ally's leader and its
               enemy cannot be the same body, so an ally never treats what it sees as a threat -- otherwise
               an ally would pick ENGAGE the first time it looked at the player it is following. Hostile and
               neutral units read the perception normally. (A real multi-target perception is the honest
               fix; this is the correct behaviour for the one target the engine has.) */
            view.enemy_visible = (ai->faction == EU_FACTION_ALLY) ? 0 : per->visible;
            view.enemy_dist = per->dist;
            view.leader_alive = s->players[0].active && s->players[0].state != STATE_DEAD;
            view.leader_dist = ai_len2(s->players[0].x - s->players[ai->player_id].x,
                                       s->players[0].z - s->players[ai->player_id].z);
            view.idle_ms = (int)(now_ms - ai->mode_entered_ms);
            view.at_home = ai_len2(s->players[ai->player_id].x - ai->home_x,
                                   s->players[ai->player_id].z - ai->home_z) < 4.0f;
            int order = eu_choose_order(&ai->eu, &g_empire_world, &view);
            if (order != ai->eu.order) {
                ai->eu.order = order;
                ai->eu.target_node = -1;
                ai->eu.target_cell = -1;
                ai->eu.exploring = 0;
                ai->last_order_ms = now_ms;
            }
            if (order == EU_ORDER_ENGAGE) { ai_set_mode(ai, AI_MODE_COMBAT, now_ms); continue; }
            if (order == EU_ORDER_FOLLOW) { ai_set_mode(ai, AI_MODE_ALLY_FOLLOW, now_ms); continue; }
            if (order == EU_ORDER_GATHER) { ai_set_mode(ai, AI_MODE_GATHER, now_ms); continue; }
            if (order == EU_ORDER_CLAIM) { ai_set_mode(ai, AI_MODE_CLAIM, now_ms); continue; }
            if (order == EU_ORDER_EXPLORE) { ai_set_mode(ai, AI_MODE_EXPLORE, now_ms); continue; }
            if (order == EU_ORDER_GUARD) { ai_set_mode(ai, AI_MODE_PATROL, now_ms); continue; }
        }

        if (ai->role == AI_ROLE_STORY_ALLY) {
            if (wants_combat) ai_set_mode(ai, AI_MODE_COMBAT, now_ms);
            else ai_set_mode(ai, AI_MODE_ALLY_FOLLOW, now_ms);
            continue;
        }

        /* S461-01: real health-gated flee, checked before combat/investigate assignment so a
           wounded, low-courage AI breaks off instead of re-entering the fight that wounded it.
           Terminal by design once triggered -- see ai_run_flee's own comment for why. */
        if (ai->mode == AI_MODE_FLEE ||
            (s->players[ai->player_id].health < STORY_AI_FLEE_HEALTH_PCT && ai->courage < STORY_AI_FLEE_COURAGE_MAX)) {
            ai_set_mode(ai, AI_MODE_FLEE, now_ms);
            continue;
        }

        if (wants_combat && attackers < STORY_AI_MAX_ATTACKERS) {
            ai_set_mode(ai, AI_MODE_COMBAT, now_ms);
            attackers++;
            continue;
        }

        if (wants_combat && attackers >= STORY_AI_MAX_ATTACKERS) {
            ai_set_mode(ai, AI_MODE_SEARCH, now_ms);
            continue;
        }

        if ((now_ms - ai->last_heard_ms) < STORY_AI_HEARING_MEMORY_MS) {
            ai_set_mode(ai, AI_MODE_INVESTIGATE, now_ms);
            continue;
        }

        if ((now_ms - ai->last_seen_ms) < STORY_AI_SEEN_MEMORY_MS) {
            ai_set_mode(ai, AI_MODE_SEARCH, now_ms);
            continue;
        }

        if (ai->mode == AI_MODE_SEARCH && (now_ms - ai->mode_entered_ms) < search_timeout) {
            continue;
        }

        ai_set_mode(ai, AI_MODE_PATROL, now_ms);
    }

    g_story_bb.active_attackers = attackers;

    for (i = 0; i < STORY_AI_MAX; i++) {
        AIController *ai = &g_story_ai[i];
        PlayerState *p;
        if (!ai->active) continue;
        if (!s->players[ai->player_id].active || s->players[ai->player_id].state == STATE_DEAD) continue;
        p = &s->players[ai->player_id];
        ai_reset_input(p);
        if (ai->mode == AI_MODE_PATROL) ai_run_patrol(s, ai, now_ms);
        else if (ai->mode == AI_MODE_INVESTIGATE) ai_run_investigate(s, ai, now_ms);
        else if (ai->mode == AI_MODE_SEARCH) ai_run_search(s, ai, now_ms);
        else if (ai->mode == AI_MODE_ALLY_FOLLOW) ai_run_ally_follow(s, ai, now_ms);
        else if (ai->mode == AI_MODE_COMBAT) ai_run_combat(s, ai, &g_story_perception[i], now_ms);
        else if (ai->mode == AI_MODE_FLEE) ai_run_flee(s, ai, now_ms);
        else if (ai->mode == AI_MODE_SCRIPTED) ai_run_scripted(s, ai, now_ms);
        else if (ai->mode == AI_MODE_LEASH_RETURN) ai_run_leash_return(s, ai, now_ms);
        else if (ai->mode == AI_MODE_GREET) ai_run_greet(s, ai, now_ms);
        else if (ai->mode == AI_MODE_EXPLORE) ai_run_explore(s, ai, now_ms);
        else if (ai->mode == AI_MODE_GATHER) ai_run_gather(s, ai, now_ms);
        else if (ai->mode == AI_MODE_CLAIM) ai_run_claim(s, ai, now_ms);
    }

#if STORY_AI_DEBUG
    if (now_ms >= g_story_debug_next_log_ms) {
        g_story_debug_next_log_ms = now_ms + 1000U;
        for (i = 0; i < STORY_AI_MAX; i++) {
            AIController *ai = &g_story_ai[i];
            if (!ai->active) continue;
            printf("[STORY_AI] id=%d role=%s mode=%s dist=%.1f visible=%d attackers=%d alert=%d\n",
                   ai->player_id,
                   ai_role_name(ai->role),
                   ai_mode_name(ai->mode),
                   g_story_perception[i].dist,
                   g_story_perception[i].visible,
                   g_story_bb.active_attackers,
                   g_story_bb.alert_level);
        }
    }
#endif
}
