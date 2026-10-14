#ifndef PHYSICS_HOST_H
#define PHYSICS_HOST_H

/* Host layer for physics.prn. No malloc/calloc/realloc/free: link PARENA's runtime built with
 * -DPARENA_NO_MALLOC and every arena block comes from its static, 64-byte-aligned pool. Each
 * call below takes its working set from that pool and returns it before returning. */

/* Largest box list one call accepts (6 doubles per box, contiguous in the arena). */
#define PHYS_HOST_MAX_BOXES 2048

#include <stddef.h>

#define PHYS_HOST_EVENT_STRIDE 4

enum PhysHostEventKind {
    PHYS_EV_LAND = 1,
    PHYS_EV_WALL_X = 2,
    PHYS_EV_WALL_Z = 3,
    PHYS_EV_FLOOR = 4
};

typedef struct {
    float x, y, z, w, h, d; /* centre + full extents, same as Box in physics.h */
} PhysHostBox;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float prev_y; /* frame-start y; ignored on input, set from y - vy */
    float pw, ph;
    int on_ground;
    int crouching, in_vehicle, dash_timer;
    float ground_friction;
} PhysHostPlayer;

typedef struct {
    int kind;
    int a, b, c;
} PhysHostEvent;

typedef struct PhysHost PhysHost;

/* Returns 0 on success. `boxes[0]` is the physics.h sentinel slot and is skipped (first = 1). */
int phys_host_trace(const PhysHostBox *boxes, int n_boxes,
                    const float seg[6], float out_hit[3], float out_normal[3], float *out_t,
                    int *out_box);

int phys_host_sweep(const PhysHostBox *boxes, int n_boxes, float radius,
                    const float origin[3], const float delta[3], float out_hit[3],
                    float out_normal[3], float *out_t, int *out_box);

/* t >= 0 on hit, -1 on miss. d must be unit length. */
float phys_host_ray_capsule(const float o[3], const float d[3],
                            const float base[3], float height, float radius);

/* 0 miss, 1 body, 2 head -- same contract as check_hit_location. */
int phys_host_hit_location(const float o[3], const float d[3],
                           const float target[3], int active, int in_vehicle);

/* Resolves one player against the box list; returns number of events written (<= max_events),
 * or -1 if the box list exceeds PHYS_HOST_MAX_BOXES (nothing is touched). */
int phys_host_resolve(const PhysHostBox *boxes, int n_boxes,
                      PhysHostPlayer *p, PhysHostEvent *events, int max_events);

/* movement.prn: operate on p->vx / p->vz only. */
void phys_host_apply_friction(PhysHostPlayer *p);
void phys_host_accelerate(PhysHostPlayer *p, float wish_x, float wish_z, float wish_speed,
                          float accel);

/* Scalar helpers (no arena). */
float phys_host_norm_yaw(float yaw);
float phys_host_clamp_pitch(float pitch);
float phys_host_angle_diff(float a, float b);
void phys_host_katana_forward(float yaw_deg, float pitch_deg, float out[3]);

/* Ground height from plain boxes (+ optional terrain sample). Returns 1 if the height came
 * from terrain, else 0; *out_h gets the height. */
int phys_host_ground_height(const PhysHostBox *boxes, int n_boxes, float x, float z,
                            int terrain_ok, float terrain_h, float *out_h);

/* Lag-compensation ring (physics.h LAG_HISTORY = 64 slots per client). Caller-owned storage:
 * PHYS_HOST_LAG_SLOTS entries per client. Exact-timestamp rewind matches phys_resolve_rewind;
 * the _lerp variant is new and interpolates between bracketing samples. */
#define PHYS_HOST_LAG_SLOTS 64
typedef struct {
    int active;
    double ts;
    float x, y, z;
} PhysHostLagRecord;
void phys_host_lag_store(PhysHostLagRecord *ring, double now, float x, float y, float z);
int phys_host_lag_rewind(const PhysHostLagRecord *ring, double target, float out[3]);
int phys_host_lag_rewind_lerp(const PhysHostLagRecord *ring, double target, float out[3]);

/* Peak bytes the static pool has had in use (diagnostic; 0 when built with malloc). */
unsigned long phys_host_pool_high_water(void);

#endif
