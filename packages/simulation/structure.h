#ifndef STRUCTURE_H
#define STRUCTURE_H

/* structure.h -- the structural layer over destructible brick: which pieces are joined, which are
 * still connected to the ground, and which one is carrying more than it can.
 *
 * Founder real-time (2026-10-30): "evolve the destructible brick logic into an intelligent, physically
 * aware material and structural simulation ... a web of self-anchoring volumetric nodes". The proposal
 * that prompted it assumed a finite-element solver and a "layer bake"; neither is here, and both would
 * be the wrong shape for an MMO tick budget. What is buildable and felt by a player is smaller and
 * still real:
 *
 *   1. JOINTS AT LOAD. Two boxes whose volumes intersect are joined, not just interpenetrating:
 *      a piece embedded in a stronger master (a wall sunk into a concrete foundation) is ANCHORED,
 *      two crossing pieces of equal class are PEGGED. Whether a pair can be joined at all is PARENA's
 *      call (struct_joint_strength) -- glass meets nothing well, so a pane leaning on a wall is
 *      resting, not anchored, which is exactly how it should fall.
 *
 *   2. A SUPPORT FLOOD. Grounded nodes spread support along joints. A piece that loses its last path
 *      to the ground is disconnected -- the same question bf_detect_detached asks of cells, asked one
 *      level up, of pieces.
 *
 *   3. A LOAD CASCADE. Mass (volume x density) accumulates downward and each support is compared with
 *      its own compression capacity (PARENA struct_compression, kg per m^2 of footprint). A support
 *      that fails is removed and the check repeats, so an overloaded pillar drops its roof and the
 *      next pillar is then asked to carry more -- up to three rounds, which is enough for the shapes
 *      a level is made of without becoming a solver.
 *
 * Deliberately NOT modelled, named so no one assumes otherwise: shear flow inside a piece, bending or
 * buckling SHAPE (a pillar holds its column load or the node fails -- it does not bow), fatigue, and
 * per-cell stress inside one box. Those need a solver this engine does not have.
 *
 * Everything here is pure, deterministic integer/float arithmetic on a fixed graph, so the server and
 * every client compute the same answer from the same level.
 *
 * The numbers are PARENA (stdlib/shankpit/structure_rules.prn -> structure_rules.c); this file is the
 * graph and the cascade.
 */

#include <math.h>
#include <string.h>

#include "../world/brick_fracture.h"   /* BF_KIND_* */

/* PARENA-generated (packages/simulation/structure_rules.c). */
int struct_density(int kind);
int struct_tensile(int kind);
int struct_compression(int kind);
int struct_shear(int kind);
int struct_damping(int kind);
int struct_fracture_kind(int kind);
int struct_joint_strength(int kind_a, int kind_b);
int struct_holds_load(int mass_kg, int capacity_kg);

#define STRUCT_MAX_NODES   2048   /* = BF_MAX_PARENTS */
#define STRUCT_MAX_JOINTS  8192
#define STRUCT_MAX_ROUNDS  3
/* kg: the sanity ceiling on both mass and capacity, so no float-to-int conversion here can wrap. It is
 * 2 million tonnes on a single piece -- above anything a level can express -- so it never decides a
 * verdict, it only bounds the arithmetic. */
#define STRUCT_MAX_LOAD_KG 2000000000

#define STRUCT_JOINT_ANCHOR 1   /* embedded in a stronger master node */
#define STRUCT_JOINT_PEG    2   /* two crossing pieces, mechanically pegged */

/* Below this permille a pair simply does not join: that is how glass ends up "resting on" a wall
 * rather than being held by it. */
#define STRUCT_JOINT_MIN_STRENGTH 300

typedef struct {
    int active;
    int kind;                 /* BF_KIND_* */
    float x, y, z, w, h, d;   /* centre + FULL extents, physics.h Box convention */
    float density;            /* kg/m^3 (the NOCK material's own value, or PARENA's default) */
    float mass;               /* kg, derived from the volume */
    /* 0..1: the share of the piece's original cross-section that is still there. A wall shot down to a
     * stub holds a fraction of what the whole wall held -- without this, a pier missing 90% of its
     * material carried exactly as much as an intact one until its very last cell went, which is the
     * single most obvious way a "realistic" collapse can look wrong. Linear in what is left, which is
     * a defensible approximation for a column and NOT a stress solver: a hollowed-out tube keeps more
     * of its capacity than this says. */
    float strength;
    int grounded;             /* rests on the world floor */
    int parent;               /* the host's own id (e.g. a BfParent index), -1 if none */
} StructNode;

