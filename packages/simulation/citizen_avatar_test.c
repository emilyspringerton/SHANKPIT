/* citizen_avatar_test.c -- `make test-citizen-avatar`.
 *
 * The citizens' bodies are a FUNCTION of their personas, so this asserts the two things that makes
 * true: (1) the rules themselves are the numbers the design names -- every expected value below is
 * hand-derived from the formula in PARENA/stdlib/forgedinface/citizen.prn, not read back from the
 * code -- and (2) the composition actually feeds them through, so a citizen with a bench-worker's
 * traits really is heavier across the chest and shorter in the leg than an endgame raider, and can
 * be rebuilt identically from the same seed. */
#include <stdio.h>
#include <string.h>

#include "citizen_avatar.h"
#include "citizen_avatar_host.h"

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

static int near_f(float a, float b) {
    float d = a - b;
    return (d < 0.0005f && d > -0.0005f);
}

/* Persona fixtures: the five traits in citizen_life.h's order, and CILIFE_* paths. */
enum { P_CRAFT = 0, P_TRADE, P_DUEL, P_ENDGAME, P_IDLE };

static void build(CitizenAvatar *av, uint32_t seed, int slot, int path) {
    citizen_avatar_build(av, seed, slot, path, 50, 50, 50, 50, 50);
}

