#include "witness_ai.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "npc_archetype.h"
#include "zombie_values.h"
#include "witness_live.h"
#include "giant_bug_values.h"
#include "avian_values.h"
#include "../common/pheromone.h" /* PheromoneMarker, pheromone_claim_slot/expire/find_nearest -- pure
    targeting primitive, BIG_O engine merge phase 5, no live consumer until this pass */
#include "../reflux/reflux_runtime.h" /* REFLUX_ACTION_ZOMBIE_SPAWNED/HARVESTED/MOOD_ESCALATED, WITNESS_ESCALATED */

/* Prototype for the PARENA-generated glue (reflux_mod.c, do-not-edit-by-hand) -- no shared .h
 * between generated units in this codebase, same convention world_alert_bridge.c's own identical
 * declaration already established. */
void reflux_dispatch(int action_type, int a, int b, int c);

/* Prototype for zombies_pheromone_rules.c (PARENA-generated, do-not-edit-by-hand) -- same
 * no-shared-header-between-generated-units convention as reflux_dispatch above. */
int zombies_pheromone_should_steer_to_marker(int hero_has_target);

typedef struct {
    int active;
    int player_id;
    int npc_index;   /* index into g_sim.n[] */
    NpcBrain brain;
    int men_last_target_pid; /* The Men only (brain.archetype == NPC_ARCHETYPE_THE_MEN): the
        player_id this Man was hunting as of last tick, or -1. Lets the dispatch loop fire
        REFLUX_ACTION_MEN_DISPATCHED once per real new-target edge instead of every tick a Man
        spends still chasing the SAME target. Explicitly set in spawn_human, not left to memset's
        zero -- 0 is a real, reachable player_id (the hero's), unlike every other "-1 = none"
        sentinel in this file. */
} WitnessAiCitizen;

typedef struct {
    int active;
    int player_id;
    ZombieState zstate;
    unsigned int last_attack_ms; /* zombie perception/melee, see witness_ai.h's own doc comment */
    unsigned int last_tick_ms;   /* for zombie_tick_dt's real elapsed step (0 = first tick) */
    unsigned int lock_until_ms;  /* hero stays the target until this time (sense memory) */
    unsigned int wander_until_ms;/* current wander leg ends here */
    float wander_yaw;
    int wander_pause;            /* this leg is a stand-still */
    unsigned char harvested_reported; /* REFLUX_ACTION_ZOMBIE_HARVESTED fired once for this corpse */
} WitnessAiZombie;

/* Giant Zombie Bug -- founder real-time, 2026-09-22: "add giant zombie bugs (feral AI units)."
 * A genuinely separate entity type from WitnessAiZombie above, not a zombie variant -- its own
 * GiantBugState (giant_bug_values.h), its own real spawn/tick/role. */
typedef struct {
    int active;
    int player_id;
    GiantBugState bstate;
    int last_health;             /* combat/pain feedback, see witness_ai.h's own doc comment */
    unsigned int last_attack_ms;
} WitnessAiGiantBug;

#define WITNESS_AI_MAX_BIRDS 6
typedef struct {
    int active;
    int player_id;
    AvianState astate;
    float angle;      /* radians around the human */
    float radius;
    float speed;      /* rad/s */
    float alt;
} WitnessAiBird;
static WitnessAiBird g_birds[WITNESS_AI_MAX_BIRDS];
static WitnessWallHitFn g_wall_hit_hook;
typedef struct { float x, y, z, w, h, d; } WaiBox; /* layout-identical to physics.h's Box */
#define WAI_PLAYER_HEIGHT 6.47f
static WitnessMapFn g_map_hook;
void witness_ai_set_map_hook(WitnessMapFn fn) { g_map_hook = fn; }
static int wai_map(const WaiBox **b) {
    const void *raw = NULL;
    if (!g_map_hook) { *b = NULL; return 0; }
    int n = g_map_hook(&raw);
    *b = (const WaiBox *)raw;
    return raw ? n : 0;
}
#define WAI_BODY_R 1.6f
static void wai_learn_reset(void);
static void wai_learn_clear_near(float x, float z);
static signed char g_wai_side[MAX_CLIENTS];
static unsigned int g_zlast_spawn_ms;
void witness_ai_set_wall_hit_hook(WitnessWallHitFn fn) { g_wall_hit_hook = fn; }
static unsigned int g_zombie_claw_ms[MAX_CLIENTS];
static unsigned char g_wai_breach[MAX_CLIENTS];

#define WITNESS_AI_MAX_GIANT_BUGS 8
#define WITNESS_AI_BUG_EAT_RADIUS 4.0f

static WitnessSim g_sim;
static WitnessAiCitizen g_citizens[WITNESS_AI_MAX_CITIZENS];
static WitnessAiZombie g_zombies[WITNESS_AI_MAX_ZOMBIES];
static WitnessAiGiantBug g_giant_bugs[WITNESS_AI_MAX_GIANT_BUGS];
static unsigned int g_last_sim_tick_ms;
static int g_have_last_sim_tick;

/* Pheromone command tool going live (2026-10-08, same-day continuation) -- see witness_ai_h's own
 * updated doc comment and witness_ai_try_throw_pheromone's below for the full account. The marker
 * pool itself (PheromoneMarker, packages/common/pheromone.h) was ported and tested in BIG_O engine
 * merge phase 5 with no live consumer; this is that consumer. */
#define WITNESS_AI_PHEROMONE_THROW_RANGE 12.0f      /* fixed throw distance, matches BIG_O's own
    "camera-forward x a fixed throw distance, no real projectile arc" v0 scope (NORTHSTAR.md section
    10) -- no projectile/arc/line-of-sight here either, same honest cut. */
#define WITNESS_AI_PHEROMONE_THROW_COOLDOWN_MS 2000u
#define WITNESS_AI_PHEROMONE_MARKER_LIFETIME_MS 30000u /* matches BIG_O's own "30s real expiry" */
/* How far a zombie can notice an active marker. Deliberately bigger than
 * WITNESS_AI_ZOMBIE_SENSE_BASE (20, ambient senses) but well under the max alertness-scaled,
 * already-hunting sense radius (~112) -- a considered, named judgment call: a thrown marker is a
 * strong, deliberate lure that should carry further than a zombie's own passive awareness, not an
 * unlimited-range summon. */
#define WITNESS_AI_PHEROMONE_DETECTION_RADIUS 60.0f
static PheromoneMarker g_pheromone_markers[PHEROMONE_MAX];
static unsigned int g_pheromone_next_throw_ms[MAX_CLIENTS];

/* Player-zone trespass state ("full phone app parity, costume changes, add The Men" follow-up).
 * See witness_ai_tick's own VOXWORLD lab-circle block for what drives this. */
static int g_player_zone;
static unsigned int g_last_player_zone_check_ms;
static int g_have_last_player_zone_check;

/* Hardcoded VOXWORLD "lab" trespass circle, deliberately away from every citizen/zombie/The Men
 * spawn position witness_ai_seed_voxworld_encounter places below (x in roughly -60..60 there) --
 * same "hardcoded coordinates, no LevelZone/JSON authoring needed" precedent that function's own
 * doc comment already set. Real, separate follow-up (7c's own named gap): a level-authored
 * LevelZone would replace this the moment VOXWORLD has real JSON level data to read. */
#define WITNESS_AI_LAB_ZONE_CX 110.0f
#define WITNESS_AI_LAB_ZONE_CZ -260.0f
#define WITNESS_AI_LAB_ZONE_RADIUS 18.0f
static FILE *g_sim_log; /* witness_sim_tick's own real log sink -- /dev/null on a live server so
                            its per-tick "TICK -> N" line doesn't spam stdout every real second;
                            falls back to stdout if /dev/null can't be opened (never observed on
                            this repo's own Linux deployment target, but a safe, real fallback). */

#define WITNESS_AI_SIM_TICK_INTERVAL_MS 1000u

/* "wheelbarrow" carry state -- see witness_ai.h's own doc comment for the full real/honest scope. */
static int g_carried_id = -1;
static int g_lab_deliveries = 0;

/* Cake-smash distraction ("if the cake gets smashed it flies everywhere and causes a big
 * distraction and distracts from heavy zombie usage" -- founder real-time, 2026-09-22). 0 = not
 * active. See witness_ai_smash_cake and witness_ai_tick's own real vigilance-halving block.
 * WITNESS_AI_DISTRACTION_MS itself lives in witness_ai.h (public -- callers/tests need it). */
static unsigned int g_distraction_until_ms = 0;

static int find_free_player_slot(ServerState *s) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!s->players[i].active) return i;
    }
    return -1;
}

/* Mirrors story_ai_spawn_enemy's own explicit field set exactly (packages/simulation/story_ai.c)
 * -- deliberately NOT a memset of the whole PlayerState, matching that proven-safe precedent
 * rather than assuming every field's zero value is a safe default. */
static void init_bot_player(ServerState *s, int slot, float x, float y, float z) {
    PlayerState *p = &s->players[slot];
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
}

/* witness_ai_hero_melee_hit -- shared shield-then-health hero damage + on-death entry, used by
 * both zombie and Giant Zombie Bug melee (2026-09-25 follow-up: real reuse instead of copy-pasting
 * the same block a second time for the bug). Same shield-then-health order story_boss_tick's own
 * attack block (local_game.h) already uses. away_x/away_z is the real direction to fling the
 * corpse on death (attacker-to-hero vector, same convention that call site already established).
 *
 * Deliberately NOT a call to physics.h's own phys_enter_death_state: that header defines its
 * functions non-static, so a second translation unit including it collides at link time
 * (confirmed live: `make server` failed with "multiple definition of phys_rand_f/accelerate/..."
 * against apps/server/src/main.c's own copy). This reimplements phys_enter_death_state's essential
 * fields directly instead, minus the attacker-reward bookkeeping (there is no attacker PlayerState
 * here, same as that call site's own NULL-attacker boss-kill case). Respawn delay 2000ms matches
 * mode_respawn_delay_ms's own real MODE_STORY/default value (local_game.h, private to that
 * translation unit) -- the hero is player 0, so local_game.h's own "i>0 dead players never
 * respawn" MODE_STORY convention doesn't zero this back out the way it would for a zombie/citizen/
 * bug. */
static void witness_ai_hero_melee_hit(ServerState *s, PlayerState *hero_p, int damage,
                                       float away_x, float away_z, unsigned int now_ms) {
    /* Real, damage-scaled hit ring -- matches katana_apply_damage's own real "defender knows it
       was hit" floor (physics.h, >= 15) rather than a flat, severity-blind value: a light zombie
       bite and a full-strength Giant Zombie Bug lunge should not look identical on screen. Capped
       at 30, the same real "kill/high dmg" ceiling draw_hud's own hit-ring code (apps/lobby/src/
       main.c) already treats as its red-double-ring tier (>=25). */
    int raw_damage = damage;
    hero_p->shield_regen_timer = SHIELD_REGEN_DELAY;
    if (hero_p->shield > 0) {
        if (hero_p->shield >= damage) { hero_p->shield -= damage; damage = 0; }
        else { damage -= hero_p->shield; hero_p->shield = 0; }
    }
    hero_p->health -= damage;
    {
        int fb = raw_damage < 15 ? 15 : (raw_damage > 30 ? 30 : raw_damage);
        hero_p->hit_feedback = fb;
    }
    if (hero_p->health > 0) return;

    hero_p->health = 0;
    hero_p->state = STATE_DEAD;
    hero_p->in_shoot = 0;
    hero_p->in_reload = 0;
    hero_p->in_use = 0;
    hero_p->in_jump = 0;
    hero_p->in_ability = 0;
    hero_p->is_shooting = 0;
    hero_p->dash_timer = 0;
    hero_p->death_time_ms = now_ms;
    hero_p->death_duration_ms = 2000u;
    hero_p->respawn_time = now_ms + 2000u;
    {
        float away_len = sqrtf(away_x * away_x + away_z * away_z);
        if (away_len > 0.0001f) {
            hero_p->death_dir_x = away_x / away_len;
            hero_p->death_dir_z = away_z / away_len;
        } else {
            hero_p->death_dir_x = 0.0f;
            hero_p->death_dir_z = 1.0f;
        }
    }
    hero_p->vx = hero_p->death_dir_x * 0.35f;
    hero_p->vz = hero_p->death_dir_z * 0.35f;
    hero_p->vy = 0.14f;
    if (s->game_mode == MODE_STORY || s->game_mode == MODE_STORY_CAVE) {
        s->story_phase = STORY_PHASE_FAILED;
        s->story_phase_start_ms = now_ms;
        s->match_over = 1;
    }
}

