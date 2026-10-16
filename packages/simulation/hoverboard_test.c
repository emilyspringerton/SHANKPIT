/* hoverboard_test.c -- real, direct coverage for hoverboard.h's own momentum-physics
 * integration, against the real, live PARENA-compiled hoverboard_rules.c (not a mock) -- same
 * real cross-package shape chat_say_test.c already establishes.
 */
#include "./hoverboard.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(board_count() == 3);
    assert(board_speedster() == 1 && board_tank() == 2 && board_glider() == 3);
    assert(board_is_valid(board_speedster()) && board_is_valid(board_tank()) && board_is_valid(board_glider()));
    assert(!board_is_valid(BOARD_NONE));
    assert(!board_is_valid(4));
    printf("PASS: real board identity/validity constants\n");

    /* Gravity scale: GLIDER is the real "air ship" -- lightest gravity of the three. TANK hugs
       the ground at full, unscaled gravity. An invalid/none board is a real, honest 1.0. */
    assert(hoverboard_gravity_scale(board_glider()) < hoverboard_gravity_scale(board_speedster()));
    assert(hoverboard_gravity_scale(board_speedster()) < hoverboard_gravity_scale(board_tank()));
    assert(hoverboard_gravity_scale(board_tank()) == 1.0f);
    assert(hoverboard_gravity_scale(BOARD_NONE) == 1.0f);
    printf("PASS: real per-board gravity scale, GLIDER lightest, TANK unscaled, invalid/none defaults to 1.0\n");

    /* Momentum: holding forward input on a real board accelerates from rest, real drag keeps it
       under control, and repeated ticks converge toward (not past) that board's own real top
       speed -- never exceeding it. */
    for (int b = 1; b <= 3; b++) {
        float vx = 0.0f, vz = 0.0f;
        float max_speed = (float)board_max_speed_x100(b) / 100.0f;
        for (int tick = 0; tick < 600; tick++) { /* 10 real seconds at a 60Hz tick, generous settle time */
            hoverboard_tick(b, 0.0f, 1.0f, 1.0f / 60.0f, &vx, &vz);
            float speed = (vx * vx + vz * vz);
            assert(speed <= max_speed * max_speed + 0.01f); /* never exceeds the real cap (small float slack) */
        }
        float final_speed = vz; /* pure +z input, vx should stay ~0 */
        assert(final_speed > 0.0f); /* real acceleration happened from rest */
        assert(final_speed > max_speed * 0.9f); /* real convergence toward top speed given 10s to settle */
        printf("PASS: board %d (%s) accelerates from rest and converges toward its real %.1f max speed (reached %.2f)\n",
               b, BOARD_NAMES[b], max_speed, final_speed);
    }

    /* Friction: with input released, real velocity decays every tick rather than coasting forever
       -- SPEEDSTER (highest retention) decays slower than TANK (lowest retention) from the same
       real starting velocity. */
    {
        float speedster_vx = 10.0f, speedster_vz = 0.0f;
        float tank_vx = 10.0f, tank_vz = 0.0f;
        hoverboard_tick(board_speedster(), 0.0f, 0.0f, 1.0f / 60.0f, &speedster_vx, &speedster_vz);
        hoverboard_tick(board_tank(), 0.0f, 0.0f, 1.0f / 60.0f, &tank_vx, &tank_vz);
        assert(speedster_vx > tank_vx); /* SPEEDSTER's own higher retention decays less per tick */
        printf("PASS: real per-board friction -- SPEEDSTER retains more velocity per tick than TANK from the same start\n");
    }

    /* A no-op board_type is a real, honest no-op -- velocity is left completely untouched, not
       zeroed or corrupted. */
    {
        float vx = 3.5f, vz = -2.25f;
        hoverboard_tick(BOARD_NONE, 1.0f, 1.0f, 1.0f / 60.0f, &vx, &vz);
        assert(vx == 3.5f && vz == -2.25f);
        printf("PASS: an invalid/none board_type leaves velocity completely untouched\n");
    }

    printf("\nALL PASS\n");
    return 0;
}
