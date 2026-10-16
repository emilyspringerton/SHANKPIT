#ifndef SHANKPIT_PHEROMONE_GRENADE_H
#define SHANKPIT_PHEROMONE_GRENADE_H

/* pheromone_grenade.h -- the pheromone thrower's projectile: a thrown canister with real mass that
 * flies on a ballistic arc, bounces off the floor and walls, rolls to a stop, and drags the
 * pheromone marker (packages/common/pheromone.h) along with it so zombies track it while it is
 * still in the air. Header-only and pure (no globals, no rand, no clock): the same inputs give the
 * same flight, so it is testable headless (pheromone_grenade_test.c).
 *
 * Mass-based, within what a thrown ball needs:
 *   - launch speed = throw impulse / mass (a heavier canister leaves the hand slower; the thrower's
 *     own velocity is inherited on top),
 *   - bounce restitution falls with mass (a heavy canister thuds, a light one skips),
 *   - the speed lost on a bounce is the normal component times (1 + e); tangential speed is cut by
 *     contact friction, and resting friction is Coulomb (decel mu*g, independent of mass).
 * Collision is sphere-vs-axis-aligned-box (the level's collision boxes, via a host callback) plus
 * the floor plane at ground_y. Units are SHANKPIT world units (player ~6.5 tall, ~3.6 per metre).
 *
 * Named v0 limits: boxes are axis-aligned (rotated ramps are treated as their bounding box by the
 * callback the host passes), the canister never pushes anything, and it is a single sphere. */

#include <math.h>
#include <string.h>

#define PG_GRAVITY        36.0f   /* units/s^2 (~9.8 m/s^2 at 3.6 units per metre) */
#define PG_RADIUS         0.55f
#define PG_MASS_KG        0.8f    /* tunable; the host may pass any mass to pg_throw */
#define PG_THROW_IMPULSE  22.0f   /* N*s: 22 / 0.8 kg = 27.5 units/s at the hand */
#define PG_REST_SPEED     1.2f    /* below this on the floor it is resting */
#define PG_REST_TIME      0.35f   /* ...for this long -> landed */
#define PG_MAX_FLIGHT     8.0f    /* seconds: lands wherever it is by then, never flies forever */
#define PG_FIXED_DT       (1.0f / 60.0f)
#define PG_TRAIL          10      /* recent positions, for the renderer's streak */

/* Static-geometry query: is the sphere (c, r) penetrating the level? 1 = yes, with the unit exit
 * normal and the penetration depth of the deepest overlap. */
typedef int (*PgPushFn)(void *user, const float c[3], float r, float nrm[3], float *pen);

typedef struct {
    int active;
    int landed;
    float pos[3], vel[3];
    float mass, radius;
    float spin;          /* visual only: angle (rad) the canister tumbles through */
    float spin_rate;     /* rad/s, decays with the ground speed */
    float age, rest_t;
    int bounces;
    float trail[PG_TRAIL][3];
    int trail_n, trail_head;
    float acc;           /* fixed-step accumulator */
} PgGrenade;

static inline float pg_restitution(float mass) {
    float e = 0.62f - 0.05f * mass;
    return e < 0.12f ? 0.12f : (e > 0.62f ? 0.62f : e);
}

static inline float pg_launch_speed(float mass, float impulse) {
    return impulse / (mass > 0.05f ? mass : 0.05f);
}

/* pg_throw -- launches from `origin` along the view (yaw/pitch in degrees, SHANKPIT convention:
 * x = sin(yaw), z = cos(yaw)), with a 25 degree upward lob bias so a level throw arcs like a grenade, plus the thrower's
 * own velocity. */
static inline void pg_throw(PgGrenade *g, const float origin[3], float yaw_deg, float pitch_deg,
                            const float inherit_vel[3], float mass, float impulse) {
    memset(g, 0, sizeof(*g));
    float yaw = yaw_deg * 3.14159265f / 180.0f;
    float pit = (pitch_deg + 25.0f) * 3.14159265f / 180.0f;
    if (pit > 1.45f) pit = 1.45f;
    if (pit < -1.2f) pit = -1.2f;
    float sp = pg_launch_speed(mass, impulse);
    g->vel[0] = sinf(yaw) * cosf(pit) * sp + (inherit_vel ? inherit_vel[0] : 0.0f);
    g->vel[1] = sinf(pit) * sp + (inherit_vel ? inherit_vel[1] : 0.0f);
    g->vel[2] = cosf(yaw) * cosf(pit) * sp + (inherit_vel ? inherit_vel[2] : 0.0f);
    g->pos[0] = origin[0]; g->pos[1] = origin[1]; g->pos[2] = origin[2];
    g->mass = mass; g->radius = PG_RADIUS;
    g->spin_rate = 9.0f;
    g->active = 1;
}

static inline void pg_trail_push(PgGrenade *g) {
    memcpy(g->trail[g->trail_head], g->pos, sizeof(g->pos));
    g->trail_head = (g->trail_head + 1) % PG_TRAIL;
    if (g->trail_n < PG_TRAIL) g->trail_n++;
}

