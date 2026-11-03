/* zombie_brain_test.c -- the OG train-mode net, as a zombie brain, asserted headless.
 *
 * Run with `make test-zombie-brain`. No checkpoint file, no engine, no window: brain_weights.h is compiled
 * in, so this is the whole inference path in one binary.
 *
 * What is actually proven here, and why each one matters:
 *
 *   1. DETERMINISM. Same body, same observation, same intent -- twice, bit for bit. A replay and a test both
 *      depend on it, and float math over fixed weights is only deterministic if nothing in here samples.
 *   2. THE OFF PATH IS THE OLD PATH. zombie_brain_enabled = 0 must answer used_net = 0 and an all-zero
 *      intent, which is what leaves witness_ai.c's hand-tuned chase untouched. This is the assertion that
 *      keeps the merge from being a second, drifting AI.
 *   3. PER-CLASS WEAVE CAPS, hand-derived from zombie_brain_rules.prn: SHAMBLER 600, WALKER 900, RUNNER 1500,
 *      BRUTE 350, ALPHA 1200 millidegrees of offset from the target bearing. A BRUTE cannot weave like a
 *      RUNNER, which is most of what makes a charge feel heavy.
 *   3b. FORWARD IS A COMMITMENT, NOT A DIRECTION. The net's own forward channel is backwards at range
 *      (measured: -0.80..-0.85, which as a direction walks a zombie away from its prey in every frame).
 *      Its magnitude becomes a 0.55..1.0 press and no class can ever produce a reverse.
 *   4. PER-CLASS LUNGE THRESHOLDS, also hand-derived: 700/500/650/300/550 permille of the net's own shoot
 *      channel. The same 0.6 the net can emit must commit a WALKER, a BRUTE and an ALPHA and NOT a SHAMBLER
 *      or a RUNNER -- that single row is the whole "same net, five bodies" claim, measured.
 *   5. FIVE CLASSES, ONE OBSERVATION, DIFFERENT INTENTS. Not five forward passes agreeing because the input
 *      was the same: the class's distance/health biases change the input too, and the turn cap changes the
 *      answer. This is the "more dynamic" the founder asked for, counted rather than asserted in prose.
 *   6. NaN IS REFUSED AT THE DOOR, not propagated into the physics.
 *   7. PHASE IS UNIQUE across every real (class, level) body -- 60 of them. It clamped once (everything above
 *      BRUTE level 7 read 899), and this is the regression test for that. */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "zombie_brain.h"
#include "zombie_brain_rules_host.h"

static int checks = 0;
static int failures = 0;

static void check(int ok, const char *what) {
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

static void checkf(float got, float want, const char *what) {
    checks++;
    /* the mapping's own arithmetic is exact here (a clamp to a value computed the same way), so equality is
     * the honest assertion -- 1e-6 would let a real off-by-one cap through */
    if (!(got == want)) {
        failures++;
        printf("  FAIL  %s (got %.6f want %.6f)\n", what, (double)got, (double)want);
    }
}

static void checknear(float got, float want, const char *what) {
    checks++;
    if (!(fabsf(got - want) <= 1e-5f)) {
        failures++;
        printf("  FAIL  %s (got %.6f want %.6f)\n", what, (double)got, (double)want);
    }
}

static void checki(int got, int want, const char *what) {
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL  %s (got %d want %d)\n", what, got, want);
    }
}

static const char *cls_name(int c) {
    switch (c) {
    case ZCLASS_SHAMBLER: return "SHAMBLER";
    case ZCLASS_WALKER:   return "WALKER";
    case ZCLASS_RUNNER:   return "RUNNER";
    case ZCLASS_BRUTE:    return "BRUTE";
    case ZCLASS_ALPHA:    return "ALPHA";
    default:              return "?";
    }
}

/* One fixed body and one fixed target, used for every per-class comparison below. Nothing here is random. */
static ZombieBrainCtx body(int cls, int level) {
    ZombieBrainCtx c;
    memset(&c, 0, sizeof c);
    c.x = 120.0f;
    c.z = -80.0f;
    c.yaw_deg = 35.0f;
    c.health_frac = 0.7f;
    c.target_x = 200.0f;
    c.target_z = 40.0f;
    c.target_dist = 140.0f;
    c.cls = cls;
    c.level = level;
    return c;
}

