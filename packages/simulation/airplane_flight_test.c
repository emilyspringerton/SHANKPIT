/* airplane_flight_test.c -- the C-110's aerodynamics, driven through airplane_simulate_step with the real
 * PARENA mod installed. This is the verification the design asks for (make test-airplane-flight): it has
 * to ROLL before it flies, it cannot fly without airspeed however it is pointed, flaps buy a shorter
 * takeoff, a stall sinks, lift is a v^2 law, and it turns like a heavy aircraft instead of a quad.
 * Build/run: make test-airplane-flight (plain gcc, -Wall, no GL, no display).
 *
 * 2026-10-31, the taxi/roll rewrite: the founder's report was "it wont let you taxi without like jumping
 * so it loses traction with the ground" and "when you get it in the air ... its like floppy", with A
 * asked for as the rudder and J/L (and the arrows) as a barrel roll. The three checks at the bottom are
 * that report turned into assertions: the wheels stay planted through a whole taxi, the rudder actually
 * steers on the ground, and a commanded barrel roll goes all the way round without changing heading. */
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
        for (int i = 0; i < 240; i++) airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
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
            airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
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
        for (int i = 0; i < 600; i++) airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
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
            airplane_simulate_step(&clean, 0.0f, 0.0f, 0, clean.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
            airplane_simulate_step(&flappy, 0.0f, 0.0f, 0, flappy.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
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
        for (int i = 0; i < 60; i++) airplane_simulate_step(&a, 1.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
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
        for (int i = 0; i < 10; i++) airplane_simulate_step(&a, 0.0f, 1.0f, 0, a.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
        float turned = fabsf(norm_yaw_deg(a.yaw - yaw0));
        CHECK(turned > 0.5f, "it will not turn at all: %.2f degrees in 10 ticks", turned);
        CHECK(turned < 40.0f, "it snapped round %.1f degrees in 10 ticks -- no inertia", turned);
        CHECK(a.roll > 5.0f, "it turned without banking (roll %.1f)", a.roll);
        printf("PASS: 10 ticks of full rudder banks %.1f deg and turns %.1f deg\n", a.roll, turned);
    }

    /* ---- TAXI TRACTION: a whole run below the liftoff speed keeps the wheels on the ground ---- */
    {
        /* Throttle 500 settles at ~6.0 units/tick, which is just under the speed where the wing carries
           the aircraft, so this is a taxi and not a takeoff. Before the rewrite every tick nudged y up
           past the ground (the stall rule drove the nose down, the clamp shoved the hull back up) and
           the gear lost the surface. Now the gear pins y for the whole run and the tyres stay planted. */
        AirplaneState a = on_runway();
        float y0 = a.y;
        int left_ground = 0;
        float max_dev = 0.0f;
        for (int i = 0; i < 600; i++) {
            airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
            a.throttle = 500.0f;   /* hold the lever: this test is the roll, not the lever */
            if (!a.grounded) left_ground++;
            float dev = fabsf(a.y - y0);
            if (dev > max_dev) max_dev = dev;
        }
        CHECK(left_ground == 0, "the aircraft left the ground %d ticks during a taxi -- it is skipping", left_ground);
        CHECK(max_dev < 0.02f, "the hull wandered %.3f units vertically while taxiing on flat ground", max_dev);
        CHECK(a.airspeed > 4.0f, "it never built taxi speed (%.2f) -- no traction to accelerate", a.airspeed);
        printf("PASS: 600 ticks of taxi stay on the gear (y drift %.3f, airspeed %.2f)\n", max_dev, a.airspeed);
    }

    /* ---- RUDDER: A/D steers on the ground, which is what a nosewheel is for ---- */
    {
        AirplaneState a = on_runway();
        for (int i = 0; i < 120; i++) {
            airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
            a.throttle = 500.0f;
        }
        float yaw0 = a.yaw;
        /* cam_yaw deliberately left pointing where it was: the old tiller was the ONLY steering, so a
           test that passed only because the camera followed would prove nothing about the rudder. */
        for (int i = 0; i < 20; i++) airplane_simulate_step(&a, 0.0f, 1.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        float steered = fabsf(norm_yaw_deg(a.yaw - yaw0));
        CHECK(steered > 3.0f, "full rudder on the ground turned it only %.2f degrees", steered);
        CHECK(a.grounded, "the rudder lifted it off the ground");
        printf("PASS: rudder steers on the ground (%.1f deg in 20 ticks, still grounded)\n", steered);
    }

    /* ---- BARREL ROLL: J/L and the arrows command a full 360, not a 60-degree bank ---- */
    {
        AirplaneState a = on_runway();
        a.y = 90.0f;
        a.grounded = 0;
        a.vx = 8.0f;
        a.throttle = 1000.0f;
        float yaw0 = a.yaw;
        float prev = a.roll;
        float travelled = 0.0f;
        float y_max = a.y, y_min = a.y;
        for (int i = 0; i < 100; i++) {
            airplane_simulate_step(&a, 0.0f, 0.0f, 1, a.yaw, 1, 0, SHANKPIT_NET_FIXED_DT);
            float d = a.roll - prev;
            if (d < -180.0f) d += 360.0f;     /* the wrap at +/-180 is one continuous roll, not a reversal */
            if (d > 180.0f) d -= 360.0f;
            travelled += d;
            prev = a.roll;
            if (a.y > y_max) y_max = a.y;
            if (a.y < y_min) y_min = a.y;
        }
        float heading = fabsf(norm_yaw_deg(a.yaw - yaw0));
        CHECK(travelled > 320.0f, "a commanded barrel roll only rolled %.0f degrees", travelled);
        CHECK(fabsf(a.roll) < 12.0f, "the roll did not come back to level (%.1f deg)", a.roll);
        CHECK(heading < 3.0f, "a barrel roll changed heading by %.1f degrees -- it is a corkscrew, not a turn", heading);
        CHECK((y_max - y_min) < 25.0f, "it gained/lost %.1f units of height through one barrel roll", y_max - y_min);
        printf("PASS: barrel roll travels %.0f deg, ends at %.1f deg roll, heading drift %.2f, height spread %.1f\n",
               travelled, a.roll, heading, y_max - y_min);
    }

    /* ---- W IS THE THROTTLE ON THE WHEELS: taxi the rocket-car way, no lever ritual ---- */
    {
        AirplaneState a = on_runway();
        float x0 = a.x;
        for (int i = 0; i < 120; i++) airplane_simulate_step(&a, 1.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(a.throttle > 500.0f, "holding W wound the throttle up to only %.0f", a.throttle);
        CHECK(fabsf(a.x - x0) > 30.0f, "holding W moved it %.1f units -- it will not taxi", fabsf(a.x - x0));
        CHECK(a.airspeed > 1.0f, "holding W built only %.2f units/tick", a.airspeed);
        CHECK(a.grounded, "W on the wheels lifted it off the ground");
        float coast_speed = a.airspeed;
        /* release: the engine falls back to idle and it coasts to a stop, like the car does */
        for (int i = 0; i < 240; i++) airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(a.grounded, "it took off from a released throttle while taxiing");
        CHECK(a.airspeed < coast_speed, "it did not coast down after the throttle was released");
        printf("PASS: W taxis it (%.0f throttle, %.1f units rolled, coasts %.2f -> %.2f)\n",
               a.throttle, fabsf(a.x - x0), coast_speed, a.airspeed);
    }

    /* ---- BOUNDED CLIMB: full throttle must not balloon the way it used to ---- */
    {
        AirplaneState a = on_runway();
        a.y = 80.0f;
        a.grounded = 0;
        a.vx = 8.0f;
        a.throttle = 1000.0f;
        float y0 = a.y;
        for (int i = 0; i < 600; i++) airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(a.vy < 0.5f, "ten seconds of full throttle left it climbing at %.2f units/tick", a.vy);
        CHECK(a.y - y0 < 200.0f, "ten seconds of full throttle gained %.0f units -- it is ballooning", a.y - y0);
        printf("PASS: full throttle settles into a %.2f units/tick climb (%.0f units in 10 s)\n",
               a.vy, a.y - y0);
    }

    /* ---- TRIM: release the stick and it settles back to level ---- */
    {
        AirplaneState a = on_runway();
        a.y = 80.0f;
        a.grounded = 0;
        a.vx = 8.0f;
        a.throttle = 1000.0f;
        for (int i = 0; i < 30; i++) airplane_simulate_step(&a, 1.0f, 1.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(a.pitch > 20.0f && a.roll > 20.0f, "the stick did not bite (pitch %.1f roll %.1f)", a.pitch, a.roll);
        for (int i = 0; i < 120; i++) airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(fabsf(a.pitch) < 8.0f, "pitch stayed at %.1f deg with the stick centred -- it is floppy", a.pitch);
        CHECK(fabsf(a.roll) < 12.0f, "roll stayed at %.1f deg with the stick centred -- it is floppy", a.roll);
        printf("PASS: centred stick trims back to level (pitch %.1f, roll %.1f)\n", a.pitch, a.roll);
    }

    /* ---- RATE, NOT ANGLE: the turn stops when the pedals are released (Bedrock Racers' jets) ---- */
    {
        AirplaneState a = on_runway();
        a.y = 80.0f; a.grounded = 0; a.vx = 8.0f; a.throttle = 1000.0f;
        for (int i = 0; i < 30; i++) airplane_simulate_step(&a, 0.0f, 1.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        float turned = fabsf(norm_yaw_deg(a.yaw - (-90.0f)));
        float held = a.yaw;
        for (int i = 0; i < 60; i++) airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        float drift = fabsf(norm_yaw_deg(a.yaw - held));
        CHECK(turned > 3.0f, "full rudder turned it only %.1f deg", turned);
        CHECK(drift < 4.0f, "it kept turning %.1f deg after the rudder was released", drift);
        CHECK(fabsf(a.roll) < 20.0f, "the bank did not follow the turn back to level (roll %.1f)", a.roll);
        printf("PASS: the turn stops on release (%.1f deg held, %.1f deg drift, roll %.1f)\n",
               turned, drift, a.roll);
    }

    /* ---- GRIP: it flies where the nose points, not sideways along its old heading ---- */
    {
        AirplaneState a = on_runway();
        a.y = 80.0f; a.grounded = 0; a.vx = 9.0f; a.throttle = 1000.0f;
        for (int i = 0; i < 40; i++) airplane_simulate_step(&a, 0.0f, 1.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        for (int i = 0; i < 60; i++) airplane_simulate_step(&a, 0.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        float r = -a.yaw * 0.0174532925f;
        float fwd = a.vx * sinf(r) + a.vz * -cosf(r);
        float lat = a.vx * cosf(r) + a.vz * sinf(r);
        float slip = fabsf(atan2f(lat, fwd)) * 57.29578f;
        CHECK(slip < 5.0f, "it is still sliding %.1f deg off its nose after a turn", slip);
        printf("PASS: it tracks its nose out of a turn (%.1f deg of slip)\n", slip);
    }

    /* ---- W/S IS THE ELEVATOR: nose up, then nose down, both rate-commanded ---- */
    {
        AirplaneState a = on_runway();
        a.y = 80.0f; a.grounded = 0; a.vx = 9.0f; a.throttle = 1000.0f;
        for (int i = 0; i < 20; i++) airplane_simulate_step(&a, 1.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        float up = a.pitch;
        CHECK(up > 10.0f, "W only pitched the nose up to %.1f deg", up);
        for (int i = 0; i < 40; i++) airplane_simulate_step(&a, -1.0f, 0.0f, 0, a.yaw, 0, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(a.pitch < 0.0f, "S did not point the nose down (%.1f deg)", a.pitch);
        printf("PASS: W/S pitches the nose (up to %.1f, then down to %.1f)\n", up, a.pitch);
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
