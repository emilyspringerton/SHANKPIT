/* physics_host.c -- host integration for physics.prn.
 *
 * Marshals plain C structs into the (Vec F64)/(Vec I32) layout physics.prn expects. Arena
 * memory comes from PARENA's runtime built with -DPARENA_NO_MALLOC (static pool, no heap).
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -pedantic -DPARENA_NO_GRAPHICS -D_DEFAULT_SOURCE \
 *       -I<PARENA>/runtime physics_host.c physics_gen.c <PARENA>/runtime/parena_runtime.c
 *       (the runtime itself compiled with -DPARENA_NO_MALLOC)
 */
#include "physics_host.h"

#include <math.h>

#include "parena_runtime.h"

/* ---- generated module prototypes (parena build physics.prn) ------------------------------ */
Vec make_out(Arena *dest);
Vec make_state(double x, double y, double z, double vx, double vy, double vz, double pw,
               double ph, Arena *dest);
int trace_segment(Vec *boxes, int first, double x1, double y1, double z1, double x2, double y2,
                  double z2, Vec *out);
int sphere_sweep(Vec *boxes, int first, double r, double ox, double oy, double oz, double dx,
                 double dy, double dz, Vec *out);
double ray_capsule(double ox, double oy, double oz, double dx, double dy, double dz, double px,
                   double py, double pz, double h, double r);
int hit_location(double ox, double oy, double oz, double dx, double dy, double dz, double tx,
                 double ty, double tz, int active, int in_vehicle);
int resolve_player(Vec *boxes, int first, Vec *state, Vec *events, Arena *dest);

/* ---- generated from movement.prn ---------------------------------------------------------- */
Vec make_player(double x, double y, double z, double vx, double vy, double vz, double pw,
                double ph, double on_ground, double crouching, double in_vehicle,
                double dash_timer, double ground_friction, Arena *dest);
Vec make_history(int clients, Arena *dest);
void apply_friction(Vec *p);
void accelerate(Vec *p, double wish_x, double wish_z, double wish_speed, double accel);
double norm_yaw(double yaw);
double clamp_pitch(double pitch);
double angle_diff(double a, double b);
void katana_forward_dir(double yaw_deg, double pitch_deg, Vec *out);
int ground_height(Vec *boxes, int first, double x, double z, int terrain_ok, double terrain_h,
                  Vec *out);
void lag_store_(Vec *hist, int client, double now, double x, double y, double z);
int lag_rewind(Vec *hist, int client, double target, Vec *out);
int lag_rewind_lerp(Vec *hist, int client, double target, Vec *out);

/* ---- arena frames -------------------------------------------------------------------- */

static Vec build_boxes(Arena *a, const PhysHostBox *boxes, int n) {
    Vec v = vec_new(a);
    int i;
    vec_reserve(&v, n * 6); /* one contiguous, 64-byte-aligned array; no regrowth */
    for (i = 0; i < n; i++) {
        vec_push_(&v, vec_box_f64(&v, (double)boxes[i].x));
        vec_push_(&v, vec_box_f64(&v, (double)boxes[i].y));
        vec_push_(&v, vec_box_f64(&v, (double)boxes[i].z));
        vec_push_(&v, vec_box_f64(&v, (double)boxes[i].w));
        vec_push_(&v, vec_box_f64(&v, (double)boxes[i].h));
        vec_push_(&v, vec_box_f64(&v, (double)boxes[i].d));
    }
    return v;
}

static double vf(Vec *v, int i) {
    return *(double *)vec_get(v, i);
}

static void fill_hit(Vec *out, float hit[3], float nrm[3], float *t, int *box) {
    hit[0] = (float)vf(out, 0);
    hit[1] = (float)vf(out, 1);
    hit[2] = (float)vf(out, 2);
    nrm[0] = (float)vf(out, 3);
    nrm[1] = (float)vf(out, 4);
    nrm[2] = (float)vf(out, 5);
    *t = (float)vf(out, 6);
    *box = (int)lround(vf(out, 7));
}

