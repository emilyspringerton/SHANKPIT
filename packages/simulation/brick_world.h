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
#include "brick_bodies.h"
#include "../common/obb.h"
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

/* Physical broken brick (brick_bodies.h): GOLDEN BAND rigid bodies. Shards inherit a FRACTION of the
   impact velocity: a bullet's nominal speed x BRICK_INHERIT_FRACTION, a blast's radial push. */
static BrickBodies g_brick_bodies;
static int g_brick_bodies_ready = 0;
#define BRICK_BULLET_SPEED      300.0f
#define BRICK_INHERIT_FRACTION  0.03f     /* 300 m/s x 0.03 = 9 m/s along the shot */
#define BRICK_BLAST_SPEED       16.0f
#define BRICK_CLAW_SPEED        1.5f

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
    if (!g_brick.pieces_dirty) { g_brick.fresh_count = 0; return 0; }
    /* authority only: any cells this change cut off from the world become rigid chunks (set gone here so
       the rebuild below carves them in the same pass; clients get them over the wire with a chunk id) */
    if (g_brick_authority) bf_detect_detached(&g_brick);
    else g_brick.fresh_count = 0;
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
    if (weapon == WPN_KNIFE || weapon == WPN_KATANA || weapon == WPN_FLASHLIGHT || weapon == WPN_SPRAYCAN || weapon == WPN_MISSILE || weapon == WPN_HAMMER) return;
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
        g_brick.impact_radial = 0;
        g_brick.impact_dir[0] = px; g_brick.impact_dir[1] = py; g_brick.impact_dir[2] = pz;
        g_brick.impact_speed = BRICK_BULLET_SPEED * BRICK_INHERIT_FRACTION;
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
    g_brick.impact_radial = 1;
    g_brick.impact_c[0] = x; g_brick.impact_c[1] = y; g_brick.impact_c[2] = z;
    g_brick.impact_radius = radius;
    g_brick.impact_speed = BRICK_BLAST_SPEED;
    bf_damage_sphere(&g_brick, x, y, z, radius * 0.9f, weapon, damage, -1, 0);
    brick_world_flush(1);
}

static inline void brick_world_on_surface(int scene_id, float hx, float hy, float hz, float nx, float ny, float nz, int damage, int weapon) {
    if (!brick_world_hook_ready(scene_id)) return;
    bf_checkpoint(&g_brick, &g_brick_cp);
    g_brick.impact_radial = 0;                       /* a claw / melee strike: push the debris into the wall's far side */
    g_brick.impact_dir[0] = -nx; g_brick.impact_dir[1] = -ny; g_brick.impact_dir[2] = -nz;
    g_brick.impact_speed = BRICK_CLAW_SPEED;
    bf_hit_surface(&g_brick, hx, hy, hz, nx, ny, nz, weapon, damage, 0.9f);
    brick_world_flush(1);
}

