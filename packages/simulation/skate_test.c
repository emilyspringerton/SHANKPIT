/* skate_test.c -- the skateboard on its own (packages/simulation/skate.h + the PARENA rules in skate_rules.c).
 * Build: make test-skate */
#include "skate.h"
#include <stdio.h>

static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) passes++; else { fails++; printf("FAIL: %s\n", msg); } } while (0)

/* n ticks on flat ground, looking `yaw`, pressing fwd */
static void ride(Skate *s, int n, float fwd, float yaw, float *x, float *z) {
    float mx, mz;
    for (int i = 0; i < n; i++) {
        skate_tick(s, fwd, yaw, 1, 0.0f, 1000, 0, 0, 0, &mx, &mz);
        if (x) *x += mx;
        if (z) *z += mz;
    }
}

/* a jump: `air` ticks off the ground, turning `turn_per_tick` degrees a tick, then the landing tick */
static int jump(Skate *s, int air, float yaw0, float turn_per_tick, int flip_at, int heel, int grab) {
    float mx, mz, yaw = yaw0;
    skate_tick(s, 0, yaw, 0, 0.9f, 1000, 0, 0, 0, &mx, &mz);  /* take-off: a jump */
    for (int i = 0; i < air; i++) {
        if (i == flip_at) skate_flip(s, heel);
        yaw += turn_per_tick;
        skate_tick(s, 0, yaw, 0, 0.5f, 1000, 0, 0, grab, &mx, &mz);
    }
    return skate_tick(s, 0, yaw, 1, 0.0f, 1000, 0, 0, 0, &mx, &mz);   /* touch down */
}

