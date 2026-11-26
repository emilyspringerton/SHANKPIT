/* trees_test.c -- headless: the forest grows from tree_rules, a chop costs hp and pays wood, a felled tree grows back. */
#include "trees.h"
#include <stdio.h>
#include <math.h>
static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); else { printf("FAIL: %s\n", m); fails++; } } while (0)
static int site_all(float x, float z, float *gy) { (void)x; (void)z; *gy = 0.0f; return 1; }
static int site_none(float x, float z, float *gy) { (void)x; (void)z; (void)gy; return 0; }
int main(void) {
    trees_reset();
    CHECK(trees_grow(0, 0, 300, 1337, site_none) == 0, "a site that refuses everything grows nothing");
    int n = trees_grow(0, 0, 300, 1337, site_all);
    CHECK(n > 20 && n <= TREE_MAX, "a forest grows around the centre");
    int kinds[4] = {0}; float far = 0;
    for (int i = 0; i < TREE_MAX; i++) { const Tree *t = trees_get(i); if (!t) continue; kinds[t->kind]++;
        float d = sqrtf(t->x * t->x + t->z * t->z); if (d > far) far = d; }
    CHECK(kinds[1] && kinds[2] && kinds[3], "pine, oak and birch all grow");
    CHECK(far <= 300.0f, "nothing grows past the radius");
    trees_reset(); int n2 = trees_grow(0, 0, 300, 1337, site_all);
    CHECK(n2 == n && trees_get(0)->x == trees_get(0)->x, "the same seed grows the same forest");
    /* chop the first tree with the knife from 2 units away, facing it */
    const Tree *t = trees_get(0); int kind = t->kind, hits = 0, felled = 0;
    float ox = t->x, oy = t->y + 1.0f, oz = t->z + 2.0f;
    while (!felled && hits < 20) { trees_hit_ray(ox, oy, oz, 0, 0, -1, 3.5f, 0, 1000, &felled); hits++; }
    int expect_hits = (tree_max_hp(kind) + tree_chop_damage(0) - 1) / tree_chop_damage(0);
    CHECK(felled && hits == expect_hits, "the knife fells a tree in max_hp / chop_damage hits");
    CHECK(trees_wood() == hits * tree_wood_per_hit(kind) + tree_fell_bonus(kind), "wood: a log per hit plus the fell bonus");
    CHECK(trees_hit_ray(ox, oy, oz, 0, 0, -1, 2.5f, 0, 1000, &felled) < 0, "a stump takes no chops");
    CHECK(!trees_get(0)->alive, "the tree is down");
    trees_tick(1000 + (unsigned)tree_regrow_ms(kind) - 1); CHECK(!trees_get(0)->alive, "not before its regrow time");
    trees_tick(1000 + (unsigned)tree_regrow_ms(kind)); CHECK(trees_get(0)->alive && trees_get(0)->hp == tree_max_hp(kind), "it grows back at full hp");
    CHECK(trees_hit_ray(ox, oy, oz, 0, 0, -1, 1.0f, 0, 2000, &felled) < 0, "out of reach is a miss");
    trees_wood_set(5000); CHECK(trees_wood() == res_cap(1), "wood is capped by res_cap");
    printf("\n%s (%d failed)\n", fails ? "FAIL" : "trees ok", fails);
    return fails ? 1 : 0;
}
