/* groove_sync.h -- the beat, found and used: the shank's SYNC DEVICE (tap tempo on the knife, key 1), the AR synced to
 * that beat (key 3), the style ranks D..SSS with their powerups, and BPM detection from the music.
 *
 * Founder 2026-11-18: "add bpm detection and powerups for matching the tempo of the beat similar to a devvil may cry
 * combo with both physical combat and arsenal ... add a bpm sync device as the special for the shank so it sets your AR
 * damage and rof based on the current beat you have to switch to 1 for knife sync the beat and then back to 3 to use
 * it".
 *
 * Rules: PARENA/stdlib/shankpit/groove_sync_rules.prn -> groove_sync_rules.c (`make regen-groove-combat`). This header
 * keeps the state (tap times, the lock, the detector's comb bank) and dispatches REFLUX COMBAT_SYNC_LOCKED/EXPIRED,
 * COMBAT_RANK_CHANGED and COMBAT_BPM_DETECTED. The style meter and the rank live on the GrooveFighter
 * (groove_combat.h), so moves and gun hits climb the same ladder.
 */
#ifndef SHANKPIT_GROOVE_SYNC_H
#define SHANKPIT_GROOVE_SYNC_H

#include "groove_combat.h"

int gs_min_bpm_x10(void); int gs_max_bpm_x10(void); int gs_min_taps(void); int gs_tap_interval_ok(int);
int gs_bpm_x10_from_interval_ms(int); int gs_interval_ms_from_bpm_x10(int); int gs_confidence_milli(int, int, int);
int gs_locked(int, int); int gs_lock_confidence_milli(void); int gs_phase_milli(int, int, int);
int gs_onset_flux(int, int); int gs_comb_alpha_milli(void); int gs_comb_update_milli(int, int);
int gs_lag_frames(int, int); int gs_lag_milli(int, int); int gs_bpm_x10_for_lag(int, int);
int gs_sync_beats(void); int gs_subdivision(int, int); int gs_synced_cooldown_ms(int, int); int gs_sync_damage_pct(int, int);
int gs_rank_count(void); int gs_rank(int); int gs_rank_floor(int); int gs_rank_damage_pct(int); int gs_rank_rof_pct(int);
int gs_rank_heal_per_hit(int); int gs_variety_pct(int); int gs_gun_style_gain(int);
int gs_weapon_knife(void); int gs_weapon_ar(void); int gs_sync_weapon(int); int gs_synced_weapon(int);

static const char *const GROOVE_RANK_NAMES[7] = { "D", "C", "B", "A", "S", "SS", "SSS" };

/* ---- the sync device ----------------------------------------------------------------------------------------------- */
#define GROOVE_SYNC_TAPS 8
typedef struct {
    int taps[GROOVE_SYNC_TAPS];  /* recent tap times, ms (engine clock), oldest first */
    int ntaps;
    int locked;                  /* 1 while a lock holds */
    int bpm_x10, confidence;     /* the locked tempo and how steady it was */
    int anchor_ms, interval_ms;  /* a beat started at anchor_ms; one beat lasts interval_ms */
    int expires_ms;
} GrooveSync;

static inline void groove_sync_init(GrooveSync *s) { memset(s, 0, sizeof *s); }

/* One press of the sync device (the knife's special). Returns 1 when this tap made a new lock. A gap that is not a beat
 * (too fast, too slow) starts the count again from this tap. */
