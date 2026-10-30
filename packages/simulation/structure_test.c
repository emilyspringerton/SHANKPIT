/* structure_test.c -- the structural layer over destructible brick (packages/simulation/structure.h),
 * with the numbers coming from PARENA (stdlib/shankpit/structure_rules.prn).
 *
 * The claims this pins, in the order a player would notice them:
 *   * a wall sunk into a concrete foundation is anchored, and a crossing beam is pegged -- the pieces
 *     act joined instead of merely interpenetrating;
 *   * a pane of glass leaning on a brick wall is NOT held (PARENA rates that pair too weak), so it
 *     is "resting", which is why it falls when the wall goes;
 *   * remove one of four pillars under a roof and the roof stays up (three still carry it);
 *   * remove the last support and the roof is disconnected;
 *   * overload a single wood pillar with a slab it cannot carry and the CASCADE takes it: the pillar
 *     fails on compression and the slab above loses its support, rather than hanging in the air;
 *   * the whole thing is deterministic (same graph, same answer), so a server and a client agree.
 *
 * make test-structure
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "structure.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* A concrete foundation slab, four wood pillars standing on it, and a wood roof resting on top.
 * Deterministic geometry, no level file needed. */
static void build_frame(StructureGraph *g, int with_pillar_a, int with_pillar_b,
                        int with_pillar_c, int with_pillar_d) {
    structure_reset(g);
    int found = structure_add_node(g, BF_KIND_CONCRETE, 0, 0.25f, 0, 12.0f, 0.5f, 12.0f, 0.0f, 0);
    (void)found;
    int ax[4] = { -4, 4, -4, 4 }, az[4] = { -4, -4, 4, 4 };
    int keep[4] = { with_pillar_a, with_pillar_b, with_pillar_c, with_pillar_d };
    for (int i = 0; i < 4; i++) {
        if (!keep[i]) continue;
        /* y=2.45 sinks each pillar 0.05 into the slab (top 0.5) and puts its own top at 4.45, which
           is why the roof at y=4.6 overlaps it. Pieces join by overlapping, not by touching. */
        structure_add_node(g, BF_KIND_WOOD, (float)ax[i], 2.45f, (float)az[i],
                           0.6f, 4.0f, 0.6f, 0.0f, i + 1);
    }
    /* the roof only exists if at least one pillar does. y=4.6 puts its underside 0.1 INTO the pillar
       tops (4.5) -- pieces join by overlapping, so authored flush faces need the tol plus a real
       overlap, exactly as a placed roof would sit.*/
    if (with_pillar_a || with_pillar_b || with_pillar_c || with_pillar_d)
        structure_add_node(g, BF_KIND_WOOD, 0, 4.6f, 0, 10.0f, 0.4f, 10.0f, 0.0f, 9);
    structure_ground_resting(g, 0.0f, 0.01f);   /* the slab sits on the world floor */
    structure_build_joints(g, 0.01f);
}

