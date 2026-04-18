#include "editor_camera.h"

#include <math.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void editor_camera_init(EditorViewportCamera *camera) {
    camera->target[0] = 0.0f;
    camera->target[1] = 2.0f;
    camera->target[2] = 0.0f;
    camera->distance = 35.0f;
    camera->yaw_degrees = 35.0f;
    camera->pitch_degrees = 25.0f;
    camera->fov_degrees = 60.0f;
}

void editor_camera_orbit(EditorViewportCamera *camera, float delta_yaw_deg, float delta_pitch_deg) {
    camera->yaw_degrees += delta_yaw_deg;
    camera->pitch_degrees += delta_pitch_deg;
    camera->pitch_degrees = clampf(camera->pitch_degrees, -89.0f, 89.0f);
}

void editor_camera_zoom(EditorViewportCamera *camera, float delta) {
    camera->distance = clampf(camera->distance + delta, 2.0f, 500.0f);
}

void editor_camera_eye_position(const EditorViewportCamera *camera, float out_eye[3]) {
    float yaw = camera->yaw_degrees * 0.017453292f;
    float pitch = camera->pitch_degrees * 0.017453292f;
    float cp = cosf(pitch);
    float sp = sinf(pitch);
    float cy = cosf(yaw);
    float sy = sinf(yaw);

    out_eye[0] = camera->target[0] + camera->distance * cp * sy;
    out_eye[1] = camera->target[1] + camera->distance * sp;
    out_eye[2] = camera->target[2] + camera->distance * cp * cy;
}

void editor_camera_pan(EditorViewportCamera *camera, float right, float up) {
    float yaw = camera->yaw_degrees * 0.017453292f;
    float right_x = cosf(yaw);
    float right_z = -sinf(yaw);

    camera->target[0] += right_x * right;
    camera->target[2] += right_z * right;
    camera->target[1] += up;
}
