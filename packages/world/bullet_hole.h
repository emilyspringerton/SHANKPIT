#ifndef SHANKPIT_BULLET_HOLE_H
#define SHANKPIT_BULLET_HOLE_H

// bullet_hole.h -- Half-Life-style per-gun bullet-hole decals (founder real-time, 2026-10-01:
// "ensure that works like half life - shooting a wall causes a bullet hole different per gun",
// "using the nock tools parena texture generator and database").
//
// The decal ARTWORK is not drawn here: each gun's hole is a PARENA program (PARENA/stdlib/
// shankpit/textures/bullet_hole_<gun>.prn) rendered by NOCK and stored in IDUNA's nock_textures
// table as `bullet-hole-<gun>`. This header only owns the engine side:
//   * shot detection  -- a shot = the tracked player's ammo for the SAME weapon dropped
//     (works for local prediction and for networked snapshots alike; no new wire field);
//   * the bounded ring buffer of placed holes, with per-hole random in-plane rotation and size
//     jitter so repeated hits from one gun don't look stamped;
//   * texture bytes -- a live fetch of the NOCK image when IDUNA serves it, else the checked-in
//     copy exported from the same NOCK rows (bullet_hole_gen.h), decoded by PARENA's own
//     png_decode. GL upload stays in apps/lobby/main.c; this header is GL-free so
//     packages/world/bullet_hole_test.c can exercise it headless.
//
// Textures are multiply-blended (white background vanishes): glBlendFunc(GL_DST_COLOR, GL_ZERO).
// Real, honest limits: holes are placed on map geometry only (trace_map); a shot that hits a
// player also leaves a hole on the wall behind them; no per-material variation yet.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../common/protocol.h"
#include "bullet_hole_gen.h"
#include "level_boxes.h"
#include "png_decode.h"

#define BULLET_HOLE_SLOTS 4
#define BULLET_HOLE_MAX 256
#define BULLET_HOLE_RANGE 150.0f
#define BULLET_HOLE_MAX_SHOTS_PER_OBSERVE 4
#define BULLET_HOLE_BASE_URL "https://okemily.com/api/v1/nock-textures/by-name/bullet-hole-"

typedef struct {
    float x, y, z;
    float nx, ny, nz;
    float rot;   /* in-plane rotation, radians */
    float half;  /* half-size in world units */
    int scene_id;
    int slot;
} BulletHole;

typedef struct {
    BulletHole holes[BULLET_HOLE_MAX];
    int count;
    int next;
    unsigned int rng;
} BulletHoleBuf;

typedef struct {
    int valid;
    int weapon;
    int ammo;
} BulletHoleTrack;

/* weapon -> texture slot; -1 for weapons that don't fire hitscan bullets (knife, katana,
   missile, flashlight). */
static inline int bullet_hole_slot_for_weapon(int weapon) {
    switch (weapon) {
        case WPN_MAGNUM:  return 0;
        case WPN_AR:      return 1;
        case WPN_SHOTGUN: return 2;
        case WPN_SNIPER:  return 3;
        default:          return -1;
    }
}

static inline const char *bullet_hole_slot_name(int slot) {
    static const char *names[BULLET_HOLE_SLOTS] = {"magnum", "ar", "shotgun", "sniper"};
    return (slot >= 0 && slot < BULLET_HOLE_SLOTS) ? names[slot] : "";
}

/* World half-size per gun, tuned against each texture's own core radius (magnum 0.15, ar 0.085,
   shotgun 0.07, sniper 0.12 of the half-extent) so the punched core is ~6-7cm across. */
static inline float bullet_hole_half_size(int slot) {
    static const float sz[BULLET_HOLE_SLOTS] = {0.42f, 0.38f, 0.32f, 0.55f};
    return (slot >= 0 && slot < BULLET_HOLE_SLOTS) ? sz[slot] : 0.4f;
}

/* Rays fired per trigger pull: the weapon's own pellet count (shotgun 8), else 1. */
static inline int bullet_hole_rays_for_weapon(int weapon) {
    if (weapon < 0 || weapon >= MAX_WEAPONS) return 1;
    return WPN_STATS[weapon].cnt > 1 ? WPN_STATS[weapon].cnt : 1;
}

static inline unsigned int bullet_hole_rand(BulletHoleBuf *b) {
    unsigned int x = b->rng ? b->rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    b->rng = x;
    return x;
}

/* uniform in [-1, 1) */
static inline float bullet_hole_randf(BulletHoleBuf *b) {
    return ((float)(bullet_hole_rand(b) & 0xFFFFu) / 32768.0f) - 1.0f;
}

/* Number of shots fired since the last observation of this player, from the drop in ammo for
   an unchanged weapon. First sample, weapon switches, reloads (ammo rising) and non-bullet
   weapons all yield 0. Updates the track. */
