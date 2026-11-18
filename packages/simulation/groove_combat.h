/* groove_combat.h -- the host for groove combat: close-quarters fighting timed to the music.
 *
 * Founder 2026-11-18: "bring in combat affordances i want to add smooth criminal style fighting ... all PARENA powered
 * all mods first all reflux powered" and "bring it all into the construct and zombie sandbox environments with real
 * player facing affordances".
 *
 * The rules -- which move an input means, its frame data, reach, damage, the beat grade, combos, style -- are PARENA:
 * PARENA/stdlib/shankpit/groove_combat_rules.prn, generated into groove_combat_rules.c (`make regen-groove-combat`).
 * This header only keeps each fighter's state, asks those rules, does the geometry, and puts every event on the
 * REFLUX log (REFLUX_ACTION_COMBAT_*, 1200-1299), so the HUD, a level script or a crowd mod reacts to the fight
 * without touching this file. It applies no damage itself: the game mode does, with the numbers returned here.
 *
 * The beat: pass where the move starts inside the current beat, 0..999. MIXFORGE's deck gives it
 * (audio_dsp_gen.h deck_beat_phase); with no deck playing, groove_fallback_phase gives a steady 120 BPM.
 */
#ifndef SHANKPIT_GROOVE_COMBAT_H
#define SHANKPIT_GROOVE_COMBAT_H

#include <math.h>
#include <string.h>

#ifndef GROOVE_MAX_FIGHTERS
#define GROOVE_MAX_FIGHTERS 70   /* protocol.h MAX_CLIENTS */
#endif

/* the PARENA rules (groove_combat_rules.c) */
int gc_move_count(void);
int gc_startup_ticks(int); int gc_active_ticks(int); int gc_recovery_ticks(int); int gc_total_ticks(int);
int gc_phase(int, int);
int gc_damage(int); int gc_range_milli(int); int gc_arc_deg(int); int gc_knockback_milli(int); int gc_knockdown(int);
int gc_iframes(int); int gc_hits_all_around(int);
int gc_grade(int); int gc_grade_mult_pct(int);
int gc_can_chain(int, int); int gc_can_cancel(int, int, int); int gc_combo_next(int, int, int, int, int);
int gc_combo_mult_pct(int); int gc_final_damage(int, int, int);
int gc_style_gain(int, int, int); int gc_style_decay(void); int gc_style_max(void);
int gc_freeze_cost(void); int gc_freeze_radius_milli(void); int gc_freeze_stagger_ticks(void);
int gc_move_for_input(int, int, int, int, int, int, int);
int gc_anim_clip(int); int gc_move_name_id(int);
int gs_variety_pct(int); int gs_rank(int);

/* REFLUX: the engine's own dispatcher (reflux_runtime.c); a test may define its own. */
void reflux_dispatch(int action_type, int a, int b, int c);
#include "../reflux/reflux_runtime.h"

enum { GC_MOVE_NONE = 0, GC_MOVE_JAB, GC_MOVE_SNAP_KICK, GC_MOVE_SPIN, GC_MOVE_LEAN, GC_MOVE_GLIDE, GC_MOVE_SWEEP,
       GC_MOVE_FREEZE, GC_MOVE_COUNT };
enum { GC_GRADE_OFF = 0, GC_GRADE_GOOD = 1, GC_GRADE_PERFECT = 2 };
enum { GC_PHASE_STARTUP = 0, GC_PHASE_ACTIVE = 1, GC_PHASE_RECOVERY = 2, GC_PHASE_DONE = 3 };

/* A combo survives this many idle ticks after a move ends (about one beat at 120 BPM, 60 ticks/s). */
#define GROOVE_COMBO_GRACE_TICKS 32

/* The HUD's move names (English; the HUD may swap in its own locale table by gc_move_name_id). */
static const char *const GROOVE_MOVE_NAMES[GC_MOVE_COUNT] = {
    "", "JAB", "SNAP KICK", "SPIN", "LEAN", "GLIDE", "SWEEP", "FREEZE"
};
static const char *const GROOVE_GRADE_NAMES[3] = { "OFF BEAT", "GOOD", "PERFECT" };

