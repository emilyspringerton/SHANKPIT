/* airplane_flight_test.c -- the C-110's flight model, driven through airplane_step with the real PARENA
 * mod installed (make test-airplane-flight; plain gcc, no GL, no display).
 *
 * FLIGHT-0/1 (founder 2026-11-24: "the PROP plane is super jank ... behave more like a real flight
 * simulator, its super floppy"): the older checks (it must ROLL before it flies, no lift below the stall,
 * flaps shorten the takeoff, lift is v^2, barrel roll, W taxis) are kept; the new ones are the
 * complaint turned into assertions -- centred stick returns level and stops turning, a step input settles
 * without the rate flipping sign, a bank turns it the RIGHT way by the right amount, a stall is
 * recoverable, assist vs sim, and the keyboard/gamepad shaping and 3-slot rebinding. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "physics.h"
#include "net_sim.h"
#include "airplane_flight.h"
#include "airplane_rules_host.h"
#include "airplane_input.h"

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define DT SHANKPIT_NET_FIXED_DT

/* yaw -90 points the nose at +X, so a takeoff run is a distance in x */
static AirplaneState on_runway(void) {
    AirplaneState a;
    airplane_spawn_defaults(&a, 0, 1, 0.0f, 0.0f, -90.0f);
    return a;
}
static AirplaneState in_air(float speed, float throttle) {
    AirplaneState a = on_runway();
    a.y = 80.0f; a.grounded = 0; a.vx = speed; a.throttle = throttle;
    return a;
}
static AirplaneControls ctl(float pitch, float roll, float rudder, int assist) {
    AirplaneControls c;
    airplane_controls_neutral(&c);
    c.pitch = pitch; c.roll = roll; c.rudder = rudder; c.assist = assist;
    return c;
}
static float yaw_delta(float to, float from) {   /* signed shortest, degrees; negative = turned RIGHT */
    float d = norm_yaw_deg(to - from);
    return d > 180.0f ? d - 360.0f : d;
}
/* a takeoff run: lever to full, rotate with back stick at rotation speed (sim) or let assist do it */
static int takeoff_tick(AirplaneState *a, int assist, int ticks, float *liftoff_x, float *liftoff_speed) {
    int t = -1;
    for (int i = 0; i < ticks; i++) {
        AirplaneControls c = ctl(0, 0, 0, assist);
        c.throttle_set = 1.0f;
        if (!assist && a->airspeed >= 0.95f * (float)airplane_rotate_speed_milli(a->flaps) * 0.001f) c.pitch = 1.0f;
        airplane_step(a, &c, DT);
        if (t < 0 && !a->grounded) { t = i; if (liftoff_x) *liftoff_x = a->x; if (liftoff_speed) *liftoff_speed = a->airspeed; }
    }
    return t;
}

