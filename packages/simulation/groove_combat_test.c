/* groove_combat_test.c -- groove combat end to end: the PARENA rules (groove_combat_rules.c) through the host
 * (groove_combat.h), with the REFLUX log captured. `make test-groove-combat`. */
#include <stdio.h>
#include "groove_combat.h"

static int passed = 0, failed = 0;
#define EQ(got, want, what) do { long g_ = (long)(got), w_ = (long)(want); \
    if (g_ == w_) passed++; else { failed++; printf("FAIL: %s (got %ld, want %ld)\n", what, g_, w_); } } while (0)

/* the REFLUX log, captured */
static int log_n = 0, log_t[256], log_a[256], log_b[256], log_c[256];
void reflux_dispatch(int t, int a, int b, int c) {
    if (log_n < 256) { log_t[log_n] = t; log_a[log_n] = a; log_b[log_n] = b; log_c[log_n] = c; log_n++; }
}
static int count_of(int t) { int n = 0, i; for (i = 0; i < log_n; i++) if (log_t[i] == t) n++; return n; }
static int last_of(int t) { int i; for (i = log_n - 1; i >= 0; i--) if (log_t[i] == t) return i; return -1; }

static void run_ticks(GrooveFighter *f, int n) { while (n-- > 0) groove_tick(f); }
static void to_phase(GrooveFighter *f, int phase) { while (groove_phase(f) != phase) groove_tick(f); }

