#ifndef SHANKPIT_EDITOR_MOVE_TOOL_H
#define SHANKPIT_EDITOR_MOVE_TOOL_H

typedef enum {
    EDITOR_MOVE_AXIS_FREE = 0,
    EDITOR_MOVE_AXIS_X,
    EDITOR_MOVE_AXIS_Y,
    EDITOR_MOVE_AXIS_Z
} EditorMoveAxis;

typedef struct {
    int active;
    EditorMoveAxis axis;
    float start_hit[3];
    float start_box[3];
} EditorMoveTool;

void editor_move_tool_begin(EditorMoveTool *tool, const float hit_point[3], const float box_position[3]);
void editor_move_tool_cancel(EditorMoveTool *tool);
void editor_move_tool_set_axis(EditorMoveTool *tool, EditorMoveAxis axis);
void editor_move_tool_apply(EditorMoveTool *tool, const float hit_point[3], float inout_box_position[3]);

#endif
