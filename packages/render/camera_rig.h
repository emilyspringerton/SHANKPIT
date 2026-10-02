/* camera_rig.h -- SHANKPIT's broadcast camera rig (cards #456 realistic cameras / #457 director / #458 serialization).
 *
 * A rig is a list of cameras (fixed tripod, follow-cam, orbit, drone), each run by a simulated OPERATOR who lags the
 * action, leads a moving subject, zooms on distant action and shakes a little, plus an auto DIRECTOR that picks who to
 * watch and when to cut. Every decision is a PARENA rule (PARENA/stdlib/shankpit/camera_rules.prn, generated into
 * camera_rules.c, `make regen-camera-rules`); this header is the plumbing: cameras, per-frame update, and the JSON rig
 * file that is the "scene collection" of a native stream (the OBS replacement, card #458) -- a rig round-trips through
 * camrig_serialize / camrig_parse exactly, so a broadcast setup can be saved, shared, and reloaded.
 *
 * Deliberately independent of physics.h: it consumes plain CamActor records (the lobby fills them from PlayerState),
 * which is what makes the whole thing headless-testable (camera_rig_test.c). Units: world units; velocities in units
 * per SECOND; yaw in the simulation's degrees (0 faces -Z). */
#ifndef CAMERA_RIG_H
#define CAMERA_RIG_H

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "level_boxes.h"   /* the small JSON scanner helpers (find_key / parse_number / parse_string) */

/* PARENA-generated (camera_rules.c) */
int cam_reaction_ms(int);
int cam_smooth_step(int, int, int, int);
int cam_lead_milli(int, int);
int cam_action_score(int, int, int, int, int);
int cam_should_cut(int, int, int, int, int);
int cam_shake_milli(int, int);
int cam_orbit_angle_milli(int, int);
int cam_fov_milli(int);

#define CAMRIG_MAX 12
#define CAMRIG_NAME 24
#define CAMRIG_DEFAULT_HOLD_MS 3500
#define CAMRIG_DEFAULT_HYST 200
#define CAMRIG_THINK_MS 250
#define CAMRIG_LEAD_MS 400
#define CAMRIG_SHAKE_UNITS 0.35f   /* shake intensity 1000 = +-0.35 world units */

typedef enum { CAM_FIXED = 0, CAM_FOLLOW = 1, CAM_ORBIT = 2, CAM_DRONE = 3 } CamKind;

typedef struct {
    char name[CAMRIG_NAME];
    int kind;                        /* CamKind */
    float x, y, z;                   /* FIXED: the tripod position */
    float aim_x, aim_y, aim_z;       /* FIXED with subject < 0 and no director subject: where it looks */
    int subject;                     /* player id to follow, or -1 = whoever the director is watching */
    float radius, height;            /* FOLLOW: distance behind + height; ORBIT/DRONE: ring radius + height */
    int orbit_ms;                    /* ORBIT: one lap (0 = 20000) */
    int skill;                       /* operator skill 0..1000 (reaction lag) */
    int shake;                       /* handheld shake 0..1000 */
    float fov;                       /* degrees; 0 = the zoom operator picks (tighter when far) */
    /* runtime (not serialized) */
    float s_eye[3], s_aim[3];
    int primed;
} CamDef;

typedef struct {
    CamDef cam[CAMRIG_MAX];
    int count;
    int program, preview;            /* indices into cam[]: what is on air / queued */
    int auto_cut;                    /* the director cuts by itself */
    int hold_ms, hysteresis;         /* min shot length; how much better a new shot must be (permille) */
    /* runtime */
    int director_subject;            /* player the director currently watches (-1 none) */
    unsigned int shot_start_ms, next_think_ms;
    int shot_score;
    int cuts;                        /* how many cuts the director has made (diagnostics/tests) */
} CameraRig;

typedef struct {
    int id, active, dead, team;
    float x, y, z, yaw;
    float vx, vz;                    /* units per second */
    int health, shooting;
    unsigned int last_kill_ms;       /* 0 = never */
} CamActor;

typedef struct { float eye[3], aim[3], fov; } CamView;

/* ---- construction ------------------------------------------------------------------------------ */

static inline void camrig_add(CameraRig *r, const char *name, int kind, float x, float y, float z, int subject,
                              float radius, float height, int orbit_ms, int skill, int shake) {
    if (r->count >= CAMRIG_MAX) return;
    CamDef *c = &r->cam[r->count++];
    memset(c, 0, sizeof(*c));
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->kind = kind; c->x = x; c->y = y; c->z = z; c->subject = subject;
    c->radius = radius; c->height = height; c->orbit_ms = orbit_ms; c->skill = skill; c->shake = shake;
}

