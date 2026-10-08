/* shadow_war_test.c -- headless tests for the basement shadow war (shadow_war_host.h).
 * make test-shadow-war (mk/shadow_war.mk). ASan+UBSan, -Wall -Wextra -Werror.
 *
 * Covers exactly the three categories BIG_O/docs/SHIP_PLAN.md's own W1 row named: determinism,
 * no-op symmetry, balance sanity -- plus Elo math and the bot policy, which that same row also
 * named as part of W1's deliverable. "No-op symmetry" here means a mirror battle (identical
 * armies, identical command on both sides) resolves as an exact draw, not "issuing no orders at
 * all" -- this system has no concept of a null/no-op command (ADVANCE/HOLD/SCATTER are the
 * entire command space), so the literal reading doesn't apply; the real property SHIP_PLAN's own
 * phrase is reaching for is order-independence, which the simultaneous-damage design in
 * shadow_war_resolve exists specifically to guarantee. */
#include "shadow_war_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* direct access to two PARENA-generated pure functions (shadow_war_rules.c), same forward-decl
 * pattern shadow_war_host.c itself uses -- lets this test check the rules module's own real
 * constants instead of a second, hand-copied literal that could silently drift from them. */
int shadow_war_max_ticks(void);
int shadow_war_unit_power(int base, int trait);

static ShadowWarArmy army(int unit_count, int base, int trait, int command) {
    ShadowWarArmy a; memset(&a, 0, sizeof a);
    a.unit_count = unit_count; a.command = command;
    for (int i = 0; i < unit_count; i++) { a.base[i] = base; a.trait[i] = trait; }
    return a;
}

