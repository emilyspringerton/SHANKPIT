/* vehicle_salvage_test.c -- `make test-vehicle-salvage`.
 *
 * Hand-derived, no PARENA binary required at run time (the generated .c is linked in, and under
 * -DSHANKPIT_VEHICLE_SALVAGE_PARENA the test also asserts the generated table says exactly what
 * vehicle_salvage.h's built-in defaults say -- if those drift, the game and this test are parting out
 * different cars).
 *
 * The checks are the invariants, not the vibes:
 *   1. parts sum to the hull at spawn (90+120+60+30 = 300, and 80+100+50+30+40 = 300 for the rocket),
 *   2. every hit removes exactly what it was given and nothing more,
 *   3. a part that reaches zero comes off ONCE and stays off,
 *   4. the handling stack is the product of the live parts' permille, in order,
 *   5. the salvage table is the hand-written one, and the roll is reproducible from identity.
 */
#include <stdio.h>

#include "vehicle_salvage.h"
#ifdef SHANKPIT_VEHICLE_SALVAGE_PARENA
#include "vehicle_salvage_rules_host.h"
#include "vehicle_burn_rules_host.h"   /* vehicle_hull -- the car this salvage table must agree with */
#endif

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

int main(void) {
#ifdef SHANKPIT_VEHICLE_SALVAGE_PARENA
    vehicle_salvage_rules_install();
    {
        const VehicleSalvageRules *r = vehicle_salvage_rules();
        CHECK(r->component_count(0) == salvage_def_component_count(0) &&
              r->component_count(1) == salvage_def_component_count(1),
              "PARENA component counts differ from the header's defaults");
        for (int kind = 0; kind <= 1; kind++)
            for (int c = 0; c < SALVAGE_MAX_COMPONENTS; c++)
                CHECK(r->component_hp(kind, c) == salvage_def_component_hp(kind, c),
                      "PARENA hp kind=%d comp=%d = %d, header says %d", kind, c,
                      r->component_hp(kind, c), salvage_def_component_hp(kind, c));
        for (int c = 0; c < SALVAGE_MAX_COMPONENTS; c++)
            CHECK(r->component_material(c) == salvage_def_component_material(c),
                  "PARENA material comp=%d differs from the header", c);
        for (int c = 0; c < SALVAGE_MAX_COMPONENTS; c++)
            for (int ch = 0; ch < SALVAGE_CH_COUNT; ch++)
                CHECK(r->handling_permille(c, ch) == salvage_def_handling_permille(c, ch),
                      "PARENA handling comp=%d ch=%d = %d, header says %d", c, ch,
                      r->handling_permille(c, ch), salvage_def_handling_permille(c, ch));
        for (int kind = 0; kind <= 1; kind++)
            for (int c = 0; c < SALVAGE_MAX_COMPONENTS; c++)
                for (int roll = 0; roll < 1000; roll++)
                    CHECK(r->tier(kind, c, roll) == salvage_def_tier(kind, c, roll),
                          "PARENA tier kind=%d comp=%d roll=%d differs", kind, c, roll);
        for (int q = 0; q <= 1000; q += 1)
            CHECK(r->quantity(q) == salvage_def_quantity(q), "PARENA quantity %d differs", q);
        CHECK(salvage_hull(0) == SALVAGE_HULL && salvage_hull(1) == SALVAGE_HULL,
              "PARENA hull %d/%d != %d", salvage_hull(0), salvage_hull(1), SALVAGE_HULL);
        /* the burn ladder and the parts must describe the same car */
        for (int kind = 0; kind <= 1; kind++)
            CHECK(salvage_hull(kind) == vehicle_hull(kind),
                  "salvage hull %d != vehicle_burn hull %d", salvage_hull(kind), vehicle_hull(kind));
    }
#endif
    /* ---- 1. the parts add up to the hull ------------------------------------------------------- */
    {   SalvageVehicle v;
        vehicle_salvage_reset(&v, 0, 7);
        CHECK(vehicle_salvage_count(&v) == 4, "buggy has %d components", vehicle_salvage_count(&v));
        CHECK(v.hp[0] == 90 && v.hp[1] == 120 && v.hp[2] == 60 && v.hp[3] == 30,
              "buggy parts %d/%d/%d/%d", v.hp[0], v.hp[1], v.hp[2], v.hp[3]);
        CHECK(vehicle_salvage_total(&v) == SALVAGE_HULL, "buggy total %d", vehicle_salvage_total(&v));
        vehicle_salvage_reset(&v, 1, 8);
        CHECK(vehicle_salvage_count(&v) == 5, "rocket car has %d components", vehicle_salvage_count(&v));
        CHECK(v.hp[0] == 80 && v.hp[1] == 100 && v.hp[2] == 50 && v.hp[3] == 30 && v.hp[4] == 40,
              "rocket parts %d/%d/%d/%d/%d", v.hp[0], v.hp[1], v.hp[2], v.hp[3], v.hp[4]);
        CHECK(vehicle_salvage_total(&v) == SALVAGE_HULL, "rocket total %d", vehicle_salvage_total(&v));
        CHECK((v.alive_mask & 0x1Fu) == 0x1Fu, "flash: mask %u", v.alive_mask);
    }

    /* ---- 2. a hit removes exactly what it was given -------------------------------------------- */
    {   SalvageVehicle v;
        vehicle_salvage_reset(&v, 0, 9);
        unsigned int fresh = vehicle_salvage_apply(&v, 120);
        /* 120 split 90/120/60/30 by weight: 36 / 48 / 24 / 12 */
        CHECK(v.hp[0] == 54 && v.hp[1] == 72 && v.hp[2] == 36 && v.hp[3] == 18,
              "120 damage gave %d/%d/%d/%d", v.hp[0], v.hp[1], v.hp[2], v.hp[3]);
        CHECK(vehicle_salvage_total(&v) == SALVAGE_HULL - 120, "total %d", vehicle_salvage_total(&v));
        CHECK(fresh == 0, "nothing should be off yet, mask %u", fresh);
        CHECK(vehicle_salvage_apply(&v, 0) == 0, "zero damage detaches nothing");
        CHECK(vehicle_salvage_total(&v) == SALVAGE_HULL - 120, "zero damage cost hull");
    }

    /* ---- 3. a part comes off once, and stays off ------------------------------------------------ */
    {   SalvageVehicle v;
        vehicle_salvage_reset(&v, 0, 10);
        /* kill the logic board (30) first: 30 by weight = 9/12/6/3 per hit, so after 10 hits of 30
           it is long gone while the chassis still holds */
        unsigned int seen = 0;
        int first_logic = -1;
        for (int i = 0; i < 12; i++) {
            unsigned int fresh = vehicle_salvage_apply(&v, 30);
            CHECK((fresh & seen) == 0, "a component was reported off twice (mask %u)", fresh & seen);
            seen |= fresh;
            if ((fresh & (1u << SALVAGE_COMP_LOGIC)) && first_logic < 0) first_logic = i;
        }
        CHECK(first_logic >= 0, "the logic board never came off");
        CHECK((v.alive_mask & (1u << SALVAGE_COMP_LOGIC)) == 0, "logic should be off, mask %u", v.alive_mask);
        CHECK((v.detached_mask & (1u << SALVAGE_COMP_LOGIC)) != 0, "detached_mask lost the fact");
        /* a part that is already off is never reported again, and its hp does not go negative */
        CHECK(v.hp[SALVAGE_COMP_LOGIC] == 0, "logic hp %d", v.hp[SALVAGE_COMP_LOGIC]);
        int before = v.hp[SALVAGE_COMP_CHASSIS];
        vehicle_salvage_apply(&v, 10);
        CHECK(v.hp[SALVAGE_COMP_CHASSIS] <= before, "chassis grew");
        CHECK(v.hp[SALVAGE_COMP_LOGIC] == 0, "a dead part absorbed damage: %d", v.hp[SALVAGE_COMP_LOGIC]);
    }

    /* ---- 2b. conservation under a real volley (the invariant that matters) ---------------------- */
    {   SalvageVehicle v;
        vehicle_salvage_reset(&v, 1, 11);
        int hull = vehicle_salvage_total(&v);
        unsigned int seed = 12345u;
        for (int i = 0; i < 400; i++) {
            seed = seed * 1103515245u + 12345u;
            int dmg = (int)((seed >> 16) % 45u);           /* 0..44 per hit */
            int before = vehicle_salvage_total(&v);
            vehicle_salvage_apply(&v, dmg);
            int after = vehicle_salvage_total(&v);
            int want = before - dmg;
            if (want < 0) want = 0;                        /* the car is a wreck; no negative hull */
            CHECK(after == want, "hit %d: %d - %d = %d, want %d", i, before, dmg, after, want);
            CHECK(after >= 0 && after <= hull, "hull left the world: %d", after);
            hull = after;
        }
    }

    /* ---- 4. the handling stack ----------------------------------------------------------------- */
    {   SalvageVehicle v;
        vehicle_salvage_reset(&v, 0, 12);
        CHECK(vehicle_salvage_handling_permille(&v, SALVAGE_CH_DRIVE_FORCE) == 1000, "intact car drives flat out");
        /* engine off alone: 1000 * 300 / 1000 */
        v.alive_mask &= ~(1u << SALVAGE_COMP_ENGINE);
        v.detached_mask |= (1u << SALVAGE_COMP_ENGINE);
        CHECK(vehicle_salvage_handling_permille(&v, SALVAGE_CH_DRIVE_FORCE) == 300,
              "engine off = %d", vehicle_salvage_handling_permille(&v, SALVAGE_CH_DRIVE_FORCE));
        CHECK(vehicle_salvage_handling_permille(&v, SALVAGE_CH_TOP_SPEED) == 600, "engine top speed");
        CHECK(vehicle_salvage_handling_permille(&v, SALVAGE_CH_GRIP) == 1000, "engine must not cost grip");
        /* + wheels off: 300 * 800 / 1000 = 240 drive */
        v.alive_mask &= ~(1u << SALVAGE_COMP_WHEELS);
        v.detached_mask |= (1u << SALVAGE_COMP_WHEELS);
        CHECK(vehicle_salvage_handling_permille(&v, SALVAGE_CH_DRIVE_FORCE) == 240, "engine+wheels");
        CHECK(vehicle_salvage_handling_permille(&v, SALVAGE_CH_GRIP) == 400, "wheels cost grip");
        /* the logic board is not a handling part */
        vehicle_salvage_reset(&v, 0, 13);
        v.alive_mask &= ~(1u << SALVAGE_COMP_LOGIC);
        v.detached_mask |= (1u << SALVAGE_COMP_LOGIC);
        for (int ch = 0; ch < SALVAGE_CH_COUNT; ch++)
            CHECK(vehicle_salvage_handling_permille(&v, ch) == 1000, "logic changed channel %d", ch);
        CHECK(vehicle_salvage_handling_permille(NULL, 0) == 1000, "no car, no penalty");
    }

    /* ---- 5. the salvage table + reproducibility ------------------------------------------------- */
    CHECK(salvage_def_tier(0, SALVAGE_COMP_LOGIC, 0) == 3, "logic 0 roll should be tier 3");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_LOGIC, 179) == 3, "logic 179");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_LOGIC, 180) == 2, "logic 180");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_LOGIC, 499) == 2, "logic 499");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_LOGIC, 500) == 1, "logic 500");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_ENGINE, 59) == 3, "engine 59");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_ENGINE, 60) == 2, "engine 60");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_CHASSIS, 29) == 2, "chassis 29");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_CHASSIS, 30) == 0, "chassis 30");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_WHEELS, 119) == 2, "wheels 119");
    CHECK(salvage_def_tier(0, SALVAGE_COMP_WHEELS, 120) == 0, "wheels 120");
    CHECK(salvage_def_tier(1, SALVAGE_COMP_TANK, 89) == 2, "tank 89");
    CHECK(salvage_def_tier(1, SALVAGE_COMP_TANK, 90) == 0, "tank 90");
    CHECK(salvage_def_quantity(1000) == 3 && salvage_def_quantity(900) == 3, "quantity 900+");
    CHECK(salvage_def_quantity(899) == 2 && salvage_def_quantity(400) == 2, "quantity 400..899");
    CHECK(salvage_def_quantity(399) == 1, "quantity under 400");
    {   SalvageVehicle v, w;
        vehicle_salvage_reset(&v, 0, 42);
        vehicle_salvage_reset(&w, 0, 42);
        for (int c = 0; c < 4; c++) {
            int a = vehicle_salvage_roll_permille(&v, c, 999u);
            int b = vehicle_salvage_roll_permille(&w, c, 999u);
            CHECK(a == b, "same car, different roll on comp %d: %d vs %d", c, a, b);
            CHECK(a >= 0 && a < 1000, "roll out of range: %d", a);
            CHECK(vehicle_salvage_tier(&v, c, 999u) == vehicle_salvage_tier(&w, c, 999u),
                  "same car, different tier on comp %d", c);
        }
        /* a different car (or a different tick) is allowed to differ -- and over a spread of ticks it
           must actually spread, or the "roll" is a constant in disguise */
        int distinct = 0, last = -1;
        for (unsigned int t = 0; t < 40; t++) {
            int r = vehicle_salvage_roll_permille(&v, SALVAGE_COMP_LOGIC, t);
            if (r != last) distinct++;
            last = r;
        }
        CHECK(distinct > 30, "the roll barely moves with the tick: %d distinct of 40", distinct);
    }
    {   /* quantity is condition: a part at 92% is worth three, a wrecked one one */
        SalvageVehicle v;
        vehicle_salvage_reset(&v, 0, 43);
        CHECK(vehicle_salvage_quantity(&v, SALVAGE_COMP_ENGINE) == 3, "intact engine quantity");
        v.hp[SALVAGE_COMP_ENGINE] = 80;    /* 80 of 90 = 888 permille */
        CHECK(vehicle_salvage_quantity(&v, SALVAGE_COMP_ENGINE) == 2, "888 permille should be 2");
        v.hp[SALVAGE_COMP_ENGINE] = 10;
        CHECK(vehicle_salvage_quantity(&v, SALVAGE_COMP_ENGINE) == 1, "wrecked engine should be 1");
    }
    {   /* materials: the logic board is glass, the wheels are wood, the rest concrete */
        CHECK(vehicle_salvage_material(SALVAGE_COMP_LOGIC) == 3, "logic material");
        CHECK(vehicle_salvage_material(SALVAGE_COMP_WHEELS) == 2, "wheels material");
        CHECK(vehicle_salvage_material(SALVAGE_COMP_CHASSIS) == 1, "chassis material");
    }

    if (fails) { printf("vehicle_salvage: %d FAILED\n", fails); return 1; }
    printf("vehicle_salvage: all checks passed\n");
    return 0;
}
