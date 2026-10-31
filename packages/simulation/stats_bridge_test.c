/* stats_bridge_test.c -- the bridge against GFD's own table, and PARENA against the C defaults.
 *
 * Three things worth proving, in order of how badly they would hurt:
 *   1. The table is GFD's. The expected values are transcribed here from
 *      GoblinFoxDragon/server/job/job.go's jobStats map, and all 23 jobs are checked field by field --
 *      a transcription slip in either place fails instead of quietly shipping a nerfed Warrior.
 *   2. PARENA and the header agree, over every job and every level the cap allows, for every derived
 *      number. That is what stops the generated rules and the compiled defaults drifting apart, which
 *      is the failure mode this arrangement exists to prevent.
 *   3. The mapping behaves: pools scale with level, melee jobs have no MP, a fast job gets a shorter
 *      cooldown but never a zero-tick weapon, and nothing wraps the wire's unsigned char.
 *
 * Build/run: make test-stats-bridge
 */
#include "stats_bridge.h"

#include <stdio.h>
#include <string.h>

/* PARENA-generated (stats_bridge.c) */
int sb_clamp(int lo, int hi, int x);
int sb_level(int level);
int sb_job_count(void);
int sb_hp_base(int job);
int sb_hp_per(int job);
int sb_mp_base(int job);
int sb_mp_per(int job);
int sb_str(int job);
int sb_dex(int job);
int sb_vit(int job);
int sb_agi(int job);
int sb_hp(int job, int level);
int sb_mp(int job, int level);
int sb_haste_permille(int job, int level);
int sb_cooldown(int rof, int haste);
int sb_move_permille(int job);
int sb_crit_permille(int job);
int sb_evade_permille(int job, int level);
int sb_damage_permille(int job, int level);
int sb_health(int job, int level);

static const StatsBridge g_parena = {
    sb_hp_base, sb_hp_per, sb_mp_base, sb_mp_per, sb_str, sb_dex, sb_vit, sb_agi,
    sb_hp, sb_mp, sb_haste_permille, sb_cooldown, sb_move_permille, sb_crit_permille,
    sb_evade_permille, sb_damage_permille, sb_health
};

static int g_checks = 0;
static int g_failures = 0;

static void check(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", what);
    } else {
        printf("ok    %s\n", what);
    }
}

/* job.go's jobStats, transcribed independently of the header: {base_hp, hp_per, base_mp, mp_per,
 * str, dex, vit, agi}. If this and stats_bridge.h ever disagree, one of them is wrong and GFD is right. */
static const int GFD[SB_JOB_COUNT][8] = {
    { 90, 15,   0,  0, 8, 7, 8, 6 },
    {100, 16,   0,  0, 8, 8, 8, 7 },
    { 65, 10,  85, 14, 5, 5, 5, 5 },
    { 55,  8, 100, 16, 4, 5, 4, 5 },
    { 70, 11,  75, 12, 6, 6, 5, 6 },
    { 75, 12,   0,  0, 6, 9, 6, 9 },
    { 95, 15,  60,  9, 8, 6, 9, 5 },
    { 90, 14,  45,  8, 9, 6, 7, 5 },
    { 80, 13,   0,  0, 7, 7, 7, 7 },
    { 65, 10,  50,  9, 5, 7, 5, 7 },
    { 75, 12,   0,  0, 7, 8, 6, 8 },
    { 85, 14,   0,  0, 8, 7, 7, 7 },
    { 75, 12,  20,  6, 6, 9, 6, 9 },
    { 85, 14,   0,  0, 8, 6, 8, 6 },
    { 60,  9,  90, 15, 4, 4, 4, 5 },
    { 80, 13,  55,  9, 7, 7, 6, 6 },
    { 70, 11,  45,  8, 6, 8, 5, 8 },
    { 75, 12,  30,  6, 7, 8, 6, 7 },
    { 80, 13,   0,  0, 7, 8, 7, 8 },
    { 55,  8,  95, 15, 4, 5, 4, 5 },
    { 60,  9,  85, 14, 5, 5, 5, 5 },
    { 80, 13,  40,  7, 7, 7, 7, 7 },
    { 78, 12,  45,  8, 7, 9, 6, 8 }
};

