/* airplane_flight_test.c -- the C-110's aerodynamics, driven through airplane_simulate_step with the real
 * PARENA mod installed. This is the verification the design asks for (make test-airplane-flight): it has
 * to ROLL before it flies, it cannot fly without airspeed however it is pointed, flaps buy a shorter
 * takeoff, a stall sinks, lift is a v^2 law, and it turns like a heavy aircraft instead of a quad.
 * Build/run: make test-airplane-flight (plain gcc, -Wall, no GL, no display). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "physics.h"
#include "net_sim.h"
#include "airplane_flight.h"
#include "airplane_rules_host.h"

/* physics.h's inline helpers reference these two bot hooks; this test links no bot brain. Same stubs
 * heli_flight_test uses. */
PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* yaw -90 points the nose at +X, so a takeoff run is a distance in x */
static AirplaneState on_runway(void) {
    AirplaneState a;
    airplane_spawn_defaults(&a, 0, 1, 0.0f, 0.0f, -90.0f);
    return a;
}

int main(void) {
    airplane_rules_install();
    map_count = 1;

    /* ---- a bridge deck is a floor: the C-110 sits on it instead of falling to the terrain ---- */
    {
        /* map_geo is a pointer to const Box (each scene owns its own table), so the test points it at
           this little deck for the duration and puts the real table back afterwards. */
        static const Box deck_geo[2] = {
            [0] = { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f, .h = 1.0f, .d = 1.0f },
            [1] = { .x = 0.0f, .y = 10.0f, .z = 0.0f, .w = 400.0f, .h = 20.0f, .d = 200.0f } /* top = 20 */
        };
        const Box *saved_geo = map_geo;
        map_geo = deck_geo;
        map_count = 2;

        AirplaneState a;
        airplane_spawn_defaults(&a, 0, 1, 0.0f, 0.0f, -90.0f);
        CHECK(fabsf(a.y - (20.0f + AF_TAXI_CLEARANCE)) < 0.05f,
              "spawned at y=%.2f, not on the deck top (20.0)", a.y);
        for (int i = 0; i < 240; i++) airplane_simulate_step(&a, 0.0f, 0.0f, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(a.y > 19.0f && a.grounded, "sank through the bridge to y=%.2f (grounded=%d)", a.y, a.grounded);
        printf("PASS: the C-110 rests on a bridge deck (y=%.2f) instead of falling to the terrain\n", a.y);
        map_geo = saved_geo;
        map_count = 1;
    }

    /* ---- full throttle: it rolls, then it flies, and the roll is a real runway ---- */
    {
        AirplaneState a = on_runway();
        int liftoff_tick = -1;
        float liftoff_speed = 0.0f;
        float liftoff_x = 0.0f;
        for (int i = 0; i < 900; i++) {
            airplane_simulate_step(&a, 0.0f, 0.0f, a.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
            if (liftoff_tick < 0 && !a.grounded) {
                liftoff_tick = i;
                liftoff_speed = a.airspeed;
                liftoff_x = a.x;   /* the RUNWAY is where it left the ground, not where it ended up */
            }
        }
        float runway = fabsf(liftoff_x);
        CHECK(liftoff_tick > 40, "it left the ground on tick %d -- an aeroplane needs a roll", liftoff_tick);
        CHECK(liftoff_tick > 0, "it never took off at full throttle");
        CHECK(liftoff_speed > af_liftoff() * 0.9f,
              "liftoff at %.2f units/tick, below the %.2f liftoff speed", liftoff_speed, af_liftoff());
        CHECK(runway > 60.0f && runway < 1200.0f, "impossible takeoff run: %.0f units", runway);
        CHECK(a.y > 1.0f, "\"took off\" but still at y=%.2f", a.y);
        printf("PASS: full throttle rolls %.0f units, liftoff on tick %d at %.2f units/tick\n",
               runway, liftoff_tick, liftoff_speed);
    }

    /* ---- no throttle: no airspeed, no lift, no flight ---- */
    {
        AirplaneState a = on_runway();
        for (int i = 0; i < 600; i++) airplane_simulate_step(&a, 0.0f, 0.0f, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(a.grounded, "an idling aircraft left the ground");
        CHECK(a.airspeed < 0.05f, "an idling aircraft built airspeed: %.2f", a.airspeed);
        printf("PASS: idle stays parked (airspeed %.3f)\n", a.airspeed);
    }

    /* ---- flaps: a lower stall speed, so a shorter runway ---- */
    {
        AirplaneState clean = on_runway();
        AirplaneState flappy = on_runway();
        flappy.flaps = 1;
        int clean_tick = -1, flap_tick = -1;
        for (int i = 0; i < 900; i++) {
            airplane_simulate_step(&clean, 0.0f, 0.0f, clean.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
            airplane_simulate_step(&flappy, 0.0f, 0.0f, flappy.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
            if (clean_tick < 0 && !clean.grounded) clean_tick = i;
            if (flap_tick < 0 && !flappy.grounded) flap_tick = i;
        }
        CHECK(af_stall(1) < af_stall(0), "flaps did not lower the stall speed");
        CHECK(clean_tick > 0 && flap_tick > 0, "one of the two did not take off (%d / %d)", clean_tick, flap_tick);
        CHECK(flap_tick < clean_tick, "flaps did not shorten the takeoff run (%d vs %d)", flap_tick, clean_tick);
        printf("PASS: flaps lift off %d ticks earlier, stall %.1f vs %.1f\n",
               clean_tick - flap_tick, af_stall(1), af_stall(0));
    }

    /* ---- a stall sinks, and the nose falls: pulling back does not save it ---- */
    {
        AirplaneState a = on_runway();
        a.y = 60.0f;
        a.grounded = 0;
        a.vx = 0.02f;                       /* far below the stall speed */
        for (int i = 0; i < 60; i++) airplane_simulate_step(&a, 1.0f, 0.0f, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(a.vy < -0.01f, "a stalled aircraft did not sink: vy=%.3f", a.vy);
        CHECK(a.pitch < 0.0f, "the nose did not fall in a stall: pitch=%.2f", a.pitch);
        printf("PASS: stall sinks (vy %.2f, nose %.1f deg) even with full back stick\n", a.vy, a.pitch);
    }

    /* ---- lift is a v^2 law, and it is exactly zero below the stall ---- */
    {
        int l5 = af_lift_permille(5000, 0, 1000);
        int l6 = af_lift_permille(6000, 0, 1000);
        int held = af_lift_permille(3000, 0, 2000);
        CHECK(l5 > 0 && l6 > 3 * l5, "lift is not quadratic: %d at 5.0 vs %d at 6.0", l5, l6);
        CHECK(held == 0, "below the stall it still made %d permille of lift", held);
        printf("PASS: lift %.0f -> %.0f permille for 5.0 -> 6.0 units/tick, zero below the stall\n",
               (double)l5, (double)l6);
    }

    /* ---- inertia: a heavy aircraft banks into a turn instead of snapping to the stick ---- */
    {
        AirplaneState a = on_runway();
        a.y = 80.0f;
        a.grounded = 0;
        a.vx = 8.0f;
        a.throttle = 1000.0f;
        float yaw0 = a.yaw;
        for (int i = 0; i < 10; i++) airplane_simulate_step(&a, 0.0f, 1.0f, a.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
        float turned = fabsf(norm_yaw_deg(a.yaw - yaw0));
        CHECK(turned > 0.5f, "it will not turn at all: %.2f degrees in 10 ticks", turned);
        CHECK(turned < 40.0f, "it snapped round %.1f degrees in 10 ticks -- no inertia", turned);
        CHECK(a.roll > 5.0f, "it turned without banking (roll %.1f)", a.roll);
        printf("PASS: 10 ticks of full aileron banks %.1f deg and turns %.1f deg\n", a.roll, turned);
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
