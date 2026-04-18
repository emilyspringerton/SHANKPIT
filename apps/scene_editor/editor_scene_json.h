#ifndef SHANKPIT_EDITOR_SCENE_JSON_H
#define SHANKPIT_EDITOR_SCENE_JSON_H

#include "editor_scene_asset.h"

int editor_scene_json_load(const char *path, EditorSceneAsset *out_scene);
int editor_scene_json_save(const char *path, const EditorSceneAsset *scene);

#endif
