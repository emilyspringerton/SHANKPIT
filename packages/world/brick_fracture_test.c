/* brick_fracture_test.c -- headless verification of packages/world/brick_fracture.h (SHANKPIT
 * destructible brick). Strict flags, ASan+UBSan clean. Every number in the scripted cases is
 * hand-derived from the .prn rules, not copied from output; the fuzz case checks the geometric
 * invariants that must hold for ANY damage history.
 *
 *   gcc -std=c99 -Wall -Wextra -pedantic -Werror -fsanitize=address,undefined -Ipackages/world \
 *       -Ipackages/simulation packages/world/brick_fracture_test.c packages/simulation/brick_mod.c -lm
 */
#include "brick_fracture.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* A 12 x 6 x 3 wall centred at (0,3,0): 4 x 2 x 1 cells of 3 units. */
static void make_wall(BrickFracture *bf) {
    bf_reset(bf);
    CHECK(bf_add_parent(bf, 0, 0.0f, 3.0f, 0.0f, 12.0f, 6.0f, 3.0f) == 1);
}

static float piece_volume_sum(const BrickFracture *bf, int parent) {
    float v = 0.0f;
    for (int i = 0; i < bf->piece_count; i++)
        if (bf->piece[i].parent == parent) v += bf->piece[i].w * bf->piece[i].h * bf->piece[i].d;
    return v;
}

static int boxes_overlap(const BfPiece *a, const BfPiece *b) {
    const float e = 1e-3f;
    return fabsf(a->x - b->x) < (a->w + b->w) * 0.5f - e &&
           fabsf(a->y - b->y) < (a->h + b->h) * 0.5f - e &&
           fabsf(a->z - b->z) < (a->d + b->d) * 0.5f - e;
}

static int point_in_piece(const BfPiece *p, float x, float y, float z) {
    return fabsf(x - p->x) < p->w * 0.5f && fabsf(y - p->y) < p->h * 0.5f && fabsf(z - p->z) < p->d * 0.5f;
}

/* Invariants that must hold for any damage history of parent `pi`. */
static void check_invariants(const BrickFracture *bf, int pi) {
    const BfParent *p = &bf->parent[pi];
    float cell_vol = p->cell[0] * p->cell[1] * p->cell[2];
    float parent_vol = p->dim[0] * p->dim[1] * p->dim[2];
    if (p->removed <= 0) { CHECK(piece_volume_sum(bf, pi) == 0.0f); return; }
    float got = piece_volume_sum(bf, pi) + (float)p->removed * cell_vol;
    CHECK(fabsf(got - parent_vol) <= parent_vol * 1e-3f);
    for (int i = 0; i < bf->piece_count; i++) {
        if (bf->piece[i].parent != pi) continue;
        for (int j = i + 1; j < bf->piece_count; j++)
            if (bf->piece[j].parent == pi) CHECK(!boxes_overlap(&bf->piece[i], &bf->piece[j]));
    }
    for (int r = p->first_rec; r >= 0; r = bf->rec[r].next) {
        if (bf->rec[r].hp != 0) continue;
        int ix, iy, iz; bf_unkey(bf->rec[r].key, &ix, &iy, &iz);
        float c[3]; bf_cell_center(p, ix, iy, iz, c);
        for (int i = 0; i < bf->piece_count; i++)
            if (bf->piece[i].parent == pi) CHECK(!point_in_piece(&bf->piece[i], c[0], c[1], c[2]));
    }
}

