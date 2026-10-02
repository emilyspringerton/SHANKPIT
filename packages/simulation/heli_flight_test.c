/* heli_flight_test.c -- the PARENA helicopter flight model (#542) driven through heli_simulate_step.
 * Behavioural checks, not golden numbers: hover holds, collective climbs/descends, the cyclic leans the
 * airframe and the lean (not a push) accelerates it, banking sags without collective, pedals turn the
 * right way, and the pedal/stick are dead on the ground. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "physics.h"
#include "net_sim.h"
#include "heli_rules_host.h"

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static HelicopterState fresh(float y) {
    HelicopterState h; memset(&h, 0, sizeof h);
    h.active = 1; h.occupant_player_id = 1; h.x = 0; h.y = y; h.z = 0; h.yaw = 0; h.collective = 500; h.health = 250;
    return h;
}
static void run(HelicopterState *h, int n) { for (int i = 0; i < n; i++) heli_simulate_step(h, SHANKPIT_NET_FIXED_DT); }

int main(void) {
    heli_rules_install();
    map_count = 1;

    /* hover: lever at the hover setting, sticks centred -> holds altitude (rotor-wash damping only) */
    HelicopterState h = fresh(100.0f); run(&h, 300);
    CHECK(fabsf(h.y - 100.0f) < 0.5f, "hover drifted: y=%.3f", h.y);
    CHECK(fabsf(h.vx) < 1e-3f && fabsf(h.vz) < 1e-3f, "hover moved horizontally");

    /* collective up climbs, down descends */
    h = fresh(100.0f); h.input.ascend = 1; run(&h, 60);
    CHECK(h.y > 105.0f, "no climb with collective up: y=%.2f", h.y);
    CHECK(h.collective > 900.0f, "lever didn't rise: %.0f", h.collective);
    h = fresh(100.0f); h.input.descend = 1; run(&h, 60);
    CHECK(h.y < 95.0f, "no descent with collective down: y=%.2f", h.y);
    /* released, the lever relaxes back to hover */
    h.input.descend = 0; run(&h, 200);
    CHECK(fabsf(h.collective - 500.0f) < 1.0f, "lever didn't return to hover: %.0f", h.collective);

    /* forward stick: airframe leans first, THEN it moves; yaw 0 faces -Z */
    h = fresh(100.0f); h.input.forward = 1.0f; run(&h, 3);
    CHECK(h.att_pitch > 0.5f && h.att_pitch < 24.0f, "pitch lag wrong after 3 ticks: %.2f", h.att_pitch);
    CHECK(fabsf(h.z) < 0.1f, "moved before it leaned: z=%.4f", h.z);
    run(&h, 200);
    CHECK(h.z < -20.0f, "didn't fly forward (-Z): z=%.2f", h.z);
    CHECK(fabsf(h.x) < 0.5f, "forward flight drifted sideways: x=%.2f", h.x);
    CHECK(h.att_pitch > 20.0f, "airframe not leaned in forward flight: %.2f", h.att_pitch);
    CHECK(h.pitch_visual < -20.0f, "pitch_visual should be nose-down (negative): %.2f", h.pitch_visual);
    float vmax = heli_max_hspeed_milli() * 0.001f;
    CHECK(sqrtf(h.vx*h.vx + h.vz*h.vz) <= vmax + 1e-4f, "exceeded top speed");
    /* leaning costs lift: it sags below the start altitude with the lever left at hover */
    CHECK(h.y < 100.0f, "no sag in a leaned forward flight: y=%.2f", h.y);
    /* stick released: the nose comes back up and it bleeds speed (drag + the tilted thrust reverses) */
    h.input.forward = 0.0f; run(&h, 400);
    CHECK(fabsf(h.att_pitch) < 0.5f, "didn't level: %.2f", h.att_pitch);

    /* reverse stick flies backward */
    h = fresh(100.0f); h.input.forward = -1.0f; run(&h, 200);
    CHECK(h.z > 20.0f, "didn't fly backward: z=%.2f", h.z);

    /* strafe stick (E = +1 = right): +X at yaw 0, banks right (roll_visual negative) */
    h = fresh(100.0f); h.input.strafe = 1.0f; run(&h, 200);
    CHECK(h.x > 20.0f, "right stick didn't go right (+X): x=%.2f", h.x);
    CHECK(h.roll_visual < -20.0f, "roll_visual should bank right (negative): %.2f", h.roll_visual);

    /* pedal right (D = +1) turns RIGHT: yaw decreases (sim yaw + = left); rate lags then saturates */
    h = fresh(100.0f); h.input.yaw = 1.0f; run(&h, 5);
    float early = 360.0f - h.yaw;          /* yaw starts at 0 and wraps below it when turning right */
    CHECK(early > 0.0f && early < 5.0f, "yaw rate didn't lag the pedal / turned left: %.2f deg right after 5 ticks (yaw=%.2f)", early, h.yaw);
    run(&h, 100);
    CHECK(h.yaw_rate > 1.5f && h.yaw_rate <= 2.0f, "yaw rate didn't saturate near 2 deg/tick: %.3f", h.yaw_rate);
    /* then forward after a right turn goes the way the nose points */
    h.input.yaw = 0.0f; h.input.forward = 1.0f; run(&h, 200);
    float fx = -sinf(h.yaw * 0.0174533f), fz = -cosf(h.yaw * 0.0174533f);
    float vs = sqrtf(h.vx*h.vx + h.vz*h.vz);
    CHECK(vs > 0.5f && (h.vx * fx + h.vz * fz) / vs > 0.97f, "velocity not along the nose: dot=%.3f", (h.vx*fx + h.vz*fz) / (vs > 0 ? vs : 1));

    /* on the skids sticks/pedals are dead */
    h = fresh(20.0f); h.input.descend = 1;                  /* pilot lowers the lever and sets it down */
    run(&h, 300);
    CHECK(h.grounded, "never settled on the skids (y=%.2f)", h.y);
    h.input.forward = 1.0f; h.input.yaw = 1.0f; run(&h, 100);
    CHECK(fabsf(h.att_pitch) < 0.01f && fabsf(h.yaw_rate) < 0.01f, "leaned/turned on the ground");

    /* buggy: the body chases the camera heading (warthog style) -- driving, coasting and reversing */
    {
        float cams[] = {90.0f, 270.0f, 179.0f, 30.0f};
        for (int ci = 0; ci < 4; ci++) {
            for (int mode = 0; mode < 3; mode++) {     /* 0 forward throttle, 1 coasting from speed, 2 reversing */
                BuggyState b; memset(&b, 0, sizeof b);
                b.active = 1; b.grounded = 1; b.y = 1.0f; b.yaw = 0.0f;
                float yr = 0.0f;
                if (mode == 1) { b.vx = sinf(yr) * 3.0f; b.vz = -cosf(yr) * 3.0f; }
                if (mode == 2) { b.vx = -sinf(yr) * 1.0f; b.vz = cosf(yr) * 1.0f; }
                float throttle = mode == 0 ? 1.0f : (mode == 2 ? -1.0f : 0.0f);
                for (int i = 0; i < 400; i++) {
                    float intent = buggy_chase_steer_intent(&b, throttle, cams[ci]);
                    simulate_buggy_state(&b, throttle, intent, SHANKPIT_NET_FIXED_DT, 1);
                }
                float err = norm_yaw_deg(cams[ci] - b.yaw); if (err > 180.0f) err -= 360.0f;
                CHECK(fabsf(err) < 8.0f, "buggy didn't chase the camera (mode %d, cam %.0f): yaw=%.1f err=%.1f", mode, cams[ci], b.yaw, err);
            }
        }
        /* parked with no throttle: it does not spin in place chasing the mouse */
        BuggyState b; memset(&b, 0, sizeof b); b.active = 1; b.grounded = 1; b.y = 1.0f;
        CHECK(buggy_chase_steer_intent(&b, 0.0f, 120.0f) == 0.0f, "parked buggy steers");
    }

    if (fails) { printf("heli_flight_test: %d FAILED\n", fails); return 1; }
    printf("heli_flight_test: all checks passed\n");
    return 0;
}
