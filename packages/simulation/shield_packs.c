#include "shield_packs.h"
#include <math.h>
#include <string.h>

static ShieldPack g_packs[SHIELD_PACK_MAX];
static unsigned char g_slot_dropped[SHIELD_PLAYER_SLOTS];
static unsigned g_version;

void shield_packs_reset(void) {
    memset(g_packs, 0, sizeof g_packs);
    memset(g_slot_dropped, 0, sizeof g_slot_dropped);
    g_version++;
}

int shield_packs_note_player(int slot, int alive, float x, float y, float z, int roll100) {
    if (slot < 0 || slot >= SHIELD_PLAYER_SLOTS) return -1;
    if (alive) { g_slot_dropped[slot] = 0; return -1; }
    if (g_slot_dropped[slot]) return -1;
    g_slot_dropped[slot] = 1;
    if (roll100 < 0 || roll100 >= SHIELD_PACK_DROP_PERCENT) return -1;
    for (int i = 0; i < SHIELD_PACK_MAX; i++) {
        if (!g_packs[i].active) {
            g_packs[i].active = 1; g_packs[i].x = x; g_packs[i].y = y; g_packs[i].z = z;
            g_version++;
            return i;
        }
    }
    return -1;
}

int shield_packs_take(float px, float py, float pz) {
    int best = -1; float best_d2 = SHIELD_PACK_RADIUS * SHIELD_PACK_RADIUS;
    for (int i = 0; i < SHIELD_PACK_MAX; i++) {
        const ShieldPack *s = &g_packs[i];
        if (!s->active) continue;
        float dx = s->x - px, dz = s->z - pz;
        float d2 = dx * dx + dz * dz;
        if (d2 <= best_d2 && fabsf(s->y - py) <= SHIELD_PACK_HEIGHT) { best = i; best_d2 = d2; }
    }
    if (best < 0) return 0;
    g_packs[best].active = 0;
    g_version++;
    return 1;
}

int shield_packs_active_count(void) {
    int n = 0;
    for (int i = 0; i < SHIELD_PACK_MAX; i++) n += g_packs[i].active;
    return n;
}

const ShieldPack *shield_packs_get(int slot) {
    return (slot >= 0 && slot < SHIELD_PACK_MAX) ? &g_packs[slot] : NULL;
}

unsigned shield_packs_version(void) { return g_version; }

int shield_packs_set_all(const float *xyz, int n) {
    memset(g_packs, 0, sizeof g_packs);
    int out = 0;
    if (xyz && n > 0) {
        for (int i = 0; i < n && out < SHIELD_PACK_MAX; i++) {
            float x = xyz[i * 3], y = xyz[i * 3 + 1], z = xyz[i * 3 + 2];
            if (!isfinite(x) || !isfinite(y) || !isfinite(z)) continue;
            g_packs[out].active = 1; g_packs[out].x = x; g_packs[out].y = y; g_packs[out].z = z;
            out++;
        }
    }
    g_version++;
    return out;
}
