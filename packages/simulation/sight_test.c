/* sight_test.c -- the sight query, headless (no GL, no level file, no display).
 *
 * Founder: "also require line of site". What this proves is the part that is easy to get subtly wrong:
 * a wall blocks, the space beside it does not, a low bench does not block an eye above it, a box BEHIND
 * the target does not block, and standing in a doorway (an endpoint inside the box) does not read as
 * standing inside a wall. The behaviour that consumes it -- a clerk that no longer sees through the
 * courthouse, a scorch that no longer burns through a bench -- is asserted in test-municipal-defense.
 *
 * Build/run: make test-line-of-sight */
#include <stdio.h>

#include "sight.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* A 2 x 4 x 2 wall sitting at (0, 2, 0) -- its top is 4 m up, so it crosses an eye at 1.25 m. */
static SightBox wall(void) {
    SightBox b;
    b.x = 0.0f; b.y = 2.0f; b.z = 0.0f;
    b.w = 2.0f; b.h = 4.0f; b.d = 2.0f;
    b.solid = 1;
    return b;
}

int main(void) {
    printf("== line of sight ==\n");

    /* ---- no geometry means clear: hosts that have not federated their boxes keep the old behaviour ---- */
    CHECK(sight_segment_clear(NULL, 0, -5.0f, 1.25f, 0.0f, 5.0f, 1.25f, 0.0f),
          "an empty world blocked a sightline");
    printf("PASS: a world with no boxes sees everything (unchanged behaviour)\n");

    /* ---- a wall between the two points blocks ---- */
    {
        SightBox w = wall();
        CHECK(!sight_segment_clear(&w, 1, -5.0f, 1.25f, 0.0f, 5.0f, 1.25f, 0.0f),
              "a wall between two points did not block");
        printf("PASS: a wall between the eye and the target blocks\n");
    }

    /* ---- the space beside it does not: the same wall, the ray off to the side ---- */
    {
        SightBox w = wall();
        CHECK(sight_segment_clear(&w, 1, -5.0f, 1.25f, 6.0f, 5.0f, 1.25f, 6.0f),
              "a wall six metres to the side blocked a ray that never touched it");
        printf("PASS: the space beside a wall stays clear\n");
    }

    /* ---- a waist-high bench does not block an eye at 1.25 m, and does block a scorch on the floor ---- */
    {
        SightBox bench;
        bench.x = 0.0f; bench.y = 0.25f; bench.z = 0.0f;
        bench.w = 2.0f; bench.h = 0.5f; bench.d = 2.0f;
        bench.solid = 1;
        CHECK(sight_segment_clear(&bench, 1, -5.0f, 1.25f, 0.0f, 5.0f, 1.25f, 0.0f),
              "a 0.5 m bench blocked an eye at 1.25 m -- the query is not 3D");
        CHECK(!sight_segment_clear(&bench, 1, -5.0f, 0.1f, 0.0f, 5.0f, 0.1f, 0.0f),
              "the same bench failed to block a ray at its own height -- the query is not 3D");
        printf("PASS: a waist-high bench blocks the floor but not the eye (the height term is real)\n");
    }

    /* ---- a box behind the target is not in the way ---- */
    {
        SightBox w = wall();
        w.x = 20.0f;
        CHECK(sight_segment_clear(&w, 1, -5.0f, 1.25f, 0.0f, 5.0f, 1.25f, 0.0f),
              "a wall past the target blocked the sightline");
        printf("PASS: geometry beyond the target does not block\n");
    }

    /* ---- standing in a doorway (endpoint inside the box) is not standing inside a wall ---- */
    {
        SightBox w = wall();
        CHECK(sight_segment_clear(&w, 1, 0.0f, 1.25f, 0.0f, 5.0f, 1.25f, 0.0f),
              "a viewer inside the box read as blocked by that same box");
        printf("PASS: an endpoint inside a box is not blocked by it\n");
    }

    /* ---- decorative boxes never block ---- */
    {
        SightBox w = wall();
        w.solid = 0;
        CHECK(sight_segment_clear(&w, 1, -5.0f, 1.25f, 0.0f, 5.0f, 1.25f, 0.0f),
              "a non-solid box blocked a sightline");
        printf("PASS: only solid boxes block\n");
    }

    /* ---- one wall among many: the loop finds it, and the result is direction-independent ---- */
    {
        SightBox boxes[3];
        boxes[0] = wall(); boxes[0].x = -30.0f;
        boxes[1] = wall();
        boxes[2] = wall(); boxes[2].x = 30.0f;
        CHECK(!sight_segment_clear(boxes, 3, -5.0f, 1.25f, 0.0f, 5.0f, 1.25f, 0.0f),
              "the loop missed the one blocking box in a list of three");
        CHECK(!sight_segment_clear(boxes, 3, 5.0f, 1.25f, 0.0f, -5.0f, 1.25f, 0.0f),
              "the sight test is not symmetric -- the ray's own direction changed the answer");
        printf("PASS: a blocking box is found in a list, whichever way the ray runs\n");
    }

    printf("%s: %d check(s) failed\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
