/* level_boxes_buggy_test.c -- level_boxes_parse_json reads an authored "buggy_spawns" array (#464/#465).
 * Same plain-assert harness as level_boxes_zone_test.c. make test-buggy-wall */
#include "level_boxes.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const char *json =
        "{\"name\":\"T\",\"width\":40,\"height\":10,\"depth\":40,\"ground_plane_enabled\":true,\"ground_plane_squares\":1,"
        "\"walls\":[],"
        "\"buggy_spawns\":[{\"x\":10,\"y\":0.1,\"z\":-20,\"yaw\":90},{\"x\":-5,\"y\":0.1,\"z\":8,\"yaw\":0}]}";
    CustomLevelData lvl;
    assert(level_boxes_parse_json(json, &lvl));
    assert(lvl.buggy_spawn_count == 2);
    assert(lvl.buggy_spawns[0].x == 10.0f && lvl.buggy_spawns[0].z == -20.0f && lvl.buggy_spawns[0].yaw == 90.0f);
    assert(lvl.buggy_spawns[1].x == -5.0f && lvl.buggy_spawns[1].yaw == 0.0f);

    /* absent key: a level exported before buggy spawns existed has none */
    const char *old_json = "{\"name\":\"T\",\"width\":40,\"height\":10,\"depth\":40,\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"walls\":[]}";
    assert(level_boxes_parse_json(old_json, &lvl));
    assert(lvl.buggy_spawn_count == 0);

    /* more entries than the cap: clamped, not overflowed */
    char big[4096];
    int n = snprintf(big, sizeof(big), "{\"name\":\"T\",\"width\":40,\"height\":10,\"depth\":40,\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"walls\":[],\"buggy_spawns\":[");
    for (int i = 0; i < LEVEL_BOXES_MAX_BUGGY_SPAWNS + 5; i++)
        n += snprintf(big + n, sizeof(big) - (size_t)n, "%s{\"x\":%d,\"y\":0,\"z\":0,\"yaw\":0}", i ? "," : "", i);
    snprintf(big + n, sizeof(big) - (size_t)n, "]}");
    assert(level_boxes_parse_json(big, &lvl));
    assert(lvl.buggy_spawn_count == LEVEL_BOXES_MAX_BUGGY_SPAWNS);

    printf("level_boxes_buggy_test OK\n");
    return 0;
}
