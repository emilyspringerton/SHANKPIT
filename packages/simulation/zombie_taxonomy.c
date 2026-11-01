/* zombie_taxonomy.c -- see zombie_taxonomy.h. Every number here is derived, none rolled. */

#include "zombie_taxonomy.h"

#include <stddef.h>

/* Per-class body. base_hp/base_dmg are level 1; hp_per/dmg_per are added per level above 1.
 * move_permille is a multiplier on the pace the pre-existing single zombie walked at, so a WALKER
 * is exactly the old behaviour and nothing that already worked changes speed. */
typedef struct {
    const char *name;
    int base_hp, hp_per;
    int base_dmg, dmg_per;
    int move_permille;
    int sense_bonus;
    int slurry_permille;
    int dna_base;         /* grade at level 1 */
    int dna_trait;
    int weight_day;       /* spawn weight, day */
    int weight_night;     /* spawn weight, night */
} ZombieClassDef;

static const ZombieClassDef ZDEF[ZCLASS_COUNT] = {
    /* name       hp  hpp  dmg dmgp  move  sense slurry dna trait        day night */
    { "SHAMBLER", 60, 8,  10,  2,    700,  0,    250,   18, ZTRAIT_SPEED,  55, 25 },
    { "WALKER",  100, 12, 14,  3,   1000,  0,    350,   32, ZTRAIT_ARMOR,  35, 30 },
    { "RUNNER",   70, 9,  12,  3,   1450,  4,    300,   45, ZTRAIT_SPEED,  10, 27 },
    { "BRUTE",   200, 26, 26,  5,    780, -2,    550,   60, ZTRAIT_ARMOR,   0, 15 },
    { "ALPHA",   300, 30, 32,  6,   1100,  6,    800,   85, ZTRAIT_SCENT,   0,  3 },
};

static int zt_clamp_level(int level) {
    if (level < 1) return 1;
    if (level > ZOMBIE_MAX_LEVEL) return ZOMBIE_MAX_LEVEL;
    return level;
}

void zombie_taxonomy_profile(ZombieProfile *out, int cls, int level) {
    if (!out) return;
    if (cls < 0 || cls >= ZCLASS_COUNT) cls = ZCLASS_SHAMBLER;
    level = zt_clamp_level(level);
    const ZombieClassDef *d = &ZDEF[cls];
    int up = level - 1;
    out->cls = cls;
    out->level = level;
    out->max_health = d->base_hp + d->hp_per * up;
    out->melee_damage = d->base_dmg + d->dmg_per * up;
    out->move_permille = d->move_permille;
    out->sense_bonus = d->sense_bonus;
    out->slurry_permille = d->slurry_permille;
    /* A strand reads better off a bigger, rarer body and off a deeper level, capped at 100 so a
       "perfect" sample stays something the lab has to work for rather than a free pass. */
    int q = d->dna_base + up * 3;
    if (q > 100) q = 100;
    if (q < 1) q = 1;
    out->dna_quality = q;
    out->dna_trait = d->dna_trait;
}

/* A tiny integer hash -- no libc RNG, so a replay of the same seed is the same horde. */
static uint32_t zt_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

void zombie_taxonomy_roll(ZombieProfile *out, uint32_t seed, int night) {
    if (!out) return;
    uint32_t h = zt_hash(seed ? seed : 1u);
    int total = 0;
    for (int i = 0; i < ZCLASS_COUNT; i++) total += night ? ZDEF[i].weight_night : ZDEF[i].weight_day;
    int pick = (int)(h % (uint32_t)total);
    int cls = 0;
    for (int i = 0; i < ZCLASS_COUNT; i++) {
        int w = night ? ZDEF[i].weight_night : ZDEF[i].weight_day;
        if (pick < w) { cls = i; break; }
        pick -= w;
    }
    /* Level: a shallow curve around the middle so most bodies are ordinary and the deep end is
       felt. Night runs one level deeper. */
    uint32_t lh = zt_hash(h ^ 0x9e3779b9u);
    int level = 1 + (int)(lh % 6u) + (night ? 1 : 0);
    zombie_taxonomy_profile(out, cls, level);
}

const char *zombie_taxonomy_class_name(int cls) {
    if (cls < 0 || cls >= ZCLASS_COUNT) return "SHAMBLER";
    return ZDEF[cls].name;
}

const char *zombie_taxonomy_trait_name(int trait) {
    switch (trait) {
        case ZTRAIT_SPEED: return "SPEED";
        case ZTRAIT_ARMOR: return "ARMOR";
        case ZTRAIT_SCENT: return "SCENT";
        default: return "SPEED";
    }
}

int zombie_taxonomy_slurry_roll(uint32_t seed, const ZombieProfile *p) {
    if (!p) return 0;
    int permille = p->slurry_permille;
    if (permille <= 0) return 0;
    if (permille >= 1000) return 1;
    return (int)(zt_hash(seed ^ 0xa5a5a5a5u) % 1000u) < permille;
}
