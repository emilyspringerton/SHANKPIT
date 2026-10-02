/* third_person_test.c -- headless checks for third_person.h (card T62892945). make test-third-person */
#include "physics.h"
#include "third_person.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
static int near(float a, float b, float e) { return fabsf(a - b) <= e; }

int main(void) {
    phys_set_scene(SCENE_CITY);

    /* yaw/pitch <-> direction round trip, physics.h convention (yaw 0 faces -Z) */
    {
        float x, y, z;
        tp_dir_from_yaw_pitch(0.0f, 0.0f, &x, &y, &z);
        CHECK(near(x, 0, 1e-5f) && near(y, 0, 1e-5f) && near(z, -1, 1e-5f));
        float kx, ky, kz; katana_forward_dir(30.0f, 10.0f, &kx, &ky, &kz);
        tp_dir_from_yaw_pitch(30.0f, 10.0f, &x, &y, &z);
        CHECK(near(x, kx, 1e-3f) && near(y, ky, 1e-3f) && near(z, kz, 1e-3f));
    }

    /* open air: camera arm unobstructed (high above the city, nothing to hit) */
    {
        float dx = 0, dy = 2, dz = 6;
        float s = tp_camera_offset_clipped(0.0f, 900.0f, 0.0f, &dx, &dy, &dz);
        CHECK(s == 1.0f && dz == 6.0f);
    }

    /* a wall behind the player pulls the camera in. Build a fixture: put a tall box 3 units behind
       (+z) a pivot far from the city, as map box 1, and aim the arm straight at it. */
    {
        static Box fixture[3];
        fixture[0] = (Box){0, -2, 0, 4000, 4, 4000};
        fixture[1] = (Box){5000, 10, 3005, 40, 20, 4};   /* wall face at z = 3003 */
        map_geo = fixture; map_count = 2;
        float dx = 0, dy = 0, dz = 6;                     /* arm toward +z */
        float s = tp_camera_offset_clipped(5000.0f, 5.0f, 2998.0f, &dx, &dy, &dz);
        CHECK(s < 1.0f);
        CHECK(dz > 0.0f && dz < 5.0f - 0.2f);             /* stops short of the wall at 5 units */
        CHECK(near(dz, 5.0f - TP_CAM_MARGIN, 0.1f));

        /* aim bridging: camera 6 units behind the eye, same yaw/pitch, wall 30 ahead of the eye.
           yaw 0 faces -Z; wall face at z = -30 for pivot z = 0 -- reposition the fixture. */
        fixture[1] = (Box){0, 10, -32, 40, 20, 4};        /* face at z = -30 */
        map_geo = fixture; map_count = 2;
        float yaw, pitch;
        tp_aim_yaw_pitch(0, 6, 6, 0, 0, 0, 6, 0, &yaw, &pitch);
        CHECK(near(yaw, 0.0f, 0.01f) && near(pitch, 0.0f, 0.01f));   /* straight ahead: unchanged */
        /* camera offset to +x: the eye must aim toward +x (negative yaw, forward.x = -sin yaw) to converge on
           the point the camera ray hits (x=3, z=-30 from a camera at x=3,z=6; eye at x=0). */
        tp_aim_yaw_pitch(3, 6, 6, 0, 0, 0, 6, 0, &yaw, &pitch);
        CHECK(yaw < 0.0f && near(yaw, -atan2f(3.0f, 30.0f) * 57.2957795f, 0.05f));
    }

    if (g_fail) { printf("third_person_test: %d FAILED\n", g_fail); return 1; }
    printf("third_person_test: all checks passed\n");
    return 0;
}