static unsigned int g_rng = 12345u;
static unsigned int rnd(void) { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
static float rndf(void) { return (float)(rnd() % 100000u) / 100000.0f; }

int main(void) {
    BrickFracture *bf = (BrickFracture *)malloc(sizeof(BrickFracture));
    BrickFracture *bf2 = (BrickFracture *)malloc(sizeof(BrickFracture));
    BfCheckpoint *cp = (BfCheckpoint *)malloc(sizeof(BfCheckpoint));
    if (!bf || !bf2 || !cp) return 2;

    /* 1. grid derivation */
    make_wall(bf);
    CHECK(bf->parent[0].n[0] == 4 && bf->parent[0].n[1] == 2 && bf->parent[0].n[2] == 1);
    CHECK(fabsf(bf->parent[0].cell[0] - 3.0f) < 1e-6f && fabsf(bf->parent[0].lo[0] + 6.0f) < 1e-6f);
    CHECK(bf->max_hp == 80 && bf->material == 2);
    CHECK(bf_add_parent(bf, 1, 0, 0, 0, 0.0f, 5.0f, 5.0f) == 0);        /* degenerate refused */
    CHECK(bf_add_parent(bf, 9999, 0, 0, 0, 5.0f, 5.0f, 5.0f) == 0);     /* out-of-range index refused */

    /* 2. one cell, AR (20 dmg): 10 per hit after 50% resistance. Cell (1,0,0) centre = (-1.5,1.5,0).
          hp: 80,70,60,50,40(<48 CRACKED, debris 1),30,20,10(<20 TORN, debris 2),0 (GONE, debris 4) */
    {
        unsigned int key = bf_key(1, 0, 0);
        int expect_hp[8] = { 70, 60, 50, 40, 30, 20, 10, 0 };
        for (int s = 0; s < 8; s++) {
            int gone = bf_damage_sphere(bf, -1.5f, 1.5f, 0.0f, 0.1f, 2, 20, 0, key);
            CHECK(bf_cell_hp(bf, 0, key) == expect_hp[s]);
            CHECK(gone == (s == 7 ? 1 : 0));
        }
        CHECK(bf->ev_count == 3);
        CHECK(bf->ev[0].state == BF_STATE_CRACKED && bf->ev[0].debris == 1);
        CHECK(bf->ev[1].state == BF_STATE_TORN && bf->ev[1].debris == 2);
        CHECK(bf->ev[2].state == BF_STATE_GONE && bf->ev[2].debris == 4);
        CHECK(fabsf(bf->ev[2].x + 1.5f) < 1e-5f && fabsf(bf->ev[2].y - 1.5f) < 1e-5f);
        CHECK(bf->pieces_dirty == 1 && bf->parent[0].removed == 1);
        CHECK(bf_cell_hp(bf, 0, bf_key(0, 0, 0)) == 80);                  /* neighbours untouched */
        CHECK(bf->net_count == 1 && bf->net[0].hp == 0);                   /* one entry per cell, latest hp */
        /* a gone cell cannot be hurt again */
        CHECK(bf_damage_sphere(bf, -1.5f, 1.5f, 0.0f, 0.1f, 2, 20, 0, key) == 0);
    }

    /* 3. decomposition of the 4x2x1 grid minus cell (1,0,0): hand-traced bisection gives exactly
          3 pieces: [0,1)x[0,2), [1,2)x[1,2), [2,4)x[0,2). Volumes: 216 - 27 = 189. */
    CHECK(bf_rebuild(bf) == 1);
    CHECK(bf->piece_count == 3);
    CHECK(fabsf(piece_volume_sum(bf, 0) - 189.0f) < 1e-2f);
    check_invariants(bf, 0);
    CHECK(bf->pieces_dirty == 0);
    /* piece 0 is cell column 0 (x -6..-3), full height, centre (-4.5,3,0) */
    CHECK(fabsf(bf->piece[0].x + 4.5f) < 1e-5f && fabsf(bf->piece[0].h - 6.0f) < 1e-5f);
    /* uv offset = piece centre - parent centre (0,3,0) */
    CHECK(fabsf(bf->piece[0].uvo[0] + 4.5f) < 1e-5f && fabsf(bf->piece[0].uvo[1]) < 1e-5f);

    /* 4. net round trip: a second instance given only the (parent,key,hp) entries reaches the
          identical piece set, bit for bit */
    make_wall(bf2);
    for (int i = 0; i < bf->net_count; i++) CHECK(bf_apply_net(bf2, bf->net[i].parent, bf->net[i].key, bf->net[i].hp) == 1);
    CHECK(bf2->parent[0].removed == 1);
    CHECK(bf_rebuild(bf2) == 1);
    CHECK(bf2->piece_count == bf->piece_count && memcmp(bf2->piece, bf->piece, sizeof(BfPiece) * (size_t)bf->piece_count) == 0);
    /* idempotent and validated */
    CHECK(bf_apply_net(bf2, 0, bf_key(1, 0, 0), 0) == 0);
    CHECK(bf_apply_net(bf2, 7, 0, 10) == 0);                  /* no such parent */
    CHECK(bf_apply_net(bf2, 0, bf_key(9, 0, 0), 10) == 0);    /* outside the grid */
    CHECK(bf_apply_net(bf2, 0, bf_key(0, 0, 0), 81) == 0);    /* hp above max */
    CHECK(bf_apply_net(bf2, 0, bf_key(0, 0, 0), -1) == 0);
    CHECK(bf_apply_net(bf2, 0, 0xC0000000u, 10) == 0);        /* stray high bits */
    CHECK(bf_apply_net(bf2, 0, bf_key(0, 0, 0), 50) == 1);

    /* 5. missile blast at the front-face centre, radius 4.5: weapon 6 x5 = 650; the four central
          cells are 2.598 away (577 permille) -> 650*423/1000 = 274 -> 137 effective >= 80, GONE;
          the outer columns are 4.97 away, out of range. Result: 2 outer columns survive. */
    make_wall(bf);
    CHECK(bf_damage_sphere(bf, 0.0f, 3.0f, -1.5f, 4.5f, 6, 130, -1, 0) == 4);
    CHECK(bf_rebuild(bf) == 1);
    CHECK(bf->piece_count == 2);
    CHECK(fabsf(piece_volume_sum(bf, 0) - 2.0f * 3.0f * 6.0f * 3.0f) < 1e-2f);
    check_invariants(bf, 0);
    /* blades and a flashlight do nothing */
    make_wall(bf);
    CHECK(bf_damage_sphere(bf, 0.0f, 3.0f, 0.0f, 9.0f, 0, 200, -1, 0) == 0 && bf->rec_count == 0);
    CHECK(bf_damage_sphere(bf, 0.0f, 3.0f, 0.0f, 9.0f, 5, 40, -1, 0) == 0 && bf->rec_count == 0);

    /* 6. surface hit: an AR round into the front face centre */
    make_wall(bf);
    CHECK(bf_hit_surface(bf, 0.0f, 3.0f, -1.5f, 0.0f, 0.0f, -1.0f, 2, 20, 0.9f) == 0);
    {
        unsigned int k = 0;
        int p = bf_find_parent_at(bf, 0.0f, 3.0f, -1.45f, &k);
        CHECK(p == 0 && k == bf_key(2, 1, 0));
        CHECK(bf_cell_hp(bf, 0, k) == 70);                    /* primary: 20 -> 10 */
        CHECK(bf_cell_hp(bf, 0, bf_key(1, 1, 0)) < 80);        /* neighbour inside the radius */
        CHECK(bf_cell_hp(bf, 0, bf_key(0, 0, 0)) == 80);       /* far cell untouched */
    }
    CHECK(bf_hit_surface(bf, 50.0f, 3.0f, -1.5f, 0.0f, 0.0f, -1.0f, 2, 20, 0.9f) == 0);   /* misses everything */

    /* 7. refresh cursor visits every record and wraps */
    {
        BfNetEntry e; int seen = 0;
        int n = bf->rec_count;
        CHECK(n > 0);
        for (int i = 0; i < n * 2 + 1; i++) { CHECK(bf_next_refresh(bf, &e) == 1); seen++; }
        CHECK(seen == n * 2 + 1);
        BrickFracture *empty = (BrickFracture *)malloc(sizeof(BrickFracture)); bf_reset(empty);
        CHECK(bf_next_refresh(empty, &e) == 0);
        free(empty);
    }

    /* 8. clear restores everything */
    bf_clear_damage(bf);
    CHECK(bf->rec_count == 0 && bf->parent[0].removed == 0 && bf_rebuild(bf) == 1 && bf->piece_count == 0);

    /* 9. fuzz: a big building (100^3 cells), thousands of random blasts and bullets with the
          real checkpoint/rollback discipline. Invariants must hold after EVERY accepted step;
          the piece budget must never be exceeded; a rollback must leave the previous state intact. */
    bf_reset(bf);
    CHECK(bf_add_parent(bf, 0, 0.0f, 150.0f, 0.0f, 300.0f, 300.0f, 300.0f) == 1);
    CHECK(bf_add_parent(bf, 1, 400.0f, 10.0f, 0.0f, 20.0f, 20.0f, 4.0f) == 1);
    CHECK(bf->parent[0].n[0] == 100);
    int accepted = 0, rolled = 0;
    for (int step = 0; step < 400; step++) {
        float cx = -150.0f + rndf() * 300.0f, cy = rndf() * 300.0f, cz = -150.0f + rndf() * 300.0f;
        if (step % 5 == 0) { cx = 400.0f - 10.0f + rndf() * 20.0f; cy = rndf() * 20.0f; cz = -2.0f + rndf() * 4.0f; }
        int weapon = (step % 3 == 0) ? 6 : (step % 3 == 1 ? 4 : 1);
        float radius = (weapon == 6) ? 4.5f : 2.7f;
        bf_checkpoint(bf, cp);
        bf_damage_sphere(bf, cx, cy, cz, radius, weapon, 130, -1, 0);
        if (bf_rebuild(bf)) {
            accepted++;
            CHECK(bf->piece_count <= BF_MAX_PIECES);
            if (step % 25 == 0) { check_invariants(bf, 0); check_invariants(bf, 1); }
        } else {
            rolled++;
            bf_restore(bf, cp);
            CHECK(bf_rebuild(bf) == 1);
        }
    }
    check_invariants(bf, 0); check_invariants(bf, 1);
    CHECK(accepted > 0);
    printf("fuzz: %d accepted, %d rolled back, %d records, %d pieces, %d removed\n", accepted, rolled,
           bf->rec_count, bf->piece_count, bf->parent[0].removed + bf->parent[1].removed);

    /* 10. forced overflow rolls back whole: one-cell craters scattered through a huge parent
           until the piece budget cannot hold the next one */
    bf_reset(bf);
    CHECK(bf_add_parent(bf, 0, 0.0f, 150.0f, 0.0f, 300.0f, 300.0f, 300.0f) == 1);
    int overflowed = 0;
    for (int i = 0; i < 2000 && !overflowed; i++) {
        float cx = -148.0f + rndf() * 296.0f, cy = 2.0f + rndf() * 296.0f, cz = -148.0f + rndf() * 296.0f;
        bf_checkpoint(bf, cp);
        unsigned int pk = 0;
        int ppi = bf_find_parent_at(bf, cx, cy, cz, &pk);
        if (ppi < 0) continue;
        bf_damage_sphere(bf, cx, cy, cz, 0.5f, 6, 200, ppi, pk);   /* primary cell: guaranteed gone */
        if (!bf_rebuild(bf)) {
            overflowed = 1;
            int removed_during = bf->parent[0].removed;
            bf_restore(bf, cp);
            CHECK(bf->parent[0].removed < removed_during);
            CHECK(bf_rebuild(bf) == 1);
            check_invariants(bf, 0);
        }
    }
    CHECK(overflowed);

    /* 9. per-parent kinds: concrete / wood / glass walls (same 3x3x3 box each), AR round (20) at
          the cell centre. Hand-derived from brick_rules.prn: wood (mat 1, 20% resist -> 16,
          60 HP) is gone in 4 shots; concrete (mat 2, 50% -> 10, 120 HP) in 12; glass (mat 4,
          0% -> 20 vs 10 HP) in 1. Kinds coexist in one fracture, and bf_apply_net bounds hp by
          the PARENT's own max (glass rejects 11, concrete accepts 120). */
    {
        BrickFracture *k = (BrickFracture *)malloc(sizeof(BrickFracture));
        if (!k) return 2;
        bf_reset(k);
        CHECK(bf_add_parent_kind(k, 0, 0, 1.5f, 0, 3, 3, 3, BF_KIND_WOOD) == 1);
        CHECK(bf_add_parent_kind(k, 1, 10, 1.5f, 0, 3, 3, 3, BF_KIND_CONCRETE) == 1);
        CHECK(bf_add_parent_kind(k, 2, 20, 1.5f, 0, 3, 3, 3, BF_KIND_GLASS) == 1);
        CHECK(k->parent[0].max_hp == 60 && k->parent[0].material == 1);
        CHECK(k->parent[1].max_hp == 120 && k->parent[1].material == 2);
        CHECK(k->parent[2].max_hp == 10 && k->parent[2].material == 4);
        unsigned int key = bf_key(0, 0, 0);
        int want[3] = { 4, 12, 1 };
        for (int pi = 0; pi < 3; pi++) {
            int shots = 0;
            while (bf_cell_hp(k, pi, key) > 0 && shots < 30) {
                bf_damage_sphere(k, 10.0f * (float)pi, 1.5f, 0.0f, 0.1f, 2, 20, pi, key);
                shots++;
            }
            CHECK(shots == want[pi]);
            CHECK(k->parent[pi].removed == 1);
        }
        CHECK(bf_apply_net(k, 2, bf_key(0, 0, 0), 11) == 0);
        CHECK(bf_apply_net(k, 1, bf_key(0, 0, 0), 120) == 1);
        free(k);
    }

    free(bf); free(bf2); free(cp);
    if (g_fail) { printf("brick_fracture_test: %d FAILED\n", g_fail); return 1; }
    printf("brick_fracture_test: all checks passed\n");
    return 0;
}
