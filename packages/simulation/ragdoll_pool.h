/* ragdoll_pool.h -- a bounded, deterministic pool of rigid-body ragdolls for deaths (BIG_O ship-plan package W4;
 * founder real-time, 2026-10-01: "use rigidbody ragdoll physics" + "the models and animations available").
 *
 * WHAT THIS IS
 *   The XPBD rigid-body engine already exists: packages/goldenband/grb.{c,h} (6-DOF capsules, ball/hinge joints with swing/twist
 *   limits, plane contacts, 24 substeps) and packages/simulation/rigid_ragdoll.{c,h} (17 Winter/Dempster-mass segments on the
 *   mannequin skeleton assets/goldenband/mannequin_npc.gskel). What was missing, and this is, is everything around ONE ragdoll
 *   that a game needs to put them on screen at deaths: a fixed number of simultaneous bodies, spawning one from the victim's
 *   living pose, shoving it by what killed it, a fixed internal timestep with a catch-up cap, settle/freeze/fade/despawn timing,
 *   deciding WHICH body to give up when a new death arrives and the pool is full, collision against the static map, and handing
 *   the renderer a pose it can skin with the existing GOLDEN BAND path.
 *
 *   Every DECISION is PARENA (PARENA/stdlib/big_o/ragdoll_rules.prn, compiled to packages/simulation/ragdoll_rules.c): bone from
 *   hit height, impulse by weapon kind/damage/distance, per-segment share, caps, calm/freeze/despawn/fade timing, eviction
 *   priority. This host owns only arrays, the XPBD world, clocks and plumbing. The rules ABI is declared at the bottom of this
 *   header (and cross-checked against the generated file at compile time by `make test-ragdoll-pool`).
 *
 * UNITS AND FRAMES
 *   Metres, seconds, radians, y up, right-handed -- the mannequin skeleton is authored in metres and the lobby draws it unscaled,
 *   so SHANKPIT world units == metres here. The ragdoll is simulated directly in WORLD space; get_pose therefore returns a pose
 *   whose forward kinematics yields world-space joints (bake NO further model transform: use an identity npc_world when skinning).
 *   `facing_rad` is the exact value apps/lobby passes to gband_skel_npc_draw (a rotation about +Y, mat4_rotate_y convention:
 *   x' = x cos + z sin, z' = -x sin + z cos), so the ragdoll starts exactly where the living mesh was drawn.
 *
 * LIFECYCLE   FREE --spawn--> ACTIVE (simulated, 64 Hz) --calm long enough / max time--> FROZEN (pose held, fading) --hold over--> FREE
 *   Timings come from the PARENA rules per death kind (bullet / explosion / melee / environment); a body can never stay ACTIVE past
 *   its kind's maximum and never live past the absolute cap, so a jittery solve cannot leak a slot.
 *
 * DETERMINISM
 *   Same init + same spawn sequence + same dt sequence => bit-identical bodies and poses. No wall clock, no rand(), no threads, no
 *   floating-point state outside the pool; the time accumulator is integer microseconds. (Bit-identical on the same binary/CPU
 *   architecture, the same guarantee grb itself documents.) now_tick is stored for diagnostics only and never influences physics.
 *
 * MEMORY / THREADING
 *   No allocation after init, and none inside init either: the caller owns the RagdollPool (sizeof is ~1.5 MB at 8 slots -- make it
 *   static or heap, NEVER stack). One thread may call spawn/step/get_pose at a time: rigid_ragdoll.c keeps function-local static
 *   scratch for build/pose, which this pool does not own and cannot make re-entrant.
 *
 * NAMED v0 LIMITS (honest, not hidden)
 *   - Bodies collide with the ground plane (solved inside grb, with friction) and with a list/callback of static AABBs (solved here,
 *     once per 64 Hz tick AFTER the XPBD step: end- and mid-capsule spheres are pushed out, inward velocity removed, tangential
 *     velocity and spin damped). No body-vs-body and no limb-vs-limb collision (grb v0 has none), no continuous collision (a thin
 *     wall hit faster than ~0.1 m/tick can be tunnelled; the 40 m/s speed cap bounds it), no sleeping in grb (this pool freezes instead).
 *   - One skeleton per pool (the mannequin; rigid_ragdoll_build requires its joint names). Other character kits can be drawn with
 *     this pose only if the renderer retargets, which this package does not do.
 *   - A supplied animation pose that violates the ragdoll's own joint limits by more than RAGDOLL_POSE_MAX_VIOLATION_RAD is rejected
 *     and the default (rest pose, arms lowered) is used instead; stats.poses_rejected counts it.
 */