typedef struct {
    int move;            /* GC_MOVE_*, NONE when free */
    int tick;            /* ticks since the move started */
    int grade;           /* GC_GRADE_* the move started with */
    int combo;           /* current combo count (0 = none) */
    int style;           /* 0..gc_style_max() */
    int idle;            /* ticks since the last move ended */
    int stagger;         /* ticks left staggered (cannot start a move) */
    int down;            /* ticks left knocked down (cannot start a move) */
    int last_hits;       /* victims hit by the current move */
    int rank;            /* style rank, 0 D .. 6 SSS (groove_sync.h groove_rank_update) */
    int last_kind;       /* the last style source: a GC_MOVE_*, or 100 + WPN_* for a gun hit */
    int repeats;         /* how many times in a row that source repeated (variety: groove_sync_rules gs-variety-pct) */
    unsigned char hit[GROOVE_MAX_FIGHTERS];  /* victims already hit by the current move (one hit per move each) */
} GrooveFighter;

static inline void groove_init(GrooveFighter *f) { memset(f, 0, sizeof *f); }

/* A steady 120 BPM beat phase (0..999) from the engine clock, for when no MIXFORGE deck is playing. */
static inline int groove_fallback_phase(unsigned int now_ms) { return (int)((now_ms % 500u) * 2u); }

static inline int groove_can_act(const GrooveFighter *f) { return f->stagger <= 0 && f->down <= 0; }

static inline int groove_phase(const GrooveFighter *f) { return gc_phase(f->move, f->tick); }

/* 1 while the current move makes the fighter untouchable (the glide's whole run, the lean's first ticks). */
static inline int groove_invulnerable(const GrooveFighter *f) {
    return f->move != GC_MOVE_NONE && f->tick < gc_iframes(f->move);
}

/* The anim_rules clip to play while a move runs, or -1 to let locomotion decide. */
static inline int groove_anim_clip(const GrooveFighter *f) {
    return (f->move != GC_MOVE_NONE && groove_phase(f) != GC_PHASE_DONE) ? gc_anim_clip(f->move) : -1;
}

/* Start `move` now, if the rules allow it after the current one. Returns 1 when it started. */
static inline int groove_start(GrooveFighter *f, int player_id, int move, int beat_phase_milli) {
    int from, chained, grade;
    if (move <= GC_MOVE_NONE || move >= GC_MOVE_COUNT || !groove_can_act(f)) return 0;
    from = f->move;
    if (from != GC_MOVE_NONE && !gc_can_cancel(from, move, f->tick)) return 0;
    /* chained: straight out of a running move, or within the grace after one ended */
    chained = (from != GC_MOVE_NONE) || (f->combo > 0 && f->idle <= GROOVE_COMBO_GRACE_TICKS);
    grade = gc_grade(beat_phase_milli);
    f->combo = gc_combo_next(f->combo, from, move, grade, chained);
    if (move == GC_MOVE_FREEZE) {
        f->style -= gc_freeze_cost();
        if (f->style < 0) f->style = 0;
    } else {
        f->repeats = (move == f->last_kind) ? f->repeats + 1 : 0;
        f->last_kind = move;
        f->style += gc_style_gain(move, grade, f->combo) * gs_variety_pct(f->repeats) / 100;
        if (f->style > gc_style_max()) f->style = gc_style_max();
    }
    f->move = move;
    f->tick = 0;
    f->grade = grade;
    f->idle = 0;
    f->last_hits = 0;
    memset(f->hit, 0, sizeof f->hit);
    reflux_dispatch(REFLUX_ACTION_COMBAT_MOVE_STARTED, player_id, move, grade);
    if (f->combo >= 2) reflux_dispatch(REFLUX_ACTION_COMBAT_COMBO, player_id, f->combo, f->style);
    if (gs_rank(f->style) != f->rank) {
        f->rank = gs_rank(f->style);
        reflux_dispatch(REFLUX_ACTION_COMBAT_RANK_CHANGED, player_id, f->rank, f->style);
    }
    return 1;
}

/* The player-facing input: buttons are 0/1 for this tick (edge-triggered by the caller). Returns the move started,
 * or GC_MOVE_NONE. */
static inline int groove_input(GrooveFighter *f, int player_id, int attack, int style_btn, int fwd, int back,
                               int crouch, int airborne, int beat_phase_milli) {
    int move = gc_move_for_input(attack, style_btn, fwd, back, crouch, airborne, f->style);
    if (move == GC_MOVE_NONE) return GC_MOVE_NONE;
    return groove_start(f, player_id, move, beat_phase_milli) ? move : GC_MOVE_NONE;
}

