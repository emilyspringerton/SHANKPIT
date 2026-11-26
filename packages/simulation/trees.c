/* trees.c -- the tree pool, the chop and the wood count; see trees.h. */
#include "trees.h"
#include <string.h>
#include <math.h>

static Tree g_trees[TREE_MAX];
static int g_wood;

void trees_reset(void) { memset(g_trees, 0, sizeof g_trees); g_wood = 0; }
int trees_count(void) { int n = 0; for (int i = 0; i < TREE_MAX; i++) n += g_trees[i].active; return n; }
const Tree *trees_get(int slot) { return (slot >= 0 && slot < TREE_MAX && g_trees[slot].active) ? &g_trees[slot] : NULL; }
int trees_wood(void) { return g_wood; }
void trees_wood_set(int n) { int cap = res_cap(1); g_wood = n < 0 ? 0 : (n > cap ? cap : n); }

float trees_part(const Tree *t, int part, int axis) { return t ? (float)tree_part(t->kind, part, axis) * 0.001f * t->scale : 0.0f; }
int trees_parts(const Tree *t) { return t ? tree_parts(t->kind) : 0; }
unsigned int trees_part_color(const Tree *t, int part) { return t ? (unsigned int)tree_part_color(t->kind, part) : 0u; }

int trees_grow(float cx, float cz, float radius, int seed, TreeSiteFn site) {
    float cell = (float)tree_cell_milli() * 0.001f;
    int rings = (int)(radius / cell) + 1, n = trees_count();
    for (int ring = 0; ring <= rings && n < TREE_MAX; ring++) {
        for (int gx = -ring; gx <= ring && n < TREE_MAX; gx++) {
            for (int gz = -ring; gz <= ring && n < TREE_MAX; gz++) {
                if ((gx < 0 ? -gx : gx) != ring && (gz < 0 ? -gz : gz) != ring) continue;   /* the ring's edge only */
                /* the grid is anchored to the world, not to the centre, so every client grows the same forest */
                int wx = (int)floorf(cx / cell) + gx, wz = (int)floorf(cz / cell) + gz;
                int kind = tree_at(wx, wz, seed);
                if (kind <= 0) continue;
                float x = (float)wx * cell + (float)tree_jitter_milli(wx, wz, seed, 0) * 0.001f;
                float z = (float)wz * cell + (float)tree_jitter_milli(wx, wz, seed, 1) * 0.001f;
                if ((x - cx) * (x - cx) + (z - cz) * (z - cz) > radius * radius) continue;
                float gy = 0.0f;
                if (site && !site(x, z, &gy)) continue;
                Tree *t = &g_trees[n++];
                memset(t, 0, sizeof *t);
                t->active = 1; t->alive = 1; t->kind = kind; t->hp = tree_max_hp(kind);
                t->x = x; t->y = gy; t->z = z;
                t->scale = (float)tree_scale_permille(wx, wz, seed) * 0.001f;
            }
        }
    }
    return trees_count();
}

/* slab test of a unit ray against an axis-aligned box; the entry distance or a negative number */
static float ray_box(float ox, float oy, float oz, float dx, float dy, float dz,
                     float minx, float miny, float minz, float maxx, float maxy, float maxz, float range) {
    float tn = 0.0f, tf = range;
    const float o[3] = {ox, oy, oz}, d[3] = {dx, dy, dz}, lo[3] = {minx, miny, minz}, hi[3] = {maxx, maxy, maxz};
    for (int a = 0; a < 3; a++) {
        if (fabsf(d[a]) < 1e-6f) { if (o[a] < lo[a] || o[a] > hi[a]) return -1.0f; continue; }
        float t1 = (lo[a] - o[a]) / d[a], t2 = (hi[a] - o[a]) / d[a];
        if (t1 > t2) { float s = t1; t1 = t2; t2 = s; }
        if (t1 > tn) tn = t1;
        if (t2 < tf) tf = t2;
        if (tn > tf) return -1.0f;
    }
    return tn;
}

int trees_hit_ray(float ox, float oy, float oz, float dx, float dy, float dz, float range, int weapon, unsigned int now_ms, int *felled) {
    if (felled) *felled = 0;
    int best = -1; float bt = range;
    for (int i = 0; i < TREE_MAX; i++) {
        const Tree *t = &g_trees[i];
        if (!t->active || !t->alive) continue;
        float cx = t->x + trees_part(t, 0, 0), cy = t->y + trees_part(t, 0, 1), cz = t->z + trees_part(t, 0, 2);
        float hw = trees_part(t, 0, 3) * 0.5f, hh = trees_part(t, 0, 4) * 0.5f, hd = trees_part(t, 0, 5) * 0.5f;
        float d = ray_box(ox, oy, oz, dx, dy, dz, cx - hw, cy - hh, cz - hd, cx + hw, cy + hh, cz + hd, bt);
        if (d >= 0.0f && d < bt) { bt = d; best = i; }
    }
    if (best < 0) return -1;
    Tree *t = &g_trees[best];
    t->hp -= tree_chop_damage(weapon);
    t->hurt_ms = now_ms;
    trees_wood_set(g_wood + tree_wood_per_hit(t->kind));
    if (t->hp <= 0) {
        t->hp = 0; t->alive = 0;
        t->regrow_at_ms = now_ms + (unsigned int)tree_regrow_ms(t->kind);
        trees_wood_set(g_wood + tree_fell_bonus(t->kind));
        if (felled) *felled = 1;
    }
    return best;
}

void trees_tick(unsigned int now_ms) {
    for (int i = 0; i < TREE_MAX; i++) {
        Tree *t = &g_trees[i];
        if (t->active && !t->alive && now_ms >= t->regrow_at_ms) { t->alive = 1; t->hp = tree_max_hp(t->kind); }
    }
}