typedef struct {
    unsigned short a, b;
    unsigned char kind;       /* STRUCT_JOINT_* */
    unsigned short strength;  /* permille, from PARENA */
} StructJoint;

typedef struct {
    StructNode node[STRUCT_MAX_NODES];
    int node_count;
    StructJoint joint[STRUCT_MAX_JOINTS];
    int joint_count;
    unsigned char support[STRUCT_MAX_NODES];  /* 1 = has a path to the ground */
    int load_kg[STRUCT_MAX_NODES];            /* mass it is carrying (own + above) */
    unsigned char failed[STRUCT_MAX_NODES];   /* 1 = its support capacity was exceeded */
    int failed_load_kg[STRUCT_MAX_NODES];     /* the mass it was carrying when it failed (0 if it held) */
    /* Host bookkeeping, not physics: 1 = this piece's failure has already been acted on (dropped, or
     * was already hanging when the level loaded). Keeps a host from re-dropping the same wall every
     * frame, and keeps authored floating decoration from collapsing the moment the level loads. */
    unsigned char dropped[STRUCT_MAX_NODES];
} StructureGraph;

static inline void structure_reset(StructureGraph *g) {
    if (!g) return;
    memset(g, 0, sizeof *g);
}

/* Adds a piece. `density <= 0` means "use PARENA's default for this kind". */
static inline int structure_add_node(StructureGraph *g, int kind,
                                     float x, float y, float z, float w, float h, float d,
                                     float density, int parent) {
    if (!g || g->node_count >= STRUCT_MAX_NODES) return -1;
    if (!(w > 0.0f) || !(h > 0.0f) || !(d > 0.0f)) return -1;
    StructNode *n = &g->node[g->node_count];
    memset(n, 0, sizeof *n);
    n->active = 1;
    n->kind = kind;
    n->x = x; n->y = y; n->z = z;
    n->w = w; n->h = h; n->d = d;
    n->density = (density > 0.0f) ? density : (float)struct_density(kind);
    n->mass = w * h * d * n->density;
    n->strength = 1.0f;
    n->parent = parent;
    return g->node_count++;
}

/* Marks every piece whose underside sits on (or below) `ground_y` as grounded. */
static inline int structure_ground_resting(StructureGraph *g, float ground_y, float eps) {
    if (!g) return 0;
    int n = 0;
    for (int i = 0; i < g->node_count; i++) {
        StructNode *s = &g->node[i];
        if (!s->active) continue;
        if (s->y - s->h * 0.5f <= ground_y + eps) { s->grounded = 1; n++; }
    }
    return n;
}

/* Overlapping volumes on all three axes. `tol` is a WELDING DISTANCE, not a fudge: authored geometry
 * is frequently flush (a roof sitting exactly on a pillar's top face, two walls meeting at a corner)
 * and shares a face with zero overlap. Those are joints. Anything with a real gap is not. */
static inline int structure_boxes_meet(const StructNode *a, const StructNode *b, float tol) {
    if (!a || !b) return 0;
    float ax0 = a->x - a->w * 0.5f, ax1 = a->x + a->w * 0.5f;
    float bx0 = b->x - b->w * 0.5f, bx1 = b->x + b->w * 0.5f;
    float ay0 = a->y - a->h * 0.5f, ay1 = a->y + a->h * 0.5f;
    float by0 = b->y - b->h * 0.5f, by1 = b->y + b->h * 0.5f;
    float az0 = a->z - a->d * 0.5f, az1 = a->z + a->d * 0.5f;
    float bz0 = b->z - b->d * 0.5f, bz1 = b->z + b->d * 0.5f;
    float ox = (ax1 < bx1 ? ax1 : bx1) - (ax0 > bx0 ? ax0 : bx0);
    float oy = (ay1 < by1 ? ay1 : by1) - (ay0 > by0 ? ay0 : by0);
    float oz = (az1 < bz1 ? az1 : bz1) - (az0 > bz0 ? az0 : bz0);
    return ox >= -tol && oy >= -tol && oz >= -tol;
}

