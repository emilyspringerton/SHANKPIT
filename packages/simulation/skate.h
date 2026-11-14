/* skate.h -- SHANKPIT's skateboard (founder, 2026-11-14: "can we get shankpit skateboarding working?").
 *
 * K steps on and off. While riding, the board owns the horizontal motion and the engine keeps everything else
 * (gravity, the jump -- which is the ollie -- terrain and box collision):
 *   W pushes, S brakes, nothing pressed rolls (and slows a little); the board turns toward where you look, as fast as
 *   its speed allows (fast boards carve wide); it rolls only along its heading, never sideways.
 *   A hill speeds you up going down and slows you going up (the terrain gradient the crouch-slide already reads).
 *   Space ollies. In the air: Z kickflip, Shift+Z heelflip, hold Ctrl to grab, and turn with the mouse to spin --
 *   every 180 counts. The landing compares where the board points with where it is going: in line is clean,
 *   backwards is switch (you roll away fakie), sideways or mid-flip is a bail: you come off, lose the combo, and are
 *   back on a moment later. Landed tricks score, times the combo (tricks landed in a row, up to x5).
 *
 * Every number and decision is PARENA (PARENA/stdlib/shankpit/skate_rules.prn, generated into skate_rules.c by
 * scripts/gen_skate_rules.sh); this file is the state and its integration. Pure C99, no GL/SDL: the lobby feeds it
 * input and the surface each tick and applies the motion it returns; packages/simulation/skate_test.c drives it.
 *
 * Not done, named: grinds and rails, manuals, box ramps (a slope is terrain only, as for the slide), other players'
 * boards over the network (this is the local player's board), and a board model beyond a deck with four wheels.
 */
#ifndef SHANKPIT_SKATE_H
#define SHANKPIT_SKATE_H

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the generated PARENA rules (skate_rules.c) */
int skate_push_accel_milli(void);
int skate_push_top_milli(void);
int skate_cap_milli(void);
int skate_roll_keep_permille(void);
int skate_brake_milli(void);
int skate_carve_decideg(int speed_milli);
int skate_slope_accel_milli(int ny_permille, int align_permille);
int skate_landing(int angle, int flip_left, int speed_milli);
int skate_spin_halves(int degrees);
int skate_trick_points(int trick);
int skate_combo_mult(int landed_in_a_row);
int skate_flip_ticks(void);
int skate_bail_ticks(void);

enum { SKATE_TRICK_OLLIE = 1, SKATE_TRICK_KICKFLIP = 2, SKATE_TRICK_HEELFLIP = 3, SKATE_TRICK_GRAB = 4, SKATE_TRICK_SPIN = 5 };
enum { SKATE_EV_NONE = 0, SKATE_EV_LANDED = 1, SKATE_EV_SWITCH = 2, SKATE_EV_BAILED = 3, SKATE_EV_BACK_ON = 4 };

typedef struct {
    int on;                 /* riding */
    float heading;          /* degrees, where the board points */
    float speed;            /* units per tick along the motion direction, >= 0 */
    int switch_stance;      /* rolling backwards (fakie): the motion is heading + 180 */
    int airborne;
    float travel_x, travel_z;   /* the motion direction (unit), frozen in the air */
    float last_yaw, air_spin;   /* the rider's yaw last tick, and the spin accumulated this jump (degrees) */
    int flip, flip_left;        /* the flip in progress (KICKFLIP/HEELFLIP), and ticks until it comes round */
    int flip_queued, queue_ticks;   /* Z pressed on the ground just before the ollie: the flip starts at take-off */
    int grab_ticks, grabbed;
    int air_points;             /* tricks done this jump, not yet landed */
    char air_names[64];
    int combo, score, best;
    int bail_ticks;             /* > 0: off the board after a bail, getting back on */
    char text[96];              /* the last thing to show the player */
    int text_ticks;
} Skate;

