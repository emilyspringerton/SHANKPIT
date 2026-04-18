#ifndef SHANKPIT_EDITOR_SELECTION_H
#define SHANKPIT_EDITOR_SELECTION_H

#include "editor_scene_asset.h"

int editor_selection_pick_box(const EditorSceneAsset *scene, const float ray_origin[3], const float ray_dir[3]);

#endif