static inline int structure_add_joint(StructureGraph *g, int a, int b, unsigned char kind, unsigned short strength) {
    if (!g || g->joint_count >= STRUCT_MAX_JOINTS || a == b) return 0;
    StructJoint *j = &g->joint[g->joint_count++];
    j->a = (unsigned short)a;
    j->b = (unsigned short)b;
    j->kind = kind;
    j->strength = strength;
    return 1;
}

/* Stage 1: one pass over every pair at load (n <= 2048, so this is cheap and runs when a level loads
 * and after a breach, never per tick). Concrete is the master class that anchors what is sunk into
 * it; everything else is pegged. A pair PARENA rates too weakly to join is simply not joined. */
static inline int structure_build_joints(StructureGraph *g, float tol) {
    if (!g) return 0;
    g->joint_count = 0;
    for (int i = 0; i < g->node_count; i++) {
        if (!g->node[i].active) continue;
        for (int k = i + 1; k < g->node_count; k++) {
            if (!g->node[k].active) continue;
            if (!structure_boxes_meet(&g->node[i], &g->node[k], tol)) continue;
            int ca = g->node[i].kind, cb = g->node[k].kind;
            int strength = struct_joint_strength(ca, cb);
            if (strength < STRUCT_JOINT_MIN_STRENGTH) continue;
            unsigned char kind = (ca == BF_KIND_CONCRETE || cb == BF_KIND_CONCRETE)
                                 ? STRUCT_JOINT_ANCHOR : STRUCT_JOINT_PEG;
            if (strength > 1000) strength = 1000;
            structure_add_joint(g, i, k, kind, (unsigned short)strength);
        }
    }
    return g->joint_count;
}

static inline int structure_supported_neighbour(const StructureGraph *g, int i, int j) {
    return g->node[j].grounded || (g->node[j].y < g->node[i].y - 1e-4f && g->support[j]);
}

/* Support flood: from every grounded piece outward along joints. Deterministic (fixed scan order),
 * and idempotent, so re-running it after a change is the whole "is this still standing?" question.
 * A piece that has failed cannot pass support on, which is what makes a cascade cascade.
 *
 * Note there is NO height condition here: a joint holds in both directions, so a roof resting on a
 * pillar is supported by it just as the pillar is anchored by the foundation. Direction only matters
 * for the LOAD cascade below, which follows gravity. */
static inline void structure_spread_support(StructureGraph *g) {
    if (!g) return;
    for (int i = 0; i < g->node_count; i++)
        g->support[i] = (g->node[i].grounded && !g->failed[i]) ? 1 : 0;
    for (int pass = 0; pass < g->node_count + 1; pass++) {
        int changed = 0;
        for (int jn = 0; jn < g->joint_count; jn++) {
            int a = g->joint[jn].a, b = g->joint[jn].b;
            if (!g->node[a].active || !g->node[b].active) continue;
            if (g->support[a] && !g->support[b] && !g->failed[b]) {
                g->support[b] = 1; changed = 1;
            } else if (g->support[b] && !g->support[a] && !g->failed[a]) {
                g->support[a] = 1; changed = 1;
            }
        }
        if (!changed) break;
    }
}

static inline void structure_flood_support(StructureGraph *g) {
    if (!g) return;
    memset(g->failed, 0, sizeof g->failed);
    structure_spread_support(g);
}

/* Stage 2: the load cascade. Top-down, each supported piece hands its own mass plus everything it
 * carries to the supported pieces below it, equally divided; each of those is then checked against
 * its own compression capacity. A failure removes that piece (and its subtree loses support), and the
 * round repeats, which is what turns one broken pillar into a collapse instead of a floating roof. */
