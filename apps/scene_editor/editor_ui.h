#ifndef SHANKPIT_EDITOR_UI_H
#define SHANKPIT_EDITOR_UI_H

#include "editor_camera.h"
#include "editor_move_tool.h"
#include "editor_scene_asset.h"

void editor_ui_draw_overlay(int width, int height,
                            const EditorSceneAsset *scene,
                            int selected_box,
                            int selected_stage_camera,
                            int stage_camera_drives_viewport,
                            const EditorMoveTool *move_tool,
                            const EditorViewportCamera *camera,
                            const char *active_path);

#endif