int phys_host_trace(const PhysHostBox *boxes, int n_boxes,
                    const float seg[6], float out_hit[3], float out_normal[3], float *out_t,
                    int *out_box) {
    Arena frame;
    Vec b, out;
    int hit;
    if (n_boxes > PHYS_HOST_MAX_BOXES) return -1;
    arena_init(&frame);
    b = build_boxes(&frame, boxes, n_boxes);
    out = make_out(&frame);
    hit = trace_segment(&b, 1, seg[0], seg[1], seg[2], seg[3], seg[4], seg[5], &out);
    if (hit) fill_hit(&out, out_hit, out_normal, out_t, out_box);
    arena_free_all(&frame);
    return hit ? 1 : 0;
}

int phys_host_sweep(const PhysHostBox *boxes, int n_boxes, float radius,
                    const float origin[3], const float delta[3], float out_hit[3],
                    float out_normal[3], float *out_t, int *out_box) {
    Arena frame;
    Vec b, out;
    int hit;
    if (n_boxes > PHYS_HOST_MAX_BOXES) return -1;
    arena_init(&frame);
    b = build_boxes(&frame, boxes, n_boxes);
    out = make_out(&frame);
    hit = sphere_sweep(&b, 1, (double)radius, origin[0], origin[1], origin[2], delta[0], delta[1],
                       delta[2], &out);
    if (hit) fill_hit(&out, out_hit, out_normal, out_t, out_box);
    arena_free_all(&frame);
    return hit ? 1 : 0;
}

float phys_host_ray_capsule(const float o[3], const float d[3],
                            const float base[3], float height, float radius) {
    return (float)ray_capsule(o[0], o[1], o[2], d[0], d[1], d[2], base[0], base[1], base[2],
                              (double)height, (double)radius);
}

int phys_host_hit_location(const float o[3], const float d[3],
                           const float target[3], int active, int in_vehicle) {
    return hit_location(o[0], o[1], o[2], d[0], d[1], d[2], target[0], target[1], target[2],
                        active, in_vehicle);
}

int phys_host_resolve(const PhysHostBox *boxes, int n_boxes,
                      PhysHostPlayer *p, PhysHostEvent *events, int max_events) {
    Arena frame;
    Vec b, st, ev;
    int i, n_ev;
    if (n_boxes > PHYS_HOST_MAX_BOXES) return -1;
    arena_init(&frame);
    b = build_boxes(&frame, boxes, n_boxes);
    st = make_player(p->x, p->y, p->z, p->vx, p->vy, p->vz, p->pw, p->ph,
                    (double)p->on_ground, (double)p->crouching, (double)p->in_vehicle, (double)p->dash_timer,
                    (double)p->ground_friction, &frame);
    ev = vec_new(&frame); /* the deferred-event scratchpad lives in the frame arena */
    (void)resolve_player(&b, 1, &st, &ev, &frame);
    p->x = (float)vf(&st, 0);
    p->y = (float)vf(&st, 1);
    p->z = (float)vf(&st, 2);
    p->vx = (float)vf(&st, 3);
    p->vy = (float)vf(&st, 4);
    p->vz = (float)vf(&st, 5);
    p->on_ground = vf(&st, 9) > 0.5 ? 1 : 0;
    p->ground_friction = (float)vf(&st, 13);
    n_ev = vec_len(&ev) / PHYS_HOST_EVENT_STRIDE;
    if (n_ev > max_events) n_ev = max_events;
    for (i = 0; i < n_ev; i++) {
        int base = i * PHYS_HOST_EVENT_STRIDE;
        events[i].kind = *(int *)vec_get(&ev, base);
        events[i].a = *(int *)vec_get(&ev, base + 1);
        events[i].b = *(int *)vec_get(&ev, base + 2);
        events[i].c = *(int *)vec_get(&ev, base + 3);
    }
    arena_free_all(&frame);
    return n_ev;
}

static Vec player_vec(Arena *a, const PhysHostPlayer *p) {
    return make_player(p->x, p->y, p->z, p->vx, p->vy, p->vz, p->pw, p->ph,
                       (double)p->on_ground, (double)p->crouching, (double)p->in_vehicle, (double)p->dash_timer,
                       (double)p->ground_friction, a);
}

void phys_host_apply_friction(PhysHostPlayer *p) {
    Arena a;
    Vec v;
    arena_init(&a);
    v = player_vec(&a, p);
    apply_friction(&v);
    p->vx = (float)vf(&v, 3);
    p->vz = (float)vf(&v, 5);
    arena_free_all(&a);
}

