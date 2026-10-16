#ifndef SHANKPIT_BRICK_FRACTURE_H
#define SHANKPIT_BRICK_FRACTURE_H

/* brick_fracture.h -- destructible brick for SHANKPIT level boxes, the SHANKPIT half of the
 * PAPERCRAFT "Paper Engine" (PAPERCRAFT/docs/NORTHSTAR_PAPER_ENGINE.md).
 *
 * Founder real-time, 2026-10-01: "get papercraft tech shipped to shankpit i want brick to be
 * destructable."
 *
 * What it is. Each brick level box (a "parent") is overlaid with a virtual grid of cells, about
 * BF_CELL_TARGET world units on a side (a hole is roughly a player wide; a thin wall is one cell
 * thick, so a breach goes straight through). Cells are NOT stored until they are hurt: only
 * damaged or gone cells get a record (sparse), so a 300x300x300 building costs nothing until
 * somebody shoots it. Per-cell HP and the damage maths come from PARENA (PAPERCRAFT's
 * paper_fragment_mod + interact_falloff_mod, plus stdlib/shankpit/brick_rules.prn -- all compiled
 * into packages/simulation/brick_mod.c); this file is only the host half: the grid, the sparse
 * store and the geometry.
 *
 * What a gone cell does to the world. The surviving volume of every fractured parent is
 * re-expressed as a handful of axis-aligned boxes by recursive bisection along the cell grid
 * (bf_rebuild). Those boxes are written back into the SAME collision/render slots every ordinary
 * level box already uses, so movement, hitscan, projectiles, lighting and the material shaders
 * all see real geometry with a real hole in it and no other engine system needed to change.
 * The decomposition is a pure function of (level, set of gone cells), so a server and a client
 * that agree on the cell state agree on the geometry, to the bit.
 *
 * Wire model (see PACKET_BRICK_STATE): the server is authoritative; the unit that crosses the
 * network is "cell (parent, key) now has hp H" -- idempotent, order-independent, so a lost or
 * duplicated or reordered datagram can never desync the world, and a periodic refresh of the
 * damaged set heals anything that was dropped.
 *
 * Deterministic: no wall clock, no rand(); the host passes in every number. Bounded everywhere:
 * every array has a hard cap and an over-budget damage is rolled back rather than truncated, so
 * the collision set can never be left with a hole it did not mean to cut.
 */

#include <math.h>
#include <string.h>

/* PARENA-compiled decisions (packages/simulation/brick_mod.c). */
int on_paper_fragment_damage(int material, int hp, int damage);
int on_paper_fragment_state_for_hp(int hp, int max_hp);
int on_papercraft_interact_damage_falloff(int base_damage, int dist_permille);
int on_brick_weapon_damage(int weapon, int base_damage);
int on_brick_debris_count(int prev_state, int new_state);
int on_brick_paper_material(int brick_kind);
int on_brick_cell_max_hp(int brick_kind);

/* Card #628: was 128 (and BfRec.parent a u8), so in a level with nested/composed objects -- whose walls are
 * flattened AFTER the root walls, past index 127 -- nothing beyond the first 128 boxes was ever destructible
 * (no damage, no render). Now matches LEVEL_BOXES_MAX; the wire already carries a u16 parent. */
#define BF_MAX_PARENTS            2048
#define BF_MAX_RECORDS            16384
#define BF_MAX_PIECES             900   /* = physics.h CUSTOM_LEVEL_FRACTURE_SLOTS */
#define BF_MAX_EVENTS             256
#define BF_MAX_NET_DIRTY          1024
#define BF_CELL_TARGET            3.0f
#define BF_AXIS_MAX               1000
#define BF_MAX_REMOVED_PER_PARENT 1024
#define BF_BRICK_KIND             0
/* Destructible kinds (PARENA brick_rules.prn on-brick-paper-material / on-brick-cell-max-hp). */
#define BF_KIND_BRICK    0
#define BF_KIND_CONCRETE 1
#define BF_KIND_WOOD     2
#define BF_KIND_GLASS    3

/* Default material densities, kg per cubic metre (world units are metres) -- roughly real, "make it up
 * for now" (founder real-time, 2026-10-16). A NOCK material's own "density" overrides these per level
 * (level_boxes.h LevelBoxMaterial.density). A 0.2 m brick shard is 1900 x 0.008 ~= 15 kg. */
#define BF_DENSITY_BRICK    1900.0f
#define BF_DENSITY_CONCRETE 2400.0f
#define BF_DENSITY_WOOD     600.0f
#define BF_DENSITY_GLASS    2500.0f

#define BF_STATE_INTACT  0
#define BF_STATE_CRACKED 1
#define BF_STATE_TORN    2
#define BF_STATE_GONE    3

typedef struct {
    int active;            /* 1 = destructible parent */
    float lo[3];           /* min corner */
    float dim[3];          /* full extents */
    int n[3];              /* cells per axis */
    float cell[3];         /* cell size per axis */
    int first_rec;         /* head of this parent's record list, -1 if none */
    int removed;           /* number of records with hp == 0 */
    int kind;              /* BF_KIND_* */
    int max_hp;            /* full cell HP for this kind (PARENA) */
    int material;          /* PAPER_MATERIAL_* for this kind (PARENA) */
    float density;         /* mass per unit volume; the NOCK material's value, else the kind default */
} BfParent;

typedef struct {
    unsigned int key;      /* ix | iy << 10 | iz << 20 */
    int next;              /* next record of the same parent, -1 end */
    unsigned char hp;      /* 0 = gone; absent record = full HP */
    unsigned char chunk;   /* nonzero: this gone cell fell away as part of rigid chunk `chunk` (not shot out) */
    unsigned short parent;
} BfRec;