static inline int groove_sync_tap(GrooveSync *s, int player_id, int now_ms) {
    int i, gap, mn = 0, mx = 0, sum = 0, n, conf;
    if (s->ntaps > 0 && !gs_tap_interval_ok(now_ms - s->taps[s->ntaps - 1])) s->ntaps = 0;
    if (s->ntaps == GROOVE_SYNC_TAPS) { memmove(s->taps, s->taps + 1, sizeof(int) * (GROOVE_SYNC_TAPS - 1)); s->ntaps--; }
    s->taps[s->ntaps++] = now_ms;
    if (s->ntaps < 2) return 0;
    n = s->ntaps - 1;
    for (i = 1; i < s->ntaps; i++) {
        gap = s->taps[i] - s->taps[i - 1];
        if (i == 1 || gap < mn) mn = gap;
        if (i == 1 || gap > mx) mx = gap;
        sum += gap;
    }
    conf = gs_confidence_milli(mn, mx, sum / n);
    if (!gs_locked(s->ntaps, conf)) return 0;
    s->interval_ms = sum / n;
    s->bpm_x10 = gs_bpm_x10_from_interval_ms(s->interval_ms);
    s->confidence = conf;
    s->anchor_ms = now_ms;
    s->expires_ms = now_ms + s->interval_ms * gs_sync_beats();
    if (!s->locked) {
        s->locked = 1;
        reflux_dispatch(REFLUX_ACTION_COMBAT_SYNC_LOCKED, player_id, s->bpm_x10, conf);
        return 1;
    }
    return 0;   /* already locked: the taps just refreshed it */
}

/* Call every tick: lets a lock lapse after its beats (COMBAT_SYNC_EXPIRED). Returns 1 while synced. */
static inline int groove_sync_update(GrooveSync *s, int player_id, int now_ms) {
    if (s->locked && now_ms >= s->expires_ms) {
        s->locked = 0;
        s->ntaps = 0;
        reflux_dispatch(REFLUX_ACTION_COMBAT_SYNC_EXPIRED, player_id, s->bpm_x10, 0);
    }
    return s->locked;
}

static inline int groove_sync_phase(const GrooveSync *s, int now_ms) {
    return s->locked ? gs_phase_milli(now_ms, s->anchor_ms, s->interval_ms) : -1;
}

/* Beats left on the lock (the HUD's countdown). */
static inline int groove_sync_beats_left(const GrooveSync *s, int now_ms) {
    if (!s->locked || s->interval_ms <= 0) return 0;
    return (s->expires_ms - now_ms + s->interval_ms - 1) / s->interval_ms;
}

/* The AR while synced: its cooldown snapped to the beat (then sped up at SSS) and its damage scaled. When not synced, or
 * for any other weapon, the native numbers come back unchanged. */
static inline int groove_sync_cooldown_ms(const GrooveSync *s, int weapon, int native_ms, int rank) {
    int ms = native_ms;
    if (s->locked && gs_synced_weapon(weapon)) ms = gs_synced_cooldown_ms(s->interval_ms, native_ms);
    return ms * 100 / gs_rank_rof_pct(rank);
}
static inline int groove_sync_damage(const GrooveSync *s, int weapon, int base, int rank) {
    if (s->locked && gs_synced_weapon(weapon)) return base * gs_sync_damage_pct(s->confidence, rank) / 100;
    return base * gs_rank_damage_pct(rank) / 100;
}

/* ---- style ranks --------------------------------------------------------------------------------------------------- */
/* Recompute the rank from the meter and put a change on the REFLUX log. Returns the rank. */
static inline int groove_rank_update(GrooveFighter *f, int player_id) {
    int r = gs_rank(f->style);
    if (r != f->rank) {
        f->rank = r;
        reflux_dispatch(REFLUX_ACTION_COMBAT_RANK_CHANGED, player_id, r, f->style);
    }
    return r;
}

/* A gun hit (weapon = WPN_*) at beat phase `phase_milli`: graded like a move, it feeds the same meter (with variety),
 * and returns the grade. The caller already applied the damage, scaled by groove_sync_damage. */
static inline int groove_gun_hit(GrooveFighter *f, int player_id, int weapon, int phase_milli) {
    int grade = gc_grade(phase_milli);
    int kind = 100 + weapon;
    int gain = gs_gun_style_gain(grade);
    f->repeats = (kind == f->last_kind) ? f->repeats + 1 : 0;
    f->last_kind = kind;
    f->style += gain * gs_variety_pct(f->repeats) / 100;
    if (f->style > gc_style_max()) f->style = gc_style_max();
    f->idle = 0;
    groove_rank_update(f, player_id);
    return grade;
}