int main(void) {
    Skate s;
    memset(&s, 0, sizeof s);
    float mx, mz;

    /* getting on and off */
    skate_toggle(&s, 90.0f);
    CHECK(s.on && s.heading == 90.0f && s.speed == 0.0f, "K: on the board, facing where you look, standing");
    CHECK(strstr(s.text, "W PUSH") != NULL, "and it says the controls");
    skate_toggle(&s, 0.0f);
    CHECK(!s.on, "K again: off");
    CHECK(skate_tick(&s, 1, 0, 1, 0.0f, 1000, 0, 0, 0, &mx, &mz) == SKATE_EV_NONE && mx == 0 && mz == 0, "off the board it moves nothing");

    /* pushing: up to the push top speed, and no further on the flat */
    skate_toggle(&s, 0.0f);
    float x = 0, z = 0;
    ride(&s, 20, 1, 0.0f, &x, &z);
    CHECK(s.speed > 0.5f && s.speed < 0.6f, "W pushes: 20 ticks is ~0.56 units a tick");
    ride(&s, 200, 1, 0.0f, &x, &z);
    CHECK(fabsf(s.speed - 1.05f) < 1e-4f, "and tops out at the push top speed (1.05: faster than running, 0.95)");
    CHECK(z < -100.0f && fabsf(x) < 1e-3f, "yaw 0 rolls toward -z, straight");
    /* rolling and braking */
    ride(&s, 64, 0, 0.0f, NULL, NULL);
    CHECK(s.speed > 0.75f && s.speed < 0.85f, "nothing pressed: it rolls, a little slower each second");
    ride(&s, 10, -1, 0.0f, NULL, NULL);
    CHECK(s.speed > 0.3f && s.speed < 0.4f, "S brakes hard");
    ride(&s, 20, -1, 0.0f, NULL, NULL);
    CHECK(s.speed == 0.0f, "and stops, never going backwards");

    /* carving: the board turns toward the look, no faster than its speed allows */
    skate_toggle(&s, 0); skate_toggle(&s, 0);
    ride(&s, 1, 0, 90.0f, NULL, NULL);
    CHECK(fabsf(s.heading - 5.0f) < 1e-3f, "standing still it turns 5 degrees a tick toward the look");
    ride(&s, 200, 1, 0.0f, NULL, NULL);
    ride(&s, 1, 1, 90.0f, NULL, NULL);
    CHECK(s.heading > 2.5f && s.heading < 3.5f, "at speed it carves wider (~3 degrees a tick)");
    x = z = 0;
    ride(&s, 1, 1, s.heading, &x, &z);
    CHECK(fabsf(atan2f(-x, -z) * 180.0f / 3.14159265f - s.heading) < 0.01f, "it rolls along its heading, never sideways");

    /* a hill: down speeds up past the push top, up slows down */
    skate_toggle(&s, 0); skate_toggle(&s, 0);
    for (int i = 0; i < 300; i++) skate_tick(&s, 0, 0, 1, 0.0f, 750, 0, -1, 0, &mx, &mz);     /* heading -z, downhill -z */
    CHECK(s.speed > 1.5f, "rolling down a steep hill: faster than pushing can go");
    CHECK(fabsf(s.speed - 1.6f) < 1e-4f, "up to the cap (1.6)");
    for (int i = 0; i < 64; i++) skate_tick(&s, 0, 0, 1, 0.0f, 750, 0, 1, 0, &mx, &mz);        /* now uphill */
    CHECK(s.speed < 1.0f, "uphill takes the speed away");
    for (int i = 0; i < 300; i++) skate_tick(&s, 0, 0, 1, 0.0f, 750, 0, 1, 0, &mx, &mz);
    CHECK(s.speed == 0.0f, "a long climb stops the board");
    float before = 0.5f; s.speed = before;
    skate_tick(&s, 0, 0, 1, 0.0f, 995, 0, -1, 0, &mx, &mz);
    CHECK(s.speed < before, "an almost-flat face is flat: no hill");

    /* tricks */
    skate_toggle(&s, 0); skate_toggle(&s, 0);
    ride(&s, 100, 1, 0.0f, NULL, NULL);
    int ev = jump(&s, 30, 0.0f, 0.0f, -1, 0, 0);
    CHECK(ev == SKATE_EV_LANDED && strstr(s.text, "OLLIE  +100") != NULL, "an ollie lands for 100");
    CHECK(s.combo == 1 && s.score == 100, "and starts the combo");
    ev = jump(&s, 30, 0.0f, 0.0f, 2, 0, 0);
    CHECK(ev == SKATE_EV_LANDED && strstr(s.text, "OLLIE + KICKFLIP  +800  x2") != NULL, "Z: a kickflip, x2 for the second in a row");
    ev = jump(&s, 30, 0.0f, 0.0f, 2, 1, 1);
    CHECK(strstr(s.text, "OLLIE + HEELFLIP + GRAB  +1950  x3") != NULL, "Shift+Z heelflip, Ctrl grab, x3");
    ev = jump(&s, 30, 0.0f, 12.0f, -1, 0, 0);          /* 360 degrees in the air */
    CHECK(ev == SKATE_EV_LANDED && strstr(s.text, "OLLIE + 360") != NULL, "turning a full circle is a 360");
    CHECK(strstr(s.text, "+2000  x4") != NULL, "worth two half turns (100 + 400) x4");
    ev = jump(&s, 30, s.heading, 6.0f, -1, 0, 0);      /* 180: lands backwards */
    CHECK(ev == SKATE_EV_SWITCH && s.switch_stance && strstr(s.text, "180  (SWITCH)") != NULL, "a 180 rolls away switch");
    float travel_before = s.travel_z;
    ride(&s, 1, 1, s.heading - 180.0f + 360.0f, NULL, NULL);
    CHECK((s.travel_z < 0) == (travel_before < 0), "switch keeps rolling the same way, the rider facing back");

    /* bails */
    skate_toggle(&s, 0); skate_toggle(&s, 0);
    ride(&s, 100, 1, 0.0f, NULL, NULL);
    jump(&s, 30, 0.0f, 0.0f, -1, 0, 0);
    ev = jump(&s, 30, 0.0f, 3.0f, -1, 0, 0);           /* 90 degrees: sideways */
    CHECK(ev == SKATE_EV_BAILED && !s.on && s.combo == 0, "landing sideways is a bail: off, combo gone");
    CHECK(strstr(s.text, "SIDEWAYS") != NULL, "and it says why");
    int back = 0;
    for (int i = 0; i < 100 && !back; i++) back = skate_tick(&s, 0, 0, 1, 0.0f, 1000, 0, 0, 0, &mx, &mz) == SKATE_EV_BACK_ON;
    CHECK(back && s.on && s.speed == 0.0f, "back on the board after a moment, standing");
    ride(&s, 100, 1, 0.0f, NULL, NULL);
    ev = jump(&s, 10, 0.0f, 0.0f, 5, 0, 0);            /* the flip needs 26 ticks; lands after 5 */
    CHECK(ev == SKATE_EV_BAILED && strstr(s.text, "FLIP WAS STILL TURNING") != NULL, "landing mid-flip is a bail");
    skate_toggle(&s, 0);
    CHECK(!s.on && s.bail_ticks == 0, "K while bailed: off for good");
    skate_toggle(&s, 0);
    ev = jump(&s, 30, 0.0f, 3.0f, -1, 0, 0);           /* standing still, a lazy 90 */
    CHECK(ev == SKATE_EV_LANDED, "barely moving, a sideways landing is fine");


    /* rolling off a ledge is a drop, not an ollie (found in the real game) */
    skate_toggle(&s, 0); skate_toggle(&s, 0);
    ride(&s, 100, 1, 0.0f, NULL, NULL);
    int score0 = s.score, combo0 = s.combo;
    skate_tick(&s, 0, 0, 0, -0.07f, 1000, 0, 0, 0, &mx, &mz);   /* off the edge, already falling */
    for (int i = 0; i < 10; i++) skate_tick(&s, 0, 0, 0, -0.3f, 1000, 0, 0, 0, &mx, &mz);
    ev = skate_tick(&s, 0, 0, 1, 0.0f, 1000, 0, 0, 0, &mx, &mz);
    CHECK(ev == SKATE_EV_LANDED && s.score == score0 && s.combo == combo0, "a drop off a ledge lands, scores nothing, keeps the combo as it was");
    CHECK(s.speed > 1.0f, "and keeps rolling");


    /* Z pressed with Space (still on the ground) flips at the ollie; too early, it is forgotten */
    skate_toggle(&s, 0); skate_toggle(&s, 0);
    ride(&s, 100, 1, 0.0f, NULL, NULL);
    skate_flip(&s, 0);
    ev = jump(&s, 40, 0.0f, 0.0f, -1, 0, 0);
    CHECK(ev == SKATE_EV_LANDED && strstr(s.text, "OLLIE + KICKFLIP") != NULL, "Z with Space: the flip starts at the ollie");
    skate_flip(&s, 1);
    ride(&s, 30, 1, 0.0f, NULL, NULL);          /* longer than the 24-tick window */
    ev = jump(&s, 40, 0.0f, 0.0f, -1, 0, 0);
    CHECK(ev == SKATE_EV_LANDED && strstr(s.text, "HEELFLIP") == NULL, "Z long before the ollie is forgotten");

    /* a wall takes the speed */
    s.speed = 1.0f;
    skate_blocked(&s, 1.0f, 0.1f);
    CHECK(fabsf(s.speed - 0.1f) < 1e-6f, "blocked by a wall: the speed is what got through");
    skate_blocked(&s, 1.0f, 0.9f);
    CHECK(fabsf(s.speed - 0.1f) < 1e-6f, "a graze does not");

    /* the PARENA rules directly */
    CHECK(skate_landing(0, 0, 1000) == 0 && skate_landing(170, 0, 1000) == 1 && skate_landing(90, 0, 1000) == 2, "landing: clean, switch, sideways");
    CHECK(skate_landing(0, 3, 1000) == 2, "landing mid-flip");
    CHECK(skate_spin_halves(149) == 0 && skate_spin_halves(150) == 1 && skate_spin_halves(330) == 2 && skate_spin_halves(510) == 3, "spin: 180 / 360 / 540 counted 30 short");
    CHECK(skate_combo_mult(0) == 1 && skate_combo_mult(9) == 5, "combo x1..x5");
    CHECK(skate_slope_accel_milli(0, 1000) == 0 && skate_slope_accel_milli(1000, 1000) == 0, "no surface, or flat: no hill");
    CHECK(skate_slope_accel_milli(750, 1000) > 0 && skate_slope_accel_milli(750, -1000) < 0, "down gains, up loses");

    printf("skate: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