int main(void) {
    airplane_rules_install();
    map_count = 1;

    /* ---- a bridge deck is a floor ---- */
    {
        static const Box deck_geo[2] = {
            [0] = { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f, .h = 1.0f, .d = 1.0f },
            [1] = { .x = 0.0f, .y = 10.0f, .z = 0.0f, .w = 400.0f, .h = 20.0f, .d = 200.0f }
        };
        const Box *saved_geo = map_geo;
        map_geo = deck_geo; map_count = 2;
        AirplaneState a;
        airplane_spawn_defaults(&a, 0, 1, 0.0f, 0.0f, -90.0f);
        CHECK(fabsf(a.y - (20.0f + AF_TAXI_CLEARANCE)) < 0.05f, "spawned at y=%.2f, not on the deck top", a.y);
        AirplaneControls c = ctl(0, 0, 0, 1);
        for (int i = 0; i < 240; i++) airplane_step(&a, &c, DT);
        CHECK(a.y > 19.0f && a.grounded, "sank through the bridge to y=%.2f", a.y);
        printf("PASS: the C-110 rests on a bridge deck (y=%.2f)\n", a.y);
        map_geo = saved_geo; map_count = 1;
    }

    /* ---- takeoff, SIM: it must roll, be ROTATED with back stick, and only then leave the ground ---- */
    {
        AirplaneState a = on_runway();
        float lx = 0, ls = 0;
        int t = takeoff_tick(&a, 0, 900, &lx, &ls);
        float vr = (float)airplane_rotate_speed_milli(0) * 0.001f;
        CHECK(t > 40, "sim takeoff on tick %d -- an aeroplane needs a roll", t);
        CHECK(ls > vr * 0.9f, "liftoff at %.2f, below rotation speed %.2f", ls, vr);
        CHECK(fabsf(lx) > 60.0f && fabsf(lx) < 1200.0f, "impossible takeoff run: %.0f", lx);
        CHECK(a.y > 1.0f, "\"took off\" but still at y=%.2f", a.y);
        /* with the stick left alone in SIM mode it must NOT hop off on speed alone */
        AirplaneState b = on_runway();
        AirplaneControls c = ctl(0, 0, 0, 0); c.throttle_set = 1.0f;
        for (int i = 0; i < 900; i++) airplane_step(&b, &c, DT);
        CHECK(b.grounded, "sim mode took off with no rotation (the old hop)");
        printf("PASS: sim takeoff rolls %.0f units, rotates, leaves at %.2f; no rotation = no takeoff\n", fabsf(lx), ls);
    }

    /* ---- takeoff, ASSIST: it rotates itself ---- */
    {
        AirplaneState a = on_runway();
        float ls = 0;
        int t = takeoff_tick(&a, 1, 900, NULL, &ls);
        CHECK(t > 40, "assist takeoff on tick %d", t);
        CHECK(a.y > 1.0f, "assist never climbed (y=%.2f)", a.y);
        printf("PASS: assist rotates itself, off the ground on tick %d at %.2f\n", t, ls);
    }

    /* ---- idle parks ---- */
    {
        AirplaneState a = on_runway();
        AirplaneControls c = ctl(0, 0, 0, 1);
        for (int i = 0; i < 600; i++) airplane_step(&a, &c, DT);
        CHECK(a.grounded && a.airspeed < 0.05f, "idle aircraft moved (airspeed %.2f)", a.airspeed);
        printf("PASS: idle stays parked\n");
    }

    /* ---- flaps: lower stall + rotation speed, shorter run ---- */
    {
        AirplaneState clean = on_runway(), flappy = on_runway();
        flappy.flaps = 1;
        int ct = takeoff_tick(&clean, 1, 900, NULL, NULL), ft = takeoff_tick(&flappy, 1, 900, NULL, NULL);
        CHECK(af_stall(1) < af_stall(0), "flaps did not lower the stall speed");
        CHECK(ct > 0 && ft > 0 && ft < ct, "flaps did not shorten the takeoff (%d vs %d)", ft, ct);
        AirplaneState f = on_runway();
        airplane_toggle_flaps(&f);
        CHECK(f.flaps == 1, "toggle did not set flaps");
        airplane_toggle_flaps(&f);
        CHECK(f.flaps == 0, "toggle did not clear flaps");
        printf("PASS: flaps lift off %d ticks earlier\n", ct - ft);
    }

    /* ---- lift is v^2 and zero below the stall ---- */
    {
        int l5 = af_lift_permille(5000, 0, 1000), l6 = af_lift_permille(6000, 0, 1000);
        CHECK(l5 > 0 && l6 > 3 * l5, "lift not quadratic (%d, %d)", l5, l6);
        CHECK(af_lift_permille(3000, 0, 2000) == 0, "lift below the stall");
        printf("PASS: lift %d -> %d permille, zero below the stall\n", l5, l6);
    }

    /* ---- STALL: warning, nose drops, pulling back does not save it, pushing forward does ---- */
    {
        AirplaneState a = in_air(0.02f, 0.0f);
        AirplaneControls pull = ctl(1.0f, 0, 0, 0);
        for (int i = 0; i < 60; i++) airplane_step(&a, &pull, DT);
        CHECK(a.vy < -0.01f, "stalled aircraft did not sink (vy %.3f)", a.vy);
        CHECK(a.pitch < 0.0f, "nose did not fall (pitch %.2f)", a.pitch);
        CHECK(a.stall_warn == 1000, "no stall warning at the stall (%d)", a.stall_warn);
        /* recover: lever to full, stick FORWARD, wings level */
        AirplaneControls rec = ctl(-0.3f, 0, 0, 0); rec.throttle_set = 1.0f;
        a.y = 400.0f;
        int back = -1;
        for (int i = 0; i < 900; i++) {
            airplane_step(&a, &rec, DT);
            if (a.airspeed > af_stall(0) * 1.3f) { back = i; break; }
        }
        CHECK(back >= 0, "never regained flying speed (airspeed %.2f)", a.airspeed);
        AirplaneControls lvl = ctl(0, 0, 0, 1); lvl.throttle_set = 1.0f;
        for (int i = 0; i < 400; i++) airplane_step(&a, &lvl, DT);
        CHECK(a.stall_warn == 0, "still stalling after recovery (warn %d)", a.stall_warn);
        CHECK(a.vy > -0.05f && fabsf(a.pitch) < 3.0f, "did not settle level after the recovery (vy %.2f pitch %.1f)", a.vy, a.pitch);
        printf("PASS: stall sinks, warns, and recovers in %d ticks with the stick forward (then levels: pitch %.1f)\n", back, a.pitch);
    }

    /* ---- ASSIST: centred stick returns level and STOPS TURNING (no drift) ---- */
    {
        AirplaneState a = in_air(9.0f, 600.0f);
        a.roll = 35.0f; a.pitch = 12.0f;
        AirplaneControls c = ctl(0, 0, 0, 1); c.throttle_set = 0.6f;
        for (int i = 0; i < 360; i++) airplane_step(&a, &c, DT);
        float y_before = a.yaw;
        for (int i = 0; i < 90; i++) airplane_step(&a, &c, DT);
        float drift = fabsf(yaw_delta(a.yaw, y_before));
        CHECK(fabsf(a.roll) < 1.0f, "assist left bank %.2f with the stick centred", a.roll);
        CHECK(fabsf(a.pitch) < 1.0f, "assist left pitch %.2f with the stick centred", a.pitch);
        CHECK(fabsf(a.yaw_rate) < 0.02f, "still turning at %.3f deg/tick", a.yaw_rate);
        CHECK(drift < 1.0f, "heading drifted %.2f deg in 1.5 s with a centred stick", drift);
        printf("PASS: assist levels (roll %.2f pitch %.2f) and stops turning (drift %.2f deg)\n", a.roll, a.pitch, drift);
    }

    /* ---- STEP INPUT settles with no overshoot oscillation (rate never flips sign past the noise) ---- */
    {
        for (int axis = 0; axis < 2; axis++) {
            AirplaneState a = in_air(9.0f, 700.0f);
            AirplaneControls c = axis == 0 ? ctl(0, 0.5f, 0, 1) : ctl(0.5f, 0, 0, 1);
            c.throttle_set = 0.7f;
            float peak = 0.0f, last_rate = 0.0f; int flips = 0; float maxv = 0.0f;
            for (int i = 0; i < 260; i++) {
                airplane_step(&a, &c, DT);
                float v = axis == 0 ? a.roll : a.pitch, rate = axis == 0 ? a.roll_rate : a.pitch_rate;
                if (v > maxv) maxv = v;
                if (fabsf(rate) > peak) peak = fabsf(rate);
                if (rate < -0.01f && last_rate > 0.01f) flips++;
                if (fabsf(rate) > 0.01f) last_rate = rate;
            }
            float final_v = axis == 0 ? a.roll : a.pitch, target = axis == 0 ? 22.5f : 12.5f;
            CHECK(flips == 0, "%s rate flipped sign %d times -- it oscillates", axis ? "pitch" : "roll", flips);
            CHECK(maxv < target + 1.0f, "%s overshot to %.2f (target %.1f)", axis ? "pitch" : "roll", maxv, target);
            CHECK(fabsf(final_v - target) < 2.5f, "%s settled at %.2f, target %.1f", axis ? "pitch" : "roll", final_v, target);
            printf("PASS: %s step settles at %.1f (target %.1f), peak %.2f, no overshoot\n", axis ? "pitch" : "roll", final_v, target, maxv);
        }
    }

    /* ---- BANK-TO-TURN: right bank turns it RIGHT, at ~g*tan(bank)/v, and a flat wing does not turn ---- */
    {
        AirplaneState a = in_air(9.0f, 600.0f);
        AirplaneControls c = ctl(0, 1.0f, 0, 1); c.throttle_set = 0.6f;
        for (int i = 0; i < 120; i++) airplane_step(&a, &c, DT);
        float expect = -(float)airplane_turn_rate_milli((int)(a.roll * 1000.0f), (int)(a.airspeed * 1000.0f)) * 0.001f * -1.0f;
        expect = -fabsf(expect);
        CHECK(a.roll > 40.0f && a.roll < 46.0f, "full assist stick banked to %.1f, not 45", a.roll);
        CHECK(a.yaw_rate < -0.3f, "a right bank did not turn right (yaw_rate %.3f; yaw grows to the LEFT here)", a.yaw_rate);
        CHECK(fabsf(a.yaw_rate - expect) < 0.12f * fabsf(expect), "turn rate %.3f vs law %.3f", a.yaw_rate, expect);
        AirplaneState l = in_air(9.0f, 600.0f);
        AirplaneControls cl = ctl(0, -1.0f, 0, 1); cl.throttle_set = 0.6f;
        for (int i = 0; i < 120; i++) airplane_step(&l, &cl, DT);
        CHECK(l.yaw_rate > 0.3f && l.roll < -40.0f, "left bank did not turn left (yaw_rate %.3f roll %.1f)", l.yaw_rate, l.roll);
        /* wings level + rudder only: a trim, not a turn */
        AirplaneState f = in_air(9.0f, 600.0f);
        AirplaneControls cf = ctl(0, 0, 1.0f, 1); cf.throttle_set = 0.6f;
        float y0 = f.yaw;
        for (int i = 0; i < 60; i++) airplane_step(&f, &cf, DT);
        CHECK(fabsf(f.roll) < 0.5f, "rudder banked the aeroplane (%.2f)", f.roll);
        CHECK(fabsf(yaw_delta(f.yaw, y0)) < 20.0f, "rudder alone swung the nose %.1f deg", yaw_delta(f.yaw, y0));
        printf("PASS: bank-to-turn: right %.3f deg/tick (law %.3f), left %.3f, rudder-only %.1f deg\n", a.yaw_rate, expect, l.yaw_rate, yaw_delta(f.yaw, y0));
    }

    /* ---- SIM mode: stick is a RATE, centred stick HOLDS the attitude (weak stability only) ---- */
    {
        AirplaneState a = in_air(9.0f, 600.0f);
        AirplaneControls c = ctl(0, 0.6f, 0, 0); c.throttle_set = 0.6f;
        for (int i = 0; i < 20; i++) airplane_step(&a, &c, DT);
        float banked = a.roll;
        CHECK(banked > 10.0f, "sim stick did not bank (%.1f)", banked);
        c = ctl(0, 0, 0, 0); c.throttle_set = 0.6f;
        for (int i = 0; i < 40; i++) airplane_step(&a, &c, DT);
        CHECK(a.roll > banked * 0.7f, "sim mode auto-levelled (%.1f from %.1f) -- that is assist's job", a.roll, banked);
        CHECK(fabsf(a.roll) < 75.01f, "bank exceeded the 75 deg stop");
        float p0 = a.pitch;
        for (int i = 0; i < 40; i++) airplane_step(&a, &c, DT);
        CHECK(fabsf(a.pitch - p0) < 3.0f, "sim pitch drifted %.1f with the stick centred", a.pitch - p0);
        printf("PASS: sim mode holds the bank it was given (%.1f -> %.1f) and its pitch\n", banked, a.roll);
    }

    /* ---- PITCH: back stick climbs and bleeds speed, forward stick dives and gains it ---- */
    {
        AirplaneState a = in_air(9.0f, 500.0f);
        AirplaneControls up = ctl(1.0f, 0, 0, 1); up.throttle_set = 0.5f;
        float s0 = a.vx, y0 = a.y;
        for (int i = 0; i < 120; i++) airplane_step(&a, &up, DT);
        CHECK(a.pitch > 15.0f && a.y > y0 + 5.0f, "back stick did not climb (pitch %.1f, +%.1f)", a.pitch, a.y - y0);
        CHECK(a.airspeed < s0 - 0.5f, "a climb did not cost speed (%.2f from %.2f)", a.airspeed, s0);
        AirplaneState d = in_air(7.0f, 500.0f);
        d.y = 600.0f;
        AirplaneControls dn = ctl(-1.0f, 0, 0, 1); dn.throttle_set = 0.5f;
        float d0 = d.y, ds = d.vx;
        for (int i = 0; i < 45; i++) airplane_step(&d, &dn, DT);
        CHECK(d.y < d0 - 3.0f && d.pitch < -10.0f, "forward stick did not dive (pitch %.1f)", d.pitch);
        CHECK(d.airspeed > ds, "a dive did not gain speed (%.2f from %.2f)", d.airspeed, ds);
        printf("PASS: climb costs speed (%.2f -> %.2f), dive gains it (%.2f -> %.2f)\n", s0, a.airspeed, ds, d.airspeed);
    }

    /* ---- LEVEL cruise holds altitude with the stick centred (no balloon, no sink) ---- */
    {
        AirplaneState a = in_air(10.0f, 800.0f);
        AirplaneControls c = ctl(0, 0, 0, 1); c.throttle_set = 0.8f;
        float y0 = a.y;
        for (int i = 0; i < 600; i++) airplane_step(&a, &c, DT);
        CHECK(fabsf(a.y - y0) < 6.0f, "level cruise wandered %.1f units in 10 s", a.y - y0);
        printf("PASS: level cruise holds altitude (%.2f units in 10 s)\n", a.y - y0);
    }

    /* ---- taxi traction + nosewheel + brakes ---- */
    {
        AirplaneState a = on_runway();
        float y0 = a.y, max_dev = 0.0f; int off = 0;
        AirplaneControls c = ctl(0, 0, 0, 1); c.throttle_set = 0.5f;
        for (int i = 0; i < 600; i++) {
            airplane_step(&a, &c, DT);
            if (!a.grounded) off++;
            if (fabsf(a.y - y0) > max_dev) max_dev = fabsf(a.y - y0);
        }
        CHECK(off == 0 && max_dev < 0.02f, "taxi skipped (%d ticks off, %.3f dev)", off, max_dev);
        CHECK(a.airspeed > 4.0f, "taxi built no speed (%.2f)", a.airspeed);
        /* nosewheel: right stick/pedal turns RIGHT (yaw decreases), still on the wheels */
        float yaw0 = a.yaw;
        AirplaneControls st = ctl(0, 0, 1.0f, 1); st.throttle_set = 0.5f;
        for (int i = 0; i < 20; i++) airplane_step(&a, &st, DT);
        CHECK(yaw_delta(a.yaw, yaw0) < -2.0f && a.grounded, "right pedal on the ground did not turn right (%.2f)", yaw_delta(a.yaw, yaw0));
        /* brakes: lever closed + brake stops it quickly; without brake it rolls much further */
        AirplaneState b1 = a, b2 = a;
        AirplaneControls idle = ctl(0, 0, 0, 1); idle.throttle_set = 0.0f;
        AirplaneControls brk = idle; brk.brake = 1.0f;
        for (int i = 0; i < 120; i++) { airplane_step(&b1, &idle, DT); airplane_step(&b2, &brk, DT); }
        CHECK(b2.airspeed < 0.3f && b2.airspeed < b1.airspeed * 0.5f, "brakes weak (%.2f vs %.2f coasting)", b2.airspeed, b1.airspeed);
        CHECK(b2.brake_on == 1, "brake_on not reported");
        printf("PASS: taxi stays on the gear (drift %.3f), nosewheel turns right (%.1f deg), brakes stop it (%.2f vs %.2f)\n",
               max_dev, yaw_delta(a.yaw, yaw0), b2.airspeed, b1.airspeed);
    }

    /* ---- BARREL ROLL: unchanged, a corkscrew that does not turn ---- */
    {
        AirplaneState a = in_air(8.0f, 1000.0f);
        a.y = 90.0f;
        float yaw0 = a.yaw, prev = a.roll, travelled = 0, ymax = a.y, ymin = a.y;
        AirplaneControls c = ctl(0, 0, 0, 1); c.throttle_set = 1.0f; c.barrel = 1;
        for (int i = 0; i < 100; i++) {
            airplane_step(&a, &c, DT);
            float d = a.roll - prev;
            if (d < -180.0f) d += 360.0f;
            if (d > 180.0f) d -= 360.0f;
            travelled += d; prev = a.roll;
            if (a.y > ymax) ymax = a.y;
            if (a.y < ymin) ymin = a.y;
        }
        CHECK(travelled > 320.0f, "barrel roll only %.0f deg", travelled);
        CHECK(fabsf(yaw_delta(a.yaw, yaw0)) < 3.0f, "barrel roll turned %.1f deg", yaw_delta(a.yaw, yaw0));
        CHECK(ymax - ymin < 25.0f, "barrel roll height spread %.1f", ymax - ymin);
        c.barrel = 0;
        for (int i = 0; i < 200; i++) airplane_step(&a, &c, DT);
        CHECK(fabsf(a.roll) < 2.0f, "assist did not level after the barrel roll (%.1f)", a.roll);
        printf("PASS: barrel roll %.0f deg, no heading change, levels out after\n", travelled);
    }

    /* ---- GRIP: tracks its nose out of a turn ---- */
    {
        AirplaneState a = in_air(9.0f, 1000.0f);
        AirplaneControls t = ctl(0, 1.0f, 0, 1); t.throttle_set = 1.0f;
        for (int i = 0; i < 40; i++) airplane_step(&a, &t, DT);
        AirplaneControls lv = ctl(0, 0, 0, 1); lv.throttle_set = 1.0f;
        for (int i = 0; i < 160; i++) airplane_step(&a, &lv, DT);
        float r = -a.yaw * 0.0174532925f;
        float fwd = a.vx * sinf(r) + a.vz * -cosf(r), lat = a.vx * cosf(r) + a.vz * sinf(r);
        float slip = fabsf(atan2f(lat, fwd)) * 57.29578f;
        CHECK(slip < 5.0f, "sliding %.1f deg off its nose", slip);
        printf("PASS: tracks its nose out of a turn (%.1f deg slip)\n", slip);
    }

    /* ---- LEGACY signature still maps (W taxis on the wheels, coasts, stays grounded) ---- */
    {
        AirplaneState a = on_runway();
        float x0 = a.x;
        for (int i = 0; i < 120; i++) airplane_simulate_step(&a, 1.0f, 0.0f, 0, a.yaw, 0, 0, DT);
        CHECK(a.throttle > 500.0f && fabsf(a.x - x0) > 30.0f && a.grounded, "legacy W taxi broke (throttle %.0f, moved %.1f)", a.throttle, fabsf(a.x - x0));
        float v = a.airspeed;
        for (int i = 0; i < 240; i++) airplane_simulate_step(&a, 0, 0, 0, a.yaw, 0, 0, DT);
        CHECK(a.grounded && a.airspeed < v, "legacy coast broke");
        printf("PASS: legacy airplane_simulate_step still taxis and coasts\n");
    }

    /* ---- INPUT: dead zone, response curve, invert, key ramp, three binds per action ---- */
    {
        AirplaneInputConfig cfg; airplane_input_config_default(&cfg);
        CHECK(airplane_shape_axis(0.05f, &cfg) == 0.0f, "stick noise inside the dead zone leaked through");
        CHECK(fabsf(airplane_shape_axis(1.0f, &cfg) - 1.0f) < 1e-4f, "full deflection is not 1.0");
        CHECK(airplane_shape_axis(0.5f, &cfg) < 0.5f && airplane_shape_axis(0.5f, &cfg) > 0.0f, "expo curve not gentle near centre");
        CHECK(airplane_shape_axis(-0.6f, &cfg) < 0.0f && fabsf(airplane_shape_axis(-0.6f, &cfg) + airplane_shape_axis(0.6f, &cfg)) < 1e-5f, "shaping not symmetric");
        AirplaneInputConfig lin = cfg; lin.expo = 0.0f; lin.deadzone = 0.0f;
        CHECK(fabsf(airplane_shape_axis(0.5f, &lin) - 0.5f) < 1e-5f, "linear, no dead zone must be identity");

        AirplaneBinds b; airplane_binds_clear(&b);
        enum { KS = 40, K_S = 4, K_ARROW = 20, K_SPARE = 33 };
        airplane_binds_set(&b, AIR_ACT_PITCH_UP, 0, K_S);
        airplane_binds_set(&b, AIR_ACT_PITCH_UP, 1, K_ARROW);
        airplane_binds_set(&b, AIR_ACT_PITCH_UP, 2, K_SPARE);
        CHECK(!airplane_binds_set(&b, AIR_ACT_PITCH_UP, 3, 9), "a fourth bind slot was accepted");
        unsigned char keys[KS]; memset(keys, 0, sizeof(keys));
        AirplaneControls out; int fe = 0;
        AirplaneInputState st; airplane_input_state_init(&st, &cfg);
        for (int slot = 0; slot < 3; slot++) {
            memset(keys, 0, sizeof(keys));
            keys[b.key[AIR_ACT_PITCH_UP][slot]] = 1;
            AirplaneInputState s2; airplane_input_state_init(&s2, &cfg);
            for (int i = 0; i < 12; i++) airplane_keys_to_controls(&s2, &b, keys, KS, &cfg, &out, &fe);
            CHECK(out.pitch > 0.99f, "bind slot %d did not pull back (%.2f)", slot, out.pitch);
        }
        /* ramp: one tick is gentle, and release falls back */
        memset(keys, 0, sizeof(keys)); keys[K_S] = 1;
        airplane_keys_to_controls(&st, &b, keys, KS, &cfg, &out, &fe);
        CHECK(out.pitch > 0.05f && out.pitch < 0.3f, "first key tick is %.2f -- keys are not ramped", out.pitch);
        for (int i = 0; i < 20; i++) airplane_keys_to_controls(&st, &b, keys, KS, &cfg, &out, &fe);
        CHECK(out.pitch > 0.99f, "held key never reached full deflection (%.2f)", out.pitch);
        memset(keys, 0, sizeof(keys));
        for (int i = 0; i < 3; i++) airplane_keys_to_controls(&st, &b, keys, KS, &cfg, &out, &fe);
        CHECK(out.pitch < 0.5f && out.pitch > 0.0f, "release did not ramp back (%.2f)", out.pitch);
        /* flaps edge fires once per press, not every tick */
        airplane_binds_set(&b, AIR_ACT_FLAPS, 0, 7);
        memset(keys, 0, sizeof(keys)); keys[7] = 1;
        int edges = 0;
        for (int i = 0; i < 5; i++) { airplane_keys_to_controls(&st, &b, keys, KS, &cfg, &out, &fe); edges += fe; }
        CHECK(edges == 1, "flaps edge fired %d times for one press", edges);
        /* pad: left stick back = nose up, invert flips it, right stick X = rudder, trigger = lever */
        AirplanePad p; memset(&p, 0, sizeof(p)); p.connected = 1; p.ly = 0.8f; p.rx = 0.9f; p.rt = 0.9f;
        AirplaneControls pc; airplane_controls_neutral(&pc);
        int pf = 0, pa = 0; fe = 0;
        airplane_pad_merge(&st, &p, &cfg, &pc, &fe, &pf, &pa);
        CHECK(pc.pitch > 0.3f && pc.rudder > 0.3f && pc.throttle_up, "pad mapping wrong (pitch %.2f rudder %.2f up %d)", pc.pitch, pc.rudder, pc.throttle_up);
        AirplaneInputConfig inv = cfg; inv.invert_pitch = 1;
        airplane_controls_neutral(&pc);
        airplane_pad_merge(&st, &p, &inv, &pc, &fe, &pf, &pa);
        CHECK(pc.pitch < -0.3f, "invert-Y did not flip the pad pitch (%.2f)", pc.pitch);
        p.ly = 0.05f; p.lx = 0.05f;
        airplane_controls_neutral(&pc);
        airplane_pad_merge(&st, &p, &cfg, &pc, &fe, &pf, &pa);
        CHECK(pc.pitch == 0.0f && pc.roll == 0.0f, "centred pad drifted (%.2f, %.2f)", pc.pitch, pc.roll);
        printf("PASS: input shaping, 3 bind slots, key ramp, flaps edge, pad mapping + invert\n");
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
