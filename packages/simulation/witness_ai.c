#include "witness_ai.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "npc_archetype.h"
#include "zombie_values.h"
#include "witness_live.h"
#include "giant_bug_values.h"

typedef struct {
    int active;
    int player_id;
    int npc_index;   /* index into g_sim.n[] */
    NpcBrain brain;
} WitnessAiCitizen;

typedef struct {
    int active;
    int player_id;
    ZombieState zstate;
    unsigned int last_attack_ms; /* zombie perception/melee, see witness_ai.h's own doc comment */
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

#define WITNESS_AI_MAX_GIANT_BUGS 8
#define WITNESS_AI_BUG_EAT_RADIUS 4.0f

static WitnessSim g_sim;
static WitnessAiCitizen g_citizens[WITNESS_AI_MAX_CITIZENS];
static WitnessAiZombie g_zombies[WITNESS_AI_MAX_ZOMBIES];
static WitnessAiGiantBug g_giant_bugs[WITNESS_AI_MAX_GIANT_BUGS];
static unsigned int g_last_sim_tick_ms;
static int g_have_last_sim_tick;

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
    g_have_last_sim_tick = 0;
    g_player_zone = ZONE_PUBLIC; /* matches witness_sim_init's own player-0 default */
    g_have_last_player_zone_check = 0;
    g_carried_id = -1;
    g_lab_deliveries = 0;
    g_distraction_until_ms = 0;
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
    zombie_state_init(&zc->zstate, now_ms);
    return slot;
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

void witness_ai_tick(ServerState *s, unsigned int now_ms) {
    if (!s) return;

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
        if (zp->state == STATE_DEAD) { zp->in_fwd = 0.0f; continue; } /* corpse stays put, see
            local_game.h's own MODE_STORY "i>0 dead players never respawn" convention -- a killed
            zombie is a real, permanent kill, not a respawn-timer no-op. */

        /* Real perception: flat (x,z) radius against the hero, same honest "no line-of-sight
           system yet" boundary the rest of this file's zone/witness radius checks already accept.
           Closes witness_ai.h's own top-doc-comment "has_target is always 0" scope cut. */
        int has_target = 0;
        float hdx = 0.0f, hdz = 0.0f, hdist = 0.0f;
        if (hero_live && zp->scene_id == hero_p->scene_id) {
            hdx = hero_p->x - zp->x;
            hdz = hero_p->z - zp->z;
            hdist = sqrtf(hdx * hdx + hdz * hdz);
            has_target = hdist <= WITNESS_AI_ZOMBIE_PERCEPTION_RADIUS;
        }
        zombie_tick(&z->zstate, now_ms, has_target);

        /* Chase via the SAME generic accelerate() pipeline story_ai.c's own bots already move
           through -- local_game.h's per-player loop applies p->in_fwd/p->yaw for any active i>0
           player in MODE_STORY, no separate movement system needed here. */
        if (has_target && (z->zstate.mood == ZOMBIE_MOOD_HUNTING || z->zstate.mood == ZOMBIE_MOOD_FRENZIED) &&
            hdist > WITNESS_AI_ZOMBIE_MELEE_RANGE) {
            zp->yaw = atan2f(hdx, hdz) * (180.0f / 3.14159f);
            zp->in_fwd = (z->zstate.mood == ZOMBIE_MOOD_FRENZIED) ? 1.0f : 0.7f;
        } else {
            zp->in_fwd = 0.0f;
        }

        /* Melee: real, direct hero damage on contact, same shield-then-health order and
           STORY_PHASE_FAILED-on-death handling story_boss_tick's own attack block already
           establishes -- a zombie is a real, second source of lethal threat in VOXWORLD now, not
           just a background prop. */
        if (has_target && (z->zstate.mood == ZOMBIE_MOOD_HUNTING || z->zstate.mood == ZOMBIE_MOOD_FRENZIED) &&
            hdist <= WITNESS_AI_ZOMBIE_MELEE_RANGE &&
            now_ms - z->last_attack_ms >= WITNESS_AI_ZOMBIE_ATTACK_COOLDOWN_MS) {
            z->last_attack_ms = now_ms;
            zombie_get_agitated(&z->zstate, now_ms); /* landing a hit is a real stimulus */
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
            wn->state = witness_live_next_state_for_event(wn->state, count, wn->arrogance, 0);
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

        if (target_ci < 0) { mp->in_fwd = 0.0f; continue; }

        if (target_dist <= WITNESS_LIVE_DISPATCH_ARRIVAL_RADIUS) {
            mp->in_fwd = 0.0f;
            int zone = g_sim.n[g_citizens[target_ci].npc_index].zone;
            int resolved_count = witness_sim_memory_wipe(&g_sim, zone);
            if (resolved_count > 0) {
                printf("[THE MEN] player=%d resolved %d hunting NPC(s) in zone=%s\n",
                       man->player_id, resolved_count, witness_sim_zone_name(zone));
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