typedef struct {
    float x, y, z, w, h, d;   /* centre + full extents, same convention as physics.h Box */
    int parent;
    float uvo[3];             /* piece centre - parent centre: keeps the brick texture phase continuous */
} BfPiece;

/* Cosmetic/telemetry events a host drains once per frame (debris, bullet-hole cleanup). */
typedef struct {
    float x, y, z;            /* cell centre */
    float cw, ch, cd;         /* cell size */
    int parent;
    int state;                /* new BF_STATE_* */
    int debris;               /* pieces to throw (PARENA on_brick_debris_count) */
} BfEvent;

typedef struct {
    unsigned short parent;
    unsigned int key;
    unsigned char hp;
    unsigned char chunk;      /* rides the wire's spare pad byte; 0 = an ordinary cell change */
} BfNetEntry;

/* A group of cells that lost every connection to the rest of the world and so becomes a rigid body.
 * Queued for the host (drained once per frame, like BfEvent); the cells are already GONE in the
 * static geometry by the time a chunk is queued. */
#define BF_MAX_CHUNKS      16
#define BF_CHUNK_CELLS     256
#define BF_MAX_FRESH       1024
typedef struct { unsigned short parent; unsigned int key; } BfCellRef;
typedef struct {
    int id;
    int n;
    float vx, vy, vz;          /* inherited impact velocity (0 on a client that only mirrors) */
    BfCellRef cell[BF_CHUNK_CELLS];
} BfChunk;

/* A cell that was shot / blasted out (not one that merely fell with its chunk): the host turns these
 * into physical shard bodies (packages/simulation/brick_bodies.h) carrying a fraction of the impact. */
#define BF_MAX_SHARDS 128
typedef struct {
    float x, y, z;             /* cell centre */
    float vx, vy, vz;          /* inherited velocity, m/s */
    int parent;
    int count;                 /* shards to throw from this cell */
} BfShard;

typedef struct {
    BfParent parent[BF_MAX_PARENTS];
    int parent_count;
    BfRec rec[BF_MAX_RECORDS];
    int rec_count;
    BfPiece piece[BF_MAX_PIECES];
    int piece_count;
    int pieces_dirty;          /* a cell's gone-ness changed since the last bf_rebuild */
    BfEvent ev[BF_MAX_EVENTS];
    int ev_count;
    BfNetEntry net[BF_MAX_NET_DIRTY];
    int net_count;
    int refresh_cursor;        /* rotating cursor over records for the periodic refresh */
    int max_hp;
    int material;
    BfChunk chunk[BF_MAX_CHUNKS];
    int chunk_count;
    int next_chunk_id;
    BfCellRef fresh[BF_MAX_FRESH];   /* cells that went GONE since the last bf_detect_detached / flush */
    int fresh_count;
    int suppress_debris;       /* 1 while detaching: the chunk IS the visual, no flakes */
    BfShard shard[BF_MAX_SHARDS];
    int shard_count;
    /* Impact the NEXT cell changes come from, set by the host right before it applies damage: a bullet
       (impact_dir, not radial) or a blast (velocity points away from impact_c, fading out by impact_radius). */
    float impact_c[3], impact_dir[3], impact_speed, impact_radius;
    int impact_radial;
    /* host hook: 1 if (x,y,z) lies inside non-destructible solid geometry (an anchor). NULL = only the ground anchors. */
    int (*static_probe)(float x, float y, float z);
} BrickFracture;

static inline float bf_default_density(int kind) {
    switch (kind) {
        case 1: return BF_DENSITY_CONCRETE;
        case 2: return BF_DENSITY_WOOD;
        case 3: return BF_DENSITY_GLASS;
        default: return BF_DENSITY_BRICK;
    }
}

static inline unsigned int bf_key(int ix, int iy, int iz) {
    return (unsigned int)ix | ((unsigned int)iy << 10) | ((unsigned int)iz << 20);
}
static inline void bf_unkey(unsigned int key, int *ix, int *iy, int *iz) {
    *ix = (int)(key & 1023u); *iy = (int)((key >> 10) & 1023u); *iz = (int)((key >> 20) & 1023u);
}

static inline void bf_reset(BrickFracture *bf) {
    memset(bf, 0, sizeof(*bf));
    bf->max_hp = on_brick_cell_max_hp(BF_BRICK_KIND);
    bf->material = on_brick_paper_material(BF_BRICK_KIND);
}

/* bf_clear_damage -- every cell back to full HP, parents kept (a match restart). */
static inline void bf_clear_damage(BrickFracture *bf) {
    bf->rec_count = 0;
    for (int i = 0; i < bf->parent_count; i++) { bf->parent[i].first_rec = -1; bf->parent[i].removed = 0; }
    bf->piece_count = 0;
    bf->pieces_dirty = 1;
    bf->ev_count = 0;
    bf->net_count = 0;
    bf->refresh_cursor = 0;
    bf->chunk_count = 0;
    bf->fresh_count = 0;
    bf->shard_count = 0;
}

/* bf_add_parent -- registers level box `index` (centre x,y,z; full extents w,h,d) as destructible.
 * Indexes must be added in ascending order and are the same index the host uses for the box
 * everywhere else (level box array index). Returns 1 if accepted. */