/* camrig_default -- a sensible 4-camera esports setup: a hero follow-cam, an orbiter, a high wide tripod and a drone. */
static inline void camrig_default(CameraRig *r) {
    memset(r, 0, sizeof(*r));
    camrig_add(r, "Hero", CAM_FOLLOW, 0, 0, 0, -1, 11.0f, 4.5f, 0, 800, 90);
    camrig_add(r, "Orbit", CAM_ORBIT, 0, 0, 0, -1, 28.0f, 9.0f, 20000, 700, 30);
    camrig_add(r, "Wide", CAM_FIXED, 0, 38.0f, 55.0f, -1, 0, 0, 0, 500, 0);
    camrig_add(r, "Drone", CAM_DRONE, 0, 0, 0, -1, 22.0f, 34.0f, 0, 600, 60);
    r->auto_cut = 1; r->hold_ms = CAMRIG_DEFAULT_HOLD_MS; r->hysteresis = CAMRIG_DEFAULT_HYST;
    r->program = 0; r->preview = 1; r->director_subject = -1;
}

/* ---- the director ------------------------------------------------------------------------------ */

static inline const CamActor *camrig_find_actor(const CamActor *a, int n, int id) {
    for (int i = 0; i < n; i++) if (a[i].active && a[i].id == id) return &a[i];
    return NULL;
}

/* How interesting is `a` right now (PARENA cam_action_score over its nearest enemy, speed, kill recency, ...). */
static inline int camrig_actor_score(const CamActor *a, int n, const CamActor *self, unsigned int now_ms) {
    float best = 1e30f;
    for (int i = 0; i < n; i++) {
        const CamActor *o = &a[i];
        if (!o->active || o->dead || o == self) continue;
        if (self->team >= 0 && o->team == self->team) continue;
        float dx = o->x - self->x, dy = o->y - self->y, dz = o->z - self->z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < best) best = d;
    }
    int dist = best > 1e8f ? 2000000 : (int)(best * 1000.0f);
    int speed = (int)(sqrtf(self->vx * self->vx + self->vz * self->vz) * 1000.0f);
    int since = self->last_kill_ms == 0 || now_ms < self->last_kill_ms ? 1000000 : (int)(now_ms - self->last_kill_ms);
    return cam_action_score(dist, speed, since, self->shooting ? 1 : 0, self->health);
}

/* How well camera `c` can show a subject at (sx,sy,sz): chasing cameras always frame it; a tripod is best ~25 units off. */
static inline int camrig_shot_quality(const CamDef *c, float sx, float sy, float sz) {
    if (c->kind != CAM_FIXED) return 600;
    float dx = c->x - sx, dy = c->y - sy, dz = c->z - sz;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    int q = 1000 - (int)(fabsf(d - 25.0f) * 25.0f);
    return q < 0 ? 0 : q;
}

static inline void camrig_director_think(CameraRig *r, const CamActor *a, int n, unsigned int now_ms) {
    if (r->count == 0) return;
    const CamActor *best = NULL; int best_score = -1;
    for (int i = 0; i < n; i++) {
        if (!a[i].active || a[i].dead) continue;
        int s = camrig_actor_score(a, n, &a[i], now_ms);
        if (s > best_score) { best_score = s; best = &a[i]; }
    }
    if (!best) return;
    /* the best camera for the best subject, and the runner-up for preview */
    int bc = 0, bq = -1, pc = -1, pq = -1;
    for (int i = 0; i < r->count; i++) {
        int q = camrig_shot_quality(&r->cam[i], best->x, best->y, best->z);
        if (q > bq) { pc = bc; pq = bq; bc = i; bq = q; }
        else if (q > pq) { pc = i; pq = q; }
    }
    int cand = best_score + bq;
    const CamActor *cur = camrig_find_actor(a, n, r->director_subject);
    int cur_score = 0;
    if (cur && !cur->dead && r->program >= 0 && r->program < r->count)
        cur_score = camrig_actor_score(a, n, cur, now_ms) + camrig_shot_quality(&r->cam[r->program], cur->x, cur->y, cur->z);
    int same_shot = (cur == best && r->program == bc);
    if (!same_shot && (r->director_subject < 0 || !cur || cur->dead ||
                       cam_should_cut(cur_score, cand, (int)(now_ms - r->shot_start_ms), r->hold_ms, r->hysteresis))) {
        r->director_subject = best->id;
        r->program = bc;
        r->shot_start_ms = now_ms;
        r->shot_score = cand;
        r->cuts++;
    } else if (same_shot) {
        r->shot_score = cand;
    }
    r->preview = (pc >= 0 && pc != r->program) ? pc : (r->program + 1) % r->count;
}

