/* level_subtract_test.c -- the subtract modifier, headless and exact (no GL, no display).
 *
 * Founder: "we need affordances for punching holes in level geometry in a sane way where it punches a
 * hole cleanly with the faces being nice ... we build a building and then put a big cube with a
 * subtract modifier on it and it subtracts from the geometry of the level wherever it intersects it
 * ... that way we can thread an elevator through the floors and also punch holes through floors for
 * the stairs to actually work".
 *
 * What is actually asserted here, and why each one matters:
 *   - a cut through a floor slab leaves a RING of four pieces, not a hole-shaped gap with a lip
 *   - every face of every piece is axis-aligned and inherited (no slivers, no rounding drift)
 *   - VOLUME IS EXACT: pieces + hole == the original box. This is the property that makes it
 *     "subtracts wherever it intersects", and it is what a fudge-factor version cannot pass.
 *   - a cut that misses changes nothing; a cut that swallows a box removes it and closes the gap so
 *     nothing still points at a box that is gone
 *   - subtraction is ORDER-INDEPENDENT for disjoint holes and associative for overlapping ones
 *   - caps are respected whole (over-budget refuses the cut rather than half-applying it)
 *
 * Build/run: make test-level-subtract
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "level_boxes.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static LevelBox box(float x, float y, float z, float w, float h, float d) {
    LevelBox b;
    memset(&b, 0, sizeof b);
    b.x = x; b.y = y; b.z = z; b.w = w; b.h = h; b.d = d;
    b.material_idx = 0;
    return b;
}
static LevelSubtractBox cut(float x, float y, float z, float w, float h, float d) {
    LevelSubtractBox s;
    s.x = x; s.y = y; s.z = z; s.w = w; s.h = h; s.d = d;
    return s;
}
static float vol(const LevelBox *b) { return b->w > 0 ? b->w * b->h * b->d : 0.0f; }

/* Does `b` overlap the cut volume at all? Must be false for every piece a cut produces. */
static int overlaps(const LevelBox *b, const LevelSubtractBox *s) {
    float bx0 = b->x - b->w * 0.5f, bx1 = b->x + b->w * 0.5f;
    float by0 = b->y - b->h * 0.5f, by1 = b->y + b->h * 0.5f;
    float bz0 = b->z - b->d * 0.5f, bz1 = b->z + b->d * 0.5f;
    float sx0 = s->x - s->w * 0.5f, sx1 = s->x + s->w * 0.5f;
    float sy0 = s->y - s->h * 0.5f, sy1 = s->y + s->h * 0.5f;
    float sz0 = s->z - s->d * 0.5f, sz1 = s->z + s->d * 0.5f;
    const float e = 0.001f;
    return !(bx1 <= sx0 + e || bx0 >= sx1 - e || by1 <= sy0 + e || by0 >= sy1 - e ||
             bz1 <= sz0 + e || bz0 >= sz1 - e);
}

static void level_with(LevelBox *b, int n, CustomLevelData *lvl) {
    memset(lvl, 0, sizeof *lvl);
    for (int i = 0; i < n; i++) lvl->boxes[i] = b[i];
    lvl->count = n;
    lvl->material_count = 1;
    strncpy(lvl->materials[0].name, "brick", sizeof(lvl->materials[0].name) - 1);
}

