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

#define BF_MAX_PARENTS            128
#define BF_MAX_RECORDS            4096
#define BF_MAX_PIECES             900
#define BF_MAX_EVENTS             256
#define BF_MAX_NET_DIRTY          1024
#define BF_CELL_TARGET            3.0f
#define BF_AXIS_MAX               1000
#define BF_MAX_REMOVED_PER_PARENT 160
#define BF_BRICK_KIND             0
/* Destructible kinds (PARENA brick_rules.prn on-brick-paper-material / on-brick-cell-max-hp). */
#define BF_KIND_BRICK    0
#define BF_KIND_CONCRETE 1
#define BF_KIND_WOOD     2
#define BF_KIND_GLASS    3

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
} BfParent;

typedef struct {
    unsigned int key;      /* ix | iy << 10 | iz << 20 */
    int next;              /* next record of the same parent, -1 end */
    unsigned char hp;      /* 0 = gone; absent record = full HP */
    unsigned char parent;
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
} BfNetEntry;

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
} BrickFracture;

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
    if (bf->ev_count >= BF_MAX_EVENTS) return;
    int ix, iy, iz; bf_unkey(key, &ix, &iy, &iz);
    float c[3]; bf_cell_center(p, ix, iy, iz, c);
    BfEvent *e = &bf->ev[bf->ev_count++];
    e->x = c[0]; e->y = c[1]; e->z = c[2];
    e->cw = p->cell[0]; e->ch = p->cell[1]; e->cd = p->cell[2];
    e->parent = parent; e->state = state; e->debris = debris;
}

static inline void bf_push_net(BrickFracture *bf, int parent, unsigned int key, int hp) {
    for (int i = 0; i < bf->net_count; i++)
        if (bf->net[i].parent == parent && bf->net[i].key == key) { bf->net[i].hp = (unsigned char)hp; return; }
    if (bf->net_count >= BF_MAX_NET_DIRTY) return;   /* the rotating refresh will carry it */
    bf->net[bf->net_count].parent = (unsigned short)parent;
    bf->net[bf->net_count].key = key;
    bf->net[bf->net_count].hp = (unsigned char)hp;
    bf->net_count++;
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
        if (bf->rec_count >= BF_MAX_RECORDS) return -1;
        r = bf->rec_count++;
        bf->rec[r].key = key; bf->rec[r].hp = (unsigned char)pmax;
        bf->rec[r].parent = (unsigned char)parent;
        bf->rec[r].next = p->first_rec; p->first_rec = r;
    }
    bf->rec[r].hp = (unsigned char)hp;
    int sb = on_paper_fragment_state_for_hp(before, pmax);
    int sa = on_paper_fragment_state_for_hp(hp, pmax);
    if (before == 0 && hp > 0) { p->removed--; bf->pieces_dirty = 1; }
    if (before > 0 && hp == 0) { p->removed++; bf->pieces_dirty = 1; }
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
 * Piece decomposition
 * ------------------------------------------------------------------------------------------- */

typedef struct {
    BrickFracture *bf;
    int parent;
    int rem[BF_MAX_REMOVED_PER_PARENT][3];
    int nrem;
    int overflow;
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
    int axis = 0, best = hi[0] - lo[0];
    for (int a = 1; a < 3; a++) if (hi[a] - lo[a] > best) { best = hi[a] - lo[a]; axis = a; }
    int mid = lo[axis] + (hi[axis] - lo[axis]) / 2;
    int lo2[3] = { lo[0], lo[1], lo[2] }, hi1[3] = { hi[0], hi[1], hi[2] };
    hi1[axis] = mid; lo2[axis] = mid;
    bf_bisect(b, lo, hi1);
    bf_bisect(b, lo2, hi);
}

/* bf_rebuild -- recomputes the surviving-volume pieces of every fractured parent, in ascending
 * parent order. Returns 1 on success; 0 if the piece budget overflowed (pieces are then left
 * empty and the caller must roll the damage back -- see bf_checkpoint/bf_restore). */
static inline int bf_rebuild(BrickFracture *bf) {
    bf->piece_count = 0;
    BfBuild b;
    b.bf = bf; b.overflow = 0;
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

/* Checkpoint/restore of the damage state (records + parents), so an over-budget damage call can be
 * rolled back whole. The piece/event/net buffers are outputs and are not part of the checkpoint. */
typedef struct {
    BfRec rec[BF_MAX_RECORDS];
    int rec_count;
    int first_rec[BF_MAX_PARENTS];
    int removed[BF_MAX_PARENTS];
    int ev_count, net_count, pieces_dirty;
} BfCheckpoint;

static inline void bf_checkpoint(const BrickFracture *bf, BfCheckpoint *cp) {
    memcpy(cp->rec, bf->rec, sizeof(BfRec) * (size_t)bf->rec_count);
    cp->rec_count = bf->rec_count;
    for (int i = 0; i < bf->parent_count; i++) { cp->first_rec[i] = bf->parent[i].first_rec; cp->removed[i] = bf->parent[i].removed; }
    cp->ev_count = bf->ev_count; cp->net_count = bf->net_count; cp->pieces_dirty = bf->pieces_dirty;
}

static inline void bf_restore(BrickFracture *bf, const BfCheckpoint *cp) {
    memcpy(bf->rec, cp->rec, sizeof(BfRec) * (size_t)cp->rec_count);
    bf->rec_count = cp->rec_count;
    for (int i = 0; i < bf->parent_count; i++) { bf->parent[i].first_rec = cp->first_rec[i]; bf->parent[i].removed = cp->removed[i]; }
    bf->ev_count = cp->ev_count; bf->net_count = cp->net_count; bf->pieces_dirty = cp->pieces_dirty;
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

/* bf_next_refresh -- walks the damaged/gone records round-robin so the server can re-send the whole
 * damaged set a few entries per packet over time. Returns 0 when there are no records. */
static inline int bf_next_refresh(BrickFracture *bf, BfNetEntry *out) {
    if (bf->rec_count <= 0) return 0;
    if (bf->refresh_cursor >= bf->rec_count) bf->refresh_cursor = 0;
    const BfRec *r = &bf->rec[bf->refresh_cursor++];
    out->parent = r->parent; out->key = r->key; out->hp = r->hp;
    return 1;
}

#endif
