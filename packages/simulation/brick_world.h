#ifndef SHANKPIT_BRICK_WORLD_H
#define SHANKPIT_BRICK_WORLD_H

/* brick_world.h -- binds the destructible-brick engine (packages/world/brick_fracture.h) to the
 * live game: the physics slot arrays, the weapon/blast hooks, the match reset and the network.
 *
 * Founder real-time, 2026-10-01: "get papercraft tech shipped to shankpit i want brick to be
 * destructable."
 *
 * Include AFTER physics.h and local_game.h in exactly one translation unit per binary (the server
 * main, the lobby main) -- like physics.h itself, this file owns static state.
 *
 * Authority. The server and a local (single-player) match are authoritative: a shot or blast
 * damages cells, geometry is rebuilt and committed into physics.h's custom-level slots on the
 * spot. A networked client (brick_world_set_authority(0)) never damages anything itself -- it only
 * mirrors PACKET_BRICK_STATE -- so prediction can never fork the world.
 *
 * What is destructible. A level box whose material is "brick" (the default), "concrete", "wood" or "glass", unless it is
 * (a) a door or button box (those move, and are scripted), or (b) a thin horizontal slab -- a floor
 * or platform deck (height <= 12 and both horizontal extents >= 20): carving the floor out from
 * under a level would be griefing, not demolition.
 *
 * Level-agnostic and mode-agnostic by design (SHANKPIT/CLAUDE.md: levels are never story-mode-only).
 */

#include "witness_ai.h"
#include "../world/brick_fracture.h"
#include "../world/level_boxes.h"

#define BRICK_WORLD_RANGE        600.0f   /* how far a hitscan shot is traced for the map */
#define BRICK_RESEND_MAX         256
#define BRICK_REFRESH_PER_PACKET 16
#define BRICK_REFRESH_PERIOD_MS  500u

static BrickFracture g_brick;
static BfCheckpoint g_brick_cp;
static int g_brick_active = 0;       /* a level with at least one destructible parent is loaded */
static int g_brick_authority = 1;    /* 0 = networked client, mirror only */
static unsigned int g_brick_rng = 0x9E3779B9u;
static unsigned int g_brick_commit_serial = 0;   /* bumps whenever the committed geometry changed */

typedef struct { BfNetEntry e; unsigned char left; unsigned int next_ms; } BrickResend;
static BrickResend g_brick_resend[BRICK_RESEND_MAX];
static int g_brick_resend_n = 0;
static unsigned int g_brick_refresh_next_ms = 0;

static inline float brick_world_rand01(void) {
    g_brick_rng ^= g_brick_rng << 13; g_brick_rng ^= g_brick_rng >> 17; g_brick_rng ^= g_brick_rng << 5;
    return (float)(g_brick_rng % 100000u) / 100000.0f;
}

static inline void brick_world_set_authority(int authoritative) { g_brick_authority = authoritative ? 1 : 0; }
static inline int brick_world_is_active(void) { return g_brick_active; }
static inline unsigned int brick_world_commit_serial(void) { return g_brick_commit_serial; }

/* brick_world_commit -- writes the current piece set into physics.h's slots: authored parents that
 * have been carved are hidden, their surviving volume is appended as ordinary boxes. */
static inline void brick_world_commit(void) {
    phys_custom_level_fracture_begin();
    for (int pi = 0; pi < g_brick.parent_count; pi++)
        if (g_brick.parent[pi].active && g_brick.parent[pi].removed > 0) phys_custom_level_hide_authored(pi);
    for (int i = 0; i < g_brick.piece_count; i++) {
        const BfPiece *pc = &g_brick.piece[i];
        phys_custom_level_add_piece(pc->parent, pc->x, pc->y, pc->z, pc->w, pc->h, pc->d, pc->uvo);
    }
    phys_custom_level_fracture_commit();
    g_brick_commit_serial++;
}

/* brick_world_flush -- if any cell's gone-ness changed, rebuild and commit. With a checkpoint
 * (an authoritative damage call), a piece-budget overflow rolls the damage back whole; without one
 * (a client mirroring the server) a failed rebuild just leaves the last committed geometry alone.
 * Returns 1 if the geometry changed. */
static inline int brick_world_flush(int have_checkpoint) {
    if (!g_brick.pieces_dirty) return 0;
    if (bf_rebuild(&g_brick)) { brick_world_commit(); return 1; }
    if (have_checkpoint) {
        bf_restore(&g_brick, &g_brick_cp);
        if (bf_rebuild(&g_brick)) brick_world_commit();
    }
    return 0;
}