static inline int bf_add_parent_kind(BrickFracture *bf, int index, float x, float y, float z, float w, float h, float d, int kind) {
    if (index < 0 || index >= BF_MAX_PARENTS) return 0;
    if (!(w > 0.01f && h > 0.01f && d > 0.01f)) return 0;
    while (bf->parent_count <= index) {
        BfParent *e = &bf->parent[bf->parent_count++];
        memset(e, 0, sizeof(*e)); e->first_rec = -1;
    }
    BfParent *p = &bf->parent[index];
    float dim[3] = { w, h, d };
    float ctr[3] = { x, y, z };
    for (int a = 0; a < 3; a++) {
        int n = (int)floorf(dim[a] / BF_CELL_TARGET + 0.5f);
        if (n < 1) n = 1;
        if (n > BF_AXIS_MAX) n = BF_AXIS_MAX;
        p->n[a] = n;
        p->dim[a] = dim[a];
        p->cell[a] = dim[a] / (float)n;
        p->lo[a] = ctr[a] - dim[a] * 0.5f;
    }
    p->active = 1; p->first_rec = -1; p->removed = 0;
    p->kind = kind;
    p->max_hp = on_brick_cell_max_hp(kind);
    p->material = on_brick_paper_material(kind);
    p->density = bf_default_density(kind);
    return 1;
}

static inline int bf_add_parent(BrickFracture *bf, int index, float x, float y, float z, float w, float h, float d) {
    return bf_add_parent_kind(bf, index, x, y, z, w, h, d, BF_KIND_BRICK);
}

static inline int bf_find_rec(const BrickFracture *bf, int parent, unsigned int key) {
    for (int r = bf->parent[parent].first_rec; r >= 0; r = bf->rec[r].next)
        if (bf->rec[r].key == key) return r;
    return -1;
}

static inline void bf_cell_center(const BfParent *p, int ix, int iy, int iz, float *c) {
    c[0] = p->lo[0] + ((float)ix + 0.5f) * p->cell[0];
    c[1] = p->lo[1] + ((float)iy + 0.5f) * p->cell[1];
    c[2] = p->lo[2] + ((float)iz + 0.5f) * p->cell[2];
}

static inline int bf_cell_hp(const BrickFracture *bf, int parent, unsigned int key) {
    int r = bf_find_rec(bf, parent, key);
    return r < 0 ? bf->parent[parent].max_hp : (int)bf->rec[r].hp;
}

static inline void bf_push_event(BrickFracture *bf, const BfParent *p, int parent, unsigned int key, int state, int debris) {
    int ix, iy, iz; bf_unkey(key, &ix, &iy, &iz);
    float c[3]; bf_cell_center(p, ix, iy, iz, c);
    /* shards first and independent of the (cosmetic) event queue being full: a host that never drains events still gets bodies */
    if (state == BF_STATE_GONE && !bf->suppress_debris && bf->shard_count < BF_MAX_SHARDS) {
        BfShard *sh = &bf->shard[bf->shard_count++];
        sh->x = c[0]; sh->y = c[1]; sh->z = c[2];
        sh->parent = parent;
        sh->count = 2;
        float dir[3] = { bf->impact_dir[0], bf->impact_dir[1], bf->impact_dir[2] };
        float spd = bf->impact_speed;
        if (bf->impact_radial) {
            float d[3] = { c[0] - bf->impact_c[0], c[1] - bf->impact_c[1], c[2] - bf->impact_c[2] };
            float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 1e-4f) { dir[0] = d[0] / len; dir[1] = d[1] / len; dir[2] = d[2] / len; }
            else { dir[0] = 0.0f; dir[1] = 1.0f; dir[2] = 0.0f; }
            float t = bf->impact_radius > 1e-4f ? len / bf->impact_radius : 1.0f;
            if (t > 1.0f) t = 1.0f;
            spd *= 1.0f - 0.7f * t;
        }
        sh->vx = dir[0] * spd; sh->vy = dir[1] * spd; sh->vz = dir[2] * spd;
    }
    if (bf->ev_count >= BF_MAX_EVENTS) return;
    BfEvent *e = &bf->ev[bf->ev_count++];
    e->x = c[0]; e->y = c[1]; e->z = c[2];
    e->cw = p->cell[0]; e->ch = p->cell[1]; e->cd = p->cell[2];
    e->parent = parent; e->state = state; e->debris = bf->suppress_debris ? 0 : debris;
}

static inline void bf_push_net_chunk(BrickFracture *bf, int parent, unsigned int key, int hp, int chunk) {
    for (int i = 0; i < bf->net_count; i++)
        if (bf->net[i].parent == parent && bf->net[i].key == key) {
            bf->net[i].hp = (unsigned char)hp; bf->net[i].chunk = (unsigned char)chunk; return;
        }
    if (bf->net_count >= BF_MAX_NET_DIRTY) return;   /* the rotating refresh will carry it */
    bf->net[bf->net_count].parent = (unsigned short)parent;
    bf->net[bf->net_count].key = key;
    bf->net[bf->net_count].hp = (unsigned char)hp;
    bf->net[bf->net_count].chunk = (unsigned char)chunk;
    bf->net_count++;
}
static inline void bf_push_net(BrickFracture *bf, int parent, unsigned int key, int hp) {
    bf_push_net_chunk(bf, parent, key, hp, 0);
}

/* bf_chunk_note -- add a fallen cell to chunk `id` of this frame's queue (same id = same body). */
static inline void bf_chunk_note(BrickFracture *bf, int id, int parent, unsigned int key) {
    BfChunk *c = NULL;
    for (int i = 0; i < bf->chunk_count; i++) if (bf->chunk[i].id == id) { c = &bf->chunk[i]; break; }
    if (!c) {
        if (bf->chunk_count >= BF_MAX_CHUNKS) return;
        c = &bf->chunk[bf->chunk_count++];
        c->id = id; c->n = 0;
        c->vx = bf->impact_dir[0] * bf->impact_speed * 0.5f;     /* a chunk takes half a shard's push */
        c->vy = bf->impact_dir[1] * bf->impact_speed * 0.5f;
        c->vz = bf->impact_dir[2] * bf->impact_speed * 0.5f;
    }
    if (c->n >= BF_CHUNK_CELLS) return;
    c->cell[c->n].parent = (unsigned short)parent;
    c->cell[c->n].key = key;
    c->n++;
}