void witness_ai_reset(unsigned int seed, unsigned int now_ms) {
    (void)now_ms;
    if (!g_sim_log) {
        g_sim_log = fopen("/dev/null", "w");
        if (!g_sim_log) g_sim_log = stdout;
    }
    /* WITNESS_SIM_MAX_PLAYERS real human "suspect" slots aren't this module's concern (a future
       hook wires the live human player into g_sim.p[0]) -- 1 is a safe, real minimum so
       witness_sim_init's own internal loops have at least one valid player index. */
    witness_sim_init(&g_sim, seed, 1, g_sim_log);
    memset(g_citizens, 0, sizeof(g_citizens));
    memset(g_zombies, 0, sizeof(g_zombies));
    memset(g_giant_bugs, 0, sizeof(g_giant_bugs));
    memset(g_birds, 0, sizeof(g_birds));
    memset(g_wai_breach, 0, sizeof(g_wai_breach));
    memset(g_wai_side, 0, sizeof(g_wai_side));
    wai_learn_reset();
    g_have_last_sim_tick = 0;
    g_zlast_spawn_ms = 0;
    g_player_zone = ZONE_PUBLIC; /* matches witness_sim_init's own player-0 default */
    g_have_last_player_zone_check = 0;
    g_carried_id = -1;
    g_lab_deliveries = 0;
    g_distraction_until_ms = 0;
    memset(g_pheromone_markers, 0, sizeof(g_pheromone_markers));
    memset(g_pheromone_next_throw_ms, 0, sizeof(g_pheromone_next_throw_ms));
}

/* Real, live cake-smash distraction: every active citizen/The Men's effective vigilance is halved
 * (witness_ai_tick's own write-back block below) for WITNESS_AI_DISTRACTION_MS -- lower vigilance
 * feeds directly into witness_rules.c's own real noticed(vigilance, conspicuous, roll) formula, so
 * a zombie event happening elsewhere during the window is genuinely less likely to be noticed.
 * Real, honest scope cut: a flat vigilance debuff on every managed NPC scene-wide, not a
 * spatial "everyone near the smash looks that way" simulation -- the real, separate, bigger lift
 * a true distraction-target mechanic would need. */
void witness_ai_smash_cake(unsigned int now_ms) {
    g_distraction_until_ms = now_ms + WITNESS_AI_DISTRACTION_MS;
    printf("[CAKE] smashed -- distraction active for %ums\n", WITNESS_AI_DISTRACTION_MS);
}

int witness_ai_distraction_active(unsigned int now_ms) {
    return now_ms < g_distraction_until_ms;
}

/* Shared by witness_ai_spawn_citizen/witness_ai_spawn_the_men -- identical slot/npc bookkeeping,
 * differing only in the archetype handed to npc_brain_init. Not exposed; callers use the two
 * named wrappers below so a role is always explicit at the call site. */
static int spawn_human(ServerState *s, NpcArchetype archetype, int zone, int base_vigilance,
                        int arrogance, float x, float y, float z, unsigned int now_ms) {
    if (!s) return -1;

    WitnessAiCitizen *c = NULL;
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (!g_citizens[i].active) { c = &g_citizens[i]; break; }
    }
    if (!c) return -1;

    int npc_index = witness_sim_add_npc(&g_sim, zone, base_vigilance, arrogance);
    if (npc_index < 0) return -1;

    int slot = find_free_player_slot(s);
    if (slot < 0) return -1;

    init_bot_player(s, slot, x, y, z);
    c->active = 1;
    c->player_id = slot;
    c->npc_index = npc_index;
    c->men_last_target_pid = -1;
    npc_brain_init(&c->brain, archetype, now_ms);
    return slot;
}

int witness_ai_spawn_citizen(ServerState *s, int zone, int base_vigilance, int arrogance,
                              float x, float y, float z, unsigned int now_ms) {
    return spawn_human(s, NPC_ARCHETYPE_CITIZEN, zone, base_vigilance, arrogance, x, y, z, now_ms);
}

int witness_ai_spawn_the_men(ServerState *s, int zone, int base_vigilance, int arrogance,
                              float x, float y, float z, unsigned int now_ms) {
    return spawn_human(s, NPC_ARCHETYPE_THE_MEN, zone, base_vigilance, arrogance, x, y, z, now_ms);
}

int witness_ai_role_for_player(int player_id) {
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (g_citizens[i].active && g_citizens[i].player_id == player_id) {
            return g_citizens[i].brain.archetype == NPC_ARCHETYPE_THE_MEN
                       ? WITNESS_AI_ROLE_THE_MEN : WITNESS_AI_ROLE_CITIZEN;
        }
    }
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        if (g_zombies[i].active && g_zombies[i].player_id == player_id) return WITNESS_AI_ROLE_ZOMBIE;
    }
    for (int i = 0; i < WITNESS_AI_MAX_GIANT_BUGS; i++) {
        if (g_giant_bugs[i].active && g_giant_bugs[i].player_id == player_id) return WITNESS_AI_ROLE_GIANT_BUG;
    }
    for (int i = 0; i < WITNESS_AI_MAX_BIRDS; i++) {
        if (g_birds[i].active && g_birds[i].player_id == player_id) return WITNESS_AI_ROLE_BIRD;
    }
    return WITNESS_AI_ROLE_NONE;
}

void witness_ai_set_player_costume(int costume) {
    witness_sim_set_costume(&g_sim, 0, costume);
}

int witness_ai_player_decorum(void) {
    return g_sim.p[0].decorum;
}

int witness_ai_player_decorum_band(void) {
    return witness_sim_decorum_band(g_sim.p[0].decorum);
}

/* Real despawn of one managed citizen/zombie -- same "only touch what I spawned" discipline
 * story_ai_despawn_all_characters (S480) already established, narrowed to a single slot. Frees
 * the real PlayerState slot for reuse (find_free_player_slot's own !active check), same as any
 * other bot leaving the match. */
static void deactivate_managed_player(ServerState *s, int player_id) {
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (g_citizens[i].active && g_citizens[i].player_id == player_id) {
            g_citizens[i].active = 0;
            break;
        }
    }
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        if (g_zombies[i].active && g_zombies[i].player_id == player_id) {
            g_zombies[i].active = 0;
            break;
        }
    }
    for (int i = 0; i < WITNESS_AI_MAX_BIRDS; i++) {
        if (g_birds[i].active && g_birds[i].player_id == player_id) { g_birds[i].active = 0; break; }
    }
    if (s && player_id > 0 && player_id < MAX_CLIENTS) s->players[player_id].active = 0;
}

int witness_ai_try_pickup(ServerState *s, float px, float py, float pz, unsigned int now_ms) {
    (void)now_ms;
    if (!s || g_carried_id >= 0) return -1;

    int best_id = -1;
    float best_d2 = WITNESS_AI_PICKUP_RADIUS * WITNESS_AI_PICKUP_RADIUS;
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (!g_citizens[i].active) continue;
        PlayerState *cp = &s->players[g_citizens[i].player_id];
        float dx = cp->x - px, dy = cp->y - py, dz = cp->z - pz;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= best_d2) { best_d2 = d2; best_id = g_citizens[i].player_id; }
    }
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        if (!g_zombies[i].active) continue;
        PlayerState *zp = &s->players[g_zombies[i].player_id];
        float dx = zp->x - px, dy = zp->y - py, dz = zp->z - pz;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= best_d2) { best_d2 = d2; best_id = g_zombies[i].player_id; }
    }

    if (best_id >= 0) {
        g_carried_id = best_id;
        printf("[WHEELBARROW] picked up player=%d role=%d\n", best_id, witness_ai_role_for_player(best_id));
    }
    return best_id;
}

void witness_ai_drop_carried(void) {
    g_carried_id = -1;
}

int witness_ai_carried_player_id(void) {
    return g_carried_id;
}

int witness_ai_lab_deliveries(void) {
    return g_lab_deliveries;
}

/* witness_ai_try_throw_pheromone -- the real consumer pheromone.h's own doc comment named as
 * "phase 7" and still didn't have as of this pass. Self-rate-limited per player_id (so the caller
 * never needs its own edge-trigger state, see protocol.h's own in_pheromone doc comment), throws a
 * marker at the player's current position plus a fixed distance along their own facing (same
 * degrees-to-radians, sin=x/cos=z convention this file's own zombie-chase block already uses when
 * it sets zp->yaw from atan2f). Returns 1 if a marker was actually thrown, 0 on cooldown or an
 * inactive/dead/out-of-range player_id -- matching witness_ai_try_pickup's own real/honest
 * "nothing happened" return convention (that one uses -1, this uses 0, since 0 is never itself a
 * valid "thrown" outcome here, unlike a player_id where 0 is the hero and a real value). */
int witness_ai_try_throw_pheromone(ServerState *s, int player_id, unsigned int now_ms) {
    if (!s || player_id < 0 || player_id >= MAX_CLIENTS) return 0;
    PlayerState *p = &s->players[player_id];
    if (!p->active || p->state == STATE_DEAD) return 0;
    if (now_ms < g_pheromone_next_throw_ms[player_id]) return 0;
    g_pheromone_next_throw_ms[player_id] = now_ms + WITNESS_AI_PHEROMONE_THROW_COOLDOWN_MS;

    pheromone_marker_expire(g_pheromone_markers, PHEROMONE_MAX, now_ms);
    int slot = pheromone_claim_slot(g_pheromone_markers, PHEROMONE_MAX);
    float rad = p->yaw * 3.14159f / 180.0f;
    g_pheromone_markers[slot].active = 1;
    g_pheromone_markers[slot].x = p->x + sinf(rad) * WITNESS_AI_PHEROMONE_THROW_RANGE;
    g_pheromone_markers[slot].z = p->z + cosf(rad) * WITNESS_AI_PHEROMONE_THROW_RANGE;
    g_pheromone_markers[slot].expires_at_ms = now_ms + WITNESS_AI_PHEROMONE_MARKER_LIFETIME_MS;

    printf("[PHEROMONE] player=%d threw marker slot=%d at (%.1f, %.1f)\n",
           player_id, slot, g_pheromone_markers[slot].x, g_pheromone_markers[slot].z);
    reflux_dispatch(REFLUX_ACTION_PHEROMONE_THROWN, player_id, slot, 0);
    return 1;
}

int witness_ai_spawn_zombie(ServerState *s, float x, float y, float z, unsigned int now_ms) {
    if (!s) return -1;

    WitnessAiZombie *zc = NULL;
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        if (!g_zombies[i].active) { zc = &g_zombies[i]; break; }
    }
    if (!zc) return -1;

    int slot = find_free_player_slot(s);
    if (slot < 0) return -1;

    init_bot_player(s, slot, x, y, z);
    zc->active = 1;
    zc->player_id = slot;
    zc->last_attack_ms = 0;
    zc->last_tick_ms = 0; zc->lock_until_ms = 0; zc->wander_until_ms = 0; zc->wander_yaw = 0.0f; zc->wander_pause = 0;
    zc->harvested_reported = 0;
    zombie_state_init(&zc->zstate, now_ms);
    /* not every zombie starts equally hungry -- desynchronises the wander->scent turn */
    zc->zstate.hunger = (float)((((unsigned int)slot * 2654435761u) ^ now_ms) % 40u) / 100.0f;
    reflux_dispatch(REFLUX_ACTION_ZOMBIE_SPAWNED, slot, 0, 0);
    return slot;
}

float witness_ai_zombie_hunger(int player_id) {
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++)
        if (g_zombies[i].active && g_zombies[i].player_id == player_id) return g_zombies[i].zstate.hunger;
    return -1.0f;
}

int witness_ai_spawn_giant_bug(ServerState *s, float x, float y, float z, unsigned int now_ms) {
    if (!s) return -1;

    WitnessAiGiantBug *bc = NULL;
    for (int i = 0; i < WITNESS_AI_MAX_GIANT_BUGS; i++) {
        if (!g_giant_bugs[i].active) { bc = &g_giant_bugs[i]; break; }
    }
    if (!bc) return -1;

    int slot = find_free_player_slot(s);
    if (slot < 0) return -1;

    init_bot_player(s, slot, x, y, z);
    bc->active = 1;
    bc->player_id = slot;
    bc->last_health = 100; /* matches init_bot_player's own real health baseline */
    bc->last_attack_ms = 0;
    giant_bug_state_init(&bc->bstate, now_ms);
    return slot;
}

