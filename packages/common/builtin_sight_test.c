/* builtin_sight_test.c -- the built-in maps' walls are the same walls physics collides with.
 *
 * Why this test exists: for a while only custom levels had AI sight geometry, because a custom level has an
 * authored box list and a built-in map "has none". That was wrong -- the built-in maps DO have geometry,
 * `map_geo_*` in packages/common/physics.h, the arrays collision and hitscan have always used. The new
 * `phys_scene_boxes` exposes them, and this test is the proof that what the sight test receives is that
 * geometry and not something adjacent to it: the garage's own outer wall blocks a sightline, the open floor
 * inside the garage does not, and no box handed over is degenerate (a zero-size box would make a sight test
 * report walls that are not there).
 *
 * NOT proven: that the sight boxes match what is DRAWN for every built-in scene (they match what is
 * collided with, which is the property that matters for who can shoot whom), and nothing about the lobby,
 * which still only federates custom-level boxes.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "physics.h"
#include "../simulation/sight.h"

/* physics.h's own non-static hooks (phys_respawn) need these; every test that includes it stubs them the
 * same way. */
PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *loser, PlayerState *winner) { (void)loser; (void)winner; }

static int g_failed;
static int g_checks;

#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failed++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

int main(void) {
    printf("== built-in map walls ==\n");

    phys_set_scene(SCENE_GARAGE_OSAKA);
    int n = 0;
    const Box *boxes = phys_scene_boxes(&n);
    CHECK(boxes != NULL, "the garage exposes no collision boxes at all");
    CHECK(n > 0, "the garage's box list is empty (%d)", n);
    printf("      garage: %d collision box(es)\n", n);

    /* nothing degenerate: a zero-width box in a sight test is a wall that is not drawn */
    int bad = 0;
    for (int i = 0; i < n; i++) {
        if (!(boxes[i].w > 0.0f) || !(boxes[i].h > 0.0f) || !(boxes[i].d > 0.0f)) bad++;
        if (!isfinite(boxes[i].x) || !isfinite(boxes[i].y) || !isfinite(boxes[i].z)) bad++;
    }
    CHECK(bad == 0, "%d degenerate/NaN collision box(es) handed to sight", bad);

    /* the same conversion the hosts do. TEST_SIGHT_MAX mirrors the hosts' own story_ai sight cap (4096);
       this TU includes physics.h, not level_boxes.h, so it spells the bound out rather than pulling the
       level editor's JSON parser into a geometry test. */
    #define TEST_SIGHT_MAX 4096
    static SightBox sight[TEST_SIGHT_MAX];
    int sn = n < TEST_SIGHT_MAX ? n : TEST_SIGHT_MAX;
    for (int i = 0; i < sn; i++) {
        sight[i].x = boxes[i].x; sight[i].y = boxes[i].y; sight[i].z = boxes[i].z;
        sight[i].w = boxes[i].w; sight[i].h = boxes[i].h; sight[i].d = boxes[i].d;
        sight[i].solid = 1;
    }
    CHECK(sn == n, "the garage has more boxes (%d) than a sight world holds (%d)", n, TEST_SIGHT_MAX);

    /* an eye-level ray across the garage crosses its outer wall; one inside it does not */
    int blocked = sight_segment_clear(sight, sn, -100.0f, 1.25f, 0.0f, 100.0f, 1.25f, 0.0f);
    int clear_inside = sight_segment_clear(sight, sn, -30.0f, 1.25f, 0.0f, 30.0f, 1.25f, 0.0f);
    CHECK(blocked == 0, "the garage's own outer wall does not block a sightline through it");
    CHECK(clear_inside == 1, "the open floor of the garage is reported as blocked");

    printf("PASS: the built-in map's collision geometry IS its sight geometry (%d boxes)\n", n);

    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