/* bf_compact_records -- the record table is full: forget every merely-damaged (hp>0) cell, keeping
 * all gone cells (those ARE the geometry). Forgotten cells read as undamaged again, so the client
 * is told via the net queue. Returns how many records were freed. Only gone cells are permanent,
 * so destruction is bounded by the level's cell count, not by this table. */
static inline int bf_compact_records(BrickFracture *bf) {
    int keep = 0, freed = 0;
    for (int i = 0; i < bf->parent_count; i++) bf->parent[i].first_rec = -1;
    for (int r = 0; r < bf->rec_count; r++) {
        BfRec rc = bf->rec[r];
        if (rc.hp == 0) {
            bf->rec[keep] = rc;
            bf->rec[keep].next = bf->parent[rc.parent].first_rec;
            bf->parent[rc.parent].first_rec = keep++;
        } else {
            freed++;
        }
    }
    bf->rec_count = keep;
    bf->refresh_cursor = 0;
    return freed;
}

/* bf_set_cell_hp -- the one place a cell's HP changes. Emits events, queues the network entry and
 * keeps the removed count / dirty flag honest. Returns 1 if anything changed, -1 if there was no
 * room for a record, 0 if unchanged. */
static inline int bf_set_cell_hp(BrickFracture *bf, int parent, unsigned int key, int hp, int emit_events) {
    BfParent *p = &bf->parent[parent];
    if (hp < 0) hp = 0;
    int pmax = p->max_hp;
    if (hp > pmax) hp = pmax;
    int r = bf_find_rec(bf, parent, key);
    int before = (r < 0) ? pmax : (int)bf->rec[r].hp;
    if (before == hp) return 0;
    if (r < 0) {
        if (bf->rec_count >= BF_MAX_RECORDS && bf_compact_records(bf) == 0) return -1;
        r = bf->rec_count++;
        bf->rec[r].key = key; bf->rec[r].hp = (unsigned char)pmax;
        bf->rec[r].parent = (unsigned short)parent;
        bf->rec[r].chunk = 0;
        bf->rec[r].next = p->first_rec; p->first_rec = r;
    }
    bf->rec[r].hp = (unsigned char)hp;
    int sb = on_paper_fragment_state_for_hp(before, pmax);
    int sa = on_paper_fragment_state_for_hp(hp, pmax);
    if (before == 0 && hp > 0) { p->removed--; bf->pieces_dirty = 1; }
    if (before > 0 && hp == 0) {
        p->removed++; bf->pieces_dirty = 1;
        if (bf->fresh_count < BF_MAX_FRESH) {
            bf->fresh[bf->fresh_count].parent = (unsigned short)parent;
            bf->fresh[bf->fresh_count].key = key;
            bf->fresh_count++;
        }
    }
    if (emit_events && sa != sb) bf_push_event(bf, p, parent, key, sa, on_brick_debris_count(sb, sa));
    return 1;
}

/* bf_damage_sphere -- every cell of every destructible parent whose centre lies within `radius`
 * of (cx,cy,cz) takes weapon-adjusted, distance-falloff damage, then PARENA's material resistance.
 * `primary_parent`/`primary_key` (or -1) name the cell the shot actually struck: it always takes
 * full (distance 0) damage even if its centre is off to one side of a large cell. Returns the
 * number of cells that reached GONE. Over-budget (record table full) cells are skipped. */
static inline int bf_damage_sphere(BrickFracture *bf, float cx, float cy, float cz, float radius,
                                   int weapon, int base_damage, int primary_parent, unsigned int primary_key) {
    if (radius <= 0.0f) radius = 0.01f;
    int eff = on_brick_weapon_damage(weapon, base_damage);
    if (eff <= 0) return 0;
    float c[3] = { cx, cy, cz };
    int gone = 0;
    for (int pi = 0; pi < bf->parent_count; pi++) {
        BfParent *p = &bf->parent[pi];
        if (!p->active) continue;
        int i0[3], i1[3], skip = 0;
        for (int a = 0; a < 3; a++) {
            float lo = c[a] - radius, hi = c[a] + radius;
            if (hi < p->lo[a] || lo > p->lo[a] + p->dim[a]) { skip = 1; break; }
            int a0 = (int)floorf((lo - p->lo[a]) / p->cell[a]);
            int a1 = (int)floorf((hi - p->lo[a]) / p->cell[a]);
            if (a0 < 0) a0 = 0;
            if (a1 > p->n[a] - 1) a1 = p->n[a] - 1;
            i0[a] = a0; i1[a] = a1;
        }
        if (skip) continue;
        for (int iz = i0[2]; iz <= i1[2]; iz++)
            for (int iy = i0[1]; iy <= i1[1]; iy++)
                for (int ix = i0[0]; ix <= i1[0]; ix++) {
                    unsigned int key = bf_key(ix, iy, iz);
                    int is_primary = (pi == primary_parent && key == primary_key);
                    float cc[3]; bf_cell_center(p, ix, iy, iz, cc);
                    float dx = cc[0] - cx, dy = cc[1] - cy, dz = cc[2] - cz;
                    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                    if (!is_primary && dist > radius) continue;
                    int permille = is_primary ? 0 : (int)((dist / radius) * 1000.0f);
                    int dmg = on_papercraft_interact_damage_falloff(eff, permille);
                    if (dmg <= 0) continue;
                    int before = bf_cell_hp(bf, pi, key);
                    if (before <= 0) continue;
                    int after = on_paper_fragment_damage(p->material, before, dmg);
                    if (after == 0 && p->removed >= BF_MAX_REMOVED_PER_PARENT) after = 1;
                    if (after == before) continue;
                    int rc = bf_set_cell_hp(bf, pi, key, after, 1);
                    if (rc <= 0) continue;
                    bf_push_net(bf, pi, key, after);
                    if (after == 0) gone++;
                }
    }
    return gone;
}

