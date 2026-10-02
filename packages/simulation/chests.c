#include "chests.h"
#include "gun_items.h"
#include <math.h>
#include <string.h>

static Chest g_chests[CHEST_MAX];

void chests_reset(void) { memset(g_chests, 0, sizeof g_chests); }

int chests_place(int tier, float x, float y, float z) {
    if (tier < 0) tier = 0;
    if (tier > 2) tier = 2;
    for (int i = 0; i < CHEST_MAX; i++) {
        if (g_chests[i].active) continue;
        g_chests[i].active = 1; g_chests[i].tier = tier;
        g_chests[i].max_hp = g_chests[i].hp = chest_max_hp(tier);
        g_chests[i].x = x; g_chests[i].y = y; g_chests[i].z = z; g_chests[i].hurt_ms = 0;
        return i;
    }
    return -1;
}

int chests_active_count(void) {
    int n = 0;
    for (int i = 0; i < CHEST_MAX; i++) n += g_chests[i].active;
    return n;
}

const Chest *chests_get(int slot) { return (slot >= 0 && slot < CHEST_MAX) ? &g_chests[slot] : NULL; }

/* Hurts chest i; returns 1 if it broke (and dropped its weapon). */
static int chest_hurt(int i, int weapon, int dmg, int loot_roll) {
    Chest *c = &g_chests[i];
    int d = dmg * chest_damage_permille(weapon) / 1000;
    if (d < 1) d = 1;
    c->hp -= d;
    if (c->hp > 0) return 0;
    c->active = 0;
    gun_items_drop(chest_loot_weapon(c->tier, ((loot_roll % 100) + 100) % 100), c->x, c->y, c->z);
    return 1;
}

/* Slab test of the ray against chest c's box; returns the entry distance or -1. */
static float ray_box(const Chest *c, float ox, float oy, float oz, float dx, float dy, float dz, float range) {
    float lo[3] = { c->x - CHEST_HALF_X, c->y, c->z - CHEST_HALF_Z };
    float hi[3] = { c->x + CHEST_HALF_X, c->y + 2.0f * CHEST_HALF_Y, c->z + CHEST_HALF_Z };
    float o[3] = { ox, oy, oz }, d[3] = { dx, dy, dz };
    float t0 = 0.0f, t1 = range;
    for (int a = 0; a < 3; a++) {
        if (fabsf(d[a]) < 1e-8f) { if (o[a] < lo[a] || o[a] > hi[a]) return -1.0f; continue; }
        float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
        if (ta > tb) { float t = ta; ta = tb; tb = t; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

int chests_hit_ray(float ox, float oy, float oz, float dx, float dy, float dz, float range,
                   int weapon, int dmg, int loot_roll, int *broke) {
    int best = -1; float best_t = range + 1.0f;
    if (broke) *broke = 0;
    for (int i = 0; i < CHEST_MAX; i++) {
        if (!g_chests[i].active) continue;
        float t = ray_box(&g_chests[i], ox, oy, oz, dx, dy, dz, range);
        if (t >= 0.0f && t < best_t) { best = i; best_t = t; }
    }
    if (best < 0) return -1;
    int b = chest_hurt(best, weapon, dmg, loot_roll);
    if (broke) *broke = b;
    return best;
}

int chests_hit_blast(float x, float y, float z, float radius, int weapon, int dmg, int loot_roll) {
    int broke = 0;
    for (int i = 0; i < CHEST_MAX; i++) {
        const Chest *c = &g_chests[i];
        if (!c->active) continue;
        /* distance from the blast centre to the chest's box */
        float cx = fmaxf(c->x - CHEST_HALF_X, fminf(x, c->x + CHEST_HALF_X));
        float cy = fmaxf(c->y, fminf(y, c->y + 2.0f * CHEST_HALF_Y));
        float cz = fmaxf(c->z - CHEST_HALF_Z, fminf(z, c->z + CHEST_HALF_Z));
        float dx = cx - x, dy = cy - y, dz = cz - z;
        if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
        broke += chest_hurt(i, weapon, dmg, loot_roll + i * 37);
    }
    return broke;
}

int chests_seed_roads(float cx, float y, float cz, int count, float r0, float r_step, float pitch) {
    int placed = 0;
    for (int i = 0; i < count; i++) {
        /* offset half a step from gun_items_seed_roads's angles so chests do not sit on top of guns */
        float a = (6.2831853f * ((float)i + 0.5f)) / (float)(count > 0 ? count : 1);
        float r = r0 + r_step * (float)(i % 3);
        float x = cx + r * cosf(a), z = cz + r * sinf(a);
        float rx = (floorf(x / pitch) + 0.5f) * pitch, rz = (floorf(z / pitch) + 0.5f) * pitch;
        if (fabsf(x - rx) <= fabsf(z - rz)) x = rx; else z = rz;
        /* spread rolls so a ring of 12 carries every tier: 6 / 29 / 52 / 75 / 98 ... (wooden x3, reinforced, rare) */
        static const int roll[5] = { 6, 29, 52, 75, 98 };
        if (chests_place(chest_tier_for_roll(roll[i % 5]), x, y, z) >= 0) placed++;
    }
    return placed;
}