/* One fixed step. */
static inline void pg_tick(PgGrenade *g, float dt, PgPushFn push, void *user, float ground_y) {
    if (!g->active || g->landed) return;
    float e = pg_restitution(g->mass);
    int on_floor = 0;

    g->vel[1] -= PG_GRAVITY * dt;
    for (int k = 0; k < 3; k++) g->pos[k] += g->vel[k] * dt;
    g->age += dt;

    /* floor plane */
    if (g->pos[1] - g->radius < ground_y) {
        g->pos[1] = ground_y + g->radius;
        if (g->vel[1] < 0.0f) {
            if (-g->vel[1] > 3.0f) { g->bounces++; g->vel[1] = -g->vel[1] * e; }
            else g->vel[1] = 0.0f;
            float keep = 1.0f - 0.18f; /* contact friction takes a slice of the sliding speed */
            g->vel[0] *= keep; g->vel[2] *= keep;
        }
        on_floor = 1;
    }

    /* level geometry: up to 3 resolves so a corner does not tunnel */
    for (int it = 0; it < 3 && push; it++) {
        float n[3]; float pen = 0.0f;
        if (!push(user, g->pos, g->radius, n, &pen) || pen <= 0.0f) break;
        g->pos[0] += n[0] * pen; g->pos[1] += n[1] * pen; g->pos[2] += n[2] * pen;
        float vn = g->vel[0] * n[0] + g->vel[1] * n[1] + g->vel[2] * n[2];
        if (vn < 0.0f) {
            float j = (fabsf(vn) > 3.0f) ? -(1.0f + e) * vn : -vn; /* tiny impacts just stop */
            if (fabsf(vn) > 3.0f) g->bounces++;
            for (int k = 0; k < 3; k++) g->vel[k] += n[k] * j;
            float keep = 0.85f;
            float vt[3];
            for (int k = 0; k < 3; k++) vt[k] = g->vel[k] - n[k] * (g->vel[0] * n[0] + g->vel[1] * n[1] + g->vel[2] * n[2]);
            for (int k = 0; k < 3; k++) g->vel[k] -= vt[k] * (1.0f - keep);
        }
        if (n[1] > 0.7f) on_floor = 1;
    }

    if (on_floor) { /* Coulomb rolling friction: decel is mu*g whatever the mass */
        float sp = sqrtf(g->vel[0] * g->vel[0] + g->vel[2] * g->vel[2]);
        float dec = 0.55f * PG_GRAVITY * dt;
        if (sp <= dec) { g->vel[0] = g->vel[2] = 0.0f; }
        else { g->vel[0] -= g->vel[0] / sp * dec; g->vel[2] -= g->vel[2] / sp * dec; }
    }
    {
        float sp = sqrtf(g->vel[0] * g->vel[0] + g->vel[1] * g->vel[1] + g->vel[2] * g->vel[2]);
        g->spin += g->spin_rate * dt;
        g->spin_rate = 1.5f + sp * 0.3f;
        if (on_floor && sp < PG_REST_SPEED) g->rest_t += dt; else g->rest_t = 0.0f;
        if (g->rest_t >= PG_REST_TIME || g->age >= PG_MAX_FLIGHT) { g->landed = 1; g->vel[0] = g->vel[1] = g->vel[2] = 0.0f; g->spin_rate = 0.0f; }
    }
    pg_trail_push(g);
}

/* Advance by dt seconds in fixed PG_FIXED_DT steps (catch-up capped at 6). Returns 1 on the call
 * that lands it. */
static inline int pg_step(PgGrenade *g, float dt, PgPushFn push, void *user, float ground_y) {
    if (!g->active || g->landed) return 0;
    g->acc += dt;
    int n = 0;
    while (g->acc >= PG_FIXED_DT && n < 6) {
        pg_tick(g, PG_FIXED_DT, push, user, ground_y);
        g->acc -= PG_FIXED_DT; n++;
        if (g->landed) return 1;
    }
    if (n == 6) g->acc = 0.0f;
    return 0;
}

/* Sphere vs one centre-based axis-aligned box. 1 if overlapping, with exit normal and depth. */
static inline int pg_sphere_aabb(const float c[3], float r, float bx, float by, float bz,
                                 float bw, float bh, float bd, float nrm[3], float *pen) {
    float hx = bw * 0.5f, hy = bh * 0.5f, hz = bd * 0.5f;
    float q[3] = { c[0] - bx, c[1] - by, c[2] - bz };
    float cl[3] = { q[0] < -hx ? -hx : (q[0] > hx ? hx : q[0]),
                    q[1] < -hy ? -hy : (q[1] > hy ? hy : q[1]),
                    q[2] < -hz ? -hz : (q[2] > hz ? hz : q[2]) };
    float d[3] = { q[0] - cl[0], q[1] - cl[1], q[2] - cl[2] };
    float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (d2 > 1e-8f) {
        if (d2 >= r * r) return 0;
        float dl = sqrtf(d2);
        for (int k = 0; k < 3; k++) nrm[k] = d[k] / dl;
        *pen = r - dl;
        return 1;
    }
    /* centre inside the box: leave by the nearest face */
    float ex = hx - fabsf(q[0]), ey = hy - fabsf(q[1]), ez = hz - fabsf(q[2]);
    nrm[0] = nrm[1] = nrm[2] = 0.0f;
    if (ex <= ey && ex <= ez) { nrm[0] = q[0] < 0 ? -1.0f : 1.0f; *pen = ex + r; }
    else if (ey <= ez)        { nrm[1] = q[1] < 0 ? -1.0f : 1.0f; *pen = ey + r; }
    else                      { nrm[2] = q[2] < 0 ? -1.0f : 1.0f; *pen = ez + r; }
    return 1;
}

#endif