/* bf_find_parent_at -- the destructible parent whose SOLID volume contains the point (a point a
 * hair inside a struck face). The cell must not already be gone, so overlapping parents resolve to
 * the one that is actually still there. Returns parent index or -1; fills *key. */
static inline int bf_find_parent_at(const BrickFracture *bf, float x, float y, float z, unsigned int *key) {
    float c[3] = { x, y, z };
    for (int pi = 0; pi < bf->parent_count; pi++) {
        const BfParent *p = &bf->parent[pi];
        if (!p->active) continue;
        int idx[3], inside = 1;
        for (int a = 0; a < 3; a++) {
            float t = (c[a] - p->lo[a]) / p->cell[a];
            if (c[a] < p->lo[a] - 0.001f || c[a] > p->lo[a] + p->dim[a] + 0.001f) { inside = 0; break; }
            int i = (int)floorf(t);
            if (i < 0) i = 0;
            if (i > p->n[a] - 1) i = p->n[a] - 1;
            idx[a] = i;
        }
        if (!inside) continue;
        unsigned int k = bf_key(idx[0], idx[1], idx[2]);
        if (bf_cell_hp(bf, pi, k) <= 0) continue;
        *key = k;
        return pi;
    }
    return -1;
}

/* bf_hit_surface -- one bullet/pellet that struck a face at (hx,hy,hz) with outward normal
 * (nx,ny,nz). Damages the struck cell fully and its neighbours by falloff. Returns cells gone. */
static inline int bf_hit_surface(BrickFracture *bf, float hx, float hy, float hz,
                                 float nx, float ny, float nz, int weapon, int base_damage, float radius_scale) {
    unsigned int key = 0;
    int pi = bf_find_parent_at(bf, hx - nx * 0.05f, hy - ny * 0.05f, hz - nz * 0.05f, &key);
    if (pi < 0) return 0;
    const BfParent *p = &bf->parent[pi];
    float cmax = p->cell[0];
    if (p->cell[1] > cmax) cmax = p->cell[1];
    if (p->cell[2] > cmax) cmax = p->cell[2];
    float depth = 0.25f * cmax;
    return bf_damage_sphere(bf, hx - nx * depth, hy - ny * depth, hz - nz * depth,
                            cmax * radius_scale, weapon, base_damage, pi, key);
}

/* ---------------------------------------------------------------------------------------------
 * Disconnected geometry -> rigid chunks
 *
 * Founder real-time, 2026-10-16: "if you shoot a circle in glass and it is totally unconnected to
 * anything the pieces shouldn't float in the air, they should fall with rigid body physics".
 *
 * After damage, every surviving cell face-adjacent to a cell that just went gone is flood-filled
 * (bounded: BF_DET_BUDGET cells, so a big wall costs nothing -- a region that big is simply
 * supported). A region is ANCHORED if it reaches the ground (y ~ 0) or any non-destructible solid
 * (bf->static_probe) -- floors, frames, metal -- or runs past the budget. A region that closes up
 * with no anchor has lost every connection to the world and falls: its cells are set GONE in the
 * static geometry (so collision/render/network all see the hole at once, on the existing hp
 * wire) and queued as a BfChunk for the host's rigid-body solver.
 *
 * One deliberate exception: geometry the level author floated in mid-air with nothing under it was
 * already unsupported before the shot. Shooting a hole in it must not drop the whole pane, so for a
 * region that was ALREADY unanchored before this damage, only a minor fragment (under half of its
 * pre-damage region) falls -- the disc shot out of the middle drops, the pane it came from stays.
 * ------------------------------------------------------------------------------------------- */

#define BF_DET_TABLE   16384          /* power of two */
#define BF_DET_BUDGET  2048

typedef struct { unsigned int key[BF_DET_TABLE]; short par[BF_DET_TABLE]; unsigned short gen[BF_DET_TABLE]; unsigned short cur; int count; } BfSet;

static inline void bf_set_clear(BfSet *s) {
    for (int i = 0; i < BF_DET_TABLE; i++) s->par[i] = -1;
    s->count = 0;
    s->cur = 1;
}
/* 1 = newly added, 0 = already present from THIS flood, 2 = present from an EARLIER flood, -1 = table full */
static inline int bf_set_add(BfSet *s, int parent, unsigned int key) {
    if (s->count >= BF_DET_TABLE / 2) return -1;
    unsigned int h = (key * 2654435761u + (unsigned int)parent * 40503u) & (BF_DET_TABLE - 1);
    while (s->par[h] >= 0) {
        if (s->par[h] == parent && s->key[h] == key) return s->gen[h] == s->cur ? 0 : 2;
        h = (h + 1) & (BF_DET_TABLE - 1);
    }
    s->par[h] = (short)parent; s->key[h] = key; s->gen[h] = s->cur; s->count++;
    return 1;
}

static inline int bf_is_fresh(const BrickFracture *bf, int parent, unsigned int key) {
    for (int i = 0; i < bf->fresh_count; i++)
        if (bf->fresh[i].parent == parent && bf->fresh[i].key == key) return 1;
    return 0;
}

