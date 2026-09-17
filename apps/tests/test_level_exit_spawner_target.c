// test_level_exit_spawner_target.c -- S491, real test for GTA-style building-exit spawner
// targeting (founder real-time: "how can i specify which spawner the exit leads to for the
// seamless experience of exiting the building"). Proves, with real function calls (not mocked):
//   1. level_boxes_parse_json correctly reads a spawner's own "id" and a level exit's own
//      "target_spawner_id" (both real, found-live gaps -- LevelSpawner never captured its author-
//      assigned id at all before this pass, dropped silently since nothing needed it);
//   2. phys_set_custom_level_spawners threads that id through into the native runtime tables;
//   3. custom_level_pick_spawner_by_id finds the right spawner by id, and honestly returns 0 (no
//      match, no crash) for a stale/missing id or the real target_spawner_id=0 "no target" sentinel.
#include <stdio.h>
#include <string.h>

#include "../../packages/common/net_sim.h"
#include "../../packages/simulation/local_game.h"
#include "../../packages/world/level_boxes.h"

int g_story_cutscene_done   = 0;
int g_story_outro_requested = 0;
int g_shankpit_is_server    = 1;

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
    printf("--- Level Exit Spawner Targeting (S491) ---\n");

    // A real, minimal level JSON: two spawners (ids 5 and 9) and one level exit targeting
    // spawner 9 specifically -- exactly the "front door leads to the exact spot outside" shape.
    const char *json =
        "{\"name\":\"TEST\",\"width\":40,\"height\":10,\"depth\":40,"
        "\"ground_plane_enabled\":true,\"ground_plane_squares\":1,"
        "\"walls\":[],"
        "\"spawners\":["
        "{\"id\":5,\"x\":1,\"y\":0,\"z\":1,\"yaw\":0,\"team\":-1},"
        "{\"id\":9,\"x\":100,\"y\":0,\"z\":200,\"yaw\":0,\"team\":-1}"
        "],"
        "\"level_exits\":["
        "{\"x\":0,\"y\":0,\"z\":0,\"radius\":4,\"target_spawner_id\":9}"
        "]}";

    CustomLevelData lvl;
    int ok = level_boxes_parse_json(json, &lvl);
    ASSERT_TRUE(ok, "level JSON parses");
    ASSERT_TRUE(lvl.spawner_count == 2, "both spawners parsed");
    ASSERT_TRUE(lvl.spawners[0].id == 5, "first spawner's real id parsed (was silently dropped before S491)");
    ASSERT_TRUE(lvl.spawners[1].id == 9, "second spawner's real id parsed");
    ASSERT_TRUE(lvl.level_exit_count == 1, "one level exit parsed");
    ASSERT_TRUE(lvl.level_exits[0].target_spawner_id == 9, "the exit's own real target_spawner_id parsed");

    // Thread it through the native runtime exactly like server_apply_custom_level does.
    float sp_x[LEVEL_BOXES_MAX_SPAWNERS], sp_y[LEVEL_BOXES_MAX_SPAWNERS], sp_z[LEVEL_BOXES_MAX_SPAWNERS];
    int sp_team[LEVEL_BOXES_MAX_SPAWNERS], sp_id[LEVEL_BOXES_MAX_SPAWNERS];
    for (int i = 0; i < lvl.spawner_count; i++) {
        sp_x[i] = lvl.spawners[i].x; sp_y[i] = lvl.spawners[i].y; sp_z[i] = lvl.spawners[i].z;
        sp_team[i] = lvl.spawners[i].team; sp_id[i] = lvl.spawners[i].id;
    }
    phys_set_custom_level_spawners(sp_x, sp_y, sp_z, sp_team, sp_id, lvl.spawner_count);

    float tx = -1, ty = -1, tz = -1;
    int found = custom_level_pick_spawner_by_id(lvl.level_exits[0].target_spawner_id, &tx, &ty, &tz);
    ASSERT_TRUE(found, "custom_level_pick_spawner_by_id finds the targeted spawner (id=9)");
    ASSERT_TRUE(tx == 100.0f && ty == 0.0f && tz == 200.0f, "returns spawner 9's own real position, not spawner 5's");

    float mx, my, mz;
    ASSERT_TRUE(!custom_level_pick_spawner_by_id(404, &mx, &my, &mz), "a stale/missing target id honestly misses, no crash");
    ASSERT_TRUE(!custom_level_pick_spawner_by_id(0, &mx, &my, &mz), "target_spawner_id=0 (the real 'no specific target' sentinel) never matches");

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