/* brick_world_reset_match -- a new match: every wall whole again. Safe with no level loaded. */
static inline void brick_world_reset_match(void) {
    if (!g_brick_active) return;
    bf_clear_damage(&g_brick);
    if (g_brick_bodies_ready) bb_clear(&g_brick_bodies);
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

/* Non-destructible solid geometry (floors, metal, frames, rotated boxes ...) -- what a wall can hang
   from. Doors and buttons are excluded: they move. */
#define BRICK_STATIC_MAX 2048
static float g_brick_static[BRICK_STATIC_MAX][6];   /* x,y,z,w,h,d */
static int g_brick_static_n = 0;
static int brick_world_static_probe(float x, float y, float z) {
    for (int i = 0; i < g_brick_static_n; i++) {
        const float *b = g_brick_static[i];
        if (fabsf(x - b[0]) <= b[3] * 0.5f && fabsf(y - b[1]) <= b[4] * 0.5f && fabsf(z - b[2]) <= b[5] * 0.5f) return 1;
    }
    return 0;
}

static inline void brick_world_init_from_level(const CustomLevelData *lvl) {
    brick_world_install_ai_hooks();
    bf_reset(&g_brick);
    g_brick.static_probe = brick_world_static_probe;
    g_brick_static_n = 0;
    g_brick_active = 0;
    if (g_brick_bodies_ready) bb_clear(&g_brick_bodies);
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
        /* rotated cubes / ramps are not axis-aligned: fracture pieces would be, so they stay solid */
        int fracturable = !(excluded[i] || brick_world_box_is_slab(b) || b->ramp || b->rot_x != 0.0f || b->rot_y != 0.0f || b->rot_z != 0.0f);
        int kind = -1;
        if (fracturable && b->material_idx >= 0 && b->material_idx < lvl->material_count)
            kind = brick_world_kind_for_material(lvl->materials[b->material_idx].name);
        if (kind >= 0 && bf_add_parent_kind(&g_brick, i, b->x, b->y, b->z, b->w, b->h, b->d, kind)) {
            float dens = lvl->materials[b->material_idx].density;
            if (dens > 0.0f) g_brick.parent[i].density = dens;      /* the NOCK material's own mass density */
            added++;
        } else if (!excluded[i] && g_brick_static_n < BRICK_STATIC_MAX) {
            float *sb = g_brick_static[g_brick_static_n++];
            sb[0] = b->x; sb[1] = b->y; sb[2] = b->z; sb[3] = b->w; sb[4] = b->h; sb[5] = b->d;
        }
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
    g_brick.fresh_count = 0;
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
    w->pad = e->chunk;
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
        bf_apply_net_chunk(&g_brick, pkt.e[i].parent, key, pkt.e[i].hp, pkt.e[i].pad);
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

/* ---- physical bodies ---------------------------------------------------------------------------- */

/* Static geometry for the bodies: the level's live collision boxes, oriented boxes and RAMPS included
   (obb.h), so a shard that lands on a ramp slides/tumbles down it. */
static int brick_world_body_push(void *user, const double c[3], double r, double nrm[3], double *pen) {
    (void)user;
    static BoxOrient ident;
    static int ident_ready = 0;
    if (!ident_ready) { orient_identity(&ident); ident_ready = 1; }
    float best = 0.0f; int hit = 0;
    for (int i = 1; i < map_count; i++) {
        const Box *b = &map_geo[i];
        if (b->w <= 0.0f || b->h <= 0.0f || b->d <= 0.0f) continue;
        float R = (b->w + b->h + b->d) * 0.5f + (float)r;
        if (fabsf((float)c[0] - b->x) > R || fabsf((float)c[1] - b->y) > R || fabsf((float)c[2] - b->z) > R) continue;
        const BoxOrient *o = phys_box_orient(i);
        float n[3], depth;
        if (!obb_sphere_push(b->x, b->y, b->z, b->w, b->h, b->d, o ? o : &ident,
                             (float)c[0], (float)c[1], (float)c[2], (float)r, n, &depth)) continue;
        if (depth > best) { best = depth; nrm[0] = n[0]; nrm[1] = n[1]; nrm[2] = n[2]; hit = 1; }
    }
    *pen = best;
    return hit;
}

static inline void brick_world_bodies_init(void) {
    if (g_brick_bodies_ready) return;
    bb_init(&g_brick_bodies, brick_world_body_push, NULL);
    g_brick_bodies_ready = 1;
}

static inline unsigned int brick_world_hash(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static inline double brick_world_h01(unsigned int *s) {
    *s = brick_world_hash(*s + 0x9E3779B9u);
    return (double)(*s & 0xFFFFu) / 65535.0;
}

/* shard box half extents per material kind: brick/concrete/wood a 0.2 m block, glass a thin plate */
static inline void brick_world_shard_half(int kind, double h[3]) {
    if (kind == BF_KIND_GLASS) { h[0] = 0.12; h[1] = 0.03; h[2] = 0.12; }
    else { h[0] = h[1] = h[2] = BB_SHARD_HALF; }
}

/* brick_world_spawn_bodies -- everything the fracture engine queued becomes GOLDEN BAND bodies:
 * shards for shot-out cells, one body per chunk of cells cut off from the world. Safe on every
 * process (the server, a local match, a mirroring client); a client knows no impact, so its bodies
 * simply drop with the same deterministic scatter. */
static inline void brick_world_spawn_bodies(void) {
    brick_world_bodies_init();
    for (int i = 0; i < g_brick.shard_count; i++) {
        const BfShard *sh = &g_brick.shard[i];
        const BfParent *p = &g_brick.parent[sh->parent];
        unsigned int seed = brick_world_hash((unsigned int)(int)(sh->x * 16.0f) * 73856093u ^ (unsigned int)(int)(sh->y * 16.0f) * 19349663u ^
                                              (unsigned int)(int)(sh->z * 16.0f) * 83492791u ^ (unsigned int)i);
        double half[3]; brick_world_shard_half(p->kind, half);
        double mass = (double)p->density * 8.0 * half[0] * half[1] * half[2];
        for (int k = 0; k < sh->count; k++) {
            double pos[3] = { sh->x + (brick_world_h01(&seed) - 0.5) * p->cell[0] * 0.6,
                              sh->y + (brick_world_h01(&seed) - 0.5) * p->cell[1] * 0.6,
                              sh->z + (brick_world_h01(&seed) - 0.5) * p->cell[2] * 0.6 };
            double vel[3] = { sh->vx + (brick_world_h01(&seed) - 0.5) * 3.0,
                              sh->vy + 1.0 + brick_world_h01(&seed) * 2.5,
                              sh->vz + (brick_world_h01(&seed) - 0.5) * 3.0 };
            double om[3] = { (brick_world_h01(&seed) - 0.5) * 14.0, (brick_world_h01(&seed) - 0.5) * 14.0, (brick_world_h01(&seed) - 0.5) * 14.0 };
            bb_spawn(&g_brick_bodies, p->kind, 0, mass, pos, half, vel, om);
        }
    }
    g_brick.shard_count = 0;
    for (int ci = 0; ci < g_brick.chunk_count; ci++) {
        const BfChunk *ch = &g_brick.chunk[ci];
        double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
        double mass = 0.0, kind_mass[4] = { 0, 0, 0, 0 };
        unsigned int seed = brick_world_hash((unsigned int)ch->id * 2654435761u);
        for (int i = 0; i < ch->n; i++) {
            const BfParent *p = &g_brick.parent[ch->cell[i].parent];
            int ix, iy, iz; bf_unkey(ch->cell[i].key, &ix, &iy, &iz);
            float c[3]; bf_cell_center(p, ix, iy, iz, c);
            for (int a = 0; a < 3; a++) {
                double l = (double)c[a] - 0.5 * p->cell[a], h = (double)c[a] + 0.5 * p->cell[a];
                if (l < lo[a]) lo[a] = l;
                if (h > hi[a]) hi[a] = h;
            }
            double m = (double)p->density * p->cell[0] * p->cell[1] * p->cell[2];
            mass += m;
            if (p->kind >= 0 && p->kind < 4) kind_mass[p->kind] += m;
            seed = brick_world_hash(seed ^ ch->cell[i].key);
        }
        int kind = 0;
        for (int k = 1; k < 4; k++) if (kind_mass[k] > kind_mass[kind]) kind = k;
        double pos[3], half[3];
        for (int a = 0; a < 3; a++) { pos[a] = 0.5 * (lo[a] + hi[a]); half[a] = 0.5 * (hi[a] - lo[a]); }
        /* Founder real-time, 2026-10-16: "there should be a bit of rotation so they don't all fall straight
           down ... you can fake it". A seeded sideways drift + tumble, strongest for light/thin glass. */
        double wob = (kind == BF_KIND_GLASS) ? 1.0 : 0.55;
        double vel[3] = { (double)ch->vx + (brick_world_h01(&seed) - 0.5) * 2.4 * wob, (double)ch->vy,
                          (double)ch->vz + (brick_world_h01(&seed) - 0.5) * 2.4 * wob };
        double om[3] = { (brick_world_h01(&seed) - 0.5) * 3.2 * wob, (brick_world_h01(&seed) - 0.5) * 1.6 * wob,
                         (brick_world_h01(&seed) - 0.5) * 3.2 * wob };
        bb_spawn(&g_brick_bodies, kind, 1, mass, pos, half, vel, om);
    }
    g_brick.chunk_count = 0;
}

/* brick_world_discard_bodies -- the dedicated server: broken brick bodies are purely visual (they never feed
 * back into gameplay or the network), so it only empties the queue the fracture engine fills and spends
 * nothing simulating them. Clients do the spawning and stepping. */
static inline void brick_world_discard_bodies(void) {
    g_brick.shard_count = 0;
    g_brick.chunk_count = 0;
}

/* brick_world_tick -- once per frame / server tick with a monotonic millisecond clock: spawn what the
 * fracture engine queued and step the bodies. */
static inline void brick_world_tick(unsigned int now_ms) {
    static unsigned int last_ms = 0;
    brick_world_bodies_init();
    if (g_brick_active) brick_world_spawn_bodies();
    double dt = last_ms ? (double)(now_ms - last_ms) * 0.001 : 0.0;
    last_ms = now_ms;
    if (dt < 0.0 || dt > 1.0) dt = 0.0;
    if (bb_active_count(&g_brick_bodies) > 0) bb_step(&g_brick_bodies, dt);
}

#endif