static inline int brick_world_hook_ready(int scene_id) {
    return g_brick_active && g_brick_authority && scene_id == SCENE_CUSTOM_LEVEL && map_geo == g_custom_level_geo;
}

static inline void brick_world_on_hitscan(int scene_id, float ox, float oy, float oz, float dx, float dy, float dz, int weapon) {
    if (!brick_world_hook_ready(scene_id)) return;
    if (weapon < 0 || weapon >= MAX_WEAPONS) return;
    if (weapon == WPN_KNIFE || weapon == WPN_KATANA || weapon == WPN_FLASHLIGHT || weapon == WPN_MISSILE) return;
    int cnt = WPN_STATS[weapon].cnt;
    if (cnt < 1) cnt = 1;
    int dmg = WPN_STATS[weapon].dmg / cnt;
    float spr = WPN_STATS[weapon].spr;
    bf_checkpoint(&g_brick, &g_brick_cp);
    for (int i = 0; i < cnt; i++) {
        float px = dx, py = dy, pz = dz;
        if (spr > 0.0f) {
            px += (brick_world_rand01() * 2.0f - 1.0f) * spr;
            py += (brick_world_rand01() * 2.0f - 1.0f) * spr;
            pz += (brick_world_rand01() * 2.0f - 1.0f) * spr;
            float len = sqrtf(px * px + py * py + pz * pz);
            if (len > 1e-6f) { px /= len; py /= len; pz /= len; }
        }
        float hx, hy, hz, nx, ny, nz;
        if (!trace_map(ox, oy, oz, ox + px * BRICK_WORLD_RANGE, oy + py * BRICK_WORLD_RANGE, oz + pz * BRICK_WORLD_RANGE,
                       &hx, &hy, &hz, &nx, &ny, &nz)) continue;
        bf_hit_surface(&g_brick, hx, hy, hz, nx, ny, nz, weapon, dmg, (cnt > 1) ? 0.7f : 0.9f);
    }
    brick_world_flush(1);
}

static inline void brick_world_on_blast(int scene_id, float x, float y, float z, float radius, int damage, int weapon) {
    if (!brick_world_hook_ready(scene_id)) return;
    /* centre the blast 0.35 radii INTO the wall when we know which way the struck face points, so a
       rocket opens a hole through a thin wall instead of scratching its front */
    x -= g_phys_blast_normal[0] * radius * 0.35f;
    y -= g_phys_blast_normal[1] * radius * 0.35f;
    z -= g_phys_blast_normal[2] * radius * 0.35f;
    bf_checkpoint(&g_brick, &g_brick_cp);
    bf_damage_sphere(&g_brick, x, y, z, radius * 0.9f, weapon, damage, -1, 0);
    brick_world_flush(1);
}

static inline void brick_world_on_surface(int scene_id, float hx, float hy, float hz, float nx, float ny, float nz, int damage, int weapon) {
    if (!brick_world_hook_ready(scene_id)) return;
    bf_checkpoint(&g_brick, &g_brick_cp);
    bf_hit_surface(&g_brick, hx, hy, hz, nx, ny, nz, weapon, damage, 0.9f);
    brick_world_flush(1);
}

/* brick_world_reset_match -- a new match: every wall whole again. Safe with no level loaded. */
static inline void brick_world_reset_match(void) {
    if (!g_brick_active) return;
    bf_clear_damage(&g_brick);
    g_brick_resend_n = 0;
    if (bf_rebuild(&g_brick)) brick_world_commit();
}

static inline int brick_world_box_is_slab(const LevelBox *b) {
    float m = b->w < b->d ? b->w : b->d;
    return b->h <= 12.0f && m >= 20.0f;
}

/* brick_world_init_from_level -- call right after the level's boxes reach physics.h
 * (phys_set_custom_level). Picks the destructible parents, resets all damage and installs the hooks. */
/* AI <-> world hooks (witness_ai.c cannot include physics.h, see witness_ai.h): zombies claw brick
 * walls through brick_world_on_surface, and every AI mover reads the live collision boxes. Weapon
 * WPN_AR is the PARENA material-resistance lookup key; the damage figure is the claw's own. */
static void brick_world_ai_wall_hit(int scene_id, float hx, float hy, float hz,
                                    float nx, float ny, float nz, int damage) {
    brick_world_on_surface(scene_id, hx, hy, hz, nx, ny, nz, damage, WPN_AR);
}
static int brick_world_ai_map(const void **boxes) { *boxes = map_geo; return map_count; }
static inline void brick_world_install_ai_hooks(void) {
    witness_ai_set_wall_hit_hook(brick_world_ai_wall_hit);
    witness_ai_set_map_hook(brick_world_ai_map);
}

