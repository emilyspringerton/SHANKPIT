/* groove_sync_test.c -- the shank's sync device, the synced AR, the style ranks and BPM detection, through the real
 * generated PARENA rules (groove_sync_rules.c, groove_combat_rules.c). `make test-groove-combat`. */
#include <stdio.h>
#include <stdlib.h>
#include "groove_sync.h"

static int passed = 0, failed = 0;
#define EQ(got, want, what) do { long g_ = (long)(got), w_ = (long)(want); \
    if (g_ == w_) passed++; else { failed++; printf("FAIL: %s (got %ld, want %ld)\n", what, g_, w_); } } while (0)
#define NEAR(got, want, tol, what) do { long g_ = (long)(got), w_ = (long)(want); \
    if (labs(g_ - w_) <= (tol)) passed++; else { failed++; printf("FAIL: %s (got %ld, want %ld +- %d)\n", what, g_, w_, (tol)); } } while (0)

static int log_n = 0, log_t[512], log_a[512], log_b[512], log_c[512];
void reflux_dispatch(int t, int a, int b, int c) {
    if (log_n < 512) { log_t[log_n] = t; log_a[log_n] = a; log_b[log_n] = b; log_c[log_n] = c; log_n++; }
}
static int count_of(int t) { int n = 0, i; for (i = 0; i < log_n; i++) if (log_t[i] == t) n++; return n; }
static int last_of(int t) { int i; for (i = log_n - 1; i >= 0; i--) if (log_t[i] == t) return i; return -1; }

/* A synthetic track: one energy frame per 1/fps s, a kick every beat, a quieter hat on the off-beat, a little noise. */
static int track_energy(int frame, int fps, int bpm) {
    int beat_frames_x1000 = fps * 60000 / bpm;
    int pos = (frame * 1000) % beat_frames_x1000;
    int e = 50 + (int)((frame * 2654435761u) % 40);
    if (pos < 1000) e += 1000;                                   /* the kick */
    else if (abs(pos - beat_frames_x1000 / 2) < 1000) e += 300;  /* the off-beat hat */
    return e;
}