/* Advance one tick. */
static inline void groove_tick(GrooveFighter *f) {
    if (f->stagger > 0) f->stagger--;
    if (f->down > 0) f->down--;
    if (f->move != GC_MOVE_NONE) {
        f->tick++;
        if (gc_phase(f->move, f->tick) == GC_PHASE_DONE) {
            f->move = GC_MOVE_NONE;
            f->tick = 0;
            f->idle = 0;
        }
        return;
    }
    f->idle++;
    if (f->idle > GROOVE_COMBO_GRACE_TICKS) f->combo = 0;
    f->style -= gc_style_decay();
    if (f->style < 0) f->style = 0;
}

/* 1 on exactly the first active tick of a freeze: the caller staggers everyone within groove_freeze_radius, then
 * reports how many with groove_freeze_report. */
static inline int groove_freeze_fires(const GrooveFighter *f) {
    return f->move == GC_MOVE_FREEZE && f->tick == gc_startup_ticks(GC_MOVE_FREEZE);
}
static inline float groove_freeze_radius(void) { return (float)gc_freeze_radius_milli() / 1000.0f; }
static inline void groove_freeze_report(int player_id, int staggered) {
    reflux_dispatch(REFLUX_ACTION_COMBAT_FREEZE, player_id, staggered, gc_freeze_cost());
}
static inline void groove_stagger(GrooveFighter *victim) {
    victim->stagger = gc_freeze_stagger_ticks();
    victim->move = GC_MOVE_NONE;
    victim->tick = 0;
}

/* Does the attacker's current move hit a target at (vx, vz) this tick? Attacker at (ax, az) facing `yaw_rad`
 * (the engine's yaw, 0 = -z, matching physics.h's forward vector). Returns the damage to deal (0 = no hit) and
 * dispatches COMBAT_HIT (and COMBAT_KNOCKDOWN). A target is hit at most once per move; an invulnerable defender
 * (pass its fighter, or NULL for a non-fighter such as a zombie) evades instead (COMBAT_EVADED). */
static inline int groove_hit(GrooveFighter *f, int attacker_id, float ax, float az, float yaw_rad,
                             int victim_id, float vx, float vz, GrooveFighter *defender) {
    float dx, dz, dist, reach, fx, fz, cosang, limit;
    int dmg;
    if (f->move == GC_MOVE_NONE || groove_phase(f) != GC_PHASE_ACTIVE) return 0;
    if (victim_id < 0 || victim_id >= GROOVE_MAX_FIGHTERS || victim_id == attacker_id) return 0;
    if (f->hit[victim_id]) return 0;
    if (gc_range_milli(f->move) <= 0) return 0;
    dx = vx - ax; dz = vz - az;
    dist = sqrtf(dx * dx + dz * dz);
    reach = (float)gc_range_milli(f->move) / 1000.0f;
    if (dist > reach) return 0;
    if (!gc_hits_all_around(f->move) && dist > 0.001f) {
        fx = sinf(yaw_rad); fz = -cosf(yaw_rad);
        cosang = (dx * fx + dz * fz) / dist;
        limit = cosf((float)gc_arc_deg(f->move) * 3.14159265f / 180.0f);
        if (cosang < limit) return 0;
    }
    f->hit[victim_id] = 1;
    if (defender && groove_invulnerable(defender)) {
        reflux_dispatch(REFLUX_ACTION_COMBAT_EVADED, victim_id, attacker_id, defender->move);
        return 0;
    }
    dmg = gc_final_damage(f->move, f->grade, f->combo);
    f->last_hits++;
    reflux_dispatch(REFLUX_ACTION_COMBAT_HIT, attacker_id, victim_id, dmg);
    if (gc_knockdown(f->move)) {
        if (defender) { defender->down = 60; defender->move = GC_MOVE_NONE; defender->tick = 0; }
        reflux_dispatch(REFLUX_ACTION_COMBAT_KNOCKDOWN, attacker_id, victim_id, f->move);
    }
    return dmg;
}

/* The push to give a victim just hit, along the attacker's facing (or straight away from it, for a spin),
 * in units/tick. */
static inline void groove_knockback(const GrooveFighter *f, float ax, float az, float yaw_rad, float vx, float vz,
                                    float *out_x, float *out_z) {
    float k = (float)gc_knockback_milli(f->move) / 1000.0f;
    float dx = vx - ax, dz = vz - az, d;
    if (gc_hits_all_around(f->move)) {
        d = sqrtf(dx * dx + dz * dz);
        if (d < 0.001f) { dx = sinf(yaw_rad); dz = -cosf(yaw_rad); d = 1.0f; }
        *out_x = dx / d * k; *out_z = dz / d * k;
    } else {
        *out_x = sinf(yaw_rad) * k; *out_z = -cosf(yaw_rad) * k;
    }
}

#endif