static inline void structure_evaluate(StructureGraph *g) {
    if (!g) return;
    memset(g->failed, 0, sizeof g->failed);
    memset(g->failed_load_kg, 0, sizeof g->failed_load_kg);
    memset(g->load_kg, 0, sizeof g->load_kg);
    structure_flood_support(g);

    int order[STRUCT_MAX_NODES];
    for (int round = 0; round < STRUCT_MAX_ROUNDS; round++) {
        int n = 0;
        for (int i = 0; i < g->node_count; i++) {
            if (g->node[i].active && g->support[i] && !g->failed[i]) order[n++] = i;
        }
        /* highest first: a piece can only pass load downward once it knows its own */
        for (int a = 1; a < n; a++) {
            int v = order[a], b = a - 1;
            while (b >= 0 && g->node[order[b]].y < g->node[v].y) { order[b + 1] = order[b]; b--; }
            order[b + 1] = v;
        }
        for (int i = 0; i < g->node_count; i++) g->load_kg[i] = 0;

        int any_failure = 0;
        for (int oi = 0; oi < n; oi++) {
            int i = order[oi];
            int carried = g->load_kg[i] + (int)(g->node[i].mass + 0.5f);
            /* Sanity ceiling only: see STRUCT_MAX_LOAD_KG -- this bounds the conversion, it does not
             * decide anyone's verdict (PARENA divides the capacity before applying its 15%). */
            if (carried > STRUCT_MAX_LOAD_KG) carried = STRUCT_MAX_LOAD_KG;
            g->load_kg[i] = carried;

            int supporters = 0;
            for (int jn = 0; jn < g->joint_count; jn++) {
                int other = -1;
                if (g->joint[jn].a == i) other = g->joint[jn].b;
                else if (g->joint[jn].b == i) other = g->joint[jn].a;
                if (other < 0 || !g->node[other].active || g->failed[other]) continue;
                if (structure_supported_neighbour(g, i, other)) supporters++;
            }

            /* capacity: compression (kg per m^2) x footprint. The footprint is w x d, which is the
             * horizontal cross-section for a wall or a pillar -- the common shapes here. */
            double area_m2 = (double)g->node[i].w * (double)g->node[i].d;
            double strength = (double)g->node[i].strength;
            if (!(strength > 0.0)) strength = 0.0;               /* NaN-safe: a nonsense share holds nothing */
            if (strength > 1.0) strength = 1.0;
            double cap = (double)struct_compression(g->node[i].kind) * area_m2 * strength;
            if (cap > (double)STRUCT_MAX_LOAD_KG) cap = (double)STRUCT_MAX_LOAD_KG;
            int capacity_kg = (int)cap;

            if (supporters > 0) {
                int share = carried / supporters;
                for (int jn = 0; jn < g->joint_count; jn++) {
                    int other = -1;
                    if (g->joint[jn].a == i) other = g->joint[jn].b;
                    else if (g->joint[jn].b == i) other = g->joint[jn].a;
                    if (other < 0 || !g->node[other].active || g->failed[other]) continue;
                    if (!structure_supported_neighbour(g, i, other)) continue;
                    g->load_kg[other] += share;
                }
            }

            if (!struct_holds_load(carried, capacity_kg)) {
                g->failed[i] = 1;
                g->failed_load_kg[i] = carried;
                any_failure = 1;
            }
        }

        if (!any_failure) break;
        /* A failed support holds nothing: re-spread from the ground without it, and let the next round
         * redistribute what it used to carry over whatever is still standing. */
        structure_spread_support(g);
    }
}

static inline int structure_unsupported_count(const StructureGraph *g) {
    if (!g) return 0;
    int n = 0;
    for (int i = 0; i < g->node_count; i++)
        if (g->node[i].active && !g->support[i]) n++;
    return n;
}

static inline int structure_failed_count(const StructureGraph *g) {
    if (!g) return 0;
    int n = 0;
    for (int i = 0; i < g->node_count; i++)
        if (g->node[i].active && g->failed[i]) n++;
    return n;
}

/* Removes a piece from the structure (a wall shot away, a pillar demolished). The graph is NOT
 * re-evaluated here: call structure_evaluate afterwards, which is what makes "this piece is gone,
 * what is now falling?" one cheap, explicit step. */
static inline int structure_remove_node(StructureGraph *g, int index) {
    if (!g || index < 0 || index >= g->node_count) return 0;
    if (!g->node[index].active) return 0;
    g->node[index].active = 0;
    g->node[index].mass = 0.0f;
    g->support[index] = 0;
    g->load_kg[index] = 0;
    return 1;
}

#endif
