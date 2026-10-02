#ifndef SHANKPIT_THIRD_PERSON_H
#define SHANKPIT_THIRD_PERSON_H

/* third_person.h -- the two pieces of third-person view math that must agree with the map
 * collision, not just the renderer (card T62892945, "fix our 3rd person"):
 *
 *   tp_camera_offset_clipped  the orbit camera is pulled in along its arm when a wall is in the
 *                             way, instead of sitting inside the wall (seeing through it).
 *   tp_aim_yaw_pitch          aim bridging: the crosshair is at the screen centre, which is the
 *                             CAMERA's ray, not the player's eye. A shot fired along the camera
 *                             yaw/pitch from the eye lands off the crosshair (parallax) whenever
 *                             the camera is offset. The bridged yaw/pitch aim the eye at the
 *                             point the camera ray actually hits, so hits land on the reticle.
 *
 * Include AFTER physics.h (uses trace_map and the map in force). Pure, no GL, no globals.
 */

#include <math.h>

#define TP_CAM_MARGIN 0.35f   /* keep the camera this far in front of a wall */
#define TP_AIM_RANGE  600.0f

/* Pulls (dx,dy,dz) -- the desired camera offset from the pivot -- in so the camera stops
 * TP_CAM_MARGIN short of the first wall between pivot and camera. Returns the scale applied
 * (1.0 = unobstructed). The camera never moves closer than 15% of the arm. */
static inline float tp_camera_offset_clipped(float px, float py, float pz,
                                             float *dx, float *dy, float *dz) {
    float len = sqrtf((*dx) * (*dx) + (*dy) * (*dy) + (*dz) * (*dz));
    if (len < 0.0001f) return 1.0f;
    float hx, hy, hz, nx, ny, nz;
    if (!trace_map(px, py, pz, px + *dx, py + *dy, pz + *dz, &hx, &hy, &hz, &nx, &ny, &nz)) return 1.0f;
    float hit = sqrtf((hx - px) * (hx - px) + (hy - py) * (hy - py) + (hz - pz) * (hz - pz));
    float scale = (hit - TP_CAM_MARGIN) / len;
    if (scale < 0.15f) scale = 0.15f;
    if (scale > 1.0f) scale = 1.0f;
    *dx *= scale; *dy *= scale; *dz *= scale;
    return scale;
}

/* cam_* is the camera position, yaw/pitch (degrees, SHANKPIT convention: forward =
 * (-sin yaw*cos pitch, sin pitch, -cos yaw*cos pitch), physics.h's own convention) are the
 * camera's look angles,
 * eye_* the player's eye. Writes the yaw/pitch that aim the eye at the camera ray's hit point
 * (or a far point along the ray if it hits nothing). */
static inline void tp_dir_from_yaw_pitch(float yaw_deg, float pitch_deg, float *x, float *y, float *z) {
    float yr = yaw_deg * 0.01745329252f, pr = pitch_deg * 0.01745329252f;
    *x = -sinf(yr) * cosf(pr);   /* physics.h: r = -yaw; dx = sin(r)cos(p), dz = -cos(r)cos(p) */
    *y = sinf(pr);
    *z = -cosf(yr) * cosf(pr);
}

static inline void tp_aim_yaw_pitch(float cam_x, float cam_y, float cam_z, float cam_yaw, float cam_pitch,
                                    float eye_x, float eye_y, float eye_z,
                                    float *out_yaw, float *out_pitch) {
    float fx, fy, fz;
    tp_dir_from_yaw_pitch(cam_yaw, cam_pitch, &fx, &fy, &fz);
    float tx = cam_x + fx * TP_AIM_RANGE, ty = cam_y + fy * TP_AIM_RANGE, tz = cam_z + fz * TP_AIM_RANGE;
    float hx, hy, hz, nx, ny, nz;
    if (trace_map(cam_x, cam_y, cam_z, tx, ty, tz, &hx, &hy, &hz, &nx, &ny, &nz)) { tx = hx; ty = hy; tz = hz; }
    float dx = tx - eye_x, dy = ty - eye_y, dz = tz - eye_z;
    float planar = sqrtf(dx * dx + dz * dz);
    if (planar < 0.0001f && fabsf(dy) < 0.0001f) { *out_yaw = cam_yaw; *out_pitch = cam_pitch; return; }
    *out_yaw = atan2f(-dx, -dz) * 57.2957795f;
    *out_pitch = atan2f(dy, planar > 0.0001f ? planar : 0.0001f) * 57.2957795f;
}

#endif