/* ---- the operators ----------------------------------------------------------------------------- */

static inline void camrig_desired(const CameraRig *r, const CamDef *c, const CamActor *a, int n, unsigned int now_ms,
                                  float eye[3], float aim[3]) {
    int sid = c->subject >= 0 ? c->subject : r->director_subject;
    const CamActor *s = camrig_find_actor(a, n, sid);
    if (!s) { /* nobody to watch: hold position, look at the configured aim point */
        eye[0] = c->x; eye[1] = c->y; eye[2] = c->z;
        aim[0] = c->aim_x; aim[1] = c->aim_y; aim[2] = c->aim_z;
        return;
    }
    float lead_x = (float)cam_lead_milli((int)(s->vx * 1000.0f), CAMRIG_LEAD_MS) * 0.001f;
    float lead_z = (float)cam_lead_milli((int)(s->vz * 1000.0f), CAMRIG_LEAD_MS) * 0.001f;
    aim[0] = s->x + lead_x; aim[1] = s->y + 1.4f; aim[2] = s->z + lead_z;
    float ry = -s->yaw * 0.0174533f;
    float fx = sinf(ry), fz = -cosf(ry);
    switch (c->kind) {
    case CAM_FIXED:  eye[0] = c->x; eye[1] = c->y; eye[2] = c->z; break;
    case CAM_FOLLOW: eye[0] = s->x - fx * c->radius; eye[1] = s->y + c->height; eye[2] = s->z - fz * c->radius; break;
    case CAM_ORBIT: {
        int period = c->orbit_ms > 0 ? c->orbit_ms : 20000;
        float ang = (float)cam_orbit_angle_milli((int)now_ms, period) * 0.001f * 0.0174533f;
        eye[0] = s->x + cosf(ang) * c->radius; eye[1] = s->y + c->height; eye[2] = s->z + sinf(ang) * c->radius;
        break; }
    default:         eye[0] = s->x; eye[1] = s->y + c->height; eye[2] = s->z + c->radius; break;   /* CAM_DRONE */
    }
}

/* One frame: the director thinks (every CAMRIG_THINK_MS) and every operator trails its subject by its own reaction time. */
static inline void camrig_update(CameraRig *r, const CamActor *a, int n, unsigned int now_ms, int dt_ms) {
    if (dt_ms < 0) dt_ms = 0;
    if (dt_ms > 100) dt_ms = 100;                 /* cam_smooth_step's I32 range assumes sane frame times */
    if (r->auto_cut && now_ms >= r->next_think_ms) {
        r->next_think_ms = now_ms + CAMRIG_THINK_MS;
        camrig_director_think(r, a, n, now_ms);
    }
    for (int i = 0; i < r->count; i++) {
        CamDef *c = &r->cam[i];
        float eye[3], aim[3];
        camrig_desired(r, c, a, n, now_ms, eye, aim);
        if (!c->primed) { memcpy(c->s_eye, eye, sizeof(eye)); memcpy(c->s_aim, aim, sizeof(aim)); c->primed = 1; continue; }
        int tau = cam_reaction_ms(c->skill);
        for (int k = 0; k < 3; k++) {
            if (c->kind != CAM_FIXED)
                c->s_eye[k] = (float)cam_smooth_step((int)(c->s_eye[k] * 1000.0f), (int)(eye[k] * 1000.0f), dt_ms, tau * 2) * 0.001f;
            else c->s_eye[k] = eye[k];
            c->s_aim[k] = (float)cam_smooth_step((int)(c->s_aim[k] * 1000.0f), (int)(aim[k] * 1000.0f), dt_ms, tau) * 0.001f;
        }
    }
}

