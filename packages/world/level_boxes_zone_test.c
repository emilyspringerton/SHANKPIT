/* level_boxes_zone_test.c -- real test for LevelZone (BIG_O engine merge phase 7c,
 * EMILY/BACKLOG.md SECTION 536): proves level_boxes_parse_json reads an authored "zones" array,
 * and level_boxes_zone_for_position correctly resolves a live x/y/z into the containing zone's
 * zone_type (or honestly misses). Same real "small-scanner JSON, plain assert() harness"
 * convention as test_level_exit_spawner_target.c.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/level_boxes_zone_test \
 *       packages/world/level_boxes_zone_test.c && /tmp/level_boxes_zone_test
 */
#include "level_boxes.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Two zones: a LAB sphere (zone_type=1) at the origin, radius 5; a VAULT sphere (zone_type=4)
       far away at (100,0,100), radius 3. */
    const char *json =
        "{\"name\":\"TEST\",\"width\":40,\"height\":10,\"depth\":40,"
        "\"ground_plane_enabled\":true,\"ground_plane_squares\":1,"
        "\"walls\":[],"
        "\"zones\":["
        "{\"x\":0,\"y\":0,\"z\":0,\"radius\":5,\"zone_type\":1},"
        "{\"x\":100,\"y\":0,\"z\":100,\"radius\":3,\"zone_type\":4}"
        "]}";

    CustomLevelData lvl;
    int ok = level_boxes_parse_json(json, &lvl);
    assert(ok);
    assert(lvl.zone_count == 2);
    assert(lvl.zones[0].zone_type == 1 && lvl.zones[0].radius == 5.0f);
    assert(lvl.zones[1].zone_type == 4 && lvl.zones[1].x == 100.0f);
    printf("PASS: level_boxes_parse_json reads an authored \"zones\" array\n");

    /* Inside the LAB sphere. */
    assert(level_boxes_zone_for_position(&lvl, 2.0f, 0.0f, 2.0f) == 1);
    printf("PASS: a point inside the LAB zone resolves to zone_type 1\n");

    /* Exactly on the LAB sphere's own radius boundary (inclusive, per the <= check). */
    assert(level_boxes_zone_for_position(&lvl, 5.0f, 0.0f, 0.0f) == 1);
    printf("PASS: a point exactly on a zone's own radius boundary counts as inside\n");

    /* Outside every zone -- honest miss, not a crash or a default guess. */
    assert(level_boxes_zone_for_position(&lvl, 50.0f, 0.0f, 50.0f) == -1);
    printf("PASS: a point outside every authored zone honestly returns -1\n");

    /* Inside the second (VAULT) zone -- proves the scan isn't hardcoded to zones[0]. */
    assert(level_boxes_zone_for_position(&lvl, 101.0f, 0.0f, 100.0f) == 4);
    printf("PASS: a point inside the second authored zone resolves to its own zone_type (4)\n");

    /* A level with no "zones" key at all -- real, honest "none authored" state, not an error. */
    const char *no_zones_json =
        "{\"name\":\"BARE\",\"width\":10,\"height\":10,\"depth\":10,"
        "\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"walls\":[]}";
    CustomLevelData bare;
    assert(level_boxes_parse_json(no_zones_json, &bare));
    assert(bare.zone_count == 0);
    assert(level_boxes_zone_for_position(&bare, 0.0f, 0.0f, 0.0f) == -1);
    printf("PASS: a level with no authored zones parses cleanly and always misses\n");

    /* NULL level is a safe, honest miss too (a caller with no level data loaded yet). */
    assert(level_boxes_zone_for_position(NULL, 0.0f, 0.0f, 0.0f) == -1);
    printf("PASS: level_boxes_zone_for_position(NULL, ...) is a safe miss, not a crash\n");

    printf("\nALL PASS\n");
    return 0;
}