#ifndef SHANKPIT_RAGDOLL_POOL_H
#define SHANKPIT_RAGDOLL_POOL_H

#include <stdint.h>

#include "rigid_ragdoll.h"

/* Compile-time slot capacity (memory is sized for it); ragdoll_pool_init takes a runtime capacity <= this. PARENA's
 * ragdoll-default-pool-size is 8; the test asserts they agree. */
#ifndef RAGDOLL_POOL_SLOTS
#define RAGDOLL_POOL_SLOTS 8
#endif

#define RAGDOLL_BONE_COUNT 17        /* == rigid_ragdoll.c SEGMENTS[]; bone id == segment index (checked at init) */
#define RAGDOLL_BONE_AUTO (-1)       /* RagdollSpawn.hit_bone: derive the bone from hit_type/height/lateral */
#define RAGDOLL_TICK_HZ 64
#define RAGDOLL_QUERY_CAP 32         /* >= ragdoll_query_max(); compile-time size of the per-body box scratch */
#define RAGDOLL_WORLD_LIMIT 100000.0 /* spawn positions are clamped to +-100 km */
#define RAGDOLL_POSE_MAX_VIOLATION_RAD 0.60  /* a supplied pose past this joint-limit violation is rejected (~34 deg) */

/* Mirrors of the enumerations documented in ragdoll_rules.prn. */
enum { RAGDOLL_KIND_BULLET = 0, RAGDOLL_KIND_EXPLOSION = 1, RAGDOLL_KIND_MELEE = 2, RAGDOLL_KIND_ENV = 3 };
enum { RAGDOLL_STATE_FREE = 0, RAGDOLL_STATE_ACTIVE = 1, RAGDOLL_STATE_FROZEN = 2 };
enum { RAGDOLL_HIT_UNKNOWN = 0, RAGDOLL_HIT_BODY = 1, RAGDOLL_HIT_HEAD = 2 };
enum {
    RAGDOLL_BONE_PELVIS = 0, RAGDOLL_BONE_SPINE_01, RAGDOLL_BONE_SPINE_02, RAGDOLL_BONE_SPINE_03, RAGDOLL_BONE_NECK,
    RAGDOLL_BONE_UPPERARM_L, RAGDOLL_BONE_UPPERARM_R, RAGDOLL_BONE_LOWERARM_L, RAGDOLL_BONE_LOWERARM_R,
    RAGDOLL_BONE_HAND_L, RAGDOLL_BONE_HAND_R, RAGDOLL_BONE_THIGH_L, RAGDOLL_BONE_THIGH_R,
    RAGDOLL_BONE_CALF_L, RAGDOLL_BONE_CALF_R, RAGDOLL_BONE_FOOT_L, RAGDOLL_BONE_FOOT_R
};

/* ---- static world ---- */
typedef struct { double min[3], max[3]; } RagdollAabb;

/* Optional broadphase: fill `out` with up to max_out static boxes overlapping [lo, hi] and return how many (the pool clamps the
 * count and discards non-finite/inverted boxes, so a misbehaving callback cannot corrupt a body). */
typedef int (*RagdollBoxQueryFn)(void *user, const double lo[3], const double hi[3], RagdollAabb *out, int max_out);

typedef struct {
    double ground_y;                /* the floor plane (world y); handled inside grb with friction */
    const RagdollAabb *boxes;       /* BORROWED plain list scanned per body per tick (caller keeps it alive); NULL/0 = none */
    int box_count;
    RagdollBoxQueryFn query;        /* if non-NULL it is used INSTEAD of boxes/box_count */
    void *user;
} RagdollWorld;

/* ---- one death ---- */
typedef struct {
    const float *pose_rot;   /* skeleton-local joint quaternions x,y,z,w, joint_count*4 (what gseq_player_sample_pose writes). NULL = rest pose, arms lowered. */
    const float *pose_trans; /* joint_count*3 local translations; ONLY the pelvis entry is read (bone offsets are rigid). NULL = rest. */
    double x, y, z;          /* world position of the model origin (the feet), exactly what the living mesh was drawn at */
    double facing_rad;       /* rotation about +Y, as passed to gband_skel_npc_draw */
    int kind;                /* RAGDOLL_KIND_* (out-of-range values are clamped, never trusted) */
    int damage;              /* killing-blow damage, clamped 0..400 */
    int hit_type;            /* RAGDOLL_HIT_*; head always loads the neck/head segment */
    int hit_bone;            /* 0..16, or RAGDOLL_BONE_AUTO to derive from hit_height_mm / hit_lateral_mm */
    int hit_height_mm;       /* hit point height above the soles in mm; < 0 = unknown (per-kind default bone) */
    int hit_lateral_mm;      /* mm off the centreline, + = the victim's anatomical left (+X in model space) */
    double dir[3];           /* world direction the force travels (shooter->victim, blast centre->victim); any length; zero/NaN = no shove
                                (an explosion with no direction is thrown straight up) */
    int blast_dist_pm;       /* EXPLOSION only: distance from the blast centre / blast radius, permille (0 = centre) */
    double vel[3];           /* the victim's own world velocity at death, m/s (inherited per the rules' carry factor) */
    int local;               /* 1 = the local player caused or suffered it: strongly protected from eviction */
    uint32_t now_tick;       /* caller's tick, stored as spawn_tick for diagnostics */
} RagdollSpawn;