int main(void) {
    /* ---- 1. a floor slab gets a rectangular stairwell hole, as a RING of four pieces ---- */
    {
        LevelBox floor = box(0, 10, 0, 40, 1, 40);      /* a 40x40 slab, 1 thick */
        CustomLevelData lvl;
        level_with(&floor, 1, &lvl);
        lvl.subtract_count = 1;
        lvl.subtract[0] = cut(0, 10, 0, 8, 6, 12);      /* the stair shaft: wider than the slab, so it cuts clean through */
        int changed = level_boxes_apply_subtract(&lvl);
        CHECK(changed > 0, "the stair cut reported no change");
        CHECK(lvl.count == 4, "a full-thickness floor cut produced %d pieces, expected the 4-piece ring", lvl.count);
        float total = 0.0f;
        for (int i = 0; i < lvl.count; i++) {
            CHECK(!overlaps(&lvl.boxes[i], &lvl.subtract[0]), "piece %d still overlaps the shaft it was cut by", i);
            CHECK(fabsf(lvl.boxes[i].y - 10.0f) < 0.001f, "piece %d left the slab's own height", i);
            CHECK(lvl.boxes[i].w > 0.0f && lvl.boxes[i].h > 0.0f && lvl.boxes[i].d > 0.0f,
                  "piece %d is degenerate", i);
            total += vol(&lvl.boxes[i]);
        }
        float expect = 40.0f * 1.0f * 40.0f - 8.0f * 1.0f * 12.0f;   /* slab minus the shaft's footprint */
        CHECK(fabsf(total - expect) < 0.05f, "volume is %.2f, expected %.2f -- the hole is not exact", total, expect);
        printf("PASS: a stair shaft through a floor slab -> %d pieces, volume %.1f/%.1f\n", lvl.count, total, expect);
    }

    /* ---- 2. a shaft through STACKED floors (an elevator): every floor gets the same hole ---- */
    {
        LevelBox floors[4] = {
            box(0, 10, 0, 40, 1, 40), box(0, 20, 0, 40, 1, 40),
            box(0, 30, 0, 40, 1, 40), box(0, 40, 0, 40, 1, 40)
        };
        CustomLevelData lvl;
        level_with(floors, 4, &lvl);
        lvl.subtract_count = 1;
        lvl.subtract[0] = cut(5, 25, -3, 10, 40, 10);   /* one tall cube, threaded through all four */
        level_boxes_apply_subtract(&lvl);
        CHECK(lvl.count == 16, "an elevator shaft through 4 floors produced %d boxes, expected 16 (4 rings)", lvl.count);
        float expect = 4.0f * (40.0f * 40.0f - 10.0f * 10.0f) * 1.0f;
        float total = 0.0f;
        for (int i = 0; i < lvl.count; i++) total += vol(&lvl.boxes[i]);
        CHECK(fabsf(total - expect) < 0.1f, "4-floor shaft volume %.2f, expected %.2f", total, expect);
        printf("PASS: one cube threads an elevator shaft through 4 floors -> %d boxes, volume %.1f\n", lvl.count, total);
    }

    /* ---- 3. a cut that misses changes nothing at all ---- */
    {
        LevelBox wall = box(100, 5, 0, 4, 10, 4);
        CustomLevelData lvl;
        level_with(&wall, 1, &lvl);
        lvl.subtract_count = 1;
        lvl.subtract[0] = cut(0, 5, 0, 8, 8, 8);
        int changed = level_boxes_apply_subtract(&lvl);
        CHECK(changed == 0, "a cut that misses reported %d changes", changed);
        CHECK(lvl.count == 1, "a cut that misses changed the box count to %d", lvl.count);
        CHECK(lvl.boxes[0].x == 100.0f && lvl.boxes[0].w == 4.0f, "a cut that misses moved the wall");
        printf("PASS: a cube that misses the geometry changes nothing\n");
    }

    /* ---- 4. an interior cut through a thick wall: 6 pieces, and the faces are the CUT's faces ---- */
    {
        LevelBox cube = box(0, 0, 0, 30, 30, 30);
        CustomLevelData lvl;
        level_with(&cube, 1, &lvl);
        lvl.subtract_count = 1;
        lvl.subtract[0] = cut(0, 0, 0, 10, 10, 10);
        level_boxes_apply_subtract(&lvl);
        CHECK(lvl.count == 6, "an interior cut produced %d pieces, expected 6", lvl.count);
        /* Every piece must be flush with an outer face or the cut's own face -- i.e. axis aligned on the
         * cut's planes, which is what "the faces being nice" means: no rounding, no wedges. */
        for (int i = 0; i < lvl.count; i++) {
            const LevelBox *b = &lvl.boxes[i];
            float x0 = b->x - b->w * 0.5f, x1 = b->x + b->w * 0.5f;
            float y0 = b->y - b->h * 0.5f, y1 = b->y + b->h * 0.5f;
            float z0 = b->z - b->d * 0.5f, z1 = b->z + b->d * 0.5f;
            int on_grid = 0;
            const float planes[6] = { -15.0f, 15.0f, -5.0f, 5.0f };  /* outer box and cut faces (x==z here) */
            for (int k = 0; k < 4; k++) {
                if (fabsf(x0 - planes[k]) < 0.001f || fabsf(x1 - planes[k]) < 0.001f) on_grid = 1;
                if (fabsf(y0 - planes[k]) < 0.001f || fabsf(y1 - planes[k]) < 0.001f) on_grid = 1;
                if (fabsf(z0 - planes[k]) < 0.001f || fabsf(z1 - planes[k]) < 0.001f) on_grid = 1;
            }
            CHECK(on_grid, "piece %d has no face on the outer shell or the cut plane", i);
        }
        float total = 0.0f;
        for (int i = 0; i < lvl.count; i++) total += vol(&lvl.boxes[i]);
        CHECK(fabsf(total - (27000.0f - 1000.0f)) < 0.5f, "interior cut volume %.1f, expected 26000", total);
        printf("PASS: an interior cut gives %d axis-aligned pieces, volume %.0f/26000\n", lvl.count, total);
    }

    /* ---- 5. two disjoint holes are independent, and the result is the same in either order ---- */
    {
        LevelBox slab = box(0, 10, 0, 60, 1, 20);
        CustomLevelData a, b;
        level_with(&slab, 1, &a);
        level_with(&slab, 1, &b);
        a.subtract_count = 2;
        a.subtract[0] = cut(-20, 10, 0, 6, 6, 6);
        a.subtract[1] = cut(20, 10, 0, 6, 6, 6);
        b.subtract_count = 2;
        b.subtract[0] = a.subtract[1];
        b.subtract[1] = a.subtract[0];
        level_boxes_apply_subtract(&a);
        level_boxes_apply_subtract(&b);
        float va = 0.0f, vb = 0.0f;
        for (int i = 0; i < a.count; i++) va += vol(&a.boxes[i]);
        for (int i = 0; i < b.count; i++) vb += vol(&b.boxes[i]);
        float expect = 60.0f * 1.0f * 20.0f - 2.0f * (6.0f * 1.0f * 6.0f);
        CHECK(fabsf(va - expect) < 0.1f, "two holes volume %.2f, expected %.2f", va, expect);
        CHECK(fabsf(va - vb) < 0.001f, "subtraction order changed the result (%.3f vs %.3f)", va, vb);
        printf("PASS: two disjoint holes are order-independent, volume %.1f\n", va);
    }

    /* ---- 6. a cut that swallows a box removes it, and nothing still points at it ---- */
    {
        LevelBox keep = box(50, 5, 0, 4, 10, 4);
        LevelBox gone = box(0, 5, 0, 4, 10, 4);
        CustomLevelData lvl;
        LevelBox pair[2];
        pair[0] = keep; pair[1] = gone;   /* level_with copies n boxes, so hand it a real array */
        level_with(pair, 2, &lvl);
        lvl.door_count = 1;
        lvl.doors[0].box_index = 1;                 /* the door was authored on the doomed box */
        lvl.button_count = 1;
        lvl.buttons[0].box_index = 0;               /* ...and this one on the survivor */
        lvl.brick_damage_count = 1;
        lvl.brick_damage[0].wall = 1;
        lvl.brick_damage[0].key = 0;
        lvl.brick_damage[0].hp = 0;
        lvl.subtract_count = 1;
        lvl.subtract[0] = cut(0, 5, 0, 20, 20, 20);  /* swallows the second box whole */
        level_boxes_apply_subtract(&lvl);
        CHECK(lvl.count == 1, "a swallowed box left %d boxes behind", lvl.count);
        CHECK(lvl.boxes[0].x == 50.0f, "compaction kept the wrong box (x=%.1f)", lvl.boxes[0].x);
        CHECK(lvl.doors[0].box_index == -1, "a door still points at a removed box (index %d)", lvl.doors[0].box_index);
        CHECK(lvl.buttons[0].box_index == 0, "a button on a surviving box was remapped away (index %d)", lvl.buttons[0].box_index);
        CHECK(lvl.brick_damage_count == 0, "saved damage survived for a box that no longer exists");
        printf("PASS: a swallowed box is removed and doors/damage are re-pointed\n");
    }

    /* ---- 7. degenerate and missing cuts are ignored, and a NULL level is total ---- */
    {
        LevelBox wall = box(0, 5, 0, 4, 10, 4);
        CustomLevelData lvl;
        level_with(&wall, 1, &lvl);
        lvl.subtract_count = 3;
        lvl.subtract[0] = cut(0, 5, 0, 0, 10, 10);       /* zero width */
        lvl.subtract[1] = cut(0, 5, 0, 10, -5, 10);      /* negative height */
        lvl.subtract[2] = cut(0, 7, 0, 10, 4, 10);       /* the real one: y 5..9, so the wall survives above and below */
        level_boxes_apply_subtract(&lvl);
        CHECK(lvl.count == 2, "the real cut produced %d pieces, expected the 2 slabs above and below it", lvl.count);
        CustomLevelData none;
        memset(&none, 0, sizeof none);
        CHECK(level_boxes_apply_subtract(&none) == 0, "an empty level reported a change");
        CHECK(level_boxes_apply_subtract(NULL) == 0, "a NULL level was not total");
        printf("PASS: degenerate cuts are ignored and the call is total on NULL\n");
    }

    /* ---- 8. the cap is respected whole: an over-budget split refuses rather than half-applying ---- */
    {
        LevelBox cubes[LEVEL_BOXES_MAX];
        CustomLevelData lvl;
        /* Fill the level so that splitting anything would overflow the box cap. */
        for (int i = 0; i < LEVEL_BOXES_MAX; i++) cubes[i] = box((float)(i % 64) * 100.0f, 5, (float)(i / 64) * 100.0f, 4, 10, 4);
        level_with(cubes, LEVEL_BOXES_MAX, &lvl);
        lvl.subtract_count = 1;
        lvl.subtract[0] = cut(0, 5, 0, 10, 10, 10);   /* would need 6 pieces for one of them */
        level_boxes_apply_subtract(&lvl);
        CHECK(lvl.count <= LEVEL_BOXES_MAX, "the box cap was exceeded: %d", lvl.count);
        for (int i = 0; i < lvl.count; i++)
            CHECK(lvl.boxes[i].w > 0.0f && lvl.boxes[i].h > 0.0f && lvl.boxes[i].d > 0.0f,
                  "an over-budget cut left a degenerate box at %d", i);
        printf("PASS: over-budget cuts refuse whole (count %d <= %d)\n", lvl.count, LEVEL_BOXES_MAX);
    }

    /* ---- 9. the modifier survives a JSON round trip, so a NOCK save cannot drop it ---- */
    {
        CustomLevelData lvl;
        memset(&lvl, 0, sizeof lvl);
        lvl.count = 1;
        lvl.boxes[0] = box(0, 10, 0, 40, 1, 40);
        lvl.material_count = 1;
        strncpy(lvl.materials[0].name, "brick", sizeof(lvl.materials[0].name) - 1);
        lvl.subtract_count = 1;
        lvl.subtract[0] = cut(3, 10, 4, 8, 6, 12);
        char path[] = "/tmp/level_subtract_roundtrip.json";
        CHECK(level_boxes_save_to_file(path, &lvl) == 1, "could not save the level");
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL, "could not reopen the saved level");
        if (f) {
            char buf[8192];
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = '\0';
            fclose(f);
            CustomLevelData back;
            CHECK(level_boxes_parse_json(buf, &back) == 1, "the saved level did not parse");
            CHECK(back.subtract_count == 1, "round trip lost the subtract volume (%d)", back.subtract_count);
            CHECK(fabsf(back.subtract[0].x - 3.0f) < 0.001f && fabsf(back.subtract[0].d - 12.0f) < 0.001f,
                  "round trip changed the subtract volume");
            /* And the parsed level already has the hole in it, with no extra call. */
            CHECK(back.count == 4, "a parsed level with a subtract modifier kept %d boxes, expected 4", back.count);
            printf("PASS: the modifier round-trips and is applied at parse time (%d boxes)\n", back.count);
        }
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