static inline int bullet_hole_shots_fired(BulletHoleTrack *t, int weapon, int ammo) {
    int shots = 0;
    if (t->valid && t->weapon == weapon && bullet_hole_slot_for_weapon(weapon) >= 0 && ammo < t->ammo) {
        shots = t->ammo - ammo;
        if (shots > BULLET_HOLE_MAX_SHOTS_PER_OBSERVE) shots = BULLET_HOLE_MAX_SHOTS_PER_OBSERVE;
    }
    t->valid = 1; t->weapon = weapon; t->ammo = ammo;
    return shots;
}

/* Perturb a unit aim direction by the weapon's own spread (same uniform-per-component model
   physics.h applies to the real hitscan), then renormalize. */
static inline void bullet_hole_spread_dir(BulletHoleBuf *b, int weapon, float *dx, float *dy, float *dz) {
    float spr = (weapon >= 0 && weapon < MAX_WEAPONS) ? WPN_STATS[weapon].spr : 0.0f;
    if (spr > 0.0f) {
        *dx += bullet_hole_randf(b) * spr;
        *dy += bullet_hole_randf(b) * spr;
        *dz += bullet_hole_randf(b) * spr;
    }
    float l = sqrtf(*dx * *dx + *dy * *dy + *dz * *dz);
    if (l > 0.0001f) { *dx /= l; *dy /= l; *dz /= l; }
}

static inline void bullet_hole_add(BulletHoleBuf *b, int slot, int scene_id,
                                   float x, float y, float z, float nx, float ny, float nz) {
    BulletHole *h = &b->holes[b->next % BULLET_HOLE_MAX];
    h->x = x; h->y = y; h->z = z;
    h->nx = nx; h->ny = ny; h->nz = nz;
    h->rot = bullet_hole_randf(b) * 3.14159265f;
    h->half = bullet_hole_half_size(slot) * (0.85f + 0.15f * (bullet_hole_randf(b) + 1.0f)); /* 0.85..1.15 */
    h->scene_id = scene_id;
    h->slot = slot;
    b->next++;
    if (b->count < BULLET_HOLE_MAX) b->count++;
}

static inline void bullet_hole_clear(BulletHoleBuf *b) { b->count = 0; b->next = 0; }

/* Decode PNG bytes into a malloc'd RGBA buffer (caller frees). Returns 0 on failure. */
static inline int bullet_hole_decode_rgba(const unsigned char *data, long n, unsigned char **rgba, int *w, int *h) {
    if (n <= 0) return 0;
    Arena a;
    arena_init(&a);
    Bytes in = bytes_alloc_impl(&a, (int)n);
    for (long i = 0; i < n; i++) bytes_set_impl(in, (int)i, data[i]);
    PngImage img = png_decode(in, &a);
    if (!img.ok || img.width <= 0 || img.height <= 0) { arena_free_all(&a); return 0; }
    size_t bytes = (size_t)img.width * (size_t)img.height * 4u;
    unsigned char *out = (unsigned char *)malloc(bytes);
    if (!out) { arena_free_all(&a); return 0; }
    for (size_t i = 0; i < bytes; i++) out[i] = (unsigned char)bytes_get_impl(img.pixels, (int)i);
    *rgba = out; *w = img.width; *h = img.height;
    arena_free_all(&a);
    return 1;
}

/* The checked-in NOCK export for a slot. */
static inline int bullet_hole_decode_embedded(int slot, unsigned char **rgba, int *w, int *h) {
    switch (slot) {
        case 0: return bullet_hole_decode_rgba(g_bullet_hole_png_magnum, (long)g_bullet_hole_png_magnum_len, rgba, w, h);
        case 1: return bullet_hole_decode_rgba(g_bullet_hole_png_ar, (long)g_bullet_hole_png_ar_len, rgba, w, h);
        case 2: return bullet_hole_decode_rgba(g_bullet_hole_png_shotgun, (long)g_bullet_hole_png_shotgun_len, rgba, w, h);
        case 3: return bullet_hole_decode_rgba(g_bullet_hole_png_sniper, (long)g_bullet_hole_png_sniper_len, rgba, w, h);
        default: return 0;
    }
}

/* Live NOCK fetch (blocking curl popen -- call from a loading checkpoint, never mid-gameplay,
   same rule as client_prefetch_default_spray). 0 on any failure (route not deployed, offline). */
static inline int bullet_hole_fetch_live(int slot, unsigned char **rgba, int *w, int *h) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s/image", BULLET_HOLE_BASE_URL, bullet_hole_slot_name(slot));
    char *buf = NULL;
    long n = level_boxes_fetch_url(url, &buf);
    if (n <= 0) return 0;
    int ok = bullet_hole_decode_rgba((const unsigned char *)buf, n, rgba, w, h);
    free(buf);
    return ok;
}

#endif
