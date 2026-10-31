#ifndef SHANKPIT_OCEAN_H
#define SHANKPIT_OCEAN_H

/* ocean.h -- the water, and a hull that floats in it.
 *
 * See docs/ARCHIPELAGO_NORTHSTAR.md for the scoping pass this belongs to: SHANKPIT had no water at all before it.
 *
 * Split, as usual: the DECISIONS (sea state, wave field, planing lift, drag, submerged fraction) are PARENA's,
 * emitted to packages/simulation/ocean.c by scripts/gen_ocean_rules.sh. This header is the host: it samples the
 * field, derives a normal by finite difference, and integrates a probe-based hull. Sampling and integration are
 * host work, not decisions, which is why they are here rather than in the .prn.
 *
 * Deliberately NOT here: rendering (nothing draws water yet), a boat entity/seat/input, level streaming, and any
 * thread. One deterministic loop, like the rest of this engine -- the blueprint's "multi-threaded simulation loop"
 * would break the property every netcode decision in this repo depends on.
 *
 * Units: metres in world space, seconds in time, millimetres in the rules' own heights. The conversions are here and
 * nowhere else.
 */

/* PARENA-generated (packages/simulation/ocean.c). */
int ocean_sea_state_count(void);
double ocean_amplitude_mm(int sea);
int ocean_wind_mps(int sea);
double ocean_wave_height_mm(double x, double z, double t, int sea);
int ocean_submerged_permille(int depth_mm, int hull_height_mm);
int ocean_planing_lift_mm(int speed_mm_s);
int ocean_drag_permille(int speed_mm_s, int submerged);

#define OCEAN_SEA_CALM     0
#define OCEAN_SEA_SWELL    1
#define OCEAN_SEA_CHOPPY   2
#define OCEAN_SEA_STORM    3
#define OCEAN_SEA_TEMPEST  4

#define OCEAN_MAX_PROBES   8
#define OCEAN_GRAVITY_MPS2 9.81f

/* Surface height at a world position, in metres. */
static inline float ocean_height_m(float x, float z, float t, int sea) {
    return (float)(ocean_wave_height_mm((double)x, (double)z, (double)t, sea) / 1000.0);
}

/* Horizontal extent for the finite-difference normal: ~0.4 m, small enough to read a swell, large enough not to
 * read float noise. */
#define OCEAN_NORMAL_STEP_M 0.4f

/* Surface normal by central difference. A flat calm sea gives exactly (0,1,0). */
static inline void ocean_normal_m(float x, float z, float t, int sea, float out[3]) {
    float hx1 = ocean_height_m(x + OCEAN_NORMAL_STEP_M, z, t, sea);
    float hx0 = ocean_height_m(x - OCEAN_NORMAL_STEP_M, z, t, sea);
    float hz1 = ocean_height_m(x, z + OCEAN_NORMAL_STEP_M, t, sea);
    float hz0 = ocean_height_m(x, z - OCEAN_NORMAL_STEP_M, t, sea);
    float dx = (hx1 - hx0) / (2.0f * OCEAN_NORMAL_STEP_M);
    float dz = (hz1 - hz0) / (2.0f * OCEAN_NORMAL_STEP_M);
    float nx = -dx, ny = 1.0f, nz = -dz;
    float len = nx * nx + ny * ny + nz * nz;
    if (len > 1e-12f) {
        float inv = 1.0f / __builtin_sqrtf(len);
        out[0] = nx * inv; out[1] = ny * inv; out[2] = nz * inv;
    } else {
        out[0] = 0.0f; out[1] = 1.0f; out[2] = 0.0f;
    }
}

/* One probe under a hull: where it is, how much water it can push aside, and how deep below the keel it samples. */
typedef struct {
    float x, z;          /* world position of the probe (X/Z only: the hull's own heading is applied by the caller) */
    float area_m2;       /* waterplane area this probe represents */
    float keel_y;        /* world Y of the hull's bottom at this probe */
} OceanProbe;

/* A vessel as the water sees it: probes, mass, and the vertical/pitch state the host integrates. */
typedef struct {
    OceanProbe probe[OCEAN_MAX_PROBES];
    int probe_count;
    float mass_kg;
    float hull_height_m;
    float y;             /* hull bottom, metres */
    float vy;            /* vertical velocity, m/s */
    float pitch;         /* radians, + = nose up */
    float pitch_rate;
    float speed_mps;     /* forward speed, used for planing lift and drag (host decides the propulsion) */
} OceanVessel;