/* ---- pool ---- */
typedef struct {
    int state;               /* RAGDOLL_STATE_* */
    int kind, local;
    uint32_t seq;            /* monotonically increasing spawn number (wraps; compared by unsigned difference) */
    uint32_t spawn_tick;
    int age;                 /* internal 64 Hz ticks since spawn */
    int calm_ticks;          /* consecutive calm ticks */
    int frozen_ticks;        /* ticks since the freeze */
    int fade_pm;             /* 1000 visible .. 0 gone (the rules' fade curve; 1000 while ACTIVE) */
    double spawn_pos[3];
    double tick_pos[RAGDOLL_BONE_COUNT][3];  /* every segment's pose at the end of the previous tick: the calm rule measures NET motion */
    double tick_rot[RAGDOLL_BONE_COUNT][4];
    RigidRagdoll rr;         /* the XPBD bodies */
} RagdollSlot;

typedef struct {
    uint32_t spawned;        /* successful spawns */
    uint32_t evicted;        /* residents given up for a newcomer */
    uint32_t refused;        /* newcomers that scored below every resident */
    uint32_t froze;          /* ACTIVE -> FROZEN */
    uint32_t despawned;      /* FROZEN/ACTIVE -> FREE by timer */
    uint32_t bad_kills;      /* bodies killed by the integrity guard (non-finite state, runaway position, torn joints) */
    uint32_t poses_rejected; /* supplied poses that violated the joint limits and were replaced by the default */
    uint32_t ticks_run;      /* fixed 64 Hz ticks simulated */
    uint32_t frames_clipped; /* host frames whose backlog exceeded the catch-up cap (the excess was dropped) */
} RagdollPoolStats;

typedef struct RagdollPool {
    const GSkel *skel;       /* borrowed; must outlive the pool */
    int capacity;            /* 1..RAGDOLL_POOL_SLOTS */
    int ready;               /* 1 after a successful init */
    int pelvis_joint, upperarm_joint[2];
    int64_t acc;             /* time accumulator in (microseconds * 64); one tick = 1 000 000 */
    uint32_t next_seq;
    RagdollWorld world;
    int viewer_set;
    double viewer[3];
    RagdollPoolStats stats;
    RigidRagdoll tmpl;       /* pristine copy every spawn starts from (so a reused slot is bit-identical to a fresh one) */
    RagdollSlot slots[RAGDOLL_POOL_SLOTS];
} RagdollPool;

typedef struct {
    int state, kind, local, age, frozen_ticks, fade_permille;
    int priority;            /* the PARENA eviction score right now (0 for FREE) */
    uint32_t seq, spawn_tick;
    double pelvis[3];        /* world position of the pelvis body */
} RagdollInfo;

/* Builds the template ragdoll on `skel` and verifies that segment ids match the skeleton's joint names. capacity <= 0 or above
 * RAGDOLL_POOL_SLOTS means all slots. Returns 1 on success; on 0 the pool is inert (every call is a no-op / -1). */
int ragdoll_pool_init(RagdollPool *pool, const GSkel *skel, int capacity);

void ragdoll_pool_set_world(RagdollPool *pool, const RagdollWorld *world);   /* copies the struct (box list/callback stay borrowed) */
void ragdoll_pool_set_viewer(RagdollPool *pool, double x, double y, double z); /* the camera: far bodies are evicted first */

/* Spawns a ragdoll. Returns its slot, or -1 when the pool is inert, the arguments are NULL, or the pool is full and the newcomer
 * scores below every resident (then nothing is evicted). Every field of *sp is sanitised: NaN/inf/huge values, bad quaternions,
 * out-of-range enums and unknown bones can never corrupt the simulation. */
int ragdoll_pool_spawn(RagdollPool *pool, const RagdollSpawn *sp);

/* Advances by a host frame of dt_ms milliseconds (NaN/negative -> 0, clamped to ragdoll_max_dt_ms). Runs whole fixed 1/64 s ticks
 * from an exact integer accumulator, at most ragdoll_max_catchup_ticks per call; a longer backlog is dropped, never queued. */
