#ifndef SHANKPIT_EDITOR_SCENE_ASSET_H
#define SHANKPIT_EDITOR_SCENE_ASSET_H

#include <stddef.h>

#define EDITOR_MAX_BOXES 512
#define EDITOR_MAX_STAGE_CAMERAS 64
#define EDITOR_NAME_LEN 64

typedef struct {
    int solid;
    int visible;
} EditorBoxFlags;

typedef struct {
    int id;
    char name[EDITOR_NAME_LEN];
    float position[3];
    float size[3];
    float rotation_y_degrees;
    float color[4];
    EditorBoxFlags flags;
} EditorSceneBox;

typedef struct {
    int id;
    char name[EDITOR_NAME_LEN];
    float target[3];
    float distance;
    float yaw_degrees;
    float pitch_degrees;
    float fov_degrees;
} EditorStageCamera;

typedef struct {
    int version;
    char scene_name[EDITOR_NAME_LEN];
    EditorSceneBox boxes[EDITOR_MAX_BOXES];
    size_t box_count;
    EditorStageCamera stage_cameras[EDITOR_MAX_STAGE_CAMERAS];
    size_t stage_camera_count;
} EditorSceneAsset;

void editor_scene_asset_init(EditorSceneAsset *scene);
void editor_scene_asset_add_default_box(EditorSceneAsset *scene, const char *name, float x, float y, float z);
void editor_scene_asset_add_default_stage_camera(EditorSceneAsset *scene, const char *name, float tx, float ty, float tz);

#endif
