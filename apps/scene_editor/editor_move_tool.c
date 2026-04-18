#include "editor_move_tool.h"

void editor_move_tool_begin(EditorMoveTool *tool, const float hit_point[3], const float box_position[3]) {
    tool->active = 1;
    tool->start_hit[0] = hit_point[0];
    tool->start_hit[1] = hit_point[1];
    tool->start_hit[2] = hit_point[2];
    tool->start_box[0] = box_position[0];
    tool->start_box[1] = box_position[1];
    tool->start_box[2] = box_position[2];
}

void editor_move_tool_cancel(EditorMoveTool *tool) {
    tool->active = 0;
}

void editor_move_tool_set_axis(EditorMoveTool *tool, EditorMoveAxis axis) {
    tool->axis = axis;
}

void editor_move_tool_apply(EditorMoveTool *tool, const float hit_point[3], float inout_box_position[3]) {
    float delta[3] = {
        hit_point[0] - tool->start_hit[0],
        hit_point[1] - tool->start_hit[1],
        hit_point[2] - tool->start_hit[2]
    };

    inout_box_position[0] = tool->start_box[0];
    inout_box_position[1] = tool->start_box[1];
    inout_box_position[2] = tool->start_box[2];

    if (tool->axis == EDITOR_MOVE_AXIS_FREE || tool->axis == EDITOR_MOVE_AXIS_X) {
        inout_box_position[0] += delta[0];
    }
    if (tool->axis == EDITOR_MOVE_AXIS_FREE || tool->axis == EDITOR_MOVE_AXIS_Y) {
        inout_box_position[1] += delta[1];
    }
    if (tool->axis == EDITOR_MOVE_AXIS_FREE || tool->axis == EDITOR_MOVE_AXIS_Z) {
        inout_box_position[2] += delta[2];
    }
}
