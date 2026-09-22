#include "witness_ai.h"

#include <string.h>
#include <stdio.h>

#include "npc_archetype.h"
#include "zombie_values.h"
#include "witness_live.h"

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

static WitnessSim g_sim;
static WitnessAiCitizen g_citizens[WITNESS_AI_MAX_CITIZENS];
static WitnessAiZombie g_zombies[WITNESS_AI_MAX_ZOMBIES];
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
    g_have_last_sim_tick = 0;
    g_player_zone = ZONE_PUBLIC; /* matches witness_sim_init's own player-0 default */
    g_have_last_player_zone_check = 0;
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

    for (int i = 0; i < WITNESS_AI_MAX_CITIZENS; i++) {
        WitnessAiCitizen *c = &g_citizens[i];
        if (!c->active) continue;
        npc_brain_tick(&c->brain, now_ms);
        g_sim.n[c->npc_index].vigilance = npc_brain_effective_vigilance(&c->brain);
    }

    for (int i = 0; i < WITNESS_AI_MAX_ZOMBIES; i++) {
        WitnessAiZombie *z = &g_zombies[i];
        if (!z->active) continue;
        /* has_target=0 -- real, named scope cut, see witness_ai.h's own top doc comment. */
        zombie_tick(&z->zstate, now_ms, 0);
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

    printf("[WITNESS] voxworld encounter seeded: 4 citizens, 2 zombies (1 HUNTING), 1 The Men\n");
}
