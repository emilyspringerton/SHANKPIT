/* level_boxes_empire_world_test.c -- the level-authored EMPIRE world (`resource_nodes` / `claim_cells`).
 *
 * What this proves: an author can place the contested ground in the level itself (the founder's "this is
 * almost like a level building affordance"), the schema reads what was written, an empty level stays
 * honestly empty instead of inventing nodes, and the NOCK editor's own save -> fetch round trip keeps it.
 *
 *   gcc -Wall -Wextra -O2 -o /tmp/lbew packages/world/level_boxes_empire_world_test.c && /tmp/lbew
 */
#include "level_boxes.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    static CustomLevelData lvl;
    const char *json =
        "{\"name\":\"EMPIRE_YARD\",\"width\":40,\"height\":10,\"depth\":40,"
        "\"walls\":[{\"x\":0,\"y\":2,\"z\":16,\"sx\":6,\"sy\":4,\"sz\":1}],"
        "\"resource_nodes\":[{\"x\":-6,\"z\":8,\"amount\":12},{\"x\":6,\"z\":-8,\"amount\":3},"
        "{\"x\":0,\"z\":0,\"amount\":0}],"
        "\"claim_cells\":[{\"x\":14,\"z\":0},{\"x\":-14,\"z\":0,\"radius\":9}]}";
    assert(level_boxes_parse_json(json, &lvl));

    /* A node with nothing in it is skipped: the AI would otherwise walk to it and stand there forever. */
    assert(lvl.resource_node_count == 2);
    assert(lvl.claim_cell_count == 2);
    assert(lvl.resource_nodes[0].x == -6.0f && lvl.resource_nodes[0].z == 8.0f);
    assert(lvl.resource_nodes[0].amount == 12);
    assert(lvl.resource_nodes[1].x == 6.0f && lvl.resource_nodes[1].amount == 3);
    assert(lvl.claim_cells[0].x == 14.0f && lvl.claim_cells[0].z == 0.0f && lvl.claim_cells[0].radius == 0);
    assert(lvl.claim_cells[1].radius == 9); /* an authored radius wins over the host default */
    assert(lvl.count == 1);                 /* the boxes are still the boxes */

    /* Absent keys are a real, honest "nothing authored": the host keeps its default ring. */
    static CustomLevelData bare;
    assert(level_boxes_parse_json("{\"name\":\"BARE\",\"walls\":[]}", &bare));
    assert(bare.resource_node_count == 0 && bare.claim_cell_count == 0);

    /* Save -> load round trip (the NOCK editor's own path through the registry). */
    assert(level_boxes_save_to_file("/tmp/empire_world_level.json", &lvl));
    static CustomLevelData back;
    assert(level_boxes_load_from_file("/tmp/empire_world_level.json", &back));
    assert(back.resource_node_count == 2 && back.claim_cell_count == 2);
    assert(back.resource_nodes[0].x == -6.0f && back.resource_nodes[0].z == 8.0f);
    assert(back.resource_nodes[1].amount == 3);
    assert(back.claim_cells[1].x == -14.0f && back.claim_cells[1].radius == 9);

    printf("level_boxes resource_nodes/claim_cells: all checks passed\n");
    return 0;
}