/* "men are the custodians of the keys for the giant zombie feral ai bugs" -- founder real-time,
 * 2026-09-22. Real, honest, bounded scope: a giant bug's own tick loop below only lets it hunt/
 * eat while at least one live The Men NPC is present to hold the key -- with none active, a
 * spawned bug just sits DORMANT-equivalent (hunger still drifts, but attack/eat never fires).
 * The TRAPX Rogue Swarm Doctrine reference is real, named, and deliberately NOT modeled further
 * here -- that's GTA7's own separate faction-doctrine system, not something to guess at without
 * checking that repo first; a real, separate follow-up. */
int witness_ai_bug_command_authorized(void) {
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (g_citizens[i].active && g_citizens[i].brain.archetype == NPC_ARCHETYPE_THE_MEN) return 1;
    }
    return 0;
}

float witness_ai_bug_strength(int player_id) {
    for (int i = 0; i < WITNESS_AI_MAX_GIANT_BUGS; i++) {
        if (g_giant_bugs[i].active && g_giant_bugs[i].player_id == player_id) return g_giant_bugs[i].bstate.strength;
    }
    return -1.0f;
}

float witness_ai_bug_speed(int player_id) {
    for (int i = 0; i < WITNESS_AI_MAX_GIANT_BUGS; i++) {
        if (g_giant_bugs[i].active && g_giant_bugs[i].player_id == player_id) return g_giant_bugs[i].bstate.speed;
    }
    return -1.0f;
}

int witness_ai_citizen_zone(int player_id) {
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (g_citizens[i].active && g_citizens[i].player_id == player_id) {
            return g_sim.n[g_citizens[i].npc_index].zone;
        }
    }
    return -1;
}

void witness_ai_sync_zones(ServerState *s, const CustomLevelData *level) {
    if (!s || !level) return;
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        WitnessAiCitizen *c = &g_citizens[i];
        if (!c->active) continue;
        PlayerState *cp = &s->players[c->player_id];
        int zt = level_boxes_zone_for_position(level, cp->x, cp->y, cp->z);
        if (zt >= 0) g_sim.n[c->npc_index].zone = zt; /* -1 (no authored zone here) keeps the last zone */
    }
}

void witness_ai_force_zombie_mood(int player_id, int mood) {
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        if (g_zombies[i].active && g_zombies[i].player_id == player_id) {
            g_zombies[i].zstate.mood = (ZombieMood)mood;
            return;
        }
    }
}

int witness_ai_citizen_state(int player_id) {
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (g_citizens[i].active && g_citizens[i].player_id == player_id) {
            return g_sim.n[g_citizens[i].npc_index].state;
        }
    }
    return -1;
}

int witness_ai_citizen_vigilance(int player_id) {
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (g_citizens[i].active && g_citizens[i].player_id == player_id) {
            return g_sim.n[g_citizens[i].npc_index].vigilance;
        }
    }
    return -1;
}

/* --- the birds: BIG_O's avian coalition as live flyers (see witness_ai.h) --- */
int witness_ai_bird_count(void) {
    int n = 0;
    for (int i = 0; i < WITNESS_AI_MAX_BIRDS; i++) if (g_birds[i].active) n++;
    return n;
}

int witness_ai_spawn_bird(ServerState *s, float x, float y, float z, unsigned int now_ms) {
    if (!s) return -1;
    WitnessAiBird *b = NULL;
    for (int i = 0; i < WITNESS_AI_MAX_BIRDS; i++) if (!g_birds[i].active) { b = &g_birds[i]; break; }
    if (!b) return -1;
    int slot = find_free_player_slot(s);
    if (slot < 0) return -1;
    init_bot_player(s, slot, x, y, z);
    s->players[slot].health = 5;
    b->active = 1;
    b->player_id = slot;
    avian_state_init(&b->astate, now_ms);
    b->angle = (float)(slot * 1.7f);
    b->radius = 38.0f + (float)((slot * 7) % 5) * 9.0f;
    b->speed = 0.22f + 0.05f * (float)(slot % 3);
    b->alt = 26.0f + (float)((slot * 5) % 4) * 6.0f;
    return slot;
}

static void witness_ai_birds_tick(ServerState *s, unsigned int now_ms) {
    static unsigned int last_ms;
    float dt = (last_ms == 0 || now_ms < last_ms) ? 0.0f : (float)(now_ms - last_ms) * 0.001f;
    if (dt > 0.25f) dt = 0.25f;
    last_ms = now_ms;
    PlayerState *hero = &s->players[0];
    if (!hero->active) return;

    int signaling = 0;
    for (int i = 0; i < WITNESS_AI_MAX_BIRDS; i++)
        if (g_birds[i].active && (g_birds[i].astate.mood == AVIAN_MOOD_SIGNALING || g_birds[i].astate.mood == AVIAN_MOOD_MOBBING)) signaling++;

    for (int i = 0; i < WITNESS_AI_MAX_BIRDS; i++) {
        WitnessAiBird *b = &g_birds[i];
        if (!b->active) continue;
        PlayerState *bp = &s->players[b->player_id];

        /* Observing the observer: BIG_O/core/avian_live.h's full 3-channel coalition, now ported
           in full (founder real-time 2026-10-08: "bring in all the features we dont have"). A
           bird never needs its own line of sight to the player -- it only needs to see (1) a
           zombie go loud [witness_live_zombie_is_witnessable_event's own HUNTING/FRENZIED gate],
           (2) a citizen's own effective vigilance spike [BIGO_AVIAN_OBSERVED_VIGILANCE_THRESHOLD
           =60], or (3) a human witness_state already escalate into {SILENCING, ENGAGE} -- channel
           3 was the one real gap left when this inline port only had channels 1/2; closed here
           with the same 90-unit radius the other two already use. Deliberately NOT BIG_O's own
           literal BIGO_AVIAN_WITNESS_ESCALATION_MIN=WS_COMPROMISED threshold -- see the
           REFLUX_ACTION_WITNESS_ESCALATED dispatch site below for why that exact value is
           unreachable on this call site, in BIG_O's own original logic too. No new struct/header
           needed since g_sim.n[].state is already this file's own real witness-state storage. */
        int alert = 0;
        for (int zi = 0; zi < WITNESS_AI_MAX_ZOMBIES && !alert; zi++) {
            if (!g_zombies[zi].active) continue;
            if (g_zombies[zi].zstate.mood < ZOMBIE_MOOD_HUNTING) continue;
            PlayerState *zp = &s->players[g_zombies[zi].player_id];
            float dx = zp->x - bp->x, dz = zp->z - bp->z;
            if (dx * dx + dz * dz < 90.0f * 90.0f) alert = 1;
        }
        for (int ci = 0; ci < WITNESS_AI_MAX_CITIZENS && !alert; ci++) {
            if (!g_citizens[ci].active) continue;
            if (npc_brain_effective_vigilance(&g_citizens[ci].brain) < 60) continue;
            PlayerState *cp = &s->players[g_citizens[ci].player_id];
            float dx = cp->x - bp->x, dz = cp->z - bp->z;
            if (dx * dx + dz * dz < 90.0f * 90.0f) alert = 1;
        }
        for (int ci = 0; ci < WITNESS_AI_MAX_CITIZENS && !alert; ci++) {
            if (!g_citizens[ci].active) continue;
            int wstate = g_sim.n[g_citizens[ci].npc_index].state;
            if (wstate != WS_SILENCING && wstate != WS_ENGAGE) continue;
            PlayerState *cp = &s->players[g_citizens[ci].player_id];
            float dx = cp->x - bp->x, dz = cp->z - bp->z;
            if (dx * dx + dz * dz < 90.0f * 90.0f) alert = 1;
        }
        if (alert) avian_get_alerted(&b->astate, now_ms);
        avian_tick(&b->astate, now_ms, signaling);

        /* A signaling flock's beacon pulls dormant/agitated zombies toward the human. */
        if (avian_beacon_strength(&b->astate) > 0.4f) {
            for (int zi = 0; zi < WITNESS_AI_MAX_ZOMBIES; zi++) {
                if (!g_zombies[zi].active || g_zombies[zi].zstate.mood >= ZOMBIE_MOOD_HUNTING) continue;
                zombie_get_agitated(&g_zombies[zi].zstate, now_ms);
            }
        }

        int mobbing = (b->astate.mood == AVIAN_MOOD_MOBBING);
        float rad = mobbing ? 10.0f : b->radius;
        float alt = mobbing ? 9.0f : b->alt;
        b->angle += b->speed * (mobbing ? 2.2f : 1.0f) * dt;
        float tx = hero->x + sinf(b->angle) * rad, tz = hero->z + cosf(b->angle) * rad;
        float ty = hero->y + alt + sinf(b->angle * 3.0f) * 2.0f;
        /* ease toward the orbit slot rather than snapping when the flock re-forms */
        float k = 1.0f - expf(-3.0f * dt);
        bp->x += (tx - bp->x) * k; bp->y += (ty - bp->y) * k; bp->z += (tz - bp->z) * k;
        bp->vx = bp->vy = bp->vz = 0.0f;
        bp->in_fwd = 0.0f;
        bp->yaw = (b->angle * 57.29578f) + 90.0f;
        bp->state = STATE_ALIVE;
        bp->health = 5;
    }
}

/* Wall awareness -- founder real-time, 2026-10-02: "make sure they dont run into the buildings
 * constantly make the citizens at least aware of the walls." Every mover in this file used to set
 * a bare straight-line yaw toward its goal (hero, away-from-zombie, a target citizen) and rely on
 * resolve_collision to stop it, so a citizen fleeing "away" from a zombie just pressed into the
 * nearest building face forever. wai_avoid_walls is a feeler (whisker) steer run as the LAST pass
 * of every tick: it probes the level's real collision boxes (map_geo, the same list the player's
 * own movement collides against, so brick-fracture holes open real doorways for the AI too) along
 * the intended heading, and if the way ahead is blocked picks the nearest-to-intended heading that
 * is clear, sticking to one side per mover so it slides along a wall instead of dithering. Fully
 * boxed in => stands still rather than grinding. Axis-aligned boxes only, same as the level data. */
static float wai_norm_yaw(float y) { while (y > 180.0f) y -= 360.0f; while (y < -180.0f) y += 360.0f; return y; }

static int wai_point_blocked(float x, float y, float z) {
    float feet = y, head = y + WAI_PLAYER_HEIGHT;
    const WaiBox *mg; int mc = wai_map(&mg);
    for (int i = 1; i < mc; i++) {
        const WaiBox *b = &mg[i];
        if (b->w <= 0.0f || b->h <= 0.0f || b->d <= 0.0f) continue;
        if (b->y + b->h * 0.5f < feet + 1.2f) continue;  /* floor / step: walkable */
        if (b->y - b->h * 0.5f > head) continue;          /* overhead */
        if (x > b->x - b->w * 0.5f - WAI_BODY_R && x < b->x + b->w * 0.5f + WAI_BODY_R &&
            z > b->z - b->d * 0.5f - WAI_BODY_R && z < b->z + b->d * 0.5f + WAI_BODY_R) return 1;
    }
    return 0;
}

static int wai_heading_clear(const PlayerState *p, float yaw_deg, float look) {
    float r = yaw_deg * 0.0174533f;
    float sx = sinf(r), cz = cosf(r);
    for (float d = 2.0f; d <= look + 0.01f; d += 2.0f) {
        if (wai_point_blocked(p->x + sx * d, p->y, p->z + cz * d)) return 0;
    }
    return 1;
}

/* wai_probe_wall -- first solid face along (yaw) from the mover, mid-body height. Fills the hit
 * point and outward (toward-mover) normal; returns 1 if a face is within maxd. Axis-aligned boxes:
 * slab entry on the dominant axis of travel gives the exact face. */