int main(void) {
    GrooveSync s;
    GrooveFighter f;
    GrooveBeatDetector *d = (GrooveBeatDetector *)calloc(1, sizeof *d);
    int t, i, grade, i_lock;

    /* ---- weapons: key 1 syncs, key 3 uses it ---- */
    EQ(gs_sync_weapon(gs_weapon_knife()), 1, "the knife (key 1) holds the sync device");
    EQ(gs_synced_weapon(gs_weapon_ar()), 1, "the AR (key 3) uses the sync");
    EQ(gs_synced_weapon(3), 0, "the shotgun is not synced");

    /* ---- the sync device: steady taps at 120 BPM lock ---- */
    groove_sync_init(&s); log_n = 0;
    t = 10000;
    for (i = 0; i < 3; i++) { EQ(groove_sync_tap(&s, 4, t), 0, "fewer than four taps do not lock"); t += 500; }
    EQ(groove_sync_tap(&s, 4, t), 1, "the fourth steady tap locks");
    EQ(s.bpm_x10, 1200, "at 120.0 BPM");
    EQ(s.confidence, 1000, "perfectly steady taps are full confidence");
    i_lock = last_of(REFLUX_ACTION_COMBAT_SYNC_LOCKED);
    EQ(i_lock >= 0 && log_a[i_lock] == 4 && log_b[i_lock] == 1200, 1, "the lock is on the REFLUX log");
    EQ(groove_sync_phase(&s, t), 0, "the last tap is on the beat");
    EQ(groove_sync_phase(&s, t + 250), 500, "a half beat later is phase 500");
    EQ(groove_sync_beats_left(&s, t), gs_sync_beats(), "a fresh lock has 32 beats");

    /* uneven taps do not lock; a long pause starts again */
    groove_sync_init(&s);
    groove_sync_tap(&s, 4, 0); groove_sync_tap(&s, 4, 400); groove_sync_tap(&s, 4, 900); groove_sync_tap(&s, 4, 1250);
    EQ(s.locked, 0, "ragged taps (400, 500, 350 ms) do not lock");
    groove_sync_tap(&s, 4, 5000);
    EQ(s.ntaps, 1, "a pause longer than a beat starts the count again");

    /* ---- the lock lapses ---- */
    groove_sync_init(&s); log_n = 0;
    for (t = 0; t <= 1500; t += 500) groove_sync_tap(&s, 4, t);
    EQ(groove_sync_update(&s, 4, 1500 + 500 * 31), 1, "still synced on beat 31");
    EQ(groove_sync_update(&s, 4, 1500 + 500 * 32), 0, "lapsed after 32 beats");
    EQ(count_of(REFLUX_ACTION_COMBAT_SYNC_EXPIRED), 1, "the lapse is on the REFLUX log, once");
    groove_sync_update(&s, 4, 1500 + 500 * 40);
    EQ(count_of(REFLUX_ACTION_COMBAT_SYNC_EXPIRED), 1, "and only once");

    /* ---- the synced AR ---- */
    groove_sync_init(&s);
    for (t = 0; t <= 1500; t += 500) groove_sync_tap(&s, 4, t);
    EQ(gs_subdivision(500, 100), 5, "at 120 BPM an AR with a 100 ms cooldown fires five times a beat");
    EQ(groove_sync_cooldown_ms(&s, 2, 100, 0), 100, "so its synced cooldown is 500 / 5 = 100 ms");
    EQ(groove_sync_cooldown_ms(&s, 2, 120, 0), 125, "a 120 ms AR snaps to 4 a beat: 125 ms, on the beat");
    EQ(groove_sync_cooldown_ms(&s, 3, 800, 0), 800, "the shotgun keeps its own rate");
    EQ(groove_sync_damage(&s, 2, 100, 0), 140, "synced, steady: +40% AR damage");
    EQ(groove_sync_damage(&s, 3, 100, 0), 100, "the shotgun is not synced (rank D)");
    s.locked = 0;
    EQ(groove_sync_cooldown_ms(&s, 2, 120, 0), 120, "unsynced, the AR is itself again");

    /* ---- ranks and powerups ---- */
    EQ(gs_rank(0), 0, "an empty meter is D");
    EQ(gs_rank(460), 3, "460 is A");
    EQ(gs_rank(1000), 6, "a full meter is SSS");
    for (i = 0; i < 7; i++) EQ(gs_rank(gs_rank_floor(i)), i, "every rank starts at its floor");
    EQ(gs_rank_damage_pct(3), 110, "A hits 10% harder");
    EQ(gs_rank_damage_pct(6), 140, "SSS hits 40% harder");
    EQ(gs_rank_heal_per_hit(5), 1, "SS heals on hit");
    EQ(gs_rank_rof_pct(6), 115, "SSS fires 15% faster");
    groove_sync_init(&s);
    for (t = 0; t <= 1500; t += 500) groove_sync_tap(&s, 4, t);
    EQ(groove_sync_cooldown_ms(&s, 2, 100, 6), 86, "synced AR at SSS: 100 ms / 1.15");
    EQ(groove_sync_damage(&s, 2, 100, 6), 180, "synced AR at SSS: +40% sync, +40% rank");

    /* ---- variety, and guns on the same meter ---- */
    groove_init(&f); log_n = 0;
    grade = groove_gun_hit(&f, 4, 2, 0);
    EQ(grade, GC_GRADE_PERFECT, "an AR hit on the beat grades perfect");
    EQ(f.style, 50, "and earns 50 style");
    groove_gun_hit(&f, 4, 2, 0);
    EQ(f.style, 80, "the same gun again earns 60%");
    groove_gun_hit(&f, 4, 2, 0);
    EQ(f.style, 95, "a third time 30%");
    groove_gun_hit(&f, 4, 3, 0);
    EQ(f.style, 145, "switching to the shotgun earns full style again");
    groove_input(&f, 4, 1, 0, 0, 0, 0, 0, 0);
    EQ(f.rank, 1, "a perfect jab on top climbs to C");
    EQ(count_of(REFLUX_ACTION_COMBAT_RANK_CHANGED), 1, "the rank change is on the REFLUX log");
    EQ(groove_gun_hit(&f, 4, 2, 500), GC_GRADE_OFF, "an off-beat hit grades off");

    /* ---- BPM detection ---- */
    EQ(gs_lag_frames(1200, 50), 25, "at 50 fps a 120 BPM beat is 25 frames");
    EQ(gs_bpm_x10_for_lag(25, 50), 1200, "and back");
    EQ(gs_onset_flux(100, 400), 300, "a rise is an onset");
    EQ(gs_onset_flux(400, 100), 0, "a fall is not");
    groove_bpm_init(d, 50); log_n = 0;
    for (i = 0; i < 50 * 20; i++) groove_bpm_feed(d, 1, track_energy(i, 50, 120));
    NEAR(d->bpm_x10, 1200, 25, "a 120 BPM track is detected at 120");
    EQ(count_of(REFLUX_ACTION_COMBAT_BPM_DETECTED) >= 1, 1, "the detection is on the REFLUX log");
    groove_bpm_init(d, 50);
    for (i = 0; i < 50 * 20; i++) groove_bpm_feed(d, 1, track_energy(i, 50, 90));
    NEAR(d->bpm_x10, 900, 25, "a 90 BPM track is detected at 90");

    free(d);
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
