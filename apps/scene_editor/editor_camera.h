#ifndef SHANKPIT_EDITOR_CAMERA_H
#define SHANKPIT_EDITOR_CAMERA_H

typedef struct {
    float target[3];
    float distance;
    float yaw_degrees;
    float pitch_degrees;
    float fov_degrees;
} EditorViewportCamera;

void editor_camera_init(EditorViewportCamera *camera);
void editor_camera_orbit(EditorViewportCamera *camera, float delta_yaw_deg, float delta_pitch_deg);
void editor_camera_zoom(EditorViewportCamera *camera, float delta);
void editor_camera_pan(EditorViewportCamera *camera, float right, float up);
void editor_camera_eye_position(const EditorViewportCamera *camera, float out_eye[3]);

#endif