static int wai_probe_wall(const PlayerState *p, float yaw_deg, float maxd,
                          float *hx, float *hy, float *hz, float *nx, float *nz) {
    float r = yaw_deg * 0.0174533f, dx = sinf(r), dz = cosf(r);
    float best_t = maxd + 1.0f; int hit = 0;
    float ymid = p->y + WAI_PLAYER_HEIGHT * 0.5f;
    const WaiBox *mg; int mc = wai_map(&mg);
    for (int i = 1; i < mc; i++) {
        const WaiBox *b = &mg[i];
        if (b->w <= 0.0f || b->h <= 0.0f || b->d <= 0.0f) continue;
        if (b->y + b->h * 0.5f < p->y + 1.2f || b->y - b->h * 0.5f > p->y + WAI_PLAYER_HEIGHT) continue;
        float lo[2] = { b->x - b->w * 0.5f, b->z - b->d * 0.5f };
        float hi[2] = { b->x + b->w * 0.5f, b->z + b->d * 0.5f };
        float o[2] = { p->x, p->z }, d[2] = { dx, dz };
        float t0 = 0.0f, t1 = maxd; int axis_in = -1; float sgn = 0.0f;
        int ok = 1;
        for (int a = 0; a < 2 && ok; a++) {
            if (fabsf(d[a]) < 1e-5f) { if (o[a] < lo[a] || o[a] > hi[a]) ok = 0; continue; }
            float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
            float sg = d[a] > 0 ? -1.0f : 1.0f;
            if (ta > tb) { float t = ta; ta = tb; tb = t; }
            if (ta > t0) { t0 = ta; axis_in = a; sgn = sg; }
            if (tb < t1) t1 = tb;
            if (t0 > t1) ok = 0;
        }
        if (!ok || axis_in < 0 || t0 >= best_t) continue;
        best_t = t0; hit = 1;
        *hx = p->x + dx * t0; *hz = p->z + dz * t0; *hy = ymid;
        *nx = (axis_in == 0) ? sgn : 0.0f; *nz = (axis_in == 1) ? sgn : 0.0f;
    }
    return hit;
}

#define WAI_ZOMBIE_CLAW_REACH 3.2f
#define WAI_ZOMBIE_CLAW_COOLDOWN_MS 450u
#define WAI_ZOMBIE_CLAW_DAMAGE 28

/* A hunting zombie whose straight line to its target is walled off tears the wall down instead of
 * steering around it (relentless). Returns 1 if it is breaching this tick (caller skips avoidance). */
static int wai_zombie_breach(ServerState *s, PlayerState *zp, int id, unsigned int now_ms) {
    if (!g_wall_hit_hook) return 0;
    float hx, hy, hz, nx, nz;
    if (!wai_probe_wall(zp, zp->yaw, WAI_ZOMBIE_CLAW_REACH + WAI_BODY_R, &hx, &hy, &hz, &nx, &nz)) return 0;
    zp->in_fwd = 0.0f;                      /* plant and claw */
    zp->anim_override = 1;                  /* GBAND_SKEL_NPC_ANIM_GREET slot == zombie_attack clip */
    if (now_ms - g_zombie_claw_ms[id] >= WAI_ZOMBIE_CLAW_COOLDOWN_MS) {
        g_zombie_claw_ms[id] = now_ms;
        g_wall_hit_hook(zp->scene_id, hx, hy, hz, nx, 0.0f, nz, WAI_ZOMBIE_CLAW_DAMAGE);
        wai_learn_clear_near(hx - nx * 1.0f, hz - nz * 1.0f);   /* the face may be gone now */
    }
    (void)s;
    return 1;
}

/* Learned city map -- card #522 (founder real-time: "the characters in zombie mode are constantly getting
 * stuck on the walls they should learn where the rooms are like a roomba to avoid hitting them they should
 * learn the city little by little"). The whisker steer above only reacts to what is straight ahead, so a
 * mover that walks into a concave pocket (a courtyard, a dead-end alley, a room whose door is behind it)
 * grinds there forever. This is the roomba memory: ONE coarse grid shared by every mover, filled in only by
 * what movers actually experience -- a cell a mover stands in is FREE, a cell its whisker found a wall in or
 * where it got physically stuck is BLOCKED. Nobody reads the level geometry to build the map ahead of time,
 * so the city is learned bump by bump and every mover benefits from every other mover's bumps. When the way
 * ahead is blocked, A* over the learned map (unknown cells cost a little extra, blocked cells are walls)
 * picks a route and the mover commits to it for a moment so it cannot dither at a corner. */
#define WAI_CELL 3.0f
#define WAI_GRID 256
#define WAI_GRID_HALF 128
#define WAI_WIN 24                 /* planner window: +-24 cells (72 m) around the mover */
#define WAI_STUCK_CHECK_MS 800u
#define WAI_STUCK_MIN_MOVE 1.0f
#define WAI_COMMIT_MS 1200u
enum { WAI_UNKNOWN = 0, WAI_FREE = 1, WAI_BLOCKED = 2 };
static unsigned char g_wai_cell[WAI_GRID * WAI_GRID];
static float g_wai_lx[MAX_CLIENTS], g_wai_lz[MAX_CLIENTS];
static unsigned int g_wai_lchk[MAX_CLIENTS], g_wai_commit_until[MAX_CLIENTS];
static float g_wai_commit_yaw[MAX_CLIENTS];
static unsigned int g_wai_now;
static float g_wai_rx[MAX_CLIENTS][48], g_wai_rz[MAX_CLIENTS][48];
static int g_wai_rn[MAX_CLIENTS], g_wai_ri[MAX_CLIENTS];
static unsigned int g_wai_route_until[MAX_CLIENTS];
static float g_wai_gx[MAX_CLIENTS], g_wai_gz[MAX_CLIENTS];
static unsigned char g_wai_blocked_drop[MAX_CLIENTS];

static void wai_learn_reset(void) {
    memset(g_wai_cell, 0, sizeof g_wai_cell); memset(g_wai_lchk, 0, sizeof g_wai_lchk); memset(g_wai_commit_until, 0, sizeof g_wai_commit_until);
    memset(g_wai_blocked_drop, 0, sizeof g_wai_blocked_drop); memset(g_wai_rn, 0, sizeof g_wai_rn); memset(g_wai_ri, 0, sizeof g_wai_ri); memset(g_wai_route_until, 0, sizeof g_wai_route_until);
}
static int wai_cell_xz(float x, float z, int *cx, int *cz) {
    int ix = (int)floorf(x / WAI_CELL) + WAI_GRID_HALF, iz = (int)floorf(z / WAI_CELL) + WAI_GRID_HALF;
    if (ix < 0 || iz < 0 || ix >= WAI_GRID || iz >= WAI_GRID) return 0;
    *cx = ix; *cz = iz; return 1;
}
static void wai_learn_mark(float x, float z, int kind) {
    int cx, cz; if (!wai_cell_xz(x, z, &cx, &cz)) return;
    unsigned char *c = &g_wai_cell[cz * WAI_GRID + cx];
    if (kind == WAI_BLOCKED) *c = WAI_BLOCKED;
    else if (*c == WAI_UNKNOWN) *c = WAI_FREE;          /* standing on a border cell never erases a wall; only a breach does */
}
/* a wall was just torn down at (x,z): forget the walls around it so the next bump re-learns the gap */
static void wai_learn_clear_near(float x, float z) {
    int cx, cz; if (!wai_cell_xz(x, z, &cx, &cz)) return;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        int ix = cx + dx, iz = cz + dz;
        if (ix >= 0 && iz >= 0 && ix < WAI_GRID && iz < WAI_GRID) g_wai_cell[iz * WAI_GRID + ix] = WAI_UNKNOWN;
    }
}
int witness_ai_learned_cell(float x, float z) {
    int cx, cz; return wai_cell_xz(x, z, &cx, &cz) ? g_wai_cell[cz * WAI_GRID + cx] : WAI_UNKNOWN;
}
int witness_ai_learned_blocked_count(void) {
    int n = 0; for (int i = 0; i < WAI_GRID * WAI_GRID; i++) if (g_wai_cell[i] == WAI_BLOCKED) n++; return n;
}

/* A* over the learned map from (sx,sz) toward (gx,gz) inside the window. Fills out_x/out_z with the route's
 * waypoints (cell centres, every 2nd cell, start excluded) and returns how many (0 = no route improves on
 * standing still). If the goal cell is walled off inside the window it routes to the reachable cell closest
 * to the goal (the frontier), which is what lets a mover back out of a pocket. */
#define WAI_ROUTE_MAX 48
static int wai_plan_route(float sx, float sz, float gx, float gz, float *out_x, float *out_z) {
    int scx, scz; if (!wai_cell_xz(sx, sz, &scx, &scz)) return 0;
    int W = 2 * WAI_WIN + 1;
    static float g[(2 * WAI_WIN + 1) * (2 * WAI_WIN + 1)];
    static short parent[(2 * WAI_WIN + 1) * (2 * WAI_WIN + 1)];
    static unsigned char state[(2 * WAI_WIN + 1) * (2 * WAI_WIN + 1)]; /* 0 new, 1 open, 2 closed */
    memset(state, 0, sizeof state);
    int gcx = (int)floorf(gx / WAI_CELL) + WAI_GRID_HALF, gcz = (int)floorf(gz / WAI_CELL) + WAI_GRID_HALF;
    int s_i = WAI_WIN * W + WAI_WIN;
    g[s_i] = 0.0f; parent[s_i] = -1; state[s_i] = 1;
    int best_i = s_i; float best_h = 1e9f;
    for (;;) {
        int cur = -1; float cur_f = 1e30f;
        for (int i = 0; i < W * W; i++) { /* open-set scan: 2401 cells, only runs when blocked */
            if (state[i] != 1) continue;
            int dx = i % W - WAI_WIN + scx - gcx, dz = i / W - WAI_WIN + scz - gcz;
            float f = g[i] + sqrtf((float)(dx * dx + dz * dz));
            if (f < cur_f) { cur_f = f; cur = i; }
        }
        if (cur < 0) break;
        state[cur] = 2;
        int ox = cur % W - WAI_WIN, oz = cur / W - WAI_WIN;
        { int dx = ox + scx - gcx, dz = oz + scz - gcz; float h = sqrtf((float)(dx * dx + dz * dz));
          if (h < best_h - 0.001f) { best_h = h; best_i = cur; }
          if (dx == 0 && dz == 0) break; }
        for (int d = 0; d < 8; d++) {
            static const int ddx[8] = {1,-1,0,0,1,1,-1,-1}, ddz[8] = {0,0,1,-1,1,-1,1,-1};
            int nx = ox + ddx[d], nz = oz + ddz[d];
            if (nx < -WAI_WIN || nx > WAI_WIN || nz < -WAI_WIN || nz > WAI_WIN) continue;
            int wx = scx + nx, wz = scz + nz;
            if (wx < 0 || wz < 0 || wx >= WAI_GRID || wz >= WAI_GRID) continue;
            unsigned char k = g_wai_cell[wz * WAI_GRID + wx];
            if (k == WAI_BLOCKED) continue;
            if (d >= 4) { /* no cutting a corner between two walls */
                if (g_wai_cell[(scz + oz) * WAI_GRID + wx] == WAI_BLOCKED || g_wai_cell[wz * WAI_GRID + (scx + ox)] == WAI_BLOCKED) continue;
            }
            int ni = (nz + WAI_WIN) * W + (nx + WAI_WIN);
            if (state[ni] == 2) continue;
            float step = (d >= 4 ? 1.414f : 1.0f) * (k == WAI_UNKNOWN ? 1.25f : 1.0f);
            if (state[ni] == 0 || g[cur] + step < g[ni]) { g[ni] = g[cur] + step; parent[ni] = (short)cur; state[ni] = 1; }
        }
    }
    if (best_i == s_i) return 0;
    int route[(2 * WAI_WIN + 1) * (2 * WAI_WIN + 1)]; int rn = 0, walk_i = best_i;
    while (walk_i != s_i && walk_i >= 0 && rn < (int)(sizeof route / sizeof route[0])) { route[rn++] = walk_i; walk_i = parent[walk_i]; }
    int n = 0;
    for (int k = rn - 1; k >= 0 && n < WAI_ROUTE_MAX; k -= (k > 1 ? 2 : 1)) {   /* start -> goal order, every 2nd cell, always keep the last */
        int ti = route[k];
        out_x[n] = ((ti % W - WAI_WIN + scx - WAI_GRID_HALF) + 0.5f) * WAI_CELL;
        out_z[n] = ((ti / W - WAI_WIN + scz - WAI_GRID_HALF) + 0.5f) * WAI_CELL;
        n++;
    }
    return n;
}