/* the surviving cell whose volume contains the point; `fresh_alive` counts this frame's gone cells as present */
static inline int bf_find_cell_at(const BrickFracture *bf, float x, float y, float z, int fresh_alive, unsigned int *key) {
    float c[3] = { x, y, z };
    for (int pi = 0; pi < bf->parent_count; pi++) {
        const BfParent *p = &bf->parent[pi];
        if (!p->active) continue;
        int idx[3], inside = 1;
        for (int a = 0; a < 3; a++) {
            if (c[a] < p->lo[a] || c[a] > p->lo[a] + p->dim[a]) { inside = 0; break; }
            int i = (int)floorf((c[a] - p->lo[a]) / p->cell[a]);
            if (i < 0) i = 0;
            if (i > p->n[a] - 1) i = p->n[a] - 1;
            idx[a] = i;
        }
        if (!inside) continue;
        unsigned int k = bf_key(idx[0], idx[1], idx[2]);
        if (bf_cell_hp(bf, pi, k) <= 0 && !(fresh_alive && bf_is_fresh(bf, pi, k))) continue;
        *key = k;
        return pi;
    }
    return -1;
}

/* One face neighbour of a cell. 1 = a present cell (np,nk), 2 = anchored by that face, 0 = air/gone. */
static inline int bf_neighbor(const BrickFracture *bf, int pi, unsigned int key, int dir, int fresh_alive,
                              int *np, unsigned int *nk) {
    const BfParent *p = &bf->parent[pi];
    int idx[3]; bf_unkey(key, &idx[0], &idx[1], &idx[2]);
    int a = dir >> 1, sgn = (dir & 1) ? 1 : -1;
    int ni = idx[a] + sgn;
    if (ni >= 0 && ni < p->n[a]) {
        int j[3] = { idx[0], idx[1], idx[2] };
        j[a] = ni;
        unsigned int k2 = bf_key(j[0], j[1], j[2]);
        if (bf_cell_hp(bf, pi, k2) > 0 || (fresh_alive && bf_is_fresh(bf, pi, k2))) { *np = pi; *nk = k2; return 1; }
        return 0;
    }
    float c[3]; bf_cell_center(p, idx[0], idx[1], idx[2], c);
    c[a] += (float)sgn * (p->cell[a] * 0.5f + 0.04f);
    if (a == 1 && sgn < 0 && c[1] < 0.02f) return 2;                    /* resting on the ground */
    unsigned int k3;
    int q = bf_find_cell_at(bf, c[0], c[1], c[2], fresh_alive, &k3);
    if (q >= 0) { *np = q; *nk = k3; return 1; }
    if (bf->static_probe && bf->static_probe(c[0], c[1], c[2])) return 2;
    return 0;
}

/* Flood from a start cell already added to `vis`. Returns 1 if anchored (or over budget), 0 if the
 * region is closed; `out[0..*n)` is the region (complete only when 0 is returned). */
static inline int bf_flood(const BrickFracture *bf, BfSet *vis, int pi, unsigned int key, int fresh_alive,
                           BfCellRef *out, int *n) {
    *n = 0;
    out[(*n)++] = (BfCellRef){ (unsigned short)pi, key };
    for (int head = 0; head < *n; head++) {
        BfCellRef cur = out[head];
        for (int dir = 0; dir < 6; dir++) {
            int np; unsigned int nk;
            int r = bf_neighbor(bf, cur.parent, cur.key, dir, fresh_alive, &np, &nk);
            if (r == 2) return 1;
            if (r != 1) continue;
            int ins = bf_set_add(vis, np, nk);
            if (ins < 0 || ins == 2) return 1;   /* full, or ran into a region an earlier flood already found supported */
            if (ins == 0) continue;
            if (*n >= BF_DET_BUDGET) return 1;
            out[(*n)++] = (BfCellRef){ (unsigned short)np, nk };
        }
    }
    return 0;
}

/* bf_detect_detached -- the whole pass; call once per changed frame BEFORE bf_rebuild so detached
 * cells are carved in the same rebuild. Consumes bf->fresh. Returns the number of chunks made. */
static inline int bf_detect_detached(BrickFracture *bf) {
    static BfSet vis, vis2;
    static BfCellRef cells[BF_DET_BUDGET], cells2[BF_DET_BUDGET];
    int nf = bf->fresh_count;
    if (nf <= 0) return 0;
    bf_set_clear(&vis);
    int made = 0;
    for (int fi = 0; fi < nf; fi++) {
        BfCellRef f = bf->fresh[fi];
        for (int dir = 0; dir < 6; dir++) {
            int np; unsigned int nk;
            if (bf_neighbor(bf, f.parent, f.key, dir, 0, &np, &nk) != 1) continue;
            vis.cur++;                                                   /* each flood is its own generation */
            int ins = bf_set_add(&vis, np, nk);
            if (ins < 0) { fi = nf; break; }
            if (ins != 1) continue;                                      /* already covered by an earlier flood */
            int n;
            if (bf_flood(bf, &vis, np, nk, 0, cells, &n)) continue;      /* supported */
            if (n > BF_CHUNK_CELLS) continue;                            /* too big to animate: stays put */
            bf_set_clear(&vis2);
            bf_set_add(&vis2, cells[0].parent, cells[0].key);
            int n2;
            int anch2 = bf_flood(bf, &vis2, cells[0].parent, cells[0].key, 1, cells2, &n2);
            if (!anch2) {
                /* authored-floating mass: it keeps its majority. Compare against the cells still standing (the
                   region before the cut also counted the cells this very cut removed). */
                int standing = 0;
                for (int i = 0; i < n2; i++) if (bf_cell_hp(bf, cells2[i].parent, cells2[i].key) > 0) standing++;
                if (n * 2 >= standing) continue;
            }
            /* budget checks: never half-detach */
            int ok = (bf->rec_count + n <= BF_MAX_RECORDS);
            for (int i = 0; i < n && ok; i++)
                if (bf->parent[cells[i].parent].removed + n > BF_MAX_REMOVED_PER_PARENT) ok = 0;
            if (!ok) continue;
            int id = ++bf->next_chunk_id;
            if (id > 255) { bf->next_chunk_id = 1; id = 1; }
            bf->suppress_debris = 1;
            for (int i = 0; i < n; i++) {
                if (bf_set_cell_hp(bf, cells[i].parent, cells[i].key, 0, 1) <= 0) continue;
                int r = bf_find_rec(bf, cells[i].parent, cells[i].key);
                if (r >= 0) bf->rec[r].chunk = (unsigned char)id;
                bf_push_net_chunk(bf, cells[i].parent, cells[i].key, 0, id);
                bf_chunk_note(bf, id, cells[i].parent, cells[i].key);
            }
            bf->suppress_debris = 0;
            made++;
        }
    }
    bf->fresh_count = 0;
    return made;
}

