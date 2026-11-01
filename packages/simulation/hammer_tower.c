/* hammer_tower.c -- the hammer's E power (see hammer_tower.h for the design and the founder's
 * own words).
 *
 * Deliberately does NOT include physics.h: that header defines its functions non-static, so a
 * second translation unit including it collides at link time against the copies the apps/ binaries
 * already link (the exact failure witness_ai.c's own header comment records). Everything here talks
 * to PlayerState and to
 * witness_ai's role/combat queries, which is all the targeting rule needs.
 */

#include "hammer_tower.h"
#include "witness_ai.h"

#include <math.h>
#include <string.h>

static HammerTower g_towers[HAMMER_TOWER_MAX];

static float tower_wrap180(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

void hammer_tower_reset(void) {
    memset(g_towers, 0, sizeof(g_towers));
    for (int i = 0; i < HAMMER_TOWER_MAX; i++) g_towers[i].target_id = -1;
}

int hammer_tower_count(void) {
    int n = 0;
    for (int i = 0; i < HAMMER_TOWER_MAX; i++) if (g_towers[i].active) n++;
    return n;
}

const HammerTower *hammer_tower_at(int index) {
    if (index < 0 || index >= HAMMER_TOWER_MAX) return NULL;
    if (!g_towers[index].active) return NULL;
    return &g_towers[index];
}

int hammer_tower_place_for_owner(const PlayerState *p, float *out_x, float *out_y, float *out_z) {
    if (!p) return 0;
    float r = -p->yaw * 0.0174533f;
    if (out_x) *out_x = p->x + sinf(r) * HAMMER_TOWER_FORWARD_OFFSET;
    if (out_y) *out_y = p->y;
    if (out_z) *out_z = p->z - cosf(r) * HAMMER_TOWER_FORWARD_OFFSET;
    return 1;
}

int hammer_tower_spawn(ServerState *s, int owner_id, float x, float y, float z, float yaw,
                       unsigned int now_ms) {
    if (!s) return -1;
    for (int i = 0; i < HAMMER_TOWER_MAX; i++) {
        HammerTower *t = &g_towers[i];
        if (t->active) continue;
        memset(t, 0, sizeof(*t));
        t->active = 1;
        t->owner_id = owner_id;
        t->scene_id = s->scene_id;
        t->x = x; t->y = y; t->z = z;
        t->yaw = tower_wrap180(yaw);
        t->spin = 0.0f;
        t->target_id = -1;
        t->shots = 0;
        t->born_ms = now_ms;
        t->acquired_ms = now_ms;
        t->last_shot_ms = now_ms;
        return i;
    }
    /* A full pool refuses rather than stealing somebody else's tower -- losing a building you
       paid a 3 s cast and a cooldown for is not a thing the player can see coming. */
    return -1;
}

/* The founder's rule, in tiers: zombies first (always), then The Men and citizens, and those two
 * only once they are actually in a fight. Lower tier wins; nearest wins inside a tier. */
static int tower_tier_for(ServerState *s, int player_id) {
    switch (witness_ai_role_for_player(player_id)) {
        case WITNESS_AI_ROLE_ZOMBIE:
        case WITNESS_AI_ROLE_GIANT_BUG:   /* a feral giant variant of the same population */
            return 0;
        case WITNESS_AI_ROLE_THE_MEN:
            return witness_ai_npc_in_combat(s, player_id) ? 1 : -1;
        case WITNESS_AI_ROLE_CITIZEN:
            return witness_ai_npc_in_combat(s, player_id) ? 2 : -1;
        default:
            return -1;                    /* the player, a plain bot, a bird: never a target */
    }
}

static int tower_pick_target(ServerState *s, const HammerTower *t) {
    int best = -1;
    int best_tier = 99;
    float best_d2 = HAMMER_TOWER_RANGE * HAMMER_TOWER_RANGE;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *p = &s->players[i];
        if (!p->active || p->state == STATE_DEAD) continue;
        if (p->scene_id != t->scene_id) continue;
        if (i == t->owner_id) continue;
        int tier = tower_tier_for(s, i);
        if (tier < 0) continue;
        float dx = p->x - t->x;
        float dz = p->z - t->z;
        float d2 = dx * dx + dz * dz;
        if (d2 > HAMMER_TOWER_RANGE * HAMMER_TOWER_RANGE) continue;
        if (tier < best_tier || (tier == best_tier && d2 < best_d2)) {
            best_tier = tier;
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

/* Shield then health, then the same death entry witness_ai.c's own melee uses -- not
 * phys_enter_death_state, for the link-time reason in this file's header. The tower is never an
 * attacker with its own PlayerState to reward, so there is no kill bookkeeping to do. */
static int tower_hit(ServerState *s, int target_id, int damage, unsigned int now_ms) {
    if (target_id < 0 || target_id >= MAX_CLIENTS) return 0;
    PlayerState *t = &s->players[target_id];
    if (!t->active || t->state == STATE_DEAD) return 0;
    t->shield_regen_timer = SHIELD_REGEN_DELAY;
    if (t->shield > 0) {
        if (t->shield >= damage) { t->shield -= damage; damage = 0; }
        else { damage -= t->shield; t->shield = 0; }
    }
    t->health -= damage;
    if (t->hit_feedback < 10) t->hit_feedback = 10;
    if (t->health > 0) return 0;

    t->health = 0;
    t->state = STATE_DEAD;
    t->in_shoot = 0;
    t->in_reload = 0;
    t->in_use = 0;
    t->in_jump = 0;
    t->in_ability = 0;
    t->is_shooting = 0;
    t->dash_timer = 0;
    t->death_time_ms = now_ms;
    t->death_duration_ms = HAMMER_TOWER_DEATH_RESPAWN_MS;
    t->respawn_time = now_ms + HAMMER_TOWER_DEATH_RESPAWN_MS;
    t->death_dir_x = 0.0f;
    t->death_dir_z = 1.0f;
    return 1;
}

void hammer_tower_tick(ServerState *s, unsigned int now_ms) {
    if (!s) return;
    for (int i = 0; i < HAMMER_TOWER_MAX; i++) {
        HammerTower *t = &g_towers[i];
        if (!t->active) continue;
        /* A scene change (or the level reloading under it) takes the tower with it: a tower is
           part of the place it was built in, not something that follows you home. */
        if (t->scene_id != s->scene_id || (unsigned int)(now_ms - t->born_ms) >= HAMMER_TOWER_LIFETIME_MS) {
            t->active = 0;
            t->target_id = -1;
            continue;
        }

        int tid = tower_pick_target(s, t);
        /* The barrels turn the whole time, faster with something to shoot -- the gatling read. */
        t->spin += (tid >= 0) ? 9.0f : 4.0f;
        if (t->spin >= 360.0f) t->spin -= 360.0f;

        if (tid < 0) {
            t->target_id = -1;
            continue;
        }
        if (tid != t->target_id) {
            t->target_id = tid;
            t->acquired_ms = now_ms;
        }

        PlayerState *target = &s->players[tid];
        float want = atan2f(target->x - t->x, target->z - t->z) * 57.29578f;
        float err = tower_wrap180(want - t->yaw);
        float step = HAMMER_TOWER_TURN_DEG_PER_TICK;
        if (err > step) err = step;
        else if (err < -step) err = -step;
        t->yaw = tower_wrap180(t->yaw + err);

        if ((unsigned int)(now_ms - t->acquired_ms) < HAMMER_TOWER_WINDUP_MS) continue;
        if (fabsf(tower_wrap180(want - t->yaw)) > HAMMER_TOWER_AIM_TOLERANCE_DEG) continue;
        if ((unsigned int)(now_ms - t->last_shot_ms) < HAMMER_TOWER_INTERVAL_MS) continue;

        t->last_shot_ms = now_ms;
        t->shots++;
        tower_hit(s, tid, HAMMER_TOWER_DAMAGE, now_ms);
    }
}
