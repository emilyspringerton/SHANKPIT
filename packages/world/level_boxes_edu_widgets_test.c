/* level_boxes_edu_widgets_test.c -- the NOCK "edu_widgets" level schema.
 *   gcc -Wall -Wextra -O2 -o /tmp/lbew packages/world/level_boxes_edu_widgets_test.c && /tmp/lbew */
#include "level_boxes.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    static CustomLevelData lvl;
    const char *json =
        "{\"name\":\"T\",\"width\":40,\"height\":10,\"depth\":40,\"ground_plane_enabled\":true,\"ground_plane_squares\":1,"
        "\"walls\":[{\"x\":0,\"y\":2,\"z\":16,\"sx\":6,\"sy\":4,\"sz\":1},{\"x\":0,\"y\":2,\"z\":24,\"sx\":6,\"sy\":4,\"sz\":1}],"
        "\"edu_widgets\":["
        "{\"entity_id\":\"plate_a\",\"machine_type\":5,\"initial_state\":0,\"memory_address\":1,\"x\":0,\"y\":0,\"z\":8,\"radius\":1.5},"
        "{\"entity_id\":\"door_gate_alpha\",\"machine_type\":1,\"initial_state\":0,\"memory_address\":2,\"x\":0,\"y\":2,\"z\":16,\"box_index\":0},"
        "{\"entity_id\":\"\",\"machine_type\":1,\"memory_address\":3},"
        "{\"entity_id\":\"badtype\",\"machine_type\":42,\"memory_address\":4},"
        "{\"entity_id\":\"far_box\",\"machine_type\":2,\"memory_address\":5,\"box_index\":99}"
        "]}";
    assert(level_boxes_parse_json(json, &lvl));
    assert(lvl.count == 2);
    /* entries 3 (empty id) and 4 (type 42) are dropped, not half-loaded */
    assert(lvl.edu_widget_count == 3);
    assert(strcmp(lvl.edu_widgets[0].entity_id, "plate_a") == 0 && lvl.edu_widgets[0].machine_type == 5);
    assert(lvl.edu_widgets[0].memory_address == 1 && lvl.edu_widgets[0].z == 8.0f && lvl.edu_widgets[0].radius == 1.5f);
    assert(lvl.edu_widgets[0].box_index == -1);
    assert(strcmp(lvl.edu_widgets[1].entity_id, "door_gate_alpha") == 0 && lvl.edu_widgets[1].box_index == 0);
    assert(strcmp(lvl.edu_widgets[2].entity_id, "far_box") == 0 && lvl.edu_widgets[2].box_index == -1); /* out-of-range box -> detached */
    printf("PASS: edu_widgets parsed, malformed entries skipped, bad box_index detached\n");

    const char *none = "{\"name\":\"B\",\"width\":10,\"height\":10,\"depth\":10,\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"walls\":[]}";
    assert(level_boxes_parse_json(none, &lvl) && lvl.edu_widget_count == 0);
    printf("PASS: a level with no edu_widgets key has none\n");

    /* the 17th widget is ignored, never overflows */
    char big[4096]; int n = snprintf(big, sizeof big, "{\"name\":\"C\",\"width\":1,\"height\":1,\"depth\":1,\"walls\":[],\"edu_widgets\":[");
    for (int i = 0; i < 20; i++) n += snprintf(big + n, sizeof big - n, "%s{\"entity_id\":\"w%d\",\"machine_type\":1,\"memory_address\":%d}", i ? "," : "", i, i);
    snprintf(big + n, sizeof big - n, "]}");
    assert(level_boxes_parse_json(big, &lvl) && lvl.edu_widget_count == LEVEL_BOXES_MAX_EDU_WIDGETS);
    printf("PASS: widget count is capped at %d\n", LEVEL_BOXES_MAX_EDU_WIDGETS);
    return 0;
}