/* ---- BPM detection from the music ---------------------------------------------------------------------------------- */
/* One comb filter per whole BPM in [GROOVE_BPM_LO, GROOVE_BPM_HI], fed one onset strength per analysis frame (the
 * caller sums |sample| over a frame and passes that energy). The comb whose output rings loudest is the tempo. */
#define GROOVE_BPM_LO 70
#define GROOVE_BPM_HI 180
#define GROOVE_BPM_N (GROOVE_BPM_HI - GROOVE_BPM_LO + 1)
#define GROOVE_COMB_HIST 128   /* >= the longest lag at the analysis rate used (fps * 60 / GROOVE_BPM_LO) */

typedef struct {
    int fps;                               /* analysis frames per second (e.g. 50: 20 ms frames) */
    int prev_energy;
    int pos;                               /* frames fed */
    int lag_milli[GROOVE_BPM_N];           /* each comb's delay, frames x1000 (fractional: gs-lag-milli) */
    int hist[GROOVE_BPM_N][GROOVE_COMB_HIST];
    long score[GROOVE_BPM_N];
    int bpm_x10, confidence;               /* the current estimate */
} GrooveBeatDetector;

static inline void groove_bpm_init(GrooveBeatDetector *d, int fps) {
    int i;
    memset(d, 0, sizeof *d);
    d->fps = fps;
    for (i = 0; i < GROOVE_BPM_N; i++) d->lag_milli[i] = gs_lag_milli((GROOVE_BPM_LO + i) * 10, fps);
}

/* A comb's output `lag_milli`/1000 frames ago, linearly interpolated between the two frames either side. */
static inline int groove_comb_delayed(const GrooveBeatDetector *d, int i) {
    int whole = d->lag_milli[i] / 1000, frac = d->lag_milli[i] % 1000, a, b;
    if (whole <= 0 || whole + 1 >= GROOVE_COMB_HIST || d->pos < whole + 1) return 0;
    a = d->hist[i][(d->pos - whole) % GROOVE_COMB_HIST];
    b = d->hist[i][(d->pos - whole - 1) % GROOVE_COMB_HIST];
    return (a * (1000 - frac) + b * frac) / 1000;
}

/* Feed one analysis frame's energy. Every `fps` frames the estimate is refreshed; a new tempo (or the first one) is
 * put on the REFLUX log as COMBAT_BPM_DETECTED (a = source id, b = BPM x10, c = confidence x1000). */
static inline void groove_bpm_feed(GrooveBeatDetector *d, int source_id, int energy) {
    int i, onset = gs_onset_flux(d->prev_energy, energy), slot = d->pos % GROOVE_COMB_HIST;
    long best = -1, second = -1;
    int best_i = 0;
    d->prev_energy = energy;
    for (i = 0; i < GROOVE_BPM_N; i++) {
        int y = gs_comb_update_milli(groove_comb_delayed(d, i), onset);
        d->hist[i][slot] = y;
        /* the comb's output ENERGY, leaky-summed: every comb has the same mean output (DC gain 1), but only the one
         * whose delay matches the beat resonates, and resonance shows up as energy (Scheirer's comb-filter method) */
        d->score[i] += (long)y * y / 1000 - d->score[i] / 256;
    }
    d->pos++;
    if (d->fps <= 0 || d->pos % d->fps != 0 || d->pos < d->fps * 4) return;
    for (i = 0; i < GROOVE_BPM_N; i++) {
        if (d->score[i] > best) { second = best; best = d->score[i]; best_i = i; }
        else if (d->score[i] > second) second = d->score[i];
    }
    if (best <= 0) return;
    {
        int bpm = (GROOVE_BPM_LO + best_i) * 10;
        int conf = (int)(1000 - (second > 0 ? second * 1000 / best : 0));
        if (conf < 0) conf = 0;
        if (bpm != d->bpm_x10) reflux_dispatch(REFLUX_ACTION_COMBAT_BPM_DETECTED, source_id, bpm, conf);
        d->bpm_x10 = bpm;
        d->confidence = conf;
    }
}

#endif