static int wai_start_route(const PlayerState *p, int id, float gx, float gz) {
    g_wai_gx[id] = gx; g_wai_gz[id] = gz;
    g_wai_rn[id] = wai_plan_route(p->x, p->z, gx, gz, g_wai_rx[id], g_wai_rz[id]);
    g_wai_ri[id] = 0;
    g_wai_route_until[id] = g_wai_now + 9000u;
    return g_wai_rn[id] > 0;
}
/* Steer at the next waypoint of the mover's route. Returns 0 (route dropped) when it ran out, timed out, or
 * the next hop is walled off for real. */
static int wai_follow_route(PlayerState *p, int id) {
    while (g_wai_ri[id] < g_wai_rn[id]) {
        float dx = g_wai_rx[id][g_wai_ri[id]] - p->x, dz = g_wai_rz[id][g_wai_ri[id]] - p->z;
        if (dx * dx + dz * dz > 2.5f * 2.5f) break;
        g_wai_ri[id]++;
    }
    if (g_wai_ri[id] >= g_wai_rn[id] || g_wai_now >= g_wai_route_until[id]) { g_wai_rn[id] = g_wai_ri[id] = 0; return 0; }
    float yaw = atan2f(g_wai_rx[id][g_wai_ri[id]] - p->x, g_wai_rz[id][g_wai_ri[id]] - p->z) * (180.0f / 3.14159f);
    if (!wai_heading_clear(p, yaw, 3.0f)) { g_wai_rn[id] = g_wai_ri[id] = 0; g_wai_blocked_drop[id] = 1; return 0; }
    p->yaw = wai_norm_yaw(yaw);
    return 1;
}

static void wai_sense(const PlayerState *p) {
    int pcx, pcz;   /* proximity ring, like a roomba's bump sensors: feel every cell within 5 cells (15 m) */
    if (wai_cell_xz(p->x, p->z, &pcx, &pcz))
        for (int dz = -5; dz <= 5; dz++) for (int dx = -5; dx <= 5; dx++) {
            float wx = ((pcx + dx) - WAI_GRID_HALF + 0.5f) * WAI_CELL, wz = ((pcz + dz) - WAI_GRID_HALF + 0.5f) * WAI_CELL;
            if (wai_point_blocked(wx, p->y, wz)) wai_learn_mark(wx, wz, WAI_BLOCKED);
        }
    for (int a = -90; a <= 90; a += 15) {   /* and a whisker fan for the longer reach */
        float r = (p->yaw + (float)a) * 0.0174533f, sx = sinf(r), cz = cosf(r);
        for (float d = 2.0f; d <= 14.0f; d += 1.5f)
            if (wai_point_blocked(p->x + sx * d, p->y, p->z + cz * d)) { wai_learn_mark(p->x + sx * d, p->z + cz * d, WAI_BLOCKED); break; }
    }
}

/* relentless: a zombie that can claw walls down (the wall-hit hook exists) walks the straight line and tears
 * through, it does not plan a detour -- it only routes (via the stuck detector) when clawing gets it nowhere. */
static void wai_avoid_walls(PlayerState *p, int id, int relentless) {
    if (p->state == STATE_DEAD) return;
    wai_learn_mark(p->x, p->z, WAI_FREE);               /* roomba: where I stand is open floor */
    if (p->in_fwd <= 0.0f) { g_wai_lchk[id] = 0; return; }
    float intended = p->yaw;                              /* the heading the chase/flee logic wants */

    /* Stuck detector: walking (in_fwd) but not getting anywhere => the cell ahead is a wall I did not
       see (a thin box, a prop, another body wedging me). Learn it and commit to a detour for a moment. */
    if (g_wai_lchk[id] == 0) { g_wai_lchk[id] = g_wai_now ? g_wai_now : 1u; g_wai_lx[id] = p->x; g_wai_lz[id] = p->z; }
    else if (g_wai_now - g_wai_lchk[id] >= WAI_STUCK_CHECK_MS) {
        float mx = p->x - g_wai_lx[id], mz = p->z - g_wai_lz[id];
        if (sqrtf(mx * mx + mz * mz) < WAI_STUCK_MIN_MOVE && g_wai_now >= g_wai_commit_until[id]) {
            float r = p->yaw * 0.0174533f;
            wai_learn_mark(p->x + sinf(r) * WAI_CELL, p->z + cosf(r) * WAI_CELL, WAI_BLOCKED);
            wai_start_route(p, id, p->x + sinf(r) * 60.0f, p->z + cosf(r) * 60.0f);
            g_wai_commit_yaw[id] = wai_norm_yaw(p->yaw + ((id & 1) ? 110.0f : -110.0f));   /* no route: shoulder out sideways */
            g_wai_commit_until[id] = g_wai_now + WAI_COMMIT_MS;
        }
        g_wai_lchk[id] = g_wai_now; g_wai_lx[id] = p->x; g_wai_lz[id] = p->z;
    }
    /* A learned route is being walked: stay on it (the straight line to the goal is the local minimum that
       put us here). If a hop turns out to be walled off for real, learn that and re-route at once -- never
       fall back to the straight line, that is what walks a mover back into the pocket. */
    if (g_wai_ri[id] < g_wai_rn[id] && wai_follow_route(p, id)) return;
    for (int tries = 0; g_wai_blocked_drop[id] && tries < 3; tries++) {
        g_wai_blocked_drop[id] = 0;
        wai_sense(p);
        if (wai_start_route(p, id, g_wai_gx[id], g_wai_gz[id]) && wai_follow_route(p, id)) return;
    }
    g_wai_blocked_drop[id] = 0;
    if (g_wai_now < g_wai_commit_until[id]) {             /* committed detour: hold it until it runs out or runs into a wall */
        if (wai_heading_clear(p, g_wai_commit_yaw[id], 3.0f)) {   /* only the next few metres: the detour bends around the wall */ p->yaw = wai_norm_yaw(g_wai_commit_yaw[id]); return; }
        g_wai_commit_until[id] = 0;
    }

    float look = 6.0f + 6.0f * p->in_fwd;
    if (wai_heading_clear(p, p->yaw, look)) { g_wai_side[id] = 0; return; }

    /* Blocked ahead: sweep the whiskers across a fan to learn the wall's face (every blocked sample along
       each ray becomes a BLOCKED cell), then ask the learned map for a route. */
    if (!relentless) {
        wai_sense(p);
        float r = p->yaw * 0.0174533f;
        if (wai_start_route(p, id, p->x + sinf(r) * 60.0f, p->z + cosf(r) * 60.0f) && wai_follow_route(p, id)) return;
    }
    (void)intended;
    if (g_wai_side[id] == 0) g_wai_side[id] = (id & 1) ? 1 : -1;
    static const float offs[] = { 30.0f, 60.0f, 90.0f, 125.0f, 160.0f };
    for (int k = 0; k < 5; k++) {
        for (int pass = 0; pass < 2; pass++) {
            float sgn = (pass == 0) ? (float)g_wai_side[id] : -(float)g_wai_side[id];
            float y = p->yaw + sgn * offs[k];
            if (wai_heading_clear(p, y, look)) {
                p->yaw = wai_norm_yaw(y);
                if (pass == 1) g_wai_side[id] = (signed char)-g_wai_side[id];
                return;
            }
        }
    }
    p->in_fwd = 0.0f; /* boxed in: stand, don't grind into the wall */
}