/* camrig_view -- what camera `i` sees right now: smoothed eye/aim + handheld shake + the zoom operator's FOV. */
static inline int camrig_view(const CameraRig *r, int i, unsigned int now_ms, CamView *v) {
    if (i < 0 || i >= r->count) return 0;
    const CamDef *c = &r->cam[i];
    float amp = CAMRIG_SHAKE_UNITS * 0.001f;
    int t = (int)(now_ms & 0x7fffffffu) % 1000000;
    float sx = (float)cam_shake_milli(t + i * 977, c->shake) * amp;
    float sy = (float)cam_shake_milli(t + i * 977 + 311, c->shake) * amp;
    v->eye[0] = c->s_eye[0] + sx; v->eye[1] = c->s_eye[1] + sy; v->eye[2] = c->s_eye[2];
    memcpy(v->aim, c->s_aim, sizeof(v->aim));
    if (c->fov > 1.0f) v->fov = c->fov;
    else {
        float dx = v->aim[0] - v->eye[0], dy = v->aim[1] - v->eye[1], dz = v->aim[2] - v->eye[2];
        v->fov = (float)cam_fov_milli((int)(sqrtf(dx * dx + dy * dy + dz * dz) * 1000.0f)) * 0.001f;
    }
    return 1;
}

/* ---- serialization (the rig file) -------------------------------------------------------------- */

static inline const char *camrig_kind_name(int k) {
    switch (k) { case CAM_FOLLOW: return "follow"; case CAM_ORBIT: return "orbit"; case CAM_DRONE: return "drone"; default: return "fixed"; }
}
static inline int camrig_kind_from_name(const char *s) {
    if (!strcmp(s, "follow")) return CAM_FOLLOW;
    if (!strcmp(s, "orbit")) return CAM_ORBIT;
    if (!strcmp(s, "drone")) return CAM_DRONE;
    return CAM_FIXED;
}

/* camrig_serialize -- the rig as one JSON document (deterministic: same rig, same bytes). Returns the length, or -1
 * if `cap` is too small. Runtime state (smoothing, director subject) is intentionally NOT saved: a rig file is a
 * setup, not a recording. */
static inline int camrig_serialize(const CameraRig *r, char *buf, size_t cap) {
    size_t off = 0;
#define CR_PRINT(...) do { int w_ = snprintf(buf + off, off < cap ? cap - off : 0, __VA_ARGS__); if (w_ < 0 || off + (size_t)w_ >= cap) return -1; off += (size_t)w_; } while (0)
    CR_PRINT("{\"camrig_version\":1,\"auto_cut\":%d,\"hold_ms\":%d,\"hysteresis\":%d,\"program\":%d,\"preview\":%d,\"cameras\":[",
             r->auto_cut ? 1 : 0, r->hold_ms, r->hysteresis, r->program, r->preview);
    for (int i = 0; i < r->count; i++) {
        const CamDef *c = &r->cam[i];
        char name[CAMRIG_NAME];
        for (int k = 0; k < CAMRIG_NAME; k++) {   /* keep the file parseable: no quotes/braces/control chars in names */
            char ch = c->name[k];
            name[k] = (ch && !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '_' || ch == '-')) ? '_' : ch;
            if (!ch) break;
        }
        name[CAMRIG_NAME - 1] = '\0';
        CR_PRINT("%s{\"name\":\"%s\",\"kind\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"aim_x\":%.3f,\"aim_y\":%.3f,\"aim_z\":%.3f,"
                 "\"subject\":%d,\"radius\":%.3f,\"height\":%.3f,\"orbit_ms\":%d,\"skill\":%d,\"shake\":%d,\"fov\":%.3f}",
                 i ? "," : "", name, camrig_kind_name(c->kind), c->x, c->y, c->z, c->aim_x, c->aim_y, c->aim_z,
                 c->subject, c->radius, c->height, c->orbit_ms, c->skill, c->shake, c->fov);
    }
    CR_PRINT("]}");
#undef CR_PRINT
    return (int)off;
}

static inline int camrig_clampi(float v, int lo, int hi) { int i = (int)v; return i < lo ? lo : (i > hi ? hi : i); }

/* camrig_parse -- a rig file -> *out. Returns the camera count, or 0 on anything that is not a v1 rig with at least
 * one camera (and then *out is left exactly as it was). Numbers are clamped to sane ranges; an unknown kind is a
 * fixed camera; more than CAMRIG_MAX cameras are dropped. */