int main(void) {
    printf("== stats_bridge: the table is GFD's ==\n");
    {
        check(sb_job_count() == SB_JOB_COUNT, "there are 23 jobs, as GFD defines them");
        check(SB_JOB_COUNT == 23, "the header agrees on 23");
        int bad = -1;
        for (int j = 0; j < SB_JOB_COUNT; j++) {
            if (sb_hp_base(j) != GFD[j][0] || sb_hp_per(j) != GFD[j][1] ||
                sb_mp_base(j) != GFD[j][2] || sb_mp_per(j) != GFD[j][3] ||
                sb_str(j) != GFD[j][4] || sb_dex(j) != GFD[j][5] ||
                sb_vit(j) != GFD[j][6] || sb_agi(j) != GFD[j][7]) { bad = j; break; }
        }
        check(bad < 0, "every job's eight base values match job.go exactly");
        if (bad >= 0) printf("      first mismatch at job %d (%s)\n", bad, SB_JOB_NAMES[bad]);
        check(strcmp(SB_JOB_NAMES[0], "WAR") == 0 && strcmp(SB_JOB_NAMES[12], "NIN") == 0 &&
              strcmp(SB_JOB_NAMES[22], "ASN") == 0, "the id order is GFD's AllJobs order");
        check(stats_bridge_job_id("NIN") == 12, "a name resolves to its index");
        check(stats_bridge_job_id("nin") == -1, "names are exact, not fuzzy");
        check(stats_bridge_job_id("QQQ") == -1, "an unknown job is reported as unknown");
        check(strcmp(stats_bridge_job_name(999), "WAR") == 0, "an out-of-range id reads as WAR, not memory");
    }

    printf("== stats_bridge: PARENA equals the C defaults ==\n");
    {
        int mismatch = 0;
        const char *which = "";
        for (int j = -2; j <= SB_JOB_COUNT + 2 && !mismatch; j++) {
            for (int lv = -2; lv <= 80 && !mismatch; lv++) {
                if (g_parena.hp_base(j) != g_stats_bridge.hp_base(j)) { mismatch = 1; which = "hp_base"; break; }
                if (g_parena.hp_per(j) != g_stats_bridge.hp_per(j)) { mismatch = 1; which = "hp_per"; break; }
                if (g_parena.mp_base(j) != g_stats_bridge.mp_base(j)) { mismatch = 1; which = "mp_base"; break; }
                if (g_parena.mp_per(j) != g_stats_bridge.mp_per(j)) { mismatch = 1; which = "mp_per"; break; }
                if (g_parena.str(j) != g_stats_bridge.str(j)) { mismatch = 1; which = "str"; break; }
                if (g_parena.dex(j) != g_stats_bridge.dex(j)) { mismatch = 1; which = "dex"; break; }
                if (g_parena.vit(j) != g_stats_bridge.vit(j)) { mismatch = 1; which = "vit"; break; }
                if (g_parena.agi(j) != g_stats_bridge.agi(j)) { mismatch = 1; which = "agi"; break; }
                if (g_parena.hp(j, lv) != g_stats_bridge.hp(j, lv)) { mismatch = 1; which = "hp"; break; }
                if (g_parena.mp(j, lv) != g_stats_bridge.mp(j, lv)) { mismatch = 1; which = "mp"; break; }
                if (g_parena.haste_permille(j, lv) != g_stats_bridge.haste_permille(j, lv)) { mismatch = 1; which = "haste"; break; }
                if (g_parena.move_permille(j) != g_stats_bridge.move_permille(j)) { mismatch = 1; which = "move"; break; }
                if (g_parena.crit_permille(j) != g_stats_bridge.crit_permille(j)) { mismatch = 1; which = "crit"; break; }
                if (g_parena.evade_permille(j, lv) != g_stats_bridge.evade_permille(j, lv)) { mismatch = 1; which = "evade"; break; }
                if (g_parena.damage_permille(j, lv) != g_stats_bridge.damage_permille(j, lv)) { mismatch = 1; which = "damage"; break; }
                if (g_parena.health(j, lv) != g_stats_bridge.health(j, lv)) { mismatch = 1; which = "health"; break; }
            }
        }
        check(!mismatch, "all 23 jobs x levels -2..80 agree on every derived number");
        if (mismatch) printf("      first mismatch: %s\n", which);

        int cooldown_mismatch = 0;
        for (int rof = 0; rof <= 60 && !cooldown_mismatch; rof++)
            for (int h = 600; h <= 1400 && !cooldown_mismatch; h += 10)
                if (g_parena.cooldown(rof, h) != g_stats_bridge.cooldown(rof, h)) cooldown_mismatch = 1;
        check(!cooldown_mismatch, "cooldown agrees across ROF 0..60 and haste 600..1400");
    }

    printf("== stats_bridge: the mapping behaves ==\n");
    {
        enum { WAR = 0, MNK = 1, WHM = 2, BLM = 3, RDM = 4, THF = 5, PLD = 6, DRK = 7, NIN = 12, ASN = 22 };

        check(sb_hp(WAR, 1) == 90, "GFD's level-1 Warrior HP is its base, not base+per");
        check(sb_hp(WAR, 2) == 105, "one level adds exactly the per-level value");
        check(sb_hp(WAR, 75) == 90 + 15 * 74, "the cap is level 75, and the curve is linear to it");
        check(sb_hp(WAR, 999) == sb_hp(WAR, 75), "past the cap stays at the cap");
        check(sb_hp(WAR, 0) == sb_hp(WAR, 1) && sb_hp(WAR, -5) == sb_hp(WAR, 1), "level 0 and negatives are level 1");

        check(sb_mp(WAR, 40) == 0 && sb_mp(MNK, 40) == 0 && sb_mp(THF, 40) == 0,
              "melee jobs have no MP at any level (job.go's own rule)");
        check(sb_mp(WHM, 1) == 85 && sb_mp(BLM, 1) == 100, "casters start with their base MP");
        check(sb_mp(WHM, 2) == 99, "caster MP grows per level");

        check(sb_health(BLM, 1) < sb_health(WAR, 1), "a level-1 Black Mage is squishier than a Warrior");
        check(sb_health(WAR, 75) == 255, "the wire's unsigned char caps health instead of wrapping");
        check(sb_health(WAR, 75) <= 255 && sb_health(WAR, 1) >= 10, "health stays inside the wire's range");

        check(sb_haste_permille(WAR, 1) == 1020, "AGI 6 is a 2% haste at level 1");
        check(sb_haste_permille(NIN, 1) > sb_haste_permille(WAR, 1), "NIN (AGI 9) is faster than WAR (AGI 6)");
        check(sb_haste_permille(ASN, 40) <= 1300, "haste is capped");
        /* These two assertions used to read the other way round -- "a knife gets LONGER under a 1300
           haste" -- because sb_cooldown multiplied the ROF by haste. That inversion made a higher-AGI job
           shoot slower, and nothing caught it while no call site read the bridge; applying it to a live
           player did (make test-stats-apply). Haste is a speed factor: the ROF is divided by it. */
        check(sb_cooldown(20, 1300) == 15, "a knife's 20-tick ROF drops to 15 ticks under a 1300 haste");
        check(sb_cooldown(20, 700) == 28, "...and stretches to 28 under a 700 haste, so the band is real");
        check(sb_cooldown(1, 1300) >= 1 && sb_cooldown(1, 700) >= 1, "the floor holds at one tick");
        check(sb_cooldown(0, 1000) == 1, "a zero ROF still cannot produce a zero-tick weapon");
        check(sb_cooldown(99999, 1000) <= 999, "an absurd ROF is clamped");

        check(sb_move_permille(THF) > sb_move_permille(WAR), "THF moves faster than WAR");
        check(sb_move_permille(THF) <= 1150 && sb_move_permille(BLM) >= 850, "movement stays inside its band, so vehicles still read right");
        check(sb_crit_permille(THF) > sb_crit_permille(WAR), "DEX 9 crits more than DEX 7");
        check(sb_crit_permille(WAR) == 80, "a Warrior's crit is a real 8%");
        check(sb_damage_permille(DRK, 1) > sb_damage_permille(WHM, 1), "the strongest STR hits harder than the weakest");
        check(sb_damage_permille(DRK, 40) > sb_damage_permille(DRK, 1), "levels raise damage");
        check(sb_damage_permille(DRK, 75) <= 2000, "damage scaling is capped");
        check(sb_evade_permille(NIN, 1) > sb_evade_permille(WAR, 1), "NIN evades more than WAR at the same level");
        check(sb_evade_permille(THF, 75) <= 350, "evasion stays under 35%, which a 60 Hz shooter can show honestly");

        check(sb_evade_permille(-99, 1) == sb_evade_permille(WAR, 1), "a negative job id reads as WAR");
        check(sb_crit_permille(9999) == sb_crit_permille(ASN), "an id past the table reads as the last job");
        check(sb_agi(0) == 6 && sb_agi(12) == 9 && sb_agi(22) == 8, "spot-checked AGI against job.go");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