void ragdoll_pool_step(RagdollPool *pool, double dt_ms);

/* The pose the renderer feeds to the existing GOLDEN BAND skinning path (gpose_compute_skin_matrices / gpose_skin_mesh):
 * out_rot = joint_count*4 floats (skeleton-local quaternions x,y,z,w, same shape as gseq_player_sample_pose),
 * out_trans = joint_count*3 floats (rest translations, except the pelvis which carries the body's WORLD position, so the
 * forward kinematics lands in world space). Returns 1 for an ACTIVE/FROZEN slot, 0 otherwise (outputs untouched). */
int ragdoll_pool_get_pose(const RagdollPool *pool, int slot, float *out_rot, float *out_trans);

/* Convenience: get_pose followed by gpose_compute_skin_matrices. out must hold joint_count matrices (float[16], column-major);
 * they are world-space skin matrices -- draw with an identity model transform (no npc_world bake). */
int ragdoll_pool_get_skin_matrices(const RagdollPool *pool, int slot, float out[][16]);

int ragdoll_pool_info(const RagdollPool *pool, int slot, RagdollInfo *out);  /* 0 for an invalid slot */
int ragdoll_pool_count(const RagdollPool *pool, int state);                  /* slots currently in `state` */
void ragdoll_pool_clear(RagdollPool *pool);                                  /* frees every slot, resets the accumulator; keeps world/viewer/stats */

/* ---- PARENA rules ABI (ragdoll_rules.c, generated from PARENA/stdlib/big_o/ragdoll_rules.prn) ----
 * Declared here so the integrator can call the unit conversions / weapon mapping directly, and so a drift between this list and
 * the generated file is a COMPILE error in the test (which includes both). */
int ragdoll_body_mass_kg(void);
int ragdoll_default_pool_size(void);
int ragdoll_max_catchup_ticks(void);
int ragdoll_max_dt_ms(void);
int ragdoll_speed_cap_mmps(void);
int ragdoll_omega_cap_mradps(void);
int ragdoll_max_dv_mmps(void);
int ragdoll_max_carry_mmps(void);
int ragdoll_default_arm_drop_mrad(void);
int ragdoll_max_pelvis_offset_mm(void);
int ragdoll_max_spawn_lift_mm(void);
int ragdoll_contact_friction_pct(void);
int ragdoll_contact_omega_pct(void);
int ragdoll_max_push_mm(void);
int ragdoll_query_max(void);
int ragdoll_clamp(int v, int lo, int hi);
int ragdoll_kind(int k);
int ragdoll_damage(int d);
int ragdoll_bone(int b);
int ragdoll_kind_from_weapon(int weapon);   /* SHANKPIT protocol.h WPN_* -> RAGDOLL_KIND_* */
int ragdoll_standing_height_mm(void);
int ragdoll_lateral_unit_mm(void);
int ragdoll_height_permille(int dy_mm);
int ragdoll_lateral_permille(int dx_mm);
int ragdoll_default_bone(int kind);
int ragdoll_bone_from_hit(int kind, int hit_type, int h_permille, int lat_permille);
int ragdoll_impulse_mmps(int kind, int damage, int dist_pm);
int ragdoll_falloff_permille(int kind, int dist_pm);
int ragdoll_bone_gain_permille(int bone);
int ragdoll_blast_share_permille(int bone);
int ragdoll_spread_permille(int kind);
int ragdoll_segment_share_permille(int kind, int hit_bone, int seg_bone);
int ragdoll_dv_mmps(int mag, int share, int dir_pm);
int ragdoll_lift_mmps(int kind, int mag, int share);
int ragdoll_spin_mradps(int kind, int mag, int bone);
int ragdoll_carry_permille(int kind);
int ragdoll_calm_tick(int lin_mmps, int ang_mradps, int gap_mm);
int ragdoll_min_active_ticks(int kind);
int ragdoll_settle_ticks(int kind);
int ragdoll_max_active_ticks(int kind);
int ragdoll_should_freeze(int kind, int age, int calm_ticks);
int ragdoll_frozen_hold_ticks(int kind);
int ragdoll_fade_ticks(void);
int ragdoll_hard_cap_ticks(void);
int ragdoll_should_despawn(int kind, int age, int frozen_ticks);
int ragdoll_fade_permille(int kind, int frozen_ticks);
int ragdoll_priority(int state, int age, int dist_mm, int kind, int local, int fade_pm);
int ragdoll_evict_ok(int newcomer_score, int victim_score);

#endif /* SHANKPIT_RAGDOLL_POOL_H */