/* ---------------------------------------------------------------------------------------------
 * Piece decomposition
 * ------------------------------------------------------------------------------------------- */

typedef struct {
    BrickFracture *bf;
    int parent;
    int rem[BF_MAX_REMOVED_PER_PARENT][3];
    int nrem;
    int overflow;
    int coarse;      /* >0: blocks no larger than this many cells on every axis are emitted/dropped whole */
} BfBuild;

static inline void bf_emit_piece(BfBuild *b, const int lo[3], const int hi[3]) {
    BrickFracture *bf = b->bf;
    if (bf->piece_count >= BF_MAX_PIECES) { b->overflow = 1; return; }
    const BfParent *p = &bf->parent[b->parent];
    float w[3], c[3];
    for (int a = 0; a < 3; a++) {
        float x0 = p->lo[a] + (float)lo[a] * p->cell[a];
        float x1 = (hi[a] == p->n[a]) ? (p->lo[a] + p->dim[a]) : (p->lo[a] + (float)hi[a] * p->cell[a]);
        w[a] = x1 - x0;
        c[a] = x0 + w[a] * 0.5f;
    }
    BfPiece *pc = &bf->piece[bf->piece_count++];
    pc->x = c[0]; pc->y = c[1]; pc->z = c[2];
    pc->w = w[0]; pc->h = w[1]; pc->d = w[2];
    pc->parent = b->parent;
    for (int a = 0; a < 3; a++) pc->uvo[a] = c[a] - (p->lo[a] + p->dim[a] * 0.5f);
}

static inline void bf_bisect(BfBuild *b, const int lo[3], const int hi[3]) {
    if (b->overflow) return;
    long vol = (long)(hi[0] - lo[0]) * (long)(hi[1] - lo[1]) * (long)(hi[2] - lo[2]);
    long inside = 0;
    for (int r = 0; r < b->nrem; r++) {
        const int *c = b->rem[r];
        if (c[0] >= lo[0] && c[0] < hi[0] && c[1] >= lo[1] && c[1] < hi[1] && c[2] >= lo[2] && c[2] < hi[2]) inside++;
    }
    if (inside == vol) return;                      /* entirely gone: nothing to emit */
    if (inside == 0) { bf_emit_piece(b, lo, hi); return; }
    if (b->coarse > 0 && hi[0] - lo[0] <= b->coarse && hi[1] - lo[1] <= b->coarse && hi[2] - lo[2] <= b->coarse) {
        /* over-budget approximation: a mostly-solid block stays one solid piece, a mostly-gone one
           is dropped. The exact per-cell damage records are untouched, so geometry sharpens again
           as soon as the budget frees up (match reset, other walls fully gone). */
        if (inside * 2 < vol) bf_emit_piece(b, lo, hi);
        return;
    }
    int axis = 0, best = hi[0] - lo[0];
    for (int a = 1; a < 3; a++) if (hi[a] - lo[a] > best) { best = hi[a] - lo[a]; axis = a; }
    int mid = lo[axis] + (hi[axis] - lo[axis]) / 2;
    int lo2[3] = { lo[0], lo[1], lo[2] }, hi1[3] = { hi[0], hi[1], hi[2] };
    hi1[axis] = mid; lo2[axis] = mid;
    bf_bisect(b, lo, hi1);
    bf_bisect(b, lo2, hi);
}

static inline int bf_rebuild_at(BrickFracture *bf, int coarse) {
    bf->piece_count = 0;
    BfBuild b;
    b.bf = bf; b.overflow = 0; b.coarse = coarse;
    for (int pi = 0; pi < bf->parent_count; pi++) {
        const BfParent *p = &bf->parent[pi];
        if (!p->active || p->removed <= 0) continue;
        b.parent = pi; b.nrem = 0;
        for (int r = p->first_rec; r >= 0 && b.nrem < BF_MAX_REMOVED_PER_PARENT; r = bf->rec[r].next) {
            if (bf->rec[r].hp != 0) continue;
            bf_unkey(bf->rec[r].key, &b.rem[b.nrem][0], &b.rem[b.nrem][1], &b.rem[b.nrem][2]);
            b.nrem++;
        }
        int lo[3] = { 0, 0, 0 }, hi[3] = { p->n[0], p->n[1], p->n[2] };
        bf_bisect(&b, lo, hi);
        if (b.overflow) { bf->piece_count = 0; return 0; }
    }
    bf->pieces_dirty = 0;
    return 1;
}