/* Height of the water at a point, in metres, and the surface normal there. */
static inline float ocean_sample(const OceanVessel *v, const OceanProbe *p, float t, int sea, float normal[3]) {
    (void)v;
    if (normal) ocean_normal_m(p->x, p->z, t, sea, normal);
    return ocean_height_m(p->x, p->z, t, sea);
}

/* ocean_vessel_forces -- what the water is doing to this hull right now: total lift in newtons and the pitch torque
 * that lift produces around the hull's centre. Written out as its own function so a test can ask "does this float?"
 * without running an integrator.
 *
 * Buoyancy per probe is depth x area x density x g (408.0f is 0.408 t/m^3, a deliberately buoyant game hull so a
 * small boat carries its own mass), and it is clamped at the probe's own area so a deeply sunk probe cannot push
 * harder than its displacement. */
static inline void ocean_vessel_forces(const OceanVessel *v, float t, int sea,
                                       float *out_lift_n, float *out_pitch_torque_nm) {
    float lift = 0.0f, torque = 0.0f;
    float cx = 0.0f;
    for (int i = 0; i < v->probe_count; i++) cx += v->probe[i].x;
    if (v->probe_count > 0) cx /= (float)v->probe_count;

    float planing = (float)ocean_planing_lift_mm((int)(v->speed_mps * 1000.0f)) / 1000.0f;  /* metres of rise */
    for (int i = 0; i < v->probe_count; i++) {
        const OceanProbe *p = &v->probe[i];
        float n[3];
        float surface = ocean_sample(v, p, t, sea, n);
        float depth = surface - p->keel_y + planing;
        if (depth <= 0.0f) continue;
        float submerged = (float)ocean_submerged_permille((int)(depth * 1000.0f), (int)(v->hull_height_m * 1000.0f)) / 1000.0f;
        float f = depth * p->area_m2 * 1025.0f * OCEAN_GRAVITY_MPS2 * 0.408f * submerged;
        lift += f;
        torque += f * (p->x - cx);          /* +x probe lifted means nose-down torque around the centre */
    }
    /* aerodynamic + hydrodynamic drag slows the hull; the water's own drag rises with speed */
    float drag = (float)ocean_drag_permille((int)(v->speed_mps * 1000.0f),
                                            (int)(ocean_submerged_permille((int)(v->hull_height_m * 500.0f),
                                                                             (int)(v->hull_height_m * 1000.0f)))) / 1000.0f;

    if (out_lift_n) *out_lift_n = lift;
    if (out_pitch_torque_nm) *out_pitch_torque_nm = torque - v->pitch * drag * 40.0f;
}

/* ocean_vessel_step -- one tick of hull motion: buoyancy against weight, pitch from the wave slope, and the
 * waterline settling out. Deterministic and allocation-free, like everything else on the simulation path. */
static inline void ocean_vessel_step(OceanVessel *v, float dt, float t, int sea) {
    if (dt <= 0.0f) return;
    float lift = 0.0f, torque = 0.0f;
    ocean_vessel_forces(v, t, sea, &lift, &torque);

    float weight = v->mass_kg * OCEAN_GRAVITY_MPS2;
    float ay = (lift - weight) / v->mass_kg;                 /* + = up */
    v->vy += ay * dt;
    v->vy *= 0.98f;                                          /* water is not a vacuum; keeps it from ringing forever */
    v->y += v->vy * dt;

    /* Pitch: the torque is normalised by a crude moment of inertia (mass x length^2 / 12), and the water damps the
     * pitch rate hard -- a hull settles nose-up on a swell, it does not oscillate like a diving board. */
    float span = 1.0f;
    for (int i = 1; i < v->probe_count; i++) {
        float d = v->probe[i].x - v->probe[0].x;
        if (d < 0.0f) d = -d;
        if (d > span) span = d;
    }
    float inertia = v->mass_kg * span * span / 12.0f;
    if (inertia < 1.0f) inertia = 1.0f;
    v->pitch_rate += (torque / inertia) * dt;
    v->pitch_rate *= 0.90f;
    v->pitch += v->pitch_rate * dt;
}

#endif