static inline int camrig_parse(CameraRig *out, const char *json) {
    const char *end = json + strlen(json);
    const char *v;
    float ver = 0;
    if (!(v = level_boxes_find_key(json, end, "camrig_version")) || !level_boxes_parse_number(v, &ver) || (int)ver != 1) return 0;
    const char *key = level_boxes_find_key(json, end, "cameras");
    if (!key) return 0;
    const char *arr = level_boxes_skip_ws(key);
    if (*arr != '[') return 0;
    const char *arr_end = level_boxes_find_array_end(arr, end);
    if (!arr_end) return 0;
    CameraRig r;
    memset(&r, 0, sizeof(r));
    r.director_subject = -1;
    float f;
    if ((v = level_boxes_find_key(json, arr, "auto_cut")) && level_boxes_parse_number(v, &f)) r.auto_cut = f != 0;
    r.hold_ms = CAMRIG_DEFAULT_HOLD_MS; r.hysteresis = CAMRIG_DEFAULT_HYST;
    if ((v = level_boxes_find_key(json, arr, "hold_ms")) && level_boxes_parse_number(v, &f)) r.hold_ms = camrig_clampi(f, 0, 600000);
    if ((v = level_boxes_find_key(json, arr, "hysteresis")) && level_boxes_parse_number(v, &f)) r.hysteresis = camrig_clampi(f, 0, 5000);
    const char *cur = arr + 1;
    while (cur < arr_end && r.count < CAMRIG_MAX) {
        cur = level_boxes_skip_ws(cur);
        if (cur >= arr_end) break;
        if (*cur != '{') { cur++; continue; }
        const char *o0 = cur, *o1 = strchr(o0, '}');
        if (!o1 || o1 > arr_end) break;
        CamDef *c = &r.cam[r.count];
        memset(c, 0, sizeof(*c));
        c->subject = -1; c->skill = 600;
        char s[CAMRIG_NAME];
        if ((v = level_boxes_find_key(o0, o1, "name")) && level_boxes_parse_string(v, s, sizeof(s))) snprintf(c->name, sizeof(c->name), "%s", s);
        else snprintf(c->name, sizeof(c->name), "Cam %d", r.count + 1);
        if ((v = level_boxes_find_key(o0, o1, "kind")) && level_boxes_parse_string(v, s, sizeof(s))) c->kind = camrig_kind_from_name(s);
        if ((v = level_boxes_find_key(o0, o1, "x"))) level_boxes_parse_number(v, &c->x);
        if ((v = level_boxes_find_key(o0, o1, "y"))) level_boxes_parse_number(v, &c->y);
        if ((v = level_boxes_find_key(o0, o1, "z"))) level_boxes_parse_number(v, &c->z);
        if ((v = level_boxes_find_key(o0, o1, "aim_x"))) level_boxes_parse_number(v, &c->aim_x);
        if ((v = level_boxes_find_key(o0, o1, "aim_y"))) level_boxes_parse_number(v, &c->aim_y);
        if ((v = level_boxes_find_key(o0, o1, "aim_z"))) level_boxes_parse_number(v, &c->aim_z);
        if ((v = level_boxes_find_key(o0, o1, "subject")) && level_boxes_parse_number(v, &f)) c->subject = camrig_clampi(f, -1, 255);
        if ((v = level_boxes_find_key(o0, o1, "radius")) && level_boxes_parse_number(v, &f)) c->radius = f < 0 ? 0 : (f > 500 ? 500 : f);
        if ((v = level_boxes_find_key(o0, o1, "height")) && level_boxes_parse_number(v, &f)) c->height = f < -100 ? -100 : (f > 500 ? 500 : f);
        if ((v = level_boxes_find_key(o0, o1, "orbit_ms")) && level_boxes_parse_number(v, &f)) c->orbit_ms = camrig_clampi(f, 0, 580000);
        if ((v = level_boxes_find_key(o0, o1, "skill")) && level_boxes_parse_number(v, &f)) c->skill = camrig_clampi(f, 0, 1000);
        if ((v = level_boxes_find_key(o0, o1, "shake")) && level_boxes_parse_number(v, &f)) c->shake = camrig_clampi(f, 0, 1000);
        if ((v = level_boxes_find_key(o0, o1, "fov")) && level_boxes_parse_number(v, &f)) c->fov = f < 0 ? 0 : (f > 170 ? 170 : f);
        r.count++;
        cur = o1 + 1;
    }
    if (r.count == 0) return 0;
    if ((v = level_boxes_find_key(json, arr, "program")) && level_boxes_parse_number(v, &f)) r.program = camrig_clampi(f, 0, r.count - 1);
    r.preview = r.count > 1 ? (r.program + 1) % r.count : 0;
    if ((v = level_boxes_find_key(json, arr, "preview")) && level_boxes_parse_number(v, &f)) r.preview = camrig_clampi(f, 0, r.count - 1);
    *out = r;
    return r.count;
}

#endif