void witness_ai_tick(ServerState *s, unsigned int now_ms) {
    if (!s) return;
    g_wai_now = now_ms;

    if (!g_have_last_sim_tick || now_ms - g_last_sim_tick_ms >= WITNESS_AI_SIM_TICK_INTERVAL_MS) {
        witness_sim_tick(&g_sim, 1);
        g_last_sim_tick_ms = now_ms;
        g_have_last_sim_tick = 1;
    }

    int distracted = witness_ai_distraction_active(now_ms);
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        WitnessAiCitizen *c = &g_citizens[i];
        if (!c->active) continue;
        npc_brain_tick(&c->brain, now_ms);
        int vig = npc_brain_effective_vigilance(&c->brain);
        if (distracted) vig /= 2; /* cake-smash distraction, see witness_ai_smash_cake's own doc comment */
        g_sim.n[c->npc_index].vigilance = vig;
    }

    PlayerState *hero_p = &s->players[0];
    int hero_live = hero_p->active && hero_p->state != STATE_DEAD;

    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        WitnessAiZombie *z = &g_zombies[i];
        if (!z->active) continue;
        PlayerState *zp = &s->players[z->player_id];
        if (zp->state == STATE_DEAD) { /* corpse stays put, see
            local_game.h's own MODE_STORY "i>0 dead players never respawn" convention -- a killed
            zombie is a real, permanent kill, not a respawn-timer no-op. */
            zp->in_fwd = 0.0f;
            if (!z->harvested_reported) { /* fire exactly once per corpse, see this struct field's doc comment */
                reflux_dispatch(REFLUX_ACTION_ZOMBIE_HARVESTED, z->player_id, (int)z->zstate.mood, 0);
                z->harvested_reported = 1;
            }
            continue;
        }

        /* Real perception: flat (x,z) radius against the hero, same honest "no line-of-sight
           system yet" boundary the rest of this file's zone/witness radius checks already accept.
           Closes witness_ai.h's own top-doc-comment "has_target is always 0" scope cut. */
        int has_target = 0;
        int targeting_hero = 0; /* distinct from has_target: stays 0 if the marker branch below
            redirects hdx/hdz/hdist at a thrown marker instead -- witness_ai_hero_melee_hit must
            never fire off a distance that's actually measured to a marker, not the hero. */
        float hdx = 0.0f, hdz = 0.0f, hdist = 0.0f;
        int wander_mode = (s->game_mode == MODE_ZOMBIES || s->game_mode == MODE_SURVIVAL);
        if (hero_live && zp->scene_id == hero_p->scene_id) {
            hdx = hero_p->x - zp->x;
            hdz = hero_p->z - zp->z;
            hdist = sqrtf(hdx * hdx + hdz * hdz);
            if (wander_mode) {
                /* Sense radius grows with the zombie's own alertness (mood + hunger + aggression, the
                   PARENA formula); a locked zombie keeps tracking out to 1.6x and remembers for
                   LOCK_MEMORY_MS. Outside that it wanders (below) instead of always knowing where the
                   hero is -- hunger "scent" still walks it home eventually. */
                float sense = WITNESS_AI_ZOMBIE_SENSE_BASE +
                              WITNESS_AI_ZOMBIE_SENSE_PER_ALERTNESS * (float)zombie_effective_alertness(&z->zstate);
                if (z->zstate.mood >= ZOMBIE_MOOD_HUNTING) sense *= 1.6f;
                if (hdist <= sense) z->lock_until_ms = now_ms + WITNESS_AI_ZOMBIE_LOCK_MEMORY_MS;
                has_target = (z->lock_until_ms != 0 && (int)(z->lock_until_ms - now_ms) > 0);
            } else {
                has_target = hdist <= WITNESS_AI_ZOMBIE_PERCEPTION_RADIUS;
            }
            targeting_hero = has_target;
        }

        /* Pheromone marker targeting (2026-10-08, same-day continuation) -- a thrown marker can
           recruit a zombie that has no hero lock at all (BIG_O/NORTHSTAR.md section 10's own "lure
           from a distance" framing); zombies_pheromone_should_steer_to_marker (PARENA, see this
           file's own prototype comment near the top) is the real decision of whether the hero lock
           above wins instead -- deliberately checked AFTER the hero block so has_target already
           reflects whichever the hero-sense computed, and BEFORE zombie_tick_dt below so a marker
           can legitimately drive mood escalation the exact same way hero-sensing already does (BIG_O's
           own v0 "escalates it to FRENZIED" behavior). No change to z->lock_until_ms here: a marker is
           a stationary world object re-checked fresh every tick via pheromone_find_nearest, it needs
           no memory of its own the way a moving hero does. */
        if (wander_mode) {
            pheromone_marker_expire(g_pheromone_markers, PHEROMONE_MAX, now_ms);
            float mtx = 0.0f, mtz = 0.0f;
            if (pheromone_find_nearest(g_pheromone_markers, PHEROMONE_MAX, zp->x, zp->z,
                                        WITNESS_AI_PHEROMONE_DETECTION_RADIUS, &mtx, &mtz) &&
                zombies_pheromone_should_steer_to_marker(has_target)) {
                hdx = mtx - zp->x;
                hdz = mtz - zp->z;
                hdist = sqrtf(hdx * hdx + hdz * hdz);
                has_target = 1;
            }
        }
        {
            float dt = z->last_tick_ms ? (float)(now_ms - z->last_tick_ms) / 1000.0f : 0.05f;
            if (dt > 1.0f) dt = 1.0f;
            z->last_tick_ms = now_ms;
            ZombieMood prev_mood = z->zstate.mood;
            zombie_tick_dt(&z->zstate, now_ms, has_target, dt);
            /* REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED -- the exact instant this zombie becomes a real
               "loud event" (witness_live_zombie_is_witnessable_event's own HUNTING/FRENZIED gate),
               fired once on the DORMANT/AGITATED -> HUNTING/FRENZIED edge, not every tick it stays
               there. Founder real-time 2026-10-08: "we need it all evented with reflux." */
            if (prev_mood < ZOMBIE_MOOD_HUNTING && z->zstate.mood >= ZOMBIE_MOOD_HUNTING) {
                reflux_dispatch(REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED, z->player_id, (int)z->zstate.mood, 0);
            }
        }

        if (wander_mode && !has_target) {
            /* Wander legs of 3-7 s: a random heading, ~30% of them standing still (less when hungry);
               when hunger >= 0.4 a leg is aimed at the hero with probability = hunger. */
            if (z->wander_until_ms == 0 || (int)(now_ms - z->wander_until_ms) >= 0) {
                unsigned int h = now_ms * 2654435761u + (unsigned int)z->player_id * 40503u;
                h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
                float hunger = z->zstate.hunger;
                z->wander_yaw = (float)(h % 360u) - 180.0f;
                z->wander_pause = (int)((h >> 9) % 100u) < (int)(30.0f * (1.0f - hunger));
                if (hero_live && hunger >= 0.4f && (int)((h >> 17) % 100u) < (int)(hunger * 100.0f)) {
                    z->wander_yaw = atan2f(hdx, hdz) * (180.0f / 3.14159f) + (float)((int)((h >> 5) % 61u) - 30);
                    z->wander_pause = 0;
                }
                z->wander_until_ms = now_ms + 3000u + (h >> 20) % 4000u;
            }
            zp->in_fwd = z->wander_pause ? 0.0f : (z->zstate.mood == ZOMBIE_MOOD_AGITATED ? 0.4f : 0.22f);
            if (!z->wander_pause) zp->yaw = z->wander_yaw;
            g_wai_breach[z->player_id] = 0;
            continue;
        }

        /* Chase via the SAME generic accelerate() pipeline story_ai.c's own bots already move
           through -- local_game.h's per-player loop applies p->in_fwd/p->yaw for any active i>0
           player in MODE_STORY, no separate movement system needed here. */
        if (has_target && (z->zstate.mood == ZOMBIE_MOOD_HUNTING || z->zstate.mood == ZOMBIE_MOOD_FRENZIED) &&
            hdist > WITNESS_AI_ZOMBIE_MELEE_RANGE) {
            zp->yaw = atan2f(hdx, hdz) * (180.0f / 3.14159f);
            zp->in_fwd = (z->zstate.mood == ZOMBIE_MOOD_FRENZIED) ? 1.0f : 0.7f;
            g_wai_breach[z->player_id] = (unsigned char)wai_zombie_breach(s, zp, z->player_id, now_ms);
            if (!g_wai_breach[z->player_id] && zp->anim_override == 1) zp->anim_override = 0;
        } else {
            zp->in_fwd = 0.0f;
            g_wai_breach[z->player_id] = 0;
        }

        /* Melee: real, direct hero damage on contact, same shield-then-health order and
           STORY_PHASE_FAILED-on-death handling story_boss_tick's own attack block already
           establishes -- a zombie is a real, second source of lethal threat in VOXWORLD now, not
           just a background prop. targeting_hero (not just has_target) guards this -- a zombie
           that's merely standing on a thrown marker's own (x,z) must never register as hdist to
           the hero being in range. */
        if (targeting_hero && has_target && (z->zstate.mood == ZOMBIE_MOOD_HUNTING || z->zstate.mood == ZOMBIE_MOOD_FRENZIED) &&
            hdist <= WITNESS_AI_ZOMBIE_MELEE_RANGE &&
            now_ms - z->last_attack_ms >= WITNESS_AI_ZOMBIE_ATTACK_COOLDOWN_MS) {
            z->last_attack_ms = now_ms;
            zombie_get_agitated(&z->zstate, now_ms); /* landing a hit is a real stimulus */
            z->zstate.hunger -= WITNESS_AI_ZOMBIE_FEED_HUNGER; /* ...and a feeding one */
            if (z->zstate.hunger < 0.0f) z->zstate.hunger = 0.0f;
            witness_ai_hero_melee_hit(s, hero_p, WITNESS_AI_ZOMBIE_MELEE_DAMAGE, hdx, hdz, now_ms);
        }
    }

    /* Citizen flee: runs away from the nearest HUNTING/FRENZIED zombie within
       WITNESS_AI_CITIZEN_FLEE_RADIUS, independent of (and faster-reacting than) the witness_sim
       state-machine escalation below -- that state machine drives the narrative, this drives what
       the player actually sees the citizen's body do. Closes this file's own "no movement/wander
       AI" scope cut for citizens. */
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        WitnessAiCitizen *c = &g_citizens[i];
        if (!c->active) continue;
        PlayerState *cp = &s->players[c->player_id];
        if (cp->state == STATE_DEAD) { cp->in_fwd = 0.0f; continue; }

        float best_d2 = WITNESS_AI_CITIZEN_FLEE_RADIUS * WITNESS_AI_CITIZEN_FLEE_RADIUS;
        float away_dx = 0.0f, away_dz = 0.0f;
        int fleeing = 0;
        for (int zi = 0; zi < WITNESS_AI_MAX_ZOMBIES; zi++) {
            WitnessAiZombie *z = &g_zombies[zi];
            if (!z->active) continue;
            if (z->zstate.mood != ZOMBIE_MOOD_HUNTING && z->zstate.mood != ZOMBIE_MOOD_FRENZIED) continue;
            PlayerState *zp = &s->players[z->player_id];
            if (zp->state == STATE_DEAD || zp->scene_id != cp->scene_id) continue;
            float dx = cp->x - zp->x, dz = cp->z - zp->z;
            float d2 = dx * dx + dz * dz;
            if (d2 <= best_d2) { best_d2 = d2; away_dx = dx; away_dz = dz; fleeing = 1; }
        }

        if (fleeing) {
            cp->yaw = atan2f(away_dx, away_dz) * (180.0f / 3.14159f);
            cp->in_fwd = 0.85f;
        } else if (s->game_mode == MODE_ZOMBIES && c->brain.archetype != NPC_ARCHETYPE_THE_MEN) {
            /* Sandbox wander: every ~6s pick a heading (or stand), deterministic per citizen. */
            unsigned int ep = now_ms / 6000u + (unsigned int)c->player_id * 2654435761u;
            ep ^= ep >> 13; ep *= 0x5bd1e995u; ep ^= ep >> 15;
            if ((ep & 3u) == 0u) { cp->in_fwd = 0.0f; }
            else { cp->yaw = (float)(ep % 360u) - 180.0f; cp->in_fwd = 0.35f; }
        } else {
            cp->in_fwd = 0.0f;
        }
    }

    /* Giant Zombie Bug tick + real "eat a nearby zombie" consumption -- gated by
       witness_ai_bug_command_authorized's own real "The Men hold the key" check. */
    {
        int live_bugs = 0;
        for (int i = 0; i < WITNESS_AI_MAX_GIANT_BUGS; i++) if (g_giant_bugs[i].active) live_bugs++;
        int authorized = witness_ai_bug_command_authorized();

        for (int i = 0; i < WITNESS_AI_MAX_GIANT_BUGS; i++) {
            WitnessAiGiantBug *bug = &g_giant_bugs[i];
            if (!bug->active) continue;
            PlayerState *bp = &s->players[bug->player_id];
            if (bp->state == STATE_DEAD) { bp->in_fwd = 0.0f; continue; } /* real, permanent kill --
                same "corpse stays, no respawn" convention as zombies/citizens above. */

            /* Real pain feedback: the player could already shoot a bug (generic hitscan, no role
               exclusion), but nothing ever fed that damage into GiantBugState.pain before this --
               giant_bug_attack_drive/flee_drive had a real input that never moved. No precise
               damage-source hook exists yet, so this is a real, honest proxy: any health drop
               since last tick becomes pain, same 0..1 scale every other bstate field uses. */
            if (bp->health < bug->last_health) {
                float hurt = (float)(bug->last_health - bp->health) / 100.0f;
                bug->bstate.pain += hurt;
                if (bug->bstate.pain > 1.0f) bug->bstate.pain = 1.0f;
            }
            bug->last_health = bp->health;

            int has_target = 0;
            int eaten_zi = -1;
            if (authorized) {
                for (int zi = 0; zi < WITNESS_AI_MAX_ZOMBIES; zi++) {
                    WitnessAiZombie *z = &g_zombies[zi];
                    if (!z->active) continue;
                    PlayerState *zp = &s->players[z->player_id];
                    float dx = zp->x - bp->x, dy = zp->y - bp->y, dz = zp->z - bp->z;
                    if (dx * dx + dy * dy + dz * dz <= WITNESS_AI_BUG_EAT_RADIUS * WITNESS_AI_BUG_EAT_RADIUS) {
                        has_target = 1;
                        eaten_zi = zi;
                        break;
                    }
                }
            }

            giant_bug_tick(&bug->bstate, now_ms, has_target, live_bugs - 1);

            if (eaten_zi >= 0) {
                WitnessAiZombie *prey = &g_zombies[eaten_zi];
                giant_bug_eat_zombie(&bug->bstate, &prey->zstate, now_ms);
                printf("[GIANT BUG] player=%d ate zombie player=%d -- strength=%.2f speed=%.2f\n",
                       bug->player_id, prey->player_id, bug->bstate.strength, bug->bstate.speed);
                /* REFLUX_ACTION_GIANT_BUG_ATE_ZOMBIE -- fired exactly once per real meal, the same
                   instant this entity's own real eat-to-grow mechanic fires, not on a timer.
                   Founder real-time 2026-10-08 continuation: "Giant Zombie Bug/The Men REFLUX
                   events" (EMILY/BACKLOG.md #4450's own named still-open item). */
                reflux_dispatch(REFLUX_ACTION_GIANT_BUG_ATE_ZOMBIE, bug->player_id, prey->player_id, 0);
                prey->active = 0;
                s->players[prey->player_id].active = 0;
            }

            /* Real chase/flee/melee against the hero, PARENA-decided (giant_bug_attack_drive/
               flee_drive), gated by the SAME "The Men hold the key" authorization the eat mechanic
               already uses -- an unauthorized bug stays DORMANT-equivalent for combat too, matching
               this entity's own "leashed asset" design intent rather than inventing a new rule. */
            bp->in_fwd = 0.0f;
            if (authorized && hero_live && bp->scene_id == hero_p->scene_id) {
                float bdx = hero_p->x - bp->x, bdz = hero_p->z - bp->z;
                float bdist = sqrtf(bdx * bdx + bdz * bdz);
                int flee_drive = giant_bug_flee_drive(&bug->bstate);
                int attack_drive = giant_bug_attack_drive(&bug->bstate);

                if (bdist <= WITNESS_AI_BUG_PERCEPTION_RADIUS &&
                    flee_drive >= WITNESS_AI_BUG_FLEE_DRIVE_THRESHOLD && flee_drive > attack_drive) {
                    bp->yaw = atan2f(-bdx, -bdz) * (180.0f / 3.14159f); /* directly away */
                    bp->in_fwd = 0.8f;
                } else if (bdist <= WITNESS_AI_BUG_PERCEPTION_RADIUS &&
                           attack_drive >= WITNESS_AI_BUG_ATTACK_DRIVE_THRESHOLD) {
                    if (bdist > WITNESS_AI_BUG_MELEE_RANGE) {
                        bp->yaw = atan2f(bdx, bdz) * (180.0f / 3.14159f);
                        /* speed grows permanently from eating zombies (giant_bug_eat_zombie) --
                           a real, live reward for the "if they eat a fast zombie they get faster"
                           founder ask, now visible in how hard this bug is to outrun. */
                        bp->in_fwd = 0.6f + 0.1f * (bug->bstate.speed - 1.0f);
                        if (bp->in_fwd > 1.0f) bp->in_fwd = 1.0f;
                    } else if (now_ms - bug->last_attack_ms >= WITNESS_AI_BUG_ATTACK_COOLDOWN_MS) {
                        bug->last_attack_ms = now_ms;
                        /* strength grows permanently the same way -- a bug that has eaten hits
                           harder, a real, live payoff for the same mechanic. */
                        int damage = (int)((float)WITNESS_AI_BUG_BASE_MELEE_DAMAGE * bug->bstate.strength);
                        witness_ai_hero_melee_hit(s, hero_p, damage, bdx, bdz, now_ms);
                    }
                }
            }
        }
    }

    for (int zi = 0; zi < WITNESS_AI_MAX_ZOMBIES; zi++) {
        WitnessAiZombie *z = &g_zombies[zi];
        if (!z->active) continue;
        if (!witness_live_zombie_is_witnessable_event(z->zstate.mood)) continue;

        PlayerState *zp = &s->players[z->player_id];
        int count = 0;
        for (int ci = 0; ci < WITNESS_AI_MAX_CITIZENS; ci++) {
            WitnessAiCitizen *c = &g_citizens[ci];
            if (!c->active) continue;
            PlayerState *cp = &s->players[c->player_id];
            if (witness_live_in_range(zp->x, zp->z, cp->x, cp->z, WITNESS_LIVE_DETECTION_RADIUS)) count++;
        }
        if (count == 0) continue;

        for (int ci = 0; ci < WITNESS_AI_MAX_CITIZENS; ci++) {
            WitnessAiCitizen *c = &g_citizens[ci];
            if (!c->active) continue;
            PlayerState *cp = &s->players[c->player_id];
            if (!witness_live_in_range(zp->x, zp->z, cp->x, cp->z, WITNESS_LIVE_DETECTION_RADIUS)) continue;

            WitnessNpc *wn = &g_sim.n[c->npc_index];
            int prev_wstate = wn->state;
            wn->state = witness_live_next_state_for_event(wn->state, count, wn->arrogance, 0);
            /* REFLUX_ACTION_WITNESS_ESCALATED -- fired once on the real edge into {SILENCING,
               ENGAGE}, not every tick a citizen stays there. Deliberately NOT BIG_O/core/
               avian_live.h's own literal BIGO_AVIAN_WITNESS_ESCALATION_MIN=WS_COMPROMISED
               threshold: witness_live_next_state_for_event always passes compromised=0 to
               npc_next_state (by design, see witness_live.h's own doc comment -- "no forced-
               compromise mechanic live here"), and npc_next_state/witness_state's own real,
               checked logic (witness_rules.c) only ever returns WS_COMPROMISED when compromised==1
               -- so that exact threshold is dead/unreachable on this call site (plausibly in
               BIG_O's own original too, since this is the same generated witness_rules.c, "logic
               unchanged"). {SILENCING, ENGAGE} is the real, reachable, already-load-bearing
               threshold in THIS codebase instead: it's the exact same test the dispatch loop
               below already uses to decide a citizen needs The Men. */
            if (prev_wstate != WS_SILENCING && prev_wstate != WS_ENGAGE &&
                (wn->state == WS_SILENCING || wn->state == WS_ENGAGE)) {
                reflux_dispatch(REFLUX_ACTION_WITNESS_ESCALATED, c->player_id, wn->state, 0);
            }
        }
    }

    /* The Men's dispatch/resolution loop -- closes witness_ai.h's own long-standing "no
     * resolution/memory-wipe loop" scope cut. Founder real-time (2026-09-25 follow-up to "add more
     * affordances... citizens reacting all the things"): a citizen escalated to SILENCING/ENGAGE
     * by the loop above previously stayed there FOREVER (witness_live_next_state_for_event's own
     * documented persistence rule) -- nothing in this engine ever called the real resolved=1 path
     * that witness_sim_memory_wipe already implements (phase 2, witness_sim.c, real and tested
     * since before this file existed, just never given a live caller). Each live "The Men" NPC
     * (spawn_human's NPC_ARCHETYPE_THE_MEN, tracked in the same g_citizens[] pool as ordinary
     * citizens) now hunts down the nearest SILENCING/ENGAGE citizen in its own scene within
     * WITNESS_AI_MEN_RESPONSE_RADIUS, walks to it via the same generic accelerate() pipeline every
     * other NPC in this file now uses, and once within WITNESS_LIVE_DISPATCH_ARRIVAL_RADIUS
     * (witness_live.h's own real, previously-unused constant) resolves EVERY hunting citizen in
     * that same zone with one witness_sim_memory_wipe call -- matching witness_sim.c's own real
     * "cleanup crew sweeps a whole zone, not one person at a time" shape (resolve_hunters takes a
     * zone, not an npc index). Idle when nothing needs cleaning up (in_fwd forced to 0), same
     * "stand guard" default the rest of this file's NPCs already fall back to. */
    for (int mi = 0; mi < WITNESS_AI_MAX_CITIZENS; mi++) {
        WitnessAiCitizen *man = &g_citizens[mi];
        if (!man->active || man->brain.archetype != NPC_ARCHETYPE_THE_MEN) continue;
        PlayerState *mp = &s->players[man->player_id];
        if (mp->state == STATE_DEAD) { mp->in_fwd = 0.0f; continue; }

        int target_ci = -1;
        float best_d2 = WITNESS_AI_MEN_RESPONSE_RADIUS * WITNESS_AI_MEN_RESPONSE_RADIUS;
        float target_dx = 0.0f, target_dz = 0.0f, target_dist = 0.0f;
        for (int ci = 0; ci < WITNESS_AI_MAX_CITIZENS; ci++) {
            WitnessAiCitizen *c = &g_citizens[ci];
            if (!c->active || c->brain.archetype == NPC_ARCHETYPE_THE_MEN) continue;
            int wstate = g_sim.n[c->npc_index].state;
            if (wstate != WS_SILENCING && wstate != WS_ENGAGE) continue;
            PlayerState *cp = &s->players[c->player_id];
            if (cp->state == STATE_DEAD || cp->scene_id != mp->scene_id) continue;
            float dx = cp->x - mp->x, dz = cp->z - mp->z;
            float d2 = dx * dx + dz * dz;
            if (d2 <= best_d2) {
                best_d2 = d2; target_ci = ci; target_dx = dx; target_dz = dz;
                target_dist = sqrtf(d2);
            }
        }

        if (target_ci < 0) { mp->in_fwd = 0.0f; man->men_last_target_pid = -1; continue; }

        /* REFLUX_ACTION_MEN_DISPATCHED -- fired once on the real edge where this Man acquires a
           NEW hunt target (no target before, or a different citizen than last tick), not every
           tick spent still closing on the same one. Founder real-time 2026-10-08 continuation:
           "Giant Zombie Bug/The Men REFLUX events" (EMILY/BACKLOG.md #4450). */
        {
            int target_pid = g_citizens[target_ci].player_id;
            if (target_pid != man->men_last_target_pid) {
                reflux_dispatch(REFLUX_ACTION_MEN_DISPATCHED, man->player_id, target_pid, 0);
                man->men_last_target_pid = target_pid;
            }
        }

        if (target_dist <= WITNESS_LIVE_DISPATCH_ARRIVAL_RADIUS) {
            mp->in_fwd = 0.0f;
            int zone = g_sim.n[g_citizens[target_ci].npc_index].zone;
            int resolved_count = witness_sim_memory_wipe(&g_sim, zone);
            if (resolved_count > 0) {
                printf("[THE MEN] player=%d resolved %d hunting NPC(s) in zone=%s\n",
                       man->player_id, resolved_count, witness_sim_zone_name(zone));
                /* REFLUX_ACTION_MEN_RESOLVED -- fired exactly on this real sweep, matching the
                   printf's own existing resolved_count>0 gate (no separate edge-tracking needed:
                   witness_sim_memory_wipe already moves every resolved citizen out of
                   {SILENCING, ENGAGE}, so a zone with nothing left to resolve naturally returns 0
                   on the next sweep instead of re-firing every tick). */
                reflux_dispatch(REFLUX_ACTION_MEN_RESOLVED, man->player_id, resolved_count, zone);
            }
        } else {
            mp->yaw = atan2f(target_dx, target_dz) * (180.0f / 3.14159f);
            mp->in_fwd = 0.75f;
        }
    }

    /* Player-zone trespass check ("costume changes" follow-up). Self-throttled to roughly once
     * per second, matching the ambient sim tick's own cadence above. player_id 0 is always the
     * hero (story_ai.c's own convention, and witness_sim_init's own nplayers=1 slot). */
    if (!g_have_last_player_zone_check || now_ms - g_last_player_zone_check_ms >= 1000u) {
        g_have_last_player_zone_check = 1;
        g_last_player_zone_check_ms = now_ms;
        if (s->scene_id == SCENE_VOXWORLD) {
            PlayerState *hero = &s->players[0];
            float dx = hero->x - WITNESS_AI_LAB_ZONE_CX;
            float dz = hero->z - WITNESS_AI_LAB_ZONE_CZ;
            int in_lab = (dx * dx + dz * dz) <= (WITNESS_AI_LAB_ZONE_RADIUS * WITNESS_AI_LAB_ZONE_RADIUS);
            int zone = in_lab ? ZONE_LAB : ZONE_PUBLIC;
            if (zone != g_player_zone) {
                witness_sim_enter(&g_sim, 0, zone);
                g_player_zone = zone;
            }
        }
    }

    /* "wheelbarrow" carry: trail the carried NPC just behind the hero every tick (same real
     * "pinned to the carrier" shape CTF's own carried_flag_team_id uses, just position instead of
     * a UI flag), then check delivery into the same hardcoded lab circle above. Real, deliberate
     * choice: delivery only checks scene==SCENE_VOXWORLD + the lab circle, same as the zone check
     * above -- there is no other real "lab" location anywhere yet. */
    if (g_carried_id > 0 && g_carried_id < MAX_CLIENTS && s->players[g_carried_id].active) {
        PlayerState *hero = &s->players[0];
        PlayerState *cargo = &s->players[g_carried_id];
        float yaw_rad = hero->yaw * 0.0174533f;
        cargo->x = hero->x - sinf(yaw_rad) * 2.0f;
        cargo->z = hero->z + cosf(yaw_rad) * 2.0f;
        cargo->y = hero->y;
        cargo->vx = cargo->vy = cargo->vz = 0.0f;

        if (s->scene_id == SCENE_VOXWORLD) {
            float dx = cargo->x - WITNESS_AI_LAB_ZONE_CX;
            float dz = cargo->z - WITNESS_AI_LAB_ZONE_CZ;
            if (dx * dx + dz * dz <= WITNESS_AI_LAB_ZONE_RADIUS * WITNESS_AI_LAB_ZONE_RADIUS) {
                printf("[WHEELBARROW] delivered player=%d to the lab (total=%d)\n",
                       g_carried_id, g_lab_deliveries + 1);
                deactivate_managed_player(s, g_carried_id);
                g_carried_id = -1;
                g_lab_deliveries++;
            }
        }
    }

    witness_ai_birds_tick(s, now_ms);

    /* Wall awareness pass -- see wai_avoid_walls. Runs last so it steers whatever heading the
       flee/chase/men/bug logic above settled on. */
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++)
        if (g_citizens[i].active) wai_avoid_walls(&s->players[g_citizens[i].player_id], g_citizens[i].player_id, 0);
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++)
        if (g_zombies[i].active && !g_wai_breach[g_zombies[i].player_id])
            wai_avoid_walls(&s->players[g_zombies[i].player_id], g_zombies[i].player_id, g_wall_hit_hook != NULL);
    for (int i = 0; i < WITNESS_AI_MAX_GIANT_BUGS; i++)
        if (g_giant_bugs[i].active) wai_avoid_walls(&s->players[g_giant_bugs[i].player_id], g_giant_bugs[i].player_id, 0);
}