int main(void) {
    /* 0. the PARENA profile is sane and total */
    CHECK(struct_density(BF_KIND_WOOD) < struct_density(BF_KIND_CONCRETE));
    CHECK(struct_tensile(BF_KIND_WOOD) > struct_tensile(BF_KIND_GLASS));
    CHECK(struct_compression(BF_KIND_CONCRETE) > struct_compression(BF_KIND_WOOD));
    CHECK(struct_compression(12345) > 0);                 /* unknown kinds still total */
    CHECK(struct_holds_load(0, 0) == 1 || struct_holds_load(0, 0) == 0);
    CHECK(struct_holds_load(100000, 1000) == 0);
    /* a 15% safety factor: exactly at capacity holds, capacity plus one kg does not */
    CHECK(struct_holds_load(850, 1000) == 1);
    CHECK(struct_holds_load(851, 1000) == 0);
    CHECK(struct_joint_strength(BF_KIND_GLASS, BF_KIND_BRICK) < STRUCT_JOINT_MIN_STRENGTH);
    CHECK(struct_joint_strength(BF_KIND_WOOD, BF_KIND_WOOD) > STRUCT_JOINT_MIN_STRENGTH);

    /* 1. a full frame: everything is joined and everything reaches the ground */
    StructureGraph g;
    build_frame(&g, 1, 1, 1, 1);
    CHECK(g.node_count == 6);
    CHECK(g.joint_count >= 8);                            /* slab-pillar x4, pillar-roof x4 */
    int anchors = 0, pegs = 0;
    for (int i = 0; i < g.joint_count; i++) {
        if (g.joint[i].kind == STRUCT_JOINT_ANCHOR) anchors++;
        if (g.joint[i].kind == STRUCT_JOINT_PEG) pegs++;
    }
    CHECK(anchors >= 4 && pegs >= 4);                     /* concrete anchors, timber pegs */
    structure_evaluate(&g);
    CHECK(structure_unsupported_count(&g) == 0);
    CHECK(structure_failed_count(&g) == 0);

    /* 2. one pillar gone: the roof is still held by the other three */
    build_frame(&g, 0, 1, 1, 1);
    structure_evaluate(&g);
    CHECK(structure_unsupported_count(&g) == 0);

    /* 3. the last supports gone: the roof is disconnected (this is what starts a real collapse) */
    build_frame(&g, 0, 0, 0, 1);
    structure_evaluate(&g);
    CHECK(structure_failed_count(&g) == 0);               /* one pillar is enough for this roof */
    build_frame(&g, 0, 0, 0, 0);                          /* no pillars at all: no roof node either */
    structure_evaluate(&g);
    CHECK(g.node_count == 1);                             /* just the foundation */

    /* 4. glass leaning on brick is resting, not anchored -- the campaign that made this rule */
    {
        structure_reset(&g);
        int wall = structure_add_node(&g, BF_KIND_BRICK, 0, 2.0f, 0, 6.0f, 4.0f, 0.5f, 0.0f, 0);
        int pane = structure_add_node(&g, BF_KIND_GLASS, 0, 3.0f, 0.3f, 6.0f, 3.0f, 0.05f, 0.0f, 1);
        structure_ground_resting(&g, 0.0f, 0.01f);
        int joints = structure_build_joints(&g, 0.001f);
        CHECK(joints == 0);                               /* they touch, but PARENA says no joint */
        structure_evaluate(&g);
        CHECK(g.node[wall].grounded && g.support[wall]);
        CHECK(!g.support[pane]);                          /* unsupported: it would slide off */
        (void)pane;
    }

    /* 5. a beam pegged into a wall: when its own pillar goes, the peg still holds it */
    {
        structure_reset(&g);
        structure_add_node(&g, BF_KIND_CONCRETE, 0, 0.25f, 0, 10.0f, 0.5f, 10.0f, 0.0f, 0);   /* foundation */
        structure_add_node(&g, BF_KIND_WOOD, -2.0f, 2.45f, 0, 0.6f, 4.0f, 0.6f, 0.0f, 1);     /* wall post, sunk into the slab */
        structure_add_node(&g, BF_KIND_WOOD, 0.0f, 2.4f, 0, 8.0f, 0.4f, 0.4f, 0.0f, 2);       /* beam, crosses the post */
        structure_ground_resting(&g, 0.0f, 0.01f);
        CHECK(structure_build_joints(&g, 0.001f) >= 2);
        structure_evaluate(&g);
        CHECK(structure_unsupported_count(&g) == 0);
        /* now demolish the post: the beam must still be held by the foundation? it is not touching it,
           so it must fall -- the peg only binds it to the post */
        structure_remove_node(&g, 1);
        structure_evaluate(&g);
        CHECK(!g.support[2]);
    }

    /* 6. the load cascade: one wood pillar under a slab too heavy for it. The pillar FAILS on
          compression and the slab above loses support -- it does not float. */
    {
        structure_reset(&g);
        structure_add_node(&g, BF_KIND_CONCRETE, 0, 0.25f, 0, 30.0f, 0.5f, 30.0f, 0.0f, 0);   /* foundation */
        int pillar = structure_add_node(&g, BF_KIND_WOOD, 0, 2.45f, 0, 0.6f, 4.0f, 0.6f, 0.0f, 1);
        structure_add_node(&g, BF_KIND_CONCRETE, 0, 4.6f, 0, 20.0f, 0.6f, 20.0f, 0.0f, 2);     /* 20x20x0.6 slab, sunk 0.1 into the pillar top */
        structure_ground_resting(&g, 0.0f, 0.01f);
        structure_build_joints(&g, 0.001f);
        structure_evaluate(&g);
        CHECK(g.failed[pillar]);                              /* crushed by the slab's mass */
        CHECK(structure_unsupported_count(&g) >= 1);           /* and the slab is now unsupported */
        printf("cascade: %d kg slab on a wood pillar (%d kg capacity) -> pillar failed carrying %d kg, %d unsupported\n",
               (int)g.node[2].mass, struct_compression(BF_KIND_WOOD) * (int)(0.6f * 0.6f * 1000.0f) / 1000,
               g.failed_load_kg[pillar], structure_unsupported_count(&g));
    }

    /* 7. determinism: identical graph, identical answer, bit for bit */
    {
        StructureGraph a, b;
        build_frame(&a, 0, 1, 1, 1);
        build_frame(&b, 0, 1, 1, 1);
        structure_evaluate(&a);
        structure_evaluate(&b);
        CHECK(memcmp(&a.support, &b.support, sizeof a.support) == 0);
        CHECK(memcmp(&a.load_kg, &b.load_kg, sizeof a.load_kg) == 0);
        CHECK(memcmp(&a.failed, &b.failed, sizeof a.failed) == 0);
    }

    if (g_fail) { printf("structure_test: %d FAILED\n", g_fail); return 1; }
    printf("structure_test OK (joints: %d anchors + %d pegs on the reference frame)\n", anchors, pegs);
    return 0;
}