int main(void) {
    printf("zombie_brain_test -- shank_brain_horde.pth driving zombies\n");

    /* ---- 1. determinism ------------------------------------------------------------------------- */
    {
        ZombieBrainCtx c = body(ZCLASS_RUNNER, 6);
        ZombieIntent a = zombie_brain_think(&c);
        ZombieIntent b = zombie_brain_think(&c);
        check(a.fwd == b.fwd && a.strafe == b.strafe && a.yaw_delta_deg == b.yaw_delta_deg &&
                  a.attack == b.attack && a.phase_milli == b.phase_milli && a.used_net == b.used_net,
              "same body + same observation => bit-identical intent");
        checki(a.used_net, 1, "brain on by default");
    }

    /* ---- 2. off is off -------------------------------------------------------------------------- */
    {
        ZombieBrainCtx c = body(ZCLASS_BRUTE, 9);
        zombie_brain_enabled = 0;
        ZombieIntent off = zombie_brain_think(&c);
        zombie_brain_enabled = 1;
        checki(off.used_net, 0, "disabled => used_net 0");
        checkf(off.fwd, 0.0f, "disabled => fwd 0 (the hand-tuned chase decides)");
        checkf(off.strafe, 0.0f, "disabled => strafe 0");
        checkf(off.yaw_delta_deg, 0.0f, "disabled => yaw 0");
        checki(off.attack, 0, "disabled => no lunge");
        ZombieIntent on = zombie_brain_think(&c);
        checki(on.used_net, 1, "re-enabled => used_net 1");
    }

    /* ---- 3. turn caps, hand-derived ------------------------------------------------------------- */
    {
        /* outputs[2] = 1.0 asks for the full ZOMBIE_BRAIN_YAW_GAIN_DEG (12 deg); every class must be capped
         * below that by its own rule. */
        float out[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
        for (int cls = 0; cls < ZCLASS_COUNT; cls++) {
            ZombieIntent it = zombie_brain_map(out, cls, 5);
            float cap = (float)zb_turn_cap(cls) / 1000.0f;
            checkf(it.yaw_delta_deg, cap, cls_name(cls));
            check(it.yaw_delta_deg < ZOMBIE_BRAIN_YAW_GAIN_DEG, "cap is below the raw gain");
        }
        checkf((float)zb_turn_cap(ZCLASS_BRUTE) / 1000.0f, 0.35f, "BRUTE turn cap 350 milli");
        checkf((float)zb_turn_cap(ZCLASS_RUNNER) / 1000.0f, 1.5f, "RUNNER turn cap 1500 milli");
        /* signed: the same cap must apply turning left */
        float neg[4] = { 0.0f, 0.0f, -1.0f, 0.0f };
        ZombieIntent left = zombie_brain_map(neg, ZCLASS_BRUTE, 5);
        checkf(left.yaw_delta_deg, -0.35f, "BRUTE turn cap is signed");
    }

    /* ---- 4. lunge thresholds, hand-derived ------------------------------------------------------ */
    {
        /* 0.6 * 1000 = 600 permille. Above WALKER(500)/BRUTE(300)/ALPHA(550), below SHAMBLER(700)/
         * RUNNER(650). One observation, two bodies that commit and three that do not. */
        float out[4] = { 0.5f, 0.0f, 0.0f, 0.6f };
        checki(zombie_brain_map(out, ZCLASS_SHAMBLER, 5).attack, 0, "0.600 does not commit a SHAMBLER (700)");
        checki(zombie_brain_map(out, ZCLASS_WALKER, 5).attack, 1, "0.600 commits a WALKER (500)");
        checki(zombie_brain_map(out, ZCLASS_RUNNER, 5).attack, 0, "0.600 does not commit a RUNNER (650)");
        checki(zombie_brain_map(out, ZCLASS_BRUTE, 5).attack, 1, "0.600 commits a BRUTE (300)");
        checki(zombie_brain_map(out, ZCLASS_ALPHA, 5).attack, 1, "0.600 commits an ALPHA (550)");
        /* 0.71 clears a SHAMBLER too -- so the threshold is a real line, not a class flag */
        float high[4] = { 0.5f, 0.0f, 0.0f, 0.71f };
        checki(zombie_brain_map(high, ZCLASS_SHAMBLER, 5).attack, 1, "0.710 commits a SHAMBLER (700)");
        /* plenty of net output never commits a BRUTE, but it commits far earlier than anything else */
        float low[4] = { 0.5f, 0.0f, 0.0f, 0.31f };
        checki(zombie_brain_map(low, ZCLASS_BRUTE, 5).attack, 1, "0.310 commits a BRUTE (300)");
        checki(zombie_brain_map(low, ZCLASS_ALPHA, 5).attack, 0, "0.310 does not commit an ALPHA (550)");
    }

    /* ---- 4b. forward is a commitment, never a reverse -------------------------------------------- */
    {
        /* The measured failure this rule exists for: at range the net asks for -0.80, and a body that
         * obeyed that channel as a direction walked 86 units AWAY from the hero in 600 ticks. Nothing in
         * here may ever produce that, in any class. */
        float kite[4] = { -0.8f, 0.0f, 0.0f, 0.0f };
        float none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float full[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        checknear(zombie_brain_map(none, ZCLASS_WALKER, 5).fwd, ZOMBIE_BRAIN_MIN_COMMIT,
                  "no opinion => the commitment floor");
        checknear(zombie_brain_map(kite, ZCLASS_WALKER, 5).fwd, 0.55f + 0.45f * 0.8f,
                  "a measured retreat of -0.8 becomes 0.91 of a press");
        checknear(zombie_brain_map(full, ZCLASS_RUNNER, 5).fwd, 1.0f, "full commit => 1.0");
        for (int cls = 0; cls < ZCLASS_COUNT; cls++) {
            check(zombie_brain_map(kite, cls, 5).fwd > 0.0f, "no class ever walks backwards");
            check(zombie_brain_map(kite, cls, 5).fwd >= ZOMBIE_BRAIN_MIN_COMMIT, "never below the floor");
        }
    }

    /* ---- 5. five classes, one observation, different intents ------------------------------------ */
    {
        printf("  class      fwd    strafe    yaw  lunge  phase\n");
        int distinct = 0;
        ZombieIntent seen[ZCLASS_COUNT];
        for (int cls = 0; cls < ZCLASS_COUNT; cls++) {
            ZombieBrainCtx same = body(cls, 5);
            ZombieIntent it = zombie_brain_think(&same);
            seen[cls] = it;
            printf("  %-9s %6.3f  %7.3f  %5.3f  %5d  %5d\n", cls_name(cls), (double)it.fwd,
                   (double)it.strafe, (double)it.yaw_delta_deg, it.attack, it.phase_milli);

            /* every one of these came through the real net, not a stub */
            checki(it.used_net, 1, "used_net set");
            check(it.fwd >= -1.0f && it.fwd <= 1.0f, "fwd in range");
            check(it.strafe >= -1.0f && it.strafe <= 1.0f, "strafe in range");
            check(fabsf(it.yaw_delta_deg) <= fabsf((float)zb_turn_cap(cls) / 1000.0f) + 1e-6f,
                  "yaw inside the class cap");
            check(it.phase_milli >= 0 && it.phase_milli <= 899, "phase in 0..899");

            int dup = 0;
            for (int j = 0; j < cls; j++) {
                if (seen[j].fwd == it.fwd && seen[j].strafe == it.strafe &&
                    seen[j].yaw_delta_deg == it.yaw_delta_deg && seen[j].attack == it.attack) {
                    dup = 1;
                }
            }
            if (!dup) distinct++;
        }
        /* One net, five bodies. If this ever drops, the per-class rules were flattened into a fork or the
         * observation stopped being class-shaped. */
        checki(distinct, ZCLASS_COUNT, "all five classes produce a distinct intent");
        check(seen[ZCLASS_BRUTE].yaw_delta_deg != seen[ZCLASS_RUNNER].yaw_delta_deg,
              "BRUTE and RUNNER turn differently on the same input");
    }

    /* ---- 6. NaN refused ------------------------------------------------------------------------- */
    {
        float nanout[4] = { NAN, NAN, NAN, NAN };
        ZombieIntent it = zombie_brain_map(nanout, ZCLASS_BRUTE, 3);
        /* NaN forward reads as "no press at all", i.e. the commitment floor -- not 0 (there is no standing
         * still option) and definitely not NaN, which is what would reach the physics */
        checknear(it.fwd, ZOMBIE_BRAIN_MIN_COMMIT, "NaN fwd => the commitment floor");
        check(it.yaw_delta_deg == 0.0f, "NaN yaw => 0");
        checki(it.attack, 0, "NaN shoot => no lunge");

        ZombieBrainCtx bad = body(ZCLASS_WALKER, 3);
        bad.target_dist = NAN;
        bad.health_frac = NAN;
        ZombieIntent safe = zombie_brain_think(&bad);
        check(safe.fwd == safe.fwd && safe.strafe == safe.strafe && safe.yaw_delta_deg == safe.yaw_delta_deg,
              "NaN observation never reaches the intent");

        /* and a NULL context is no opinion, not a crash */
        ZombieIntent nul = zombie_brain_think(NULL);
        checki(nul.used_net, 0, "NULL ctx => no opinion");
    }

    /* ---- 7. phase is unique across every real body ---------------------------------------------- */
    {
        int seen[900];
        memset(seen, 0, sizeof seen);
        int collisions = 0;
        for (int cls = 0; cls < ZCLASS_COUNT; cls++) {
            for (int level = 1; level <= ZOMBIE_MAX_LEVEL; level++) {
                int ph = zb_phase_milli(cls, level);
                if (ph < 0 || ph > 899) {
                    checks++;
                    failures++;
                    printf("  FAIL  phase out of range (%d,%d) => %d\n", cls, level, ph);
                    continue;
                }
                if (seen[ph]++) collisions++;
            }
        }
        checki(collisions, 0, "every (class, level) body has its own drift");
        /* BRUTE is class 3: 3*137 + 12*89 = 1479, one wrap = 579 (a clamp would have said 899) */
        checki(zb_phase_milli(ZCLASS_BRUTE, 12), 579, "BRUTE 12 wraps instead of clamping to 899");
    }

    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("zombie_brain_test: FAIL\n");
        return 1;
    }
    printf("zombie_brain_test: ok\n");
    return 0;
}