int main(void) {
    /* ---- the shell ---- */
    CHECK(cav_kit_count() == 6, "kits %d", cav_kit_count());
    for (int s = 0; s < 64; s++) {
        int k = cav_kit(1234, s);
        CHECK(k >= 0 && k < 6, "kit %d at slot %d", k, s);
    }
    CHECK(cav_kit(77, 3) == cav_kit(77, 3), "kit is not deterministic");

    /* ---- the masc/fem continuum, one shared body ---- */
    {
        int lo = 1001, hi = -1;
        for (int s = 0; s < 256; s++) {
            int b = cav_sex_bias(99, s, P_IDLE);
            CHECK(b >= 0 && b <= 1000, "bias %d out of range at slot %d", b, s);
            if (b < lo) lo = b;
            if (b > hi) hi = b;
        }
        /* A continuum you cannot reach both ends of is a toggle with extra steps. */
        CHECK(lo < 200, "nothing genuinely masculine in 256 citizens (lowest %d)", lo);
        CHECK(hi > 800, "nothing genuinely feminine in 256 citizens (highest %d)", hi);
        CHECK(cav_sex_bias(99, 7, P_DUEL) == cav_sex_bias(99, 7, P_DUEL), "bias is not deterministic");
    }

    /* ---- height: 1710 neutral, +/-120 across the continuum, +/-30 for bravery ---- */
    CHECK(cav_height_mm(0, 50) == 1830, "tall masculine %d", cav_height_mm(0, 50));
    CHECK(cav_height_mm(1000, 50) == 1710, "neutral feminine %d", cav_height_mm(1000, 50));
    CHECK(cav_height_mm(500, 100) == 1800, "androgynous, bold %d", cav_height_mm(500, 100));
    CHECK(cav_height_mm(1000, 0) == 1680, "feminine, timid %d", cav_height_mm(1000, 0));
    CHECK(cav_height_mm(0, 0) == 1800, "masculine, timid %d", cav_height_mm(0, 0));
    /* total clamps, both ends */
    CHECK(cav_height_mm(1000, -1000) == 1500, "low clamp %d", cav_height_mm(1000, -1000));
    CHECK(cav_height_mm(0, 1000) == 1980, "high clamp %d", cav_height_mm(0, 1000));

    /* ---- bulk: what a life does to a torso ---- */
    CHECK(cav_bulk_permille(50, 50, P_TRADE) == 1000, "average trader %d", cav_bulk_permille(50, 50, P_TRADE));
    CHECK(cav_bulk_permille(100, 0, P_CRAFT) == 1120, "bench worker %d", cav_bulk_permille(100, 0, P_CRAFT));
    CHECK(cav_bulk_permille(0, 100, P_ENDGAME) == 880, "raider %d", cav_bulk_permille(0, 100, P_ENDGAME));
    CHECK(cav_bulk_permille(100, 0, P_CRAFT) > cav_bulk_permille(0, 100, P_ENDGAME),
          "a bench worker is not heavier than a raider");
    CHECK(cav_bulk_permille(-1000, 1000, P_ENDGAME) == 780, "low clamp %d", cav_bulk_permille(-1000, 1000, P_ENDGAME));

    /* ---- shoulders: the strongest continuum signal ---- */
    CHECK(cav_shoulder_permille(0, 50) == 1000 + 300, "broad %d", cav_shoulder_permille(0, 50));
    CHECK(cav_shoulder_permille(1000, 50) == 1000, "narrow %d", cav_shoulder_permille(1000, 50));
    CHECK(cav_shoulder_permille(500, 50) == 1150, "middle %d", cav_shoulder_permille(500, 50));
    CHECK(cav_shoulder_permille(0, 100) == 1300, "clamp %d", cav_shoulder_permille(0, 100));

    /* ---- arms and legs: the paths' own signatures ---- */
    CHECK(cav_arm_permille(P_DUEL, 50) == 1060, "duelist reach %d", cav_arm_permille(P_DUEL, 50));
    CHECK(cav_arm_permille(P_IDLE, 50) == 960, "idler reach %d", cav_arm_permille(P_IDLE, 50));
    CHECK(cav_arm_permille(P_DUEL, 50) > cav_arm_permille(P_IDLE, 50), "a duelist has no more reach than an idler");
    CHECK(cav_arm_permille(P_IDLE, -1000) == 900, "arm clamp %d", cav_arm_permille(P_IDLE, -1000));

    CHECK(cav_leg_permille(P_ENDGAME, 50) == 1120, "raider legs %d", cav_leg_permille(P_ENDGAME, 50));
    CHECK(cav_leg_permille(P_CRAFT, 50) == 960, "bench legs %d", cav_leg_permille(P_CRAFT, 50));
    CHECK(cav_leg_permille(P_ENDGAME, 50) > cav_leg_permille(P_CRAFT, 50), "the long-trip player has the shorter legs");
    CHECK(cav_leg_permille(P_ENDGAME, 100) == 1150, "leg clamp %d", cav_leg_permille(P_ENDGAME, 100));
    CHECK(cav_leg_permille(P_IDLE, -1000) == 900, "leg low clamp %d", cav_leg_permille(P_IDLE, -1000));

    /* ---- head: the one proportion nobody chooses ---- */
    CHECK(cav_head_permille(0) == 1060, "masculine head %d", cav_head_permille(0));
    CHECK(cav_head_permille(1000) == 940, "feminine head %d", cav_head_permille(1000));
    CHECK(cav_head_permille(500) == 1000, "neutral head %d", cav_head_permille(500));

    /* ---- the accent belongs to the path: that is the thing that reads at a distance ---- */
    CHECK(cav_hue(2, 5, 5, P_DUEL, 0) == cav_hue(2, 5, 5, P_DUEL, 0), "accent hue not deterministic");
    CHECK(cav_hue(2, 5, 5, P_DUEL, 0) != cav_hue(2, 5, 5, P_CRAFT, 0),
          "a duelist and a crafter came out in the same accent");
    for (int p = 0; p < 5; p++) {
        int h = cav_hue(2, 5, 5, p, 0);
        CHECK(h >= 0 && h < 360, "accent hue %d for path %d", h, p);
    }
    /* a kit's coat and pants are the same family, 14 degrees apart */
    {
        int coat = cav_hue(0, 5, 5, P_TRADE, 3);
        int pants = cav_hue(1, 5, 5, P_TRADE, 3);
        int d = pants - coat;
        if (d < 0) d = -d;
        CHECK(d == 14 || d == 346, "coat/pants are not 14 degrees apart (%d vs %d)", coat, pants);
    }

    /* ---- sat/val: sociability and ambition are visible, and bounded ---- */
    CHECK(cav_sat(2, 0, P_IDLE, 50, 100) > cav_sat(2, 0, P_IDLE, 50, 0), "a sociable citizen is not dressed louder");
    CHECK(cav_val(2, 0, P_DUEL, 100) > cav_val(2, 0, P_DUEL, 0), "an ambitious citizen is not dressed brighter");
    for (int k = 0; k < 6; k++) {
        CHECK(cav_sat(0, k, P_CRAFT, 50, 50) >= 120 && cav_sat(0, k, P_CRAFT, 50, 50) <= 900, "coat sat %d", k);
        CHECK(cav_val(0, k, P_CRAFT, 50) >= 0 && cav_val(0, k, P_CRAFT, 50) <= 255, "coat val %d", k);
        CHECK(cav_val(1, k, P_CRAFT, 50) < cav_val(0, k, P_CRAFT, 50), "pants brighter than the coat in kit %d", k);
    }

    /* ---- HSV -> RGB: hand-derived anchors of the wheel ---- */
    CHECK(cav_hsv(0, 1000, 255, 0) == 255 && cav_hsv(0, 1000, 255, 1) == 0 && cav_hsv(0, 1000, 255, 2) == 0,
          "hue 0 is not red");
    CHECK(cav_hsv(120, 1000, 255, 0) == 0 && cav_hsv(120, 1000, 255, 1) == 255 && cav_hsv(120, 1000, 255, 2) == 0,
          "hue 120 is not green");
    CHECK(cav_hsv(240, 1000, 255, 0) == 0 && cav_hsv(240, 1000, 255, 1) == 0 && cav_hsv(240, 1000, 255, 2) == 255,
          "hue 240 is not blue");
    CHECK(cav_hsv(0, 0, 255, 0) == 255 && cav_hsv(0, 0, 255, 1) == 255 && cav_hsv(0, 0, 255, 2) == 255,
          "zero saturation is not white");
    CHECK(cav_hsv(350, 1000, 255, 0) == 255 && cav_hsv(350, 1000, 255, 1) == 0 && cav_hsv(350, 1000, 255, 2) == 43,
          "350 is not 255,0,43 (%d,%d,%d)", cav_hsv(350, 1000, 255, 0), cav_hsv(350, 1000, 255, 1),
          cav_hsv(350, 1000, 255, 2));
    /* -30 wraps to 330 (region 5, f=30): r is full, g is nothing, b is mid -- and val 300 clamps to
     * 255 rather than pushing a channel out of range. */
    CHECK(cav_hsv(-30, 1000, 300, 0) == 255 && cav_hsv(-30, 1000, 300, 1) == 0 && cav_hsv(-30, 1000, 300, 2) == 128,
          "negative hue / over-bright val not wrapped (%d,%d,%d)", cav_hsv(-30, 1000, 300, 0),
          cav_hsv(-30, 1000, 300, 1), cav_hsv(-30, 1000, 300, 2));
    for (int h = -400; h < 800; h += 7) {
        for (int c = 0; c < 3; c++) {
            int v = cav_hsv(h, 640, 200, c);
            CHECK(v >= 0 && v <= 255, "channel %d out of range at hue %d: %d", c, h, v);
        }
    }

    /* ---- composition: the same citizen is the same body, and the look follows the life ---- */
    {
        CitizenAvatar a, b, craft, endgame, durable;
        build(&a, 0xdeadbeefu, 12, P_CRAFT);
        build(&b, 0xdeadbeefu, 12, P_CRAFT);
        CHECK(citizen_avatar_equal(&a, &b), "the same citizen was rebuilt as a different person");
        CHECK(a.kit >= 0 && a.kit < 6, "kit %d", a.kit);
        CHECK(a.height_mm >= 1500 && a.height_mm <= 1980, "height %d", a.height_mm);
        CHECK(a.bulk_permille >= 780 && a.bulk_permille <= 1250, "bulk %d", a.bulk_permille);
        CHECK(a.shoulder_permille >= 850 && a.shoulder_permille <= 1300, "shoulder %d", a.shoulder_permille);
        CHECK(a.arm_permille >= 900 && a.arm_permille <= 1120, "arm %d", a.arm_permille);
        CHECK(a.leg_permille >= 900 && a.leg_permille <= 1150, "leg %d", a.leg_permille);
        CHECK(a.head_permille >= 880 && a.head_permille <= 1120, "head %d", a.head_permille);

        /* the composition draws from the rules, not from somewhere else */
        CHECK(a.height_mm == cav_height_mm(a.sex_bias, 50), "height is not the rule's own answer");
        CHECK(a.bulk_permille == cav_bulk_permille(50, 50, P_CRAFT), "bulk is not the rule's own answer");
        CHECK(a.coat[0] == (unsigned char)cav_hsv(cav_hue(0, (int)0xdeadbeefu, 12, P_CRAFT, a.kit),
                                                  cav_sat(0, a.kit, P_CRAFT, 50, 50),
                                                  cav_val(0, a.kit, P_CRAFT, 50), 0),
              "coat red is not the rule's own answer");

        /* personalities that differ must not come out identical; a different slot is a different person */
        build(&craft, 4242, 3, P_CRAFT);
        build(&endgame, 4242, 3, P_ENDGAME);
        durable = craft;
        build(&durable, 4242, 3, P_ENDGAME);
        CHECK(craft.bulk_permille > endgame.bulk_permille, "the crafter is not heavier than the raider");
        CHECK(endgame.leg_permille > craft.leg_permille, "the raider does not have the longer legs");
        CHECK(!citizen_avatar_equal(&craft, &endgame), "two paths produced the same body");
        /* The coat does NOT move with the path, and that is the design: a citizen's wardrobe is its
         * own, the ACCENT is what says how it plays. Same citizen, same coat, different accent. */
        CHECK(craft.coat[0] == endgame.coat[0] && craft.coat[1] == endgame.coat[1] && craft.coat[2] == endgame.coat[2],
              "a citizen changed coats when its playstyle changed");
        CHECK(endgame.accent[0] != craft.accent[0] || endgame.accent[1] != craft.accent[1] ||
              endgame.accent[2] != craft.accent[2], "the paths' accents are not distinguishable");
    }

    /* ---- a population must look like a population, not a uniform ---- */
    {
        CitizenAvatar pop[256];
        int unique = 0;
        for (int s = 0; s < 256; s++) build(&pop[s], (uint32_t)(s * 7919 + 13), s, s % 5);
        for (int i = 0; i < 256; i++) {
            int clash = 0;
            for (int j = 0; j < i; j++) if (citizen_avatar_equal(&pop[i], &pop[j])) { clash = 1; break; }
            if (!clash) unique++;
        }
        /* Not a uniqueness guarantee -- a city of 64 bodies is allowed near-twins -- but 256 citizens
         * must not read as 6. 200 of 256 is the bar this was designed against. */
        CHECK(unique >= 200, "only %d of 256 citizens are distinct", unique);
    }

    /* ---- renderer surface: permille -> multiplier, and total on nonsense ---- */
    {
        CitizenAvatar av;
        memset(&av, 0, sizeof(av));
        av.bulk_permille = 1120;
        av.leg_permille = 960;
        av.shoulder_permille = 1300;
        CHECK(near_f(citizen_avatar_scale(&av, CITIZEN_AVATAR_PART_TORSO), 1.12f),
              "torso scale %f", citizen_avatar_scale(&av, CITIZEN_AVATAR_PART_TORSO));
        CHECK(near_f(citizen_avatar_scale(&av, CITIZEN_AVATAR_PART_LEG), 0.96f),
              "leg scale %f", citizen_avatar_scale(&av, CITIZEN_AVATAR_PART_LEG));
        CHECK(near_f(citizen_avatar_scale(&av, CITIZEN_AVATAR_PART_SHOULDER), 1.30f),
              "shoulder scale %f", citizen_avatar_scale(&av, CITIZEN_AVATAR_PART_SHOULDER));
        CHECK(near_f(citizen_avatar_scale(&av, 999), 1.0f), "an unknown part did not fall back to 1.0");
        CHECK(near_f(citizen_avatar_scale(NULL, CITIZEN_AVATAR_PART_TORSO), 1.0f), "NULL did not fall back to 1.0");
    }

    /* ---- total on bad input ---- */
    {
        CitizenAvatar av;
        memset(&av, 0xAB, sizeof(av));
        citizen_avatar_build(NULL, 1, 1, 1, 1, 1, 1, 1, 1);
        citizen_avatar_build(&av, 0xffffffffu, -5, 9999, -100, 9999, -100, 9999, -100);
        CHECK(av.sex_bias >= 0 && av.sex_bias <= 1000, "bias %d on nonsense input", av.sex_bias);
        CHECK(av.height_mm >= 1500 && av.height_mm <= 1980, "height %d on nonsense input", av.height_mm);
        CHECK(av.bulk_permille >= 780 && av.bulk_permille <= 1250, "bulk %d on nonsense input", av.bulk_permille);
        CHECK(av.kit >= 0 && av.kit < 6, "kit %d on nonsense input", av.kit);
        CHECK(citizen_avatar_equal(NULL, NULL) == 1, "NULL equals NULL did not hold");
    }

    if (fails) { printf("citizen_avatar: %d FAILED\n", fails); return 1; }
    printf("citizen_avatar: ALL PASS\n");
    return 0;
}