/* brick_world_kind_for_material -- level material NAME -> BF_KIND_*, or -1 if indestructible.
 * Founder real-time, 2026-10-02: "add papercraft destructability to concrete and wood and add a
 * new one for glass". Metal and every other material stay indestructible. */
static inline int brick_world_kind_for_material(const char *name) {
    if (strcmp(name, "brick") == 0) return BF_KIND_BRICK;
    if (strcmp(name, "concrete") == 0) return BF_KIND_CONCRETE;
    if (strcmp(name, "wood") == 0) return BF_KIND_WOOD;
    if (strcmp(name, "glass") == 0) return BF_KIND_GLASS;
    return -1;
}

static inline void brick_world_init_from_level(const CustomLevelData *lvl) {
    brick_world_install_ai_hooks();
    bf_reset(&g_brick);
    g_brick_active = 0;
    g_brick_resend_n = 0;
    g_brick_refresh_next_ms = 0;
    g_brick_commit_serial++;
    unsigned char excluded[BF_MAX_PARENTS];
    memset(excluded, 0, sizeof(excluded));
    for (int i = 0; i < lvl->door_count; i++)
        if (lvl->doors[i].box_index >= 0 && lvl->doors[i].box_index < BF_MAX_PARENTS) excluded[lvl->doors[i].box_index] = 1;
    for (int i = 0; i < lvl->button_count; i++)
        if (lvl->buttons[i].box_index >= 0 && lvl->buttons[i].box_index < BF_MAX_PARENTS) excluded[lvl->buttons[i].box_index] = 1;
    int added = 0;
    for (int i = 0; i < lvl->count && i < BF_MAX_PARENTS; i++) {
        const LevelBox *b = &lvl->boxes[i];
        if (excluded[i] || brick_world_box_is_slab(b)) continue;
        if (b->material_idx < 0 || b->material_idx >= lvl->material_count) continue;
        int kind = brick_world_kind_for_material(lvl->materials[b->material_idx].name);
        if (kind < 0) continue;
        if (bf_add_parent_kind(&g_brick, i, b->x, b->y, b->z, b->w, b->h, b->d, kind)) added++;
    }
    g_brick_active = (added > 0);
    /* persisted damage from a saved snapshot: restore the records, then rebuild + commit so the
       carved geometry is live (collision AND render) before the first frame */
    if (g_brick_active && lvl->brick_damage_count > 0) {
        int restored = 0;
        for (int i = 0; i < lvl->brick_damage_count; i++) {
            const LevelBrickCell *bc = &lvl->brick_damage[i];
            if (bc->wall < 0 || bc->wall >= g_brick.parent_count || !g_brick.parent[bc->wall].active) continue;
            int ix, iy, iz; bf_unkey(bc->key, &ix, &iy, &iz);
            const BfParent *bp = &g_brick.parent[bc->wall];
            if (ix >= bp->n[0] || iy >= bp->n[1] || iz >= bp->n[2]) continue;
            if (bf_set_cell_hp(&g_brick, bc->wall, bc->key, bc->hp, 0) > 0) restored++;
        }
        if (restored > 0 && bf_rebuild(&g_brick)) brick_world_commit();
    }
    g_phys_map_hitscan_hook = brick_world_on_hitscan;
    g_phys_map_blast_hook = brick_world_on_blast;
    g_phys_map_surface_hook = brick_world_on_surface;
    g_local_match_reset_hook = brick_world_reset_match;
}

/* brick_world_export_damage -- the current damage as LevelBrickCell records (cells below full hp,
 * i.e. everything bf_set_cell_hp wrote a record for). Returns the count written (<= max). */
static inline int brick_world_export_damage(LevelBrickCell *out, int max) {
    int n = 0;
    for (int r = 0; r < g_brick.rec_count && n < max; r++) {
        const BfRec *rc = &g_brick.rec[r];
        if ((int)rc->hp >= g_brick.parent[rc->parent].max_hp) continue;
        out[n].wall = rc->parent; out[n].key = rc->key; out[n].hp = rc->hp;
        n++;
    }
    return n;
}

/* ---- Network ---------------------------------------------------------------------------------- */

static inline void brick_world_to_wire(const BfNetEntry *e, NetBrickEntry *w) {
    w->parent = e->parent;
    w->key_lo = (unsigned short)(e->key & 0xFFFFu);
    w->key_hi = (unsigned short)(e->key >> 16);
    w->hp = e->hp;
    w->pad = 0;
}