int main(void) {
    /* --- unit power table: base always dominates (BRUTE > HOUND > SCAVENGER at every trait) --- */
    for (int trait = 0; trait < 3; trait++) {
        ShadowWarArmy scav = army(1, 0, trait, SHADOW_WAR_CMD_HOLD);
        ShadowWarArmy hound = army(1, 1, trait, SHADOW_WAR_CMD_HOLD);
        ShadowWarArmy brute = army(1, 2, trait, SHADOW_WAR_CMD_HOLD);
        assert(shadow_war_army_power(&brute) > shadow_war_army_power(&hound));
        assert(shadow_war_army_power(&hound) > shadow_war_army_power(&scav));
    }

    /* --- determinism: identical inputs, run twice, byte-identical result --- */
    ShadowWarArmy a1 = army(5, 2, 1, SHADOW_WAR_CMD_ADVANCE);
    ShadowWarArmy b1 = army(4, 0, 0, SHADOW_WAR_CMD_HOLD);
    ShadowWarBattle r1, r2;
    shadow_war_resolve(&a1, &b1, &r1);
    shadow_war_resolve(&a1, &b1, &r2);
    assert(memcmp(&r1, &r2, sizeof r1) == 0);

    /* --- no-op / mirror symmetry: identical armies + identical command on both sides is a real,
     * exact draw at every real tick, not just at the end -- order-independence is the whole
     * point of computing both directions' damage from the same pre-tick snapshot. */
    {
        ShadowWarArmy mirror_a = army(6, 1, 2, SHADOW_WAR_CMD_HOLD);
        ShadowWarArmy mirror_b = army(6, 1, 2, SHADOW_WAR_CMD_HOLD);
        ShadowWarBattle rm;
        shadow_war_resolve(&mirror_a, &mirror_b, &rm);
        assert(rm.result == SHADOW_WAR_RESULT_DRAW);
        assert(rm.a_power_end == rm.b_power_end);
        assert(rm.a_power_start == rm.b_power_start);
    }
    /* same mirror property holds for every command, not just HOLD (ADVANCE/ADVANCE and
     * SCATTER/SCATTER are just as order-independent) */
    for (int cmd = 0; cmd < 3; cmd++) {
        ShadowWarArmy ma = army(3, 2, 0, cmd);
        ShadowWarArmy mb = army(3, 2, 0, cmd);
        ShadowWarBattle rm;
        shadow_war_resolve(&ma, &mb, &rm);
        assert(rm.result == SHADOW_WAR_RESULT_DRAW && rm.a_power_end == rm.b_power_end);
    }

    /* --- balance sanity: a strict superset army (same units + one more) never does worse --- */
    {
        ShadowWarArmy small = army(3, 1, 1, SHADOW_WAR_CMD_HOLD);
        ShadowWarArmy big = army(4, 1, 1, SHADOW_WAR_CMD_HOLD);   /* one more HOUND/ARMOR unit */
        assert(shadow_war_army_power(&big) > shadow_war_army_power(&small));
        ShadowWarArmy enemy = army(3, 1, 1, SHADOW_WAR_CMD_HOLD);
        ShadowWarBattle r_small, r_big;
        shadow_war_resolve(&small, &enemy, &r_small);
        shadow_war_resolve(&big, &enemy, &r_big);
        assert(r_small.result != SHADOW_WAR_RESULT_WIN);   /* equal armies, mirrored command -> draw */
        assert(r_big.result == SHADOW_WAR_RESULT_WIN);     /* the extra unit is enough to win outright */
    }

    /* --- balance sanity: at EQUAL power, HOLD blunts ADVANCE (a real, non-obvious emergent
     * property of the command multiplier table -- see shadow_war.prn's own header comment: a
     * defensive line's 1.4x defense sits exactly at an attacker's own 1.4x attack when power is
     * equal, so the advancer barely scratches the holder while taking the holder's full,
     * unpenalized counter-fire). Named explicitly rather than left to be "discovered" by a
     * reader diffing numbers, since a less careful design could easily have made ADVANCE
     * strictly dominant instead. */
    {
        ShadowWarArmy advancer = army(5, 1, 2, SHADOW_WAR_CMD_ADVANCE);
        ShadowWarArmy holder = army(5, 1, 2, SHADOW_WAR_CMD_HOLD);
        ShadowWarBattle r;
        shadow_war_resolve(&advancer, &holder, &r);
        assert(r.result == SHADOW_WAR_RESULT_LOSS);   /* from army A's (the advancer's) own side */
    }

    /* --- balance sanity: SCATTER vs SCATTER is a real, slow, near-even grind (both sides take
     * the floor 1 dmg/tick when power is equal) -- runs the full max-ticks, not an instant draw. */
    {
        ShadowWarArmy s1 = army(4, 0, 2, SHADOW_WAR_CMD_SCATTER);
        ShadowWarArmy s2 = army(4, 0, 2, SHADOW_WAR_CMD_SCATTER);
        ShadowWarBattle r;
        shadow_war_resolve(&s1, &s2, &r);
        assert(r.ticks_run == shadow_war_max_ticks());
        assert(r.a_power_end == r.a_power_start - r.ticks_run);
        assert(r.result == SHADOW_WAR_RESULT_DRAW);
    }

    /* --- a battle always terminates (HOLD/HOLD still grinds down, never an infinite tie-up) --- */
    {
        ShadowWarArmy h1 = army(1, 0, 1, SHADOW_WAR_CMD_HOLD);   /* SCAVENGER/ARMOR: the single
                                                                    weakest unit in the table (7) */
        ShadowWarArmy h2 = army(1, 0, 1, SHADOW_WAR_CMD_HOLD);
        ShadowWarBattle r;
        shadow_war_resolve(&h1, &h2, &r);
        assert(r.ticks_run <= shadow_war_max_ticks());
        assert(r.a_power_end >= 0 && r.b_power_end >= 0);
    }

    /* --- malformed input robustness: out-of-range unit_count/base/trait/command never crash and
     * never read out of bounds (ASan would catch either); unit_count is clamped, bad
     * base/trait/command fall back to a safe default rather than propagating garbage. */
    {
        ShadowWarArmy bad; memset(&bad, 0, sizeof bad);
        bad.unit_count = 999; bad.command = 77;
        for (int i = 0; i < SHADOW_WAR_MAX_UNITS; i++) { bad.base[i] = -5; bad.trait[i] = 50; }
        ShadowWarArmy ok = army(2, 1, 1, SHADOW_WAR_CMD_HOLD);
        ShadowWarBattle r;
        shadow_war_resolve(&bad, &ok, &r);   /* must not crash under ASan/UBSan */
        assert(r.a_power_start == shadow_war_unit_power(0, 0) * SHADOW_WAR_MAX_UNITS);
    }

    /* --- bot policy: deterministic, no RNG, same two numbers always choose the same command --- */
    assert(shadow_war_bot_choose_command(50, 10) == SHADOW_WAR_CMD_ADVANCE);
    assert(shadow_war_bot_choose_command(10, 50) == SHADOW_WAR_CMD_HOLD);
    assert(shadow_war_bot_choose_command(30, 30) == SHADOW_WAR_CMD_SCATTER);
    assert(shadow_war_bot_choose_command(50, 10) == shadow_war_bot_choose_command(50, 10));

    /* --- bot army: deterministic, correctly sized, clamped, a real mix not one stacked type --- */
    {
        ShadowWarArmy bot1, bot2;
        shadow_war_bot_army(5, &bot1);
        shadow_war_bot_army(5, &bot2);
        assert(memcmp(&bot1, &bot2, sizeof bot1) == 0);
        assert(bot1.unit_count == 5);
        int seen_base[3] = {0};
        for (int i = 0; i < bot1.unit_count; i++) seen_base[bot1.base[i]]++;
        assert(seen_base[0] > 0 && seen_base[1] > 0);   /* a real mix across 5 units, not one type */
        ShadowWarArmy bot_over, bot_under;
        shadow_war_bot_army(999, &bot_over); assert(bot_over.unit_count == SHADOW_WAR_MAX_UNITS);
        shadow_war_bot_army(0, &bot_under); assert(bot_under.unit_count == 1);   /* always >= 1 */
    }

    /* --- Elo: a win raises rating, a loss lowers it, a draw at equal start is a real no-op --- */
    {
        int k = SHADOW_WAR_ELO_K;
        int win = shadow_war_elo_update(1200, 1200, 1000, k);
        int loss = shadow_war_elo_update(1200, 1200, 0, k);
        int draw = shadow_war_elo_update(1200, 1200, 500, k);
        assert(win > 1200 && loss < 1200 && draw == 1200);
        /* equal starting ratings -> the win/loss deltas are equal magnitude (a real, checkable
         * zero-sum property of the logistic formula at a 50/50 expectation) */
        assert((win - 1200) == (1200 - loss));
        /* a big underdog win swings further than a big favorite's "expected" win */
        int upset = shadow_war_elo_update(1000, 1400, 1000, k);
        int expected_win = shadow_war_elo_update(1400, 1000, 1000, k);
        assert((upset - 1000) > (expected_win - 1400));
    }
    /* Elo floor: a loss against a SIMILARLY-rated opponent near the floor would go negative
     * without it (105 vs 105, expected 0.5, K=32 -> raw 105-16=89) -- a wildly higher-rated
     * opponent was tried first and rejected as the test scenario: Elo's own math already refuses
     * to punish a huge underdog much for a loss it was expected to take anyway (expected score
     * near 0 means the loss-minus-expected delta is near 0 too), so it never actually stresses
     * the floor at all, however many times it's repeated -- a real, worth-naming property of
     * the formula itself, not a bug in this test's first draft. */
    assert(shadow_war_elo_update(105, 105, 0, SHADOW_WAR_ELO_K) == 100);
    assert(shadow_war_elo_update(100, 100, 0, SHADOW_WAR_ELO_K) == 100);

    /* --- full shadow_war_deploy integration, through a real Phone (same path lab_station_host.c's
     * LABST_WAR branch takes): no clones refuses without touching elo; splicing then deploying
     * expends the roster and moves elo off the lazily-seeded default. --- */
    {
        Phone p; phone_init(&p);
        int elo = 0;
        ShadowWarDeployResult d;
        shadow_war_deploy(&p, &elo, 0, &d);
        assert(!d.ok && elo == 0 && strstr(d.msg, "NO CLONES"));

        p.clone_count = 3;
        p.clones[0] = 2; p.clone_traits[0] = 1;    /* BRUTE/ARMOR -- the strongest unit, 16 power */
        p.clones[1] = 2; p.clone_traits[1] = 1;
        p.clones[2] = 2; p.clone_traits[2] = 1;
        shadow_war_deploy(&p, &elo, 0, &d);
        assert(d.attempted && d.ok && p.clone_count == 0);
        assert(elo != 0 && d.new_elo == elo);
        assert(d.result == SHADOW_WAR_RESULT_WIN);   /* 3x BRUTE/ARMOR vs a 3-unit mixed bot: real win */
        assert(strstr(d.msg, "WAR WON"));
    }

    printf("shadow_war_test OK\n");
    return 0;
}
