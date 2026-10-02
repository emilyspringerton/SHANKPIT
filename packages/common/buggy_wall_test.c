/* buggy_wall_test.c -- the buggy must not drive through map_geo walls (card #455). make test-buggy-wall */
#include "physics.h"
#include "buggy_rules_host.h"
#include <stdio.h>

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* Drive full throttle for n ticks from z=0 toward -Z (yaw 0) and return the final z. */
static float drive(Box *fixture, int count, int ticks, float *out_speed) {
    map_geo = fixture; map_count = count;
    BuggyState b = {0};
    b.active = 1; b.occupant_player_id = 0; b.grounded = 1; b.yaw = 0.0f;
    b.x = 5000.0f; b.z = 0.0f; b.y = BUGGY_WHEEL_RADIUS + BUGGY_CHASSIS_CLEARANCE;
    for (int i = 0; i < ticks; i++) simulate_buggy_state(&b, 1.0f, 0.0f, 0.016f, 1);
    if (out_speed) *out_speed = sqrtf(b.vx * b.vx + b.vz * b.vz);
    return b.z;
}

static int fast_top(void) { return 12480; }
static const PhysBuggyRules fast = { fast_top, NULL, NULL, NULL, NULL, NULL };

int main(void) {
    phys_set_scene(SCENE_CITY);
    static Box open_[1];
    static Box wall[3];
    wall[1] = (Box){5000, 5, -60, 40, 10, 0.5f};      /* thin wall, face at z = -59.75 */
    static Box curb[3];
    curb[1] = (Box){5000, 0.2f, -60, 40, 0.4f, 4};    /* low curb: drivable */

    float z_open = drive(open_, 1, 400, NULL);
    CHECK(z_open < -150.0f);                           /* control: open ground really is fast */

    float spd = 0;
    float z_wall = drive(wall, 2, 400, &spd);
    CHECK(z_wall > -60.0f);                            /* stopped at the wall, not through it */
    CHECK(z_wall < -55.0f);                            /* ...and actually reached it */
    CHECK(spd < 0.5f);                                 /* a wall hit scrubs speed */

    float z_curb = drive(curb, 2, 400, NULL);
    CHECK(z_curb < -100.0f);                           /* a curb below step height is driven over */

    /* box tops are ground: a long 0.5-high platform ahead -- the buggy climbs on and rides on top of it */
    static Box platform[3];
    platform[1] = (Box){5000, 0.25f, -1540, 40, 0.5f, 3000};  /* top at y = 0.5, z from -40 to -3040 */
    {
        map_geo = platform; map_count = 2;
        BuggyState b = {0};
        b.active = 1; b.occupant_player_id = 0; b.grounded = 1; b.yaw = 0.0f;
        b.x = 5000.0f; b.z = 0.0f; b.y = BUGGY_WHEEL_RADIUS + BUGGY_CHASSIS_CLEARANCE;
        for (int i = 0; i < 160; i++) simulate_buggy_state(&b, 1.0f, 0.0f, 0.016f, 1);
        float on_top = BUGGY_WHEEL_RADIUS + BUGGY_CHASSIS_CLEARANCE + 0.5f;
        CHECK(b.z < -50.0f && b.z > -3000.0f);             /* actually on the platform */
        CHECK(b.y > on_top - 0.2f && b.y < on_top + 0.2f); /* riding on the top, not on the terrain beneath */
        printf("platform: z %.1f y %.2f (want ~%.2f)\n", b.z, b.y, on_top);
    }

    /* ---- #468 programmable buggy ---- */
    /* (a) the PARENA defaults reproduce the built-in constants: same 240-tick run, same distance (<1%) */
    map_geo = open_; map_count = 1;
    float dist_builtin, dist_parena;
    {
        BuggyState b = {0};
        b.active = 1; b.grounded = 1; b.x = 5000; b.y = BUGGY_WHEEL_RADIUS + BUGGY_CHASSIS_CLEARANCE;
        phys_set_buggy_rules(NULL);
        for (int i = 0; i < 240; i++) simulate_buggy_state(&b, 1.0f, 0.4f, 0.016f, 1);
        dist_builtin = sqrtf((b.x - 5000) * (b.x - 5000) + b.z * b.z);
    }
    {
        BuggyState b = {0};
        b.active = 1; b.grounded = 1; b.x = 5000; b.y = BUGGY_WHEEL_RADIUS + BUGGY_CHASSIS_CLEARANCE;
        buggy_rules_install();
        for (int i = 0; i < 240; i++) simulate_buggy_state(&b, 1.0f, 0.4f, 0.016f, 1);
        dist_parena = sqrtf((b.x - 5000) * (b.x - 5000) + b.z * b.z);
    }
    CHECK(fabsf(dist_parena - dist_builtin) < 0.01f * dist_builtin);
    printf("rules: builtin %.1f vs PARENA %.1f units in 240 ticks\n", dist_builtin, dist_parena);

    /* (b) a different mod really changes the buggy: double top speed -> measurably further */
    {
        BuggyState b = {0};
        b.active = 1; b.grounded = 1; b.x = 5000; b.y = BUGGY_WHEEL_RADIUS + BUGGY_CHASSIS_CLEARANCE;
        phys_set_buggy_rules(&fast);
        for (int i = 0; i < 240; i++) simulate_buggy_state(&b, 1.0f, 0.0f, 0.016f, 1);
        float dist_fast = sqrtf((b.x - 5000) * (b.x - 5000) + b.z * b.z);
        float speed_fast = sqrtf(b.vx * b.vx + b.vz * b.vz);
        CHECK(speed_fast > BUGGY_TOP_SPEED * 1.1f);     /* it exceeds the stock top speed */
        CHECK(dist_fast > dist_builtin * 1.1f);
        printf("rules: 2x top-speed mod -> speed %.2f, %.1f units\n", speed_fast, dist_fast);
        phys_set_buggy_rules(NULL);
    }

    if (g_fail) { printf("buggy_wall_test: %d FAILED\n", g_fail); return 1; }
    printf("buggy_wall_test OK (open %.1f, wall %.1f, curb %.1f)\n", z_open, z_wall, z_curb);
    return 0;
}