int main(void) {
    GrooveFighter a, b;
    int dmg, i;
    float kx, kz;

    /* ---- input mapping: the player-facing affordance ---- */
    EQ(gc_move_for_input(1, 0, 0, 0, 0, 0, 0), GC_MOVE_JAB, "attack = jab");
    EQ(gc_move_for_input(1, 0, 1, 0, 0, 0, 0), GC_MOVE_SNAP_KICK, "attack + forward = snap kick");
    EQ(gc_move_for_input(1, 0, 0, 0, 1, 0, 0), GC_MOVE_SWEEP, "attack + crouch = sweep");
    EQ(gc_move_for_input(0, 1, 0, 0, 0, 0, 0), GC_MOVE_SPIN, "style = spin");
    EQ(gc_move_for_input(0, 1, 1, 0, 0, 0, 0), GC_MOVE_LEAN, "style + forward = lean");
    EQ(gc_move_for_input(0, 1, 0, 1, 0, 0, 0), GC_MOVE_GLIDE, "style + back = glide");
    EQ(gc_move_for_input(1, 1, 0, 0, 0, 0, 0), GC_MOVE_SPIN, "attack + style without style meter = spin");
    EQ(gc_move_for_input(1, 1, 0, 0, 0, 0, 600), GC_MOVE_FREEZE, "attack + style with 600 style = freeze");
    EQ(gc_move_for_input(1, 0, 0, 0, 1, 1, 0), GC_MOVE_SNAP_KICK, "airborne attack = jumping kick, never a sweep");
    EQ(gc_move_for_input(0, 1, 0, 1, 0, 1, 0), GC_MOVE_SPIN, "airborne style = spin, never a glide");
    EQ(gc_move_for_input(0, 0, 1, 1, 1, 0, 999), GC_MOVE_NONE, "no button, no move");

    /* ---- the beat ---- */
    EQ(gc_grade(0), GC_GRADE_PERFECT, "on the beat is perfect");
    EQ(gc_grade(960), GC_GRADE_PERFECT, "just before the beat is perfect");
    EQ(gc_grade(150), GC_GRADE_GOOD, "150 ms-milli off is good");
    EQ(gc_grade(500), GC_GRADE_OFF, "half a beat off is off");
    EQ(gc_grade(-50), GC_GRADE_PERFECT, "a phase below 0 is clamped");
    EQ(groove_fallback_phase(1000), 0, "fallback: 120 BPM, a beat every 500 ms");
    EQ(groove_fallback_phase(1250), 500, "fallback: half way through a beat");

    /* ---- frame data is consistent ---- */
    for (i = 1; i < GC_MOVE_COUNT; i++) {
        EQ(gc_phase(i, 0) == GC_PHASE_STARTUP || gc_startup_ticks(i) == 0, 1, "every move starts in startup");
        EQ(gc_phase(i, gc_total_ticks(i)), GC_PHASE_DONE, "every move ends");
        EQ(gc_anim_clip(i) >= 0 && gc_anim_clip(i) < 13, 1, "every move plays a real anim_rules clip");
    }

    /* ---- a jab on the beat, its REFLUX event and its hit ---- */
    groove_init(&a); groove_init(&b); log_n = 0;
    EQ(groove_input(&a, 3, 1, 0, 0, 0, 0, 0, 0), GC_MOVE_JAB, "a jab starts");
    EQ(log_t[0], REFLUX_ACTION_COMBAT_MOVE_STARTED, "the start is on the REFLUX log");
    EQ(log_b[0], GC_MOVE_JAB, "with the move");
    EQ(log_c[0], GC_GRADE_PERFECT, "and the grade");
    EQ(a.style > 0, 1, "a perfect move earns style");
    EQ(groove_hit(&a, 3, 0, 0, 0, 7, 0, -5, &b), 0, "no hit during startup");
    to_phase(&a, GC_PHASE_ACTIVE);
    EQ(groove_hit(&a, 3, 0, 0, 0, 7, 5, 0, &b), 0, "a target to the side is outside the jab's cone");
    EQ(groove_hit(&a, 3, 0, 0, 0, 8, 0, -20, &b), 0, "a target past the jab's reach is not hit");
    dmg = groove_hit(&a, 3, 0, 0, 0, 7, 0, -5, &b);
    EQ(dmg, gc_final_damage(GC_MOVE_JAB, GC_GRADE_PERFECT, 1), "a target in front is hit for the graded damage");
    EQ(dmg > gc_damage(GC_MOVE_JAB), 1, "on the beat hits harder than the base");
    EQ(groove_hit(&a, 3, 0, 0, 0, 7, 0, -5, &b), 0, "the same target is hit once per move");
    i = last_of(REFLUX_ACTION_COMBAT_HIT);
    EQ(i >= 0 && log_a[i] == 3 && log_b[i] == 7 && log_c[i] == dmg, 1, "the hit is on the REFLUX log with its damage");

    /* ---- chain into a kick: the combo grows, and the multiplier with it ---- */
    to_phase(&a, GC_PHASE_RECOVERY);
    EQ(groove_input(&a, 3, 0, 1, 0, 1, 0, 0, 0), GC_MOVE_GLIDE, "a glide cancels a recovery: you can always slip away after a whiff");
    EQ(gc_can_cancel(GC_MOVE_SNAP_KICK, GC_MOVE_GLIDE, gc_startup_ticks(GC_MOVE_SNAP_KICK)), 0, "but not an active hit");
    groove_init(&a); log_n = 0;
    groove_input(&a, 3, 1, 0, 0, 0, 0, 0, 0);
    EQ(groove_input(&a, 3, 1, 0, 1, 0, 0, 0, 0), GC_MOVE_NONE, "a kick cannot cancel a jab's startup");
    to_phase(&a, GC_PHASE_RECOVERY);
    EQ(groove_input(&a, 3, 1, 0, 1, 0, 0, 0, 20), GC_MOVE_SNAP_KICK, "a kick chains out of the jab's recovery");
    EQ(a.combo, 2, "on the beat, the combo is 2");
    EQ(count_of(REFLUX_ACTION_COMBAT_COMBO), 1, "the combo is on the REFLUX log");
    EQ(gc_combo_mult_pct(2), 110, "combo 2 adds 10%");
    EQ(gc_combo_mult_pct(20), 150, "the combo bonus is capped at +50%");
    to_phase(&a, GC_PHASE_RECOVERY);
    EQ(groove_input(&a, 3, 1, 0, 0, 0, 0, 0, 500), GC_MOVE_NONE, "a jab cannot follow a kick (not in the chain table)");
    EQ(groove_input(&a, 3, 0, 1, 0, 0, 0, 0, 500), GC_MOVE_SPIN, "a spin can, even off the beat");
    EQ(a.combo, 1, "off the beat, the combo starts again");

    /* ---- the spin hits all around and pushes outward ---- */
    to_phase(&a, GC_PHASE_ACTIVE);
    EQ(groove_hit(&a, 3, 0, 0, 0, 9, 0, 8, NULL) > 0, 1, "a spin hits a target behind (a zombie: no fighter)");
    groove_knockback(&a, 0, 0, 0, 0, 8, &kx, &kz);
    EQ(kz > 0, 1, "and pushes it away from the spinner");

    /* ---- the glide evades ---- */
    groove_init(&a); groove_init(&b); log_n = 0;
    EQ(groove_input(&b, 7, 0, 1, 0, 1, 0, 0, 0), GC_MOVE_GLIDE, "the defender glides");
    EQ(groove_invulnerable(&b), 1, "a gliding fighter is untouchable");
    groove_input(&a, 3, 1, 0, 0, 0, 0, 0, 0);
    to_phase(&a, GC_PHASE_ACTIVE);
    groove_tick(&b); groove_tick(&b);
    EQ(groove_hit(&a, 3, 0, 0, 0, 7, 0, -5, &b), 0, "the jab passes through the glide");
    EQ(count_of(REFLUX_ACTION_COMBAT_EVADED), 1, "the evade is on the REFLUX log");
    EQ(gc_can_cancel(GC_MOVE_JAB, GC_MOVE_GLIDE, 1), 1, "a glide can cancel even a startup: the evade is there when needed");

    /* ---- the sweep knocks down ---- */
    groove_init(&a); groove_init(&b); log_n = 0;
    groove_input(&a, 3, 1, 0, 0, 0, 1, 0, 0);
    EQ(a.move, GC_MOVE_SWEEP, "attack + crouch sweeps");
    to_phase(&a, GC_PHASE_ACTIVE);
    EQ(groove_hit(&a, 3, 0, 0, 0, 7, 0, -6, &b) > 0, 1, "the sweep hits");
    EQ(b.down > 0, 1, "and the target is down");
    EQ(groove_can_act(&b), 0, "a downed fighter cannot start a move");
    EQ(groove_input(&b, 7, 1, 0, 0, 0, 0, 0, 0), GC_MOVE_NONE, "so its jab does nothing");
    EQ(count_of(REFLUX_ACTION_COMBAT_KNOCKDOWN), 1, "the knockdown is on the REFLUX log");

    /* ---- the freeze: costs style, fires once, staggers ---- */
    groove_init(&a); groove_init(&b); log_n = 0;
    a.style = 700;
    EQ(groove_input(&a, 3, 1, 1, 0, 0, 0, 0, 0), GC_MOVE_FREEZE, "attack + style with the meter = freeze");
    EQ(a.style, 100, "the freeze spends 600 style");
    for (i = 0; i <= gc_startup_ticks(GC_MOVE_FREEZE); i++) {
        if (groove_freeze_fires(&a)) { groove_stagger(&b); groove_freeze_report(3, 1); }
        groove_tick(&a);
    }
    EQ(count_of(REFLUX_ACTION_COMBAT_FREEZE), 1, "the freeze fires exactly once");
    EQ(b.stagger, gc_freeze_stagger_ticks(), "and staggers");
    EQ(groove_freeze_radius() > 15.0f, 1, "its radius is wider than a knife's reach");
    EQ(gc_can_chain(GC_MOVE_FREEZE, GC_MOVE_JAB), 0, "nothing chains out of a freeze");

    /* ---- style decays, the combo times out ---- */
    groove_init(&a);
    a.style = 100; a.combo = 3;
    run_ticks(&a, GROOVE_COMBO_GRACE_TICKS + 1);
    EQ(a.combo, 0, "an idle fighter's combo times out");
    EQ(a.style, 100 - (GROOVE_COMBO_GRACE_TICKS + 1) * gc_style_decay(), "style decays while idle");
    run_ticks(&a, 1000);
    EQ(a.style, 0, "and never goes below zero");

    /* ---- REFLUX bounds ---- */
    EQ(reflux_action_is_combat(REFLUX_ACTION_COMBAT_HIT), 1, "1201 is a combat action");
    EQ(reflux_action_is_combat(REFLUX_ACTION_BP_MATCH_ENDED), 0, "BRAWLPIT's are not");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
