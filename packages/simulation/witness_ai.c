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
} WitnessAiZombie;

/* Giant Zombie Bug -- founder real-time, 2026-09-22: "add giant zombie bugs (feral AI units)."
 * A genuinely separate entity type from WitnessAiZombie above, not a zombie variant -- its own
 * GiantBugState (giant_bug_values.h), its own real spawn/tick/role. */
typedef struct {
    int active;
    int player_id;
    GiantBugState bstate;
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

    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        WitnessAiZombie *z = &g_zombies[i];
        if (!z->active) continue;
        /* has_target=0 -- real, named scope cut, see witness_ai.h's own top doc comment. */
        zombie_tick(&z->zstate, now_ms, 0);
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