/* --- MODE_ZOMBIES day/night population lifecycle (see witness_ai.h) --- */
static unsigned int g_zrng = 0x2545F491u;
static unsigned int zrand(void) { g_zrng ^= g_zrng << 13; g_zrng ^= g_zrng >> 17; g_zrng ^= g_zrng << 5; return g_zrng; }
/* Card #486: is the straight (x,z) line hero -> (x,z) walled off? Sampled every 3 units at ground
   level, same honest "boxes only, no real raycast" model the rest of this file uses. */
static int zombies_hidden_from(const PlayerState *hero, float x, float z) {
    float dx = x - hero->x, dz = z - hero->z, d = sqrtf(dx * dx + dz * dz);
    for (float t = 3.0f; t < d; t += 3.0f) {
        if (wai_point_blocked(hero->x + dx * (t / d), 0.0f, hero->z + dz * (t / d))) return 1;
    }
    return 0;
}
/* hide != 0: prefer a spot with a building between it and the hero (spawn "behind buildings", out of
   sight), falling back to any open spot if none is found. */
static int zombies_ring_spot_ex(const PlayerState *hero, float rmin, float rmax, int hide, float *ox, float *oz) {
    for (int t = 0; t < (hide ? 40 : 12); t++) {
        float a = (float)(zrand() % 3600u) * (6.2831853f / 3600.0f);
        float r = rmin + (float)(zrand() % 1000u) * 0.001f * (rmax - rmin);
        float x = hero->x + sinf(a) * r, z = hero->z + cosf(a) * r;
        if (wai_point_blocked(x, 8.0f, z) || wai_point_blocked(x, 0.0f, z)) continue;
        if (hide && !zombies_hidden_from(hero, x, z)) continue;
        *ox = x; *oz = z; return 1;
    }
    return hide ? zombies_ring_spot_ex(hero, rmin, rmax, 0, ox, oz) : 0;
}
static int zombies_ring_spot(const PlayerState *hero, float rmin, float rmax, float *ox, float *oz) {
    return zombies_ring_spot_ex(hero, rmin, rmax, 0, ox, oz);
}

