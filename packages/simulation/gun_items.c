#include "gun_items.h"
#include "../common/protocol.h"   /* WPN_* */
#include <math.h>
#include <string.h>

static GunItem g_items[GUN_ITEM_MAX];
static unsigned char g_enemy_dropped[GUN_ITEM_MAX];

void gun_items_reset(void) { memset(g_items, 0, sizeof g_items); memset(g_enemy_dropped, 0, sizeof g_enemy_dropped); }

int gun_items_drop(int weapon, float x, float y, float z) {
    for (int i = 0; i < GUN_ITEM_MAX; i++) {
        if (!g_items[i].active) {
            g_items[i].active = 1; g_items[i].weapon = weapon;
            g_items[i].x = x; g_items[i].y = y; g_items[i].z = z;
            return i;
        }
    }
    return -1;
}

int gun_items_check(float px, float py, float pz) {
    int best = -1; float best_d2 = GUN_PICKUP_RADIUS * GUN_PICKUP_RADIUS;
    for (int i = 0; i < GUN_ITEM_MAX; i++) {
        const GunItem *g = &g_items[i];
        if (!g->active) continue;
        float dx = g->x - px, dz = g->z - pz;
        float d2 = dx * dx + dz * dz;
        if (d2 <= best_d2 && fabsf(g->y - py) <= GUN_PICKUP_HEIGHT) { best = i; best_d2 = d2; }
    }
    if (best < 0) return -1;
    g_items[best].active = 0;
    return g_items[best].weapon;
}

int gun_items_active_count(void) {
    int n = 0;
    for (int i = 0; i < GUN_ITEM_MAX; i++) n += g_items[i].active;
    return n;
}

const GunItem *gun_items_get(int slot) { return (slot >= 0 && slot < GUN_ITEM_MAX) ? &g_items[slot] : NULL; }

int gun_loot_pick(int roll) {
    roll = ((roll % 100) + 100) % 100;
    if (roll < 30) return WPN_MAGNUM;
    if (roll < 55) return WPN_AR;
    if (roll < 75) return WPN_SHOTGUN;
    if (roll < 85) return WPN_KATANA;
    if (roll < 95) return WPN_SNIPER;
    return WPN_MISSILE;
}

int gun_items_seed_ring(float cx, float y, float cz, int count) {
    /* one representative roll per loot-table band, so the ring always contains every weapon type */
    static const int bands[6] = { 10, 40, 60, 80, 90, 97 };
    int placed = 0;
    for (int i = 0; i < count; i++) {
        float a = (6.2831853f * (float)i) / (float)(count > 0 ? count : 1);
        float r = 14.0f + 12.0f * (float)(i % 3) / 2.0f;
        if (gun_items_drop(gun_loot_pick(bands[i % 6]), cx + r * cosf(a), y, cz + r * sinf(a)) >= 0) placed++;
    }
    return placed;
}

int gun_items_note_enemy(int slot, int alive, float x, float y, float z, int roll100, int loot_roll) {
    if (slot < 0 || slot >= GUN_ITEM_MAX) return -1;
    if (alive) { g_enemy_dropped[slot] = 0; return -1; }
    if (g_enemy_dropped[slot]) return -1;
    g_enemy_dropped[slot] = 1;
    if (roll100 >= GUN_DROP_PERCENT) return -1;
    return gun_items_drop(gun_loot_pick(loot_roll), x, y, z);
}
