#include <assert.h>
#include <stdio.h>
#include "edit_map.h"
int main(void) {
    static CustomLevelData lvl; static EditLog log, log2; memset(&lvl, 0, sizeof lvl);
    lvl.boxes[0] = (LevelBox){0,0,0, 10,2,10}; lvl.count = 1;
    lvl.spawners[0].x = 1; lvl.spawner_count = 1;
    EditOp add = {EDIT_OP_ADD_BOX, 0, 5,3,5, 2,2,2, 0};
    assert(edit_map_do(&log, &lvl, &add) && lvl.count == 2);
    assert(edit_map_box_at(&lvl, 5, 3, 5, 0.01f) == 1);
    assert(edit_map_box_at(&lvl, 0, 0, 0, 0.01f) == 0);
    assert(edit_map_box_at(&lvl, 50, 0, 0, 0.01f) == -1);
    EditOp mv = {EDIT_OP_MOVE_SPAWNER, 0, 7,8,9, 0,0,0, 90};
    assert(edit_map_do(&log, &lvl, &mv) && lvl.spawners[0].x == 7 && lvl.spawners[0].yaw == 90);
    EditOp bad = {EDIT_OP_MOVE_SPAWNER, 3, 0,0,0, 0,0,0, 0};
    assert(!edit_map_do(&log, &lvl, &bad) && log.n == 2);
    EditOp del = {EDIT_OP_DEL_BOX, 0, 0,0,0, 0,0,0, 0};
    assert(edit_map_do(&log, &lvl, &del) && lvl.count == 1 && lvl.boxes[0].x == 5);
    assert(edit_map_save(&log, "t", "/tmp/edit_map_test.json"));
    static CustomLevelData l2; memset(&l2, 0, sizeof l2);
    l2.boxes[0] = (LevelBox){0,0,0, 10,2,10}; l2.count = 1; l2.spawners[0].x = 1; l2.spawner_count = 1;
    assert(edit_map_load_apply(&log2, &l2, "/tmp/edit_map_test.json") == 3);
    assert(l2.count == 1 && l2.boxes[0].x == 5 && l2.spawners[0].z == 9);
    assert(edit_map_load_apply(&log2, &l2, "/nonexistent") == -1);
    puts("edit_map ok"); return 0;
}