static inline void brick_world_resend_add(const BfNetEntry *e, unsigned int now_ms) {
    for (int i = 0; i < g_brick_resend_n; i++)
        if (g_brick_resend[i].e.parent == e->parent && g_brick_resend[i].e.key == e->key) {
            g_brick_resend[i].e = *e; g_brick_resend[i].left = 2; g_brick_resend[i].next_ms = now_ms + 100u;
            return;
        }
    if (g_brick_resend_n >= BRICK_RESEND_MAX) return;
    g_brick_resend[g_brick_resend_n].e = *e;
    g_brick_resend[g_brick_resend_n].left = 2;
    g_brick_resend[g_brick_resend_n].next_ms = now_ms + 100u;
    g_brick_resend_n++;
}

/* brick_world_net_collect -- server: fills `out` (at most `max` entries) with what to send this
 * tick: brand-new changes first, then entries due a repeat (each change is sent three times in
 * all, spaced out), then a slice of the rotating refresh over every damaged cell so a late joiner
 * or a long-lost datagram converges. Returns the entry count. */
static inline int brick_world_net_collect(NetBrickEntry *out, int max, unsigned int now_ms) {
    if (!g_brick_active || max <= 0) return 0;
    int n = 0, used = 0;
    for (int i = 0; i < g_brick.net_count && n < max; i++, used++) {
        brick_world_to_wire(&g_brick.net[i], &out[n++]);
        brick_world_resend_add(&g_brick.net[i], now_ms);
    }
    if (used > 0) {
        memmove(g_brick.net, g_brick.net + used, sizeof(BfNetEntry) * (size_t)(g_brick.net_count - used));
        g_brick.net_count -= used;
    }
    for (int i = 0; i < g_brick_resend_n && n < max; i++) {
        BrickResend *r = &g_brick_resend[i];
        if (r->left > 0 && now_ms >= r->next_ms) {
            brick_world_to_wire(&r->e, &out[n++]);
            r->left--;
            r->next_ms = now_ms + 250u;
        }
    }
    int w = 0;
    for (int i = 0; i < g_brick_resend_n; i++) if (g_brick_resend[i].left > 0) g_brick_resend[w++] = g_brick_resend[i];
    g_brick_resend_n = w;
    if (n < max && g_brick.rec_count > 0 && now_ms >= g_brick_refresh_next_ms) {
        g_brick_refresh_next_ms = now_ms + BRICK_REFRESH_PERIOD_MS;
        for (int i = 0; i < BRICK_REFRESH_PER_PACKET && n < max; i++) {
            BfNetEntry e;
            if (!bf_next_refresh(&g_brick, &e)) break;
            brick_world_to_wire(&e, &out[n++]);
        }
    }
    return n;
}

/* brick_world_net_apply -- client: applies a received PACKET_BRICK_STATE. Everything in it is
 * untrusted: length and count are checked against what actually arrived, every entry is validated
 * by bf_apply_net. Returns 1 if the committed geometry changed. */
static inline int brick_world_net_apply(const void *buf, int len) {
    if (!g_brick_active) return 0;
    if (len < (int)(sizeof(NetHeader) + 4)) return 0;
    NetBrickState pkt;
    memset(&pkt, 0, sizeof(pkt));
    int copy = len < (int)sizeof(pkt) ? len : (int)sizeof(pkt);
    memcpy(&pkt, buf, (size_t)copy);
    int count = pkt.count;
    if (count > NET_BRICK_MAX_ENTRIES) count = NET_BRICK_MAX_ENTRIES;
    int avail = (copy - (int)(sizeof(NetHeader) + 4)) / (int)sizeof(NetBrickEntry);
    if (count > avail) count = avail;
    for (int i = 0; i < count; i++) {
        unsigned int key = (unsigned int)pkt.e[i].key_lo | ((unsigned int)pkt.e[i].key_hi << 16);
        bf_apply_net(&g_brick, pkt.e[i].parent, key, pkt.e[i].hp);
    }
    g_brick.net_count = 0;      /* a mirror never rebroadcasts */
    return brick_world_flush(0);
}

/* brick_world_drain_events -- cosmetic consumers (debris, bullet-hole cleanup) take this frame's
 * events. Returns the count copied into `out`; the queue is emptied either way. */
static inline int brick_world_drain_events(BfEvent *out, int max) {
    int n = g_brick.ev_count < max ? g_brick.ev_count : max;
    if (n > 0) memcpy(out, g_brick.ev, sizeof(BfEvent) * (size_t)n);
    g_brick.ev_count = 0;
    return n;
}

#endif