void witness_ai_zombies_tick(ServerState *s, unsigned int now_ms) {
    if (!s || s->game_mode != MODE_ZOMBIES) return;
    PlayerState *hero = &s->players[0];
    if (!hero->active) return;

    /* The birds are there from day 1: the first call seeds the whole flock, and the flock is
       topped back up whenever one is lost, regardless of time of day. */
    while (witness_ai_bird_count() < 5) {
        float bx = hero->x + (float)((int)(zrand() % 60u) - 30), bz = hero->z + (float)((int)(zrand() % 60u) - 30);
        if (witness_ai_spawn_bird(s, bx, hero->y + 30.0f, bz, now_ms) < 0) break;
    }

    int phase = (int)day_night_clock_phase(&s->story_clock);
    int want_z, want_c;
    switch (phase) {
        case DNC_DAWN:  want_z = 3;  want_c = 5; break;
        case DNC_DAY:   want_z = 2;  want_c = 8; break;
        case DNC_DUSK:  want_z = 6;  want_c = 5; break;
        default:        want_z = 12; want_c = 2; break; /* night */
    }
    if (want_z > WITNESS_AI_MAX_ZOMBIES) want_z = WITNESS_AI_MAX_ZOMBIES;

    int nz = 0, nc = 0, nm = 0;
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) if (g_zombies[i].active && s->players[g_zombies[i].player_id].state != STATE_DEAD) nz++;
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        if (!g_citizens[i].active) continue;
        if (g_citizens[i].brain.archetype == NPC_ARCHETYPE_THE_MEN) nm++; else nc++;
    }

    /* Cull: corpses are cleaned up (slot freed) once the hero is well away; surplus zombies
       "burn at dawn" and surplus citizens "go home" -- only ever out of the hero's sight. */
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        if (!g_zombies[i].active) continue;
        PlayerState *zp = &s->players[g_zombies[i].player_id];
        float dx = zp->x - hero->x, dz = zp->z - hero->z, d2 = dx * dx + dz * dz;
        if (zp->state == STATE_DEAD && d2 > 40.0f * 40.0f) { deactivate_managed_player(s, g_zombies[i].player_id); continue; }
        if (nz > want_z && d2 > 70.0f * 70.0f) { deactivate_managed_player(s, g_zombies[i].player_id); nz--; }
    }
    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS && nc > want_c; i++) {
        if (!g_citizens[i].active || g_citizens[i].brain.archetype == NPC_ARCHETYPE_THE_MEN) continue;
        PlayerState *cp = &s->players[g_citizens[i].player_id];
        float dx = cp->x - hero->x, dz = cp->z - hero->z;
        if (dx * dx + dz * dz > 70.0f * 70.0f) { deactivate_managed_player(s, g_citizens[i].player_id); nc--; }
    }

    if (now_ms - g_zlast_spawn_ms < 2500u) return;
    g_zlast_spawn_ms = now_ms;
    float x, z;
    if (nz < want_z && zombies_ring_spot_ex(hero, 72.0f, 130.0f, 1, &x, &z)) {
        int id = witness_ai_spawn_zombie(s, x, 8.0f, z, now_ms);
        if (id > 0) witness_ai_force_zombie_mood(id, ZOMBIE_MOOD_HUNTING);
    } else if (nc < want_c && zombies_ring_spot(hero, 60.0f, 150.0f, &x, &z)) {
        witness_ai_spawn_citizen(s, ZONE_PUBLIC, 30 + (int)(zrand() % 30u), 20 + (int)(zrand() % 30u), x, 8.0f, z, now_ms);
    } else if (nm < 2 && zombies_ring_spot(hero, 80.0f, 160.0f, &x, &z)) {
        witness_ai_spawn_the_men(s, ZONE_PUBLIC, 85, 15, x, 8.0f, z, now_ms);
    }
}

void witness_ai_seed_voxworld_encounter(ServerState *s, unsigned int now_ms) {
    if (!s || s->scene_id != SCENE_VOXWORLD) return;

    float cx = 0.0f;
    float cz = -260.0f;

    /* Ambient citizens -- same spatial footprint as the old story_ai_seed_voxworld_encounter's
       own enemy placement, so this plays out in the same real space, not an arbitrary new one. */
    witness_ai_spawn_citizen(s, ZONE_PUBLIC, 40, 30, cx - 42.0f, 8.0f, cz - 20.0f, now_ms);
    witness_ai_spawn_citizen(s, ZONE_PUBLIC, 45, 25, cx + 38.0f, 8.0f, cz + 16.0f, now_ms);
    witness_ai_spawn_citizen(s, ZONE_PUBLIC, 35, 40, cx + 4.0f, 8.0f, cz - 54.0f, now_ms);
    witness_ai_spawn_citizen(s, ZONE_PUBLIC, 50, 20, cx - 4.0f, 8.0f, cz + 46.0f, now_ms);

    int z1 = witness_ai_spawn_zombie(s, cx + 60.0f, 8.0f, cz - 90.0f, now_ms);
    witness_ai_spawn_zombie(s, cx - 60.0f, 8.0f, cz - 10.0f, now_ms);

    /* Real, honest bootstrap -- see this function's own header doc comment for why. */
    if (z1 > 0) witness_ai_force_zombie_mood(z1, ZOMBIE_MOOD_HUNTING);

    /* The Men -- "full phone app parity, costume changes, add The Men" follow-up. Stationed near
     * the hardcoded WITNESS_AI_LAB_ZONE_* trespass circle above (110, -260): a real, in-fiction
     * reason to be there (guarding the lab), not an arbitrary placement. High base_vigilance (85)
     * matches npc_archetype.h's own real archetype default; low arrogance (15) since The Men are
     * professional/focused, not swaggering. Does NOT wire up server_tick_dispatch's own SILENCING
     * -> resolved dispatch/sanitize loop (BIG_O core/witness_live.h's own real, separate,
     * still-not-ported follow-up -- see "the men carry pagers" design note, BIG_O/NORTHSTAR.md
     * S11 item 6) -- this is the archetype/spawn/visual half only. */
    witness_ai_spawn_the_men(s, ZONE_PUBLIC, 85, 15, cx + 110.0f, 8.0f, cz, now_ms);

    /* Giant Zombie Bug ("feral AI units" -- founder real-time, 2026-09-22) -- placed well clear
     * of the lab circle (110,-260 r18), the food-pickup ring (0,-260 r~70), and the Lost and
     * Found (-150,-260 r12): a real, distinct patch of the same VOXWORLD space, not overlapping
     * any existing landmark. Stays DORMANT-equivalent (see witness_ai_bug_command_authorized's
     * own doc comment) until The Men spawned above are active, which they are here. */
    witness_ai_spawn_giant_bug(s, cx + 150.0f, 8.0f, cz - 150.0f, now_ms);

    printf("[WITNESS] voxworld encounter seeded: 4 citizens, 2 zombies (1 HUNTING), 1 The Men, 1 Giant Zombie Bug\n");
}


/* --- MODE_SURVIVAL: wave defence on the built-in city (card #482/#488) ---
 * Waves of always-hunting zombies, spawned hidden behind buildings near the hero. A wave is
 * `quota` zombies, at most `maxalive` on the field at once; when the quota is spawned and the last
 * one is dead the next wave starts after an intermission. No day/night, citizens, birds or The
 * Men -- this is the pure fight; MODE_ZOMBIES is the sandbox. Hero death is handled where the
 * zombie melee lands (witness_ai_hero_melee_hit), same as every other mode. */
static struct { int wave, spawned, kills_total; unsigned int next_spawn_ms, wave_start_ms; int started; } g_surv;
#define SURV_INTERMISSION_MS 6000u
#define SURV_SPAWN_GAP_MS 15000u /* founder 2026-10-04: one new zombie every 15 s (was 1100) */

int witness_ai_survival_quota(int wave) { return 4 + 3 * (wave < 1 ? 1 : wave); }
int witness_ai_survival_maxalive(int wave) {
    int m = 5 + (wave < 1 ? 1 : wave);
    return m > WITNESS_AI_MAX_ZOMBIES ? WITNESS_AI_MAX_ZOMBIES : m;
}
void witness_ai_survival_reset(void) { memset(&g_surv, 0, sizeof g_surv); }
int witness_ai_survival_wave(void) { return g_surv.wave; }
int witness_ai_survival_remaining(const ServerState *s) {
    int alive = 0;
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++)
        if (g_zombies[i].active && s->players[g_zombies[i].player_id].state != STATE_DEAD) alive++;
    int left = witness_ai_survival_quota(g_surv.wave) - g_surv.spawned;
    return alive + (left > 0 ? left : 0);
}

void witness_ai_survival_tick(ServerState *s, unsigned int now_ms) {
    if (!s || s->game_mode != MODE_SURVIVAL) return;
    PlayerState *hero = &s->players[0];
    if (!hero->active || hero->state == STATE_DEAD) return;
    if (!g_surv.started) {
        g_surv.started = 1; g_surv.wave = 1; g_surv.spawned = 0;
        g_surv.wave_start_ms = now_ms; g_surv.next_spawn_ms = now_ms + SURV_INTERMISSION_MS / 2;
    }
    int alive = 0;
    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++)
        if (g_zombies[i].active && s->players[g_zombies[i].player_id].state != STATE_DEAD) alive++;
    int quota = witness_ai_survival_quota(g_surv.wave);

    if (g_surv.spawned >= quota && alive == 0) {
        /* wave cleared: free the corpses, rest, then the next wave */
        for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++)
            if (g_zombies[i].active) deactivate_managed_player(s, g_zombies[i].player_id);
        g_surv.wave++; g_surv.spawned = 0;
        g_surv.wave_start_ms = now_ms;
        g_surv.next_spawn_ms = now_ms + SURV_INTERMISSION_MS;
        return;
    }
    if (g_surv.spawned < quota && alive < witness_ai_survival_maxalive(g_surv.wave) && now_ms >= g_surv.next_spawn_ms) {
        float x, z;
        if (zombies_ring_spot_ex(hero, 60.0f, 130.0f, 1, &x, &z)) {
            int id = witness_ai_spawn_zombie(s, x, 8.0f, z, now_ms);
            if (id > 0) {
                witness_ai_force_zombie_mood(id, ZOMBIE_MOOD_HUNTING);
                g_surv.spawned++;
            }
        }
        g_surv.next_spawn_ms = now_ms + SURV_SPAWN_GAP_MS;
    }
}
