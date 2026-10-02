#include <assert.h>
#include <stdio.h>
#include "level_boxes.h"
int main(void) {
    static CustomLevelData l;
    assert(level_boxes_parse_json("{\"name\":\"a\",\"walls\":[],\"floor_tint\":{\"r\":0.2,\"g\":0.4,\"b\":0.6,\"a\":0.5}}", &l));
    assert(l.floor_tinted && l.floor_r == 0.2f && l.floor_g == 0.4f && l.floor_b == 0.6f && l.floor_a == 0.5f);
    assert(level_boxes_parse_json("{\"name\":\"a\",\"walls\":[],\"floor_tint\":null}", &l) && !l.floor_tinted);
    assert(level_boxes_parse_json("{\"name\":\"a\",\"walls\":[]}", &l) && !l.floor_tinted);
    puts("floor_tint ok"); return 0;
}
