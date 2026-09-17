// test_level_enclosed_lighting.c -- S493, real test for the enclosed/indoor lighting flag
// (founder real-time: "theres not much difference between having lights on and not having
// lights - its still basically illuminated in this totally enclosed level"). Proves the data
// layer: level_boxes_parse_json correctly reads a level's own real "enclosed" key, defaults to
// false (0) when absent (every pre-S493 level's own real export), and doesn't accidentally
// trip on a false/0 value. The actual g_world_lighting_preset switch
// (level_boxes_apply_to_physics, apps/lobby/src/main.c) is a `static` function with GL-adjacent
// globals, not independently unit-testable from outside that translation unit -- this test
// covers what CAN be tested in isolation, matching this repo's own established practice for
// similar client-only wiring.
#include <stdio.h>
#include <string.h>

#include "../../packages/world/level_boxes.h"

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("FAIL %s\n", msg); \
    } else { \
        printf("PASS %s\n", msg); \
        tests_passed++; \
    } \
} while (0)

int main(void) {
    printf("--- Level Enclosed Lighting Flag (S493) ---\n");

    CustomLevelData lvl_enclosed;
    const char *json_enclosed =
        "{\"name\":\"INTERIOR\",\"width\":40,\"height\":10,\"depth\":40,"
        "\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"enclosed\":true,\"walls\":[]}";
    ASSERT_TRUE(level_boxes_parse_json(json_enclosed, &lvl_enclosed), "enclosed level JSON parses");
    ASSERT_TRUE(lvl_enclosed.enclosed == 1, "enclosed=true parses to a real, nonzero value");

    CustomLevelData lvl_outdoor;
    const char *json_outdoor =
        "{\"name\":\"OUTDOOR\",\"width\":40,\"height\":10,\"depth\":40,"
        "\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"enclosed\":false,\"walls\":[]}";
    ASSERT_TRUE(level_boxes_parse_json(json_outdoor, &lvl_outdoor), "outdoor level JSON parses");
    ASSERT_TRUE(lvl_outdoor.enclosed == 0, "enclosed=false parses to 0");

    CustomLevelData lvl_absent;
    const char *json_absent =
        "{\"name\":\"PRE_S493\",\"width\":40,\"height\":10,\"depth\":40,"
        "\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"walls\":[]}";
    ASSERT_TRUE(level_boxes_parse_json(json_absent, &lvl_absent), "pre-S493 level JSON (no enclosed key) parses");
    ASSERT_TRUE(lvl_absent.enclosed == 0, "absent \"enclosed\" key defaults to 0 (outdoor, unchanged behavior)");

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