void phys_host_accelerate(PhysHostPlayer *p, float wish_x, float wish_z, float wish_speed,
                          float accel) {
    Arena a;
    Vec v;
    arena_init(&a);
    v = player_vec(&a, p);
    accelerate(&v, wish_x, wish_z, wish_speed, accel);
    p->vx = (float)vf(&v, 3);
    p->vz = (float)vf(&v, 5);
    arena_free_all(&a);
}

float phys_host_norm_yaw(float yaw) { return (float)norm_yaw(yaw); }
float phys_host_clamp_pitch(float pitch) { return (float)clamp_pitch(pitch); }
float phys_host_angle_diff(float a, float b) { return (float)angle_diff(a, b); }

void phys_host_katana_forward(float yaw_deg, float pitch_deg, float out[3]) {
    Arena a;
    Vec v;
    arena_init(&a);
    v = vec_new(&a);
    vec_push_(&v, vec_box_f64(&v, 0.0));
    vec_push_(&v, vec_box_f64(&v, 0.0));
    vec_push_(&v, vec_box_f64(&v, 0.0));
    katana_forward_dir(yaw_deg, pitch_deg, &v);
    out[0] = (float)vf(&v, 0);
    out[1] = (float)vf(&v, 1);
    out[2] = (float)vf(&v, 2);
    arena_free_all(&a);
}

int phys_host_ground_height(const PhysHostBox *boxes, int n_boxes, float x, float z,
                            int terrain_ok, float terrain_h, float *out_h) {
    Arena a;
    Vec b, out;
    int src;
    if (n_boxes > PHYS_HOST_MAX_BOXES) return -1;
    arena_init(&a);
    b = build_boxes(&a, boxes, n_boxes);
    out = vec_new(&a);
    vec_push_(&out, vec_box_f64(&out, 0.0));
    src = ground_height(&b, 1, x, z, terrain_ok, terrain_h, &out);
    *out_h = (float)vf(&out, 0);
    arena_free_all(&a);
    return src;
}

/* The ring is one client's 64 records; marshal it to the module's [active ts x y z] slots. */
static Vec ring_vec(Arena *a, const PhysHostLagRecord *ring) {
    Vec v = vec_new(a);
    int i;
    vec_reserve(&v, PHYS_HOST_LAG_SLOTS * 5);
    for (i = 0; i < PHYS_HOST_LAG_SLOTS; i++) {
        vec_push_(&v, vec_box_f64(&v, ring[i].active ? 1.0 : 0.0));
        vec_push_(&v, vec_box_f64(&v, ring[i].ts));
        vec_push_(&v, vec_box_f64(&v, (double)ring[i].x));
        vec_push_(&v, vec_box_f64(&v, (double)ring[i].y));
        vec_push_(&v, vec_box_f64(&v, (double)ring[i].z));
    }
    return v;
}

void phys_host_lag_store(PhysHostLagRecord *ring, double now, float x, float y, float z) {
    /* Same slot rule as physics.prn's lag-store!: (now / 16) mod 64. */
    int slot = (int)fmod(floor(now / 16.0), (double)PHYS_HOST_LAG_SLOTS);
    ring[slot].active = 1;
    ring[slot].ts = now;
    ring[slot].x = x;
    ring[slot].y = y;
    ring[slot].z = z;
}

static int rewind_common(const PhysHostLagRecord *ring, double target, float out[3], int lerp) {
    Arena a;
    Vec h, o;
    int ok;
    arena_init(&a);
    h = ring_vec(&a, ring);
    o = vec_new(&a);
    vec_push_(&o, vec_box_f64(&o, 0.0));
    vec_push_(&o, vec_box_f64(&o, 0.0));
    vec_push_(&o, vec_box_f64(&o, 0.0));
    ok = lerp ? lag_rewind_lerp(&h, 0, target, &o) : lag_rewind(&h, 0, target, &o);
    if (ok) {
        out[0] = (float)vf(&o, 0);
        out[1] = (float)vf(&o, 1);
        out[2] = (float)vf(&o, 2);
    }
    arena_free_all(&a);
    return ok;
}

int phys_host_lag_rewind(const PhysHostLagRecord *ring, double target, float out[3]) {
    return rewind_common(ring, target, out, 0);
}

int phys_host_lag_rewind_lerp(const PhysHostLagRecord *ring, double target, float out[3]) {
    return rewind_common(ring, target, out, 1);
}

unsigned long phys_host_pool_high_water(void) {
    return (unsigned long)parena_static_pool_high_water();
}