static inline float skate_wrap180(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

/* The engine's yaw is degrees with forward = (-sin, -cos) -- the same basis shankpit_yaw_basis_from_deg uses. */
static inline void skate_dir(float yaw_deg, float *x, float *z) {
    float r = yaw_deg * 3.14159265f / 180.0f;
    *x = -sinf(r);
    *z = -cosf(r);
}

static inline void skate_say(Skate *s, const char *t) {
    snprintf(s->text, sizeof s->text, "%s", t);
    s->text_ticks = 160;
}

static inline void skate_add_name(Skate *s, const char *n) {
    size_t len = strlen(s->air_names);
    if (len + strlen(n) + 2 < sizeof s->air_names) {
        if (len) strcat(s->air_names, " + ");
        strcat(s->air_names, n);
    }
}

static inline void skate_on(Skate *s, float yaw) {
    s->on = 1; s->heading = yaw; s->speed = 0.0f; s->switch_stance = 0; s->airborne = 0;
    skate_dir(yaw, &s->travel_x, &s->travel_z);
    s->flip = s->flip_left = s->grab_ticks = s->grabbed = s->air_points = 0;
    s->air_names[0] = 0; s->bail_ticks = 0;
}

/* K: step on (facing where you look) or off. */
static inline void skate_toggle(Skate *s, float yaw) {
    if (s->on || s->bail_ticks > 0) {
        s->on = 0; s->bail_ticks = 0; s->combo = 0;
        skate_say(s, "OFF THE BOARD");
    } else {
        skate_on(s, yaw);
        skate_say(s, "SKATE: W PUSH  S BRAKE  MOUSE STEER  SPACE OLLIE  Z KICKFLIP  SHIFT+Z HEELFLIP  CTRL GRAB");
    }
}

/* Z (or Shift+Z): a flip. In the air it starts now; on the ground it is held for 24 ticks (~0.4 s) and starts at the
 * ollie, so Space and Z pressed together flip -- found in the real game, where the two keys arrive on the same frame
 * and the board is still on the ground. Only one flip a jump. */
static inline void skate_flip(Skate *s, int heel) {
    if (!s->on) return;
    if (!s->airborne) { s->flip_queued = heel ? SKATE_TRICK_HEELFLIP : SKATE_TRICK_KICKFLIP; s->queue_ticks = 24; return; }
    if (s->flip) return;
    s->flip = heel ? SKATE_TRICK_HEELFLIP : SKATE_TRICK_KICKFLIP;
    s->flip_left = skate_flip_ticks();
    s->air_points += skate_trick_points(s->flip);
    skate_add_name(s, heel ? "HEELFLIP" : "KICKFLIP");
}

/* One tick. fwd: +1 push, -1 brake. yaw: where the rider looks (degrees). on_ground and vy: the engine's (vy > 0 at
 * take-off is a jump, the ollie; rolling off a ledge is a drop and scores nothing). ny_permille and
 * (down_x, down_z): the surface under the board, as physics.h phys_slide_surface_permille reports it. grab: Ctrl
 * held. Writes the motion for this tick (units) to (*mx, *mz). Returns a SKATE_EV_*. */
static inline int skate_tick(Skate *s, float fwd, float yaw, int on_ground, float vy, int ny_permille, float down_x,
                             float down_z, int grab, float *mx, float *mz) {
    *mx = *mz = 0.0f;
    if (s->text_ticks > 0) s->text_ticks--;
    if (!s->on) {
        if (s->bail_ticks > 0 && --s->bail_ticks == 0) {
            skate_on(s, yaw);
            skate_say(s, "BACK ON THE BOARD");
            return SKATE_EV_BACK_ON;
        }
        return SKATE_EV_NONE;
    }
    int speed_milli = (int)(s->speed * 1000.0f);
    if (s->queue_ticks > 0 && --s->queue_ticks == 0) s->flip_queued = 0;
    if (on_ground && !s->airborne) {
        /* turn toward where the rider looks, no faster than the board carves at this speed */
        float rate = (float)skate_carve_decideg(speed_milli) / 10.0f;
        float want = s->switch_stance ? yaw + 180.0f : yaw;   /* fakie: the rider looks back along the board */
        float d = skate_wrap180(want - s->heading);
        if (d > rate) d = rate;
        if (d < -rate) d = -rate;
        s->heading = skate_wrap180(s->heading + d);
        if (s->switch_stance) skate_dir(s->heading + 180.0f, &s->travel_x, &s->travel_z);
        else skate_dir(s->heading, &s->travel_x, &s->travel_z);
        /* push, brake or roll */
        float top = (float)skate_push_top_milli() / 1000.0f;
        if (fwd > 0.1f) {
            if (s->speed < top) { s->speed += (float)skate_push_accel_milli() / 1000.0f; if (s->speed > top) s->speed = top; }
        } else if (fwd < -0.1f) {
            s->speed -= (float)skate_brake_milli() / 1000.0f;
            if (s->speed < 0.0f) s->speed = 0.0f;
        } else {
            s->speed *= (float)skate_roll_keep_permille() / 1000.0f;
        }
        /* the hill, along the way the board is rolling */
        int align = (int)((s->travel_x * down_x + s->travel_z * down_z) * 1000.0f);
        s->speed += (float)skate_slope_accel_milli(ny_permille, align) / 1000.0f;
        if (s->speed < 0.0f) s->speed = 0.0f;
        float cap = (float)skate_cap_milli() / 1000.0f;
        if (s->speed > cap) s->speed = cap;
        if (s->speed < 0.02f && s->combo) s->combo = 0;   /* a combo ends when you stop rolling */
        s->last_yaw = yaw;
    } else if (!on_ground && !s->airborne) {
        /* take-off: the ollie -- or rolling off a ledge, a drop, which is not a trick (found in the real game: a ride
           off a box scored an OLLIE) */
        s->airborne = 1;
        s->air_spin = 0.0f; s->last_yaw = yaw;
        s->flip = s->flip_left = s->grab_ticks = s->grabbed = 0;
        if (vy > 0.0f) {
            s->air_points = skate_trick_points(SKATE_TRICK_OLLIE);
            snprintf(s->air_names, sizeof s->air_names, "OLLIE");
            if (s->flip_queued) {                 /* the flip pressed with the ollie */
                int heel = s->flip_queued == SKATE_TRICK_HEELFLIP;
                s->flip_queued = s->queue_ticks = 0;
                skate_flip(s, heel);
            }
        } else {
            s->air_points = 0;
            s->air_names[0] = 0;
        }
    } else if (!on_ground) {
        /* in the air: the board turns with the rider, the motion keeps its direction */
        s->air_spin += fabsf(skate_wrap180(yaw - s->last_yaw));
        s->last_yaw = yaw;
        s->heading = s->switch_stance ? skate_wrap180(yaw + 180.0f) : yaw;
        if (s->flip_left > 0) s->flip_left--;
        if (grab) {
            if (++s->grab_ticks == 8 && !s->grabbed) {
                s->grabbed = 1;
                s->air_points += skate_trick_points(SKATE_TRICK_GRAB);
                skate_add_name(s, "GRAB");
            }
        }
    } else {
        /* landing */
        s->airborne = 0;
        float hx, hz;
        skate_dir(s->heading, &hx, &hz);
        float dot = hx * s->travel_x + hz * s->travel_z;
        if (dot > 1.0f) dot = 1.0f;
        if (dot < -1.0f) dot = -1.0f;
        int angle = (int)(acosf(dot) * 180.0f / 3.14159265f + 0.5f);
        int code = skate_landing(angle, s->flip_left, speed_milli);
        if (code == 2) {
            s->on = 0; s->speed = 0.0f; s->combo = 0; s->air_points = 0;
            s->bail_ticks = skate_bail_ticks();
            skate_say(s, s->flip_left > 0 ? "BAILED -- THE FLIP WAS STILL TURNING" : "BAILED -- LANDED SIDEWAYS");
            return SKATE_EV_BAILED;
        }
        int halves = skate_spin_halves((int)s->air_spin);
        if (halves > 0) {
            char spin[16]; snprintf(spin, sizeof spin, "%d", halves * 180);
            skate_add_name(s, spin);
            s->air_points += halves * skate_trick_points(SKATE_TRICK_SPIN);
        }
        s->switch_stance = code == 1;
        *mx = s->travel_x * s->speed;
        *mz = s->travel_z * s->speed;
        if (s->air_points == 0) {                 /* a plain drop: landed, nothing to score */
            if (code == 1) skate_say(s, "SWITCH");
            return code == 1 ? SKATE_EV_SWITCH : SKATE_EV_LANDED;
        }
        /* the motion carries on; the board's heading is where it points now */
        s->combo++;
        int mult = skate_combo_mult(s->combo);
        int pts = s->air_points * mult;
        s->score += pts;
        if (pts > s->best) s->best = pts;
        char t[96];
        snprintf(t, sizeof t, "%s%s  +%d%s", s->air_names, code == 1 ? "  (SWITCH)" : "", pts,
                 mult > 1 ? (mult == 2 ? "  x2" : mult == 3 ? "  x3" : mult == 4 ? "  x4" : "  x5") : "");
        skate_say(s, t);
        s->air_points = 0; s->air_names[0] = 0;
        *mx = s->travel_x * s->speed;
        *mz = s->travel_z * s->speed;
        return code == 1 ? SKATE_EV_SWITCH : SKATE_EV_LANDED;
    }
    *mx = s->travel_x * s->speed;
    *mz = s->travel_z * s->speed;
    return SKATE_EV_NONE;
}

/* The lobby moved the rider by (mx, mz) and collision let it go only (got) of (want): a wall takes the speed. */
static inline void skate_blocked(Skate *s, float want, float got) {
    if (want > 0.0001f && got < want * 0.5f) s->speed *= got / want;
}

#endif
