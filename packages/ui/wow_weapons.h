/* GENERATED FILE -- do not edit. scripts/gen_wow_weapons.py from packages/common/protocol.h.
 *
 * The names are the engine's own WPN_* identifiers lowercased, so /cast magnum and the
 * number key that selects the magnum cannot disagree. Numbers 1..6 are the number keys' own
 * 0..5, 7/8/9 are flashlight/hammer/spraycan, exactly as apps/lobby binds them. */
#ifndef SHANKPIT_UI_WOW_WEAPONS_H
#define SHANKPIT_UI_WOW_WEAPONS_H

#include <string.h>

typedef struct { const char *name; int id; } WowWeapon;

static const WowWeapon wow_weapons[] = {
    { "knife", 0 },
    { "magnum", 1 },
    { "ar", 2 },
    { "shotgun", 3 },
    { "sniper", 4 },
    { "katana", 5 },
    { "missile", 6 },
    { "flashlight", 7 },
    { "hammer", 8 },
    { "spraycan", 9 },
};

#define WOW_WEAPON_COUNT ((int)(sizeof wow_weapons / sizeof wow_weapons[0]))

static inline char wow_wpn_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static inline int wow_weapon_from_name(const char *name) {
    int i;
    size_t n, j;
    if (!name || !*name) return -1;
    n = strlen(name);
    for (i = 0; i < WOW_WEAPON_COUNT; i++) {
        if (strlen(wow_weapons[i].name) != n) continue;
        for (j = 0; j < n; j++)
            if (wow_wpn_lower(name[j]) != wow_weapons[i].name[j]) break;
        if (j == n) return wow_weapons[i].id;
    }
    return -1;
}

static inline const char *wow_weapon_name(int id) {
    int i;
    for (i = 0; i < WOW_WEAPON_COUNT; i++)
        if (wow_weapons[i].id == id) return wow_weapons[i].name;
    return "?";
}

/* The number keys' own mapping, mirrored: 1..6 -> 0..5, 7 flashlight, 8 hammer, 9 spraycan. */
static inline int wow_weapon_from_slot(int slot) {
    if (slot >= 1 && slot <= 6) return slot - 1;
    if (slot == 7) return 7;
    if (slot == 8) return 8;
    if (slot == 9) return 9;
    return -1;
}

#endif /* SHANKPIT_UI_WOW_WEAPONS_H */