/* bf_rebuild -- recomputes the surviving-volume pieces of every fractured parent, in ascending
 * parent order. Exact first; if the shared piece budget overflows it retries with progressively
 * coarser blocks (1,2,4,... cells) so destruction keeps working in a busy level instead of
 * freezing -- the geometry just gets approximate. Returns 1 on success; 0 only if even the
 * coarsest pass overflows (pieces are then left empty and the caller must roll the damage back --
 * see bf_checkpoint/bf_restore). */
static inline int bf_rebuild(BrickFracture *bf) {
    if (bf_rebuild_at(bf, 0)) return 1;
    for (int c = 1; c <= BF_AXIS_MAX; c *= 2)
        if (bf_rebuild_at(bf, c)) return 1;
    return 0;
}

/* Checkpoint/restore of the damage state (records + parents), so an over-budget damage call can be
 * rolled back whole. The piece/event/net buffers are outputs and are not part of the checkpoint. */
typedef struct {
    BfRec rec[BF_MAX_RECORDS];
    int rec_count;
    int first_rec[BF_MAX_PARENTS];
    int removed[BF_MAX_PARENTS];
    int ev_count, net_count, pieces_dirty, chunk_count, fresh_count, shard_count;
} BfCheckpoint;

static inline void bf_checkpoint(const BrickFracture *bf, BfCheckpoint *cp) {
    memcpy(cp->rec, bf->rec, sizeof(BfRec) * (size_t)bf->rec_count);
    cp->rec_count = bf->rec_count;
    for (int i = 0; i < bf->parent_count; i++) { cp->first_rec[i] = bf->parent[i].first_rec; cp->removed[i] = bf->parent[i].removed; }
    cp->ev_count = bf->ev_count; cp->net_count = bf->net_count; cp->pieces_dirty = bf->pieces_dirty;
    cp->chunk_count = bf->chunk_count; cp->fresh_count = bf->fresh_count; cp->shard_count = bf->shard_count;
}

static inline void bf_restore(BrickFracture *bf, const BfCheckpoint *cp) {
    memcpy(bf->rec, cp->rec, sizeof(BfRec) * (size_t)cp->rec_count);
    bf->rec_count = cp->rec_count;
    for (int i = 0; i < bf->parent_count; i++) { bf->parent[i].first_rec = cp->first_rec[i]; bf->parent[i].removed = cp->removed[i]; }
    bf->ev_count = cp->ev_count; bf->net_count = cp->net_count; bf->pieces_dirty = cp->pieces_dirty;
    bf->chunk_count = cp->chunk_count; bf->fresh_count = cp->fresh_count; bf->shard_count = cp->shard_count;
}

/* ---------------------------------------------------------------------------------------------
 * Network
 * ------------------------------------------------------------------------------------------- */

/* bf_apply_net -- a client applying one authoritative (parent, key, hp). Validates everything (a
 * packet is untrusted input). Returns 1 if the cell changed. */
static inline int bf_apply_net(BrickFracture *bf, int parent, unsigned int key, int hp) {
    if (parent < 0 || parent >= bf->parent_count || !bf->parent[parent].active) return 0;
    int ix, iy, iz; bf_unkey(key, &ix, &iy, &iz);
    const BfParent *p = &bf->parent[parent];
    if (ix >= p->n[0] || iy >= p->n[1] || iz >= p->n[2]) return 0;
    if (key >> 30) return 0;                                  /* stray high bits */
    if (hp < 0 || hp > p->max_hp) return 0;
    return bf_set_cell_hp(bf, parent, key, hp, 1) > 0;
}

/* bf_apply_net_chunk -- bf_apply_net for an entry that may carry a chunk id. A cell going GONE that
 * way is a piece of wall that fell away whole: it is queued as part of that rigid chunk instead of
 * throwing flakes. (Refresh entries always carry chunk 0, so a late joiner never replays old falls.) */
static inline int bf_apply_net_chunk(BrickFracture *bf, int parent, unsigned int key, int hp, int chunk) {
    if (parent < 0 || parent >= bf->parent_count || !bf->parent[parent].active) return 0;
    int ix, iy, iz; bf_unkey(key, &ix, &iy, &iz);
    const BfParent *p = &bf->parent[parent];
    if (ix >= p->n[0] || iy >= p->n[1] || iz >= p->n[2]) return 0;
    if (key >> 30) return 0;
    if (hp < 0 || hp > p->max_hp) return 0;
    int falls = (chunk != 0 && hp == 0 && bf_cell_hp(bf, parent, key) > 0);
    bf->suppress_debris = falls;
    int rc = bf_set_cell_hp(bf, parent, key, hp, 1);
    bf->suppress_debris = 0;
    if (rc > 0 && falls) {
        int r = bf_find_rec(bf, parent, key);
        if (r >= 0) bf->rec[r].chunk = (unsigned char)chunk;
        bf_chunk_note(bf, chunk, parent, key);
    }
    return rc > 0;
}

/* bf_next_refresh -- walks the damaged/gone records round-robin so the server can re-send the whole
 * damaged set a few entries per packet over time. Returns 0 when there are no records. */
static inline int bf_next_refresh(BrickFracture *bf, BfNetEntry *out) {
    if (bf->rec_count <= 0) return 0;
    if (bf->refresh_cursor >= bf->rec_count) bf->refresh_cursor = 0;
    const BfRec *r = &bf->rec[bf->refresh_cursor++];
    out->parent = r->parent; out->key = r->key; out->hp = r->hp; out->chunk = 0;
    return 1;
}

#endif
