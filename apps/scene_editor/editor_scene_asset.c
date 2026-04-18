#include "editor_scene_asset.h"

#include <string.h>

void editor_scene_asset_init(EditorSceneAsset *scene) {
    memset(scene, 0, sizeof(*scene));
    scene->version = 1;
    strncpy(scene->scene_name, "untitled_scene", sizeof(scene->scene_name) - 1);
}

void editor_scene_asset_add_default_box(EditorSceneAsset *scene, const char *name, float x, float y, float z) {
    if (scene->box_count >= EDITOR_MAX_BOXES) {
        return;
    }
    EditorSceneBox *box = &scene->boxes[scene->box_count++];
    memset(box, 0, sizeof(*box));
    box->id = (int)scene->box_count;
    strncpy(box->name, name, sizeof(box->name) - 1);
    box->position[0] = x;
    box->position[1] = y;
    box->position[2] = z;
    box->size[0] = 4.0f;
    box->size[1] = 2.0f;
    box->size[2] = 4.0f;
    box->rotation_y_degrees = 0.0f;
    box->color[0] = 0.7f;
    box->color[1] = 0.7f;
    box->color[2] = 0.7f;
    box->color[3] = 1.0f;
    box->flags.solid = 1;
    box->flags.visible = 1;
}

void editor_scene_asset_add_default_stage_camera(EditorSceneAsset *scene, const char *name, float tx, float ty, float tz) {
    if (scene->stage_camera_count >= EDITOR_MAX_STAGE_CAMERAS) {
        return;
    }
    EditorStageCamera *cam = &scene->stage_cameras[scene->stage_camera_count++];
    memset(cam, 0, sizeof(*cam));
    cam->id = (int)scene->stage_camera_count;
    strncpy(cam->name, name, sizeof(cam->name) - 1);
    cam->target[0] = tx;
    cam->target[1] = ty;
    cam->target[2] = tz;
    cam->distance = 25.0f;
    cam->yaw_degrees = 30.0f;
    cam->pitch_degrees = 22.0f;
    cam->fov_degrees = 60.0f;
}
