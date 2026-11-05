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
#include "vehicle_salvage_shards.h"   /* S2: a part off the car is a body in the brick world */
#include "vehicle_salvage_loot.h"    /* S3: what the part is worth on the ground */
#include "../common/protocol.h"      /* S5: the wire struct itself, so its shape is pinned here */
#include <math.h>
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

    /* ---- 6. the parts follow the authority's hull (sync, BOTH directions) ------------------------ */
    {   SalvageVehicle v;
        unsigned int fresh = 123;
        vehicle_salvage_reset(&v, 0, 20);
        CHECK(vehicle_salvage_sync(&v, 300, &fresh) == 300 && fresh == 0,
              "an intact car at 300 moved something");
        int h = vehicle_salvage_sync(&v, 270, &fresh);
        CHECK(h == 270, "hull 270 became %d", h);
        CHECK(vehicle_salvage_total(&v) == 270, "parts %d after sync", vehicle_salvage_total(&v));
        CHECK(fresh == 0, "nothing should be off at 270 (mask %u)", fresh);
        /* the same split apply() makes for 30: 9/12/6/3 by weight */
        CHECK(v.hp[0] == 81 && v.hp[1] == 108 && v.hp[2] == 54 && v.hp[3] == 27,
              "sync split %d/%d/%d/%d", v.hp[0], v.hp[1], v.hp[2], v.hp[3]);
        /* a repair refills every attached part, exactly, back to the wall */
        h = vehicle_salvage_sync(&v, 300, &fresh);
        CHECK(h == 300, "repair to 300 gave %d", h);
        CHECK(v.hp[0] == 90 && v.hp[1] == 120 && v.hp[2] == 60 && v.hp[3] == 30,
              "repair left %d/%d/%d/%d", v.hp[0], v.hp[1], v.hp[2], v.hp[3]);
        CHECK(fresh == 0, "a repair detaches nothing");
    }
    {   /* a repaired WRECK does not grow its parts back: the lost part stays lost, and the hull the car
           can hold is only what is still attached -- otherwise a weld would duplicate a component */
        SalvageVehicle v;
        unsigned int fresh = 0;
        vehicle_salvage_reset(&v, 0, 21);
        v.alive_mask &= ~(1u << SALVAGE_COMP_LOGIC);
        v.detached_mask |= (1u << SALVAGE_COMP_LOGIC);
        v.hp[SALVAGE_COMP_LOGIC] = 0;
        v.hp[0] = 40; v.hp[1] = 50; v.hp[2] = 20;          /* 110 live, logic board gone */
        int cap = 90 + 120 + 60;
        int h = vehicle_salvage_sync(&v, 300, &fresh);
        CHECK(h == cap, "a repaired wreck held %d, want its attached cap %d", h, cap);
        CHECK(vehicle_salvage_total(&v) == h, "parts %d != hull %d after repair",
              vehicle_salvage_total(&v), h);
        CHECK(v.hp[SALVAGE_COMP_LOGIC] == 0, "a detached part came back: %d", v.hp[SALVAGE_COMP_LOGIC]);
        CHECK((v.alive_mask & (1u << SALVAGE_COMP_LOGIC)) == 0, "detached logic rejoined the car");
        CHECK(v.hp[0] == 90 && v.hp[1] == 120 && v.hp[2] == 60, "repair did not fill the attached parts");
    }
    {   /* The split is PROPORTIONAL, so wearing the whole car down does not single a part out: the hull
           falls and the parts wear together, each still attached. A part comes off when the damage is
           more than the car can absorb -- which is exactly what a destroyed car is. */
        SalvageVehicle v;
        unsigned int fresh = 99;
        vehicle_salvage_reset(&v, 0, 22);
        int h = vehicle_salvage_sync(&v, 60, &fresh);
        CHECK(h == 60, "sync to 60 gave %d", h);
        CHECK(fresh == 0, "a proportional split singled out a part (mask %u)", fresh);
        CHECK(v.hp[0] == 18 && v.hp[1] == 24 && v.hp[2] == 12 && v.hp[3] == 6,
              "sync to 60 left %d/%d/%d/%d", v.hp[0], v.hp[1], v.hp[2], v.hp[3]);
        /* a wreck: the hull reaches zero and every part comes off -- once */
        h = vehicle_salvage_sync(&v, 0, &fresh);
        CHECK(h == 0 && vehicle_salvage_total(&v) == 0, "wreck left %d hull / %d in parts",
              h, vehicle_salvage_total(&v));
        CHECK(fresh == 0xFu, "a wreck parted %u (want 0xF)", fresh);
        CHECK(v.detached_mask == 0xFu, "detached_mask %u", v.detached_mask);
        unsigned int again = 99;
        vehicle_salvage_sync(&v, 0, &again);
        CHECK(again == 0, "a second sync re-reported parts off (mask %u)", again);
    }

    /* ---- 7. a sheared part becomes a body in the brick world ----------------------------------- */
    {   BrickBodies bb;
        bb_init(&bb, NULL, NULL);
        vehicle_salvage_shards_bind(&bb);
        CHECK(bb_active_count(&bb) == 0, "fresh world has bodies");
        vehicle_salvage_shards_hook(SALVAGE_COMP_CHASSIS, vehicle_salvage_material(SALVAGE_COMP_CHASSIS),
                                    10.0f, 5.0f, 20.0f, 1.0f, 0.0f, -1.0f, 4242u);
        CHECK(bb_active_count(&bb) == 1, "one part off spawned %d bodies", bb_active_count(&bb));
        CHECK(bb.slot[0].kind == 1 /* BF_KIND_CONCRETE */, "chassis shard material %d", bb.slot[0].kind);
        CHECK(bb_slot_mass(&bb, 0) > 300.0 && bb_slot_mass(&bb, 0) < 340.0,
              "chassis shard mass %.1f", bb_slot_mass(&bb, 0));
        CHECK(fabs(bb.world.bodies[0].pos[0] - 10.0) < 0.6 && fabs(bb.world.bodies[0].pos[2] - 20.0) < 0.6,
              "shard spawned at %.2f/%.2f", bb.world.bodies[0].pos[0], bb.world.bodies[0].pos[2]);
        /* the logic board is the glass one, and a lighter lump */
        vehicle_salvage_shards_hook(SALVAGE_COMP_LOGIC, vehicle_salvage_material(SALVAGE_COMP_LOGIC),
                                    0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 7u);
        CHECK(bb_active_count(&bb) == 2, "two parts off made %d bodies", bb_active_count(&bb));
        CHECK(bb.slot[1].kind == 3 /* BF_KIND_GLASS */, "logic shard material %d", bb.slot[1].kind);
        CHECK(bb_slot_mass(&bb, 1) < 10.0, "logic shard mass %.1f", bb_slot_mass(&bb, 1));
        bb_clear(&bb);
        /* an unbound host (the dedicated server) spawns nothing and cannot crash */
        vehicle_salvage_shards_bind(NULL);
        vehicle_salvage_shards_hook(SALVAGE_COMP_ENGINE, 1, 0, 0, 0, 0, 0, 0, 1u);
        CHECK(bb_active_count(&bb) == 0, "an unbound world spawned a body");
        bb_clear(&bb);
    }


    /* ---- 8. the wire form: absolute, byte hp, and NO quantization error ------------------------ */
    {   SalvageVehicle v, w;
        unsigned int fresh = 0;
        vehicle_salvage_reset(&v, 0, 30);
        vehicle_salvage_sync(&v, 210, &fresh);          /* 90 of 300 gone, nothing detached */
        int hp[NET_VEHICLE_MAX_COMPONENTS] = { 0 };
        for (int c = 0; c < SALVAGE_MAX_COMPONENTS; c++) hp[c] = v.hp[c];
        int on_the_server = vehicle_salvage_total(&v);
        int on_the_client = vehicle_salvage_set_state(&w, 0, 30, v.alive_mask, v.detached_mask, hp);
        CHECK(on_the_client == on_the_server, "wire round trip held %d, the car had %d",
              on_the_client, on_the_server);
        CHECK(vehicle_salvage_total(&w) == on_the_server, "wire round trip sum %d", vehicle_salvage_total(&w));
        CHECK(w.alive_mask == v.alive_mask && w.detached_mask == v.detached_mask,
              "wire round trip masks %u/%u vs %u/%u", w.alive_mask, w.detached_mask,
              v.alive_mask, v.detached_mask);
        for (int c = 0; c < SALVAGE_MAX_COMPONENTS; c++)
            CHECK(w.hp[c] == v.hp[c], "part %d crossed the wire as %d, was %d", c, w.hp[c], v.hp[c]);

        /* untrusted input: hp over a part's max is clamped, and a bit the car does not have is dropped */
        int hostile[NET_VEHICLE_MAX_COMPONENTS] = { 50, 50, 50, 50, 50 };
        int h = vehicle_salvage_set_state(&w, 0, 30, 0xFFu, 0xFFu, hostile);
        CHECK(w.alive_mask == 0xFu, "a 4-part car accepted mask %u", w.alive_mask);
        CHECK(w.detached_mask == 0xFu, "a 4-part car accepted detached mask %u", w.detached_mask);
        CHECK(w.hp[3] == 30, "hull points were not clamped to the part's max: %d", w.hp[3]);
        CHECK(h == 50 + 50 + 50 + 30, "clamped state holds %d", h);

        /* hp and the mask must agree: a part the wire says is OFF holds nothing, whatever hp said */
        int lying[NET_VEHICLE_MAX_COMPONENTS] = { 90, 120, 60, 30, 0 };
        h = vehicle_salvage_set_state(&w, 0, 30, 0xEu /* engine off */, 0x1u, lying);
        CHECK(w.hp[SALVAGE_COMP_ENGINE] == 0, "an off part kept %d hull points", w.hp[SALVAGE_COMP_ENGINE]);
        CHECK(h == 120 + 60 + 30, "state with the engine off holds %d", h);
        CHECK(w.detached_mask == 0x1u, "detached mask grew on its own: %u", w.detached_mask);
    }
    {   /* the packet's own shape, pinned: a layout change must break a test, not a client */
        CHECK(PACKET_VEHICLE_STATE == 20, "packet id moved to %d", PACKET_VEHICLE_STATE);
        /* the entry is a wire layout: 5 hp bytes + kind/id/flags + seed, 16 bytes, and the police bits
           have to fit without the masks or hp moving */
        CHECK(sizeof(NetVehicleEntry) == 16, "NetVehicleEntry is %d bytes, not the wire's 16",
              (int)sizeof(NetVehicleEntry));
        {
            NetVehicleEntry fe;
            memset(&fe, 0, sizeof(fe));
            fe.flags = 0x03;
            CHECK((fe.flags & 0x01) != 0 && (fe.flags & 0x02) != 0,
                  "the police/siren flag bits do not survive in flags");
            CHECK(sizeof(fe.pad) == 2, "the entry's pad is no longer 2 bytes -- the layout moved");
        }
        CHECK(sizeof(NetVehicleEntry) == 16, "NetVehicleEntry is %d bytes, not 16", (int)sizeof(NetVehicleEntry));
        CHECK(NET_VEHICLE_MAX_COMPONENTS == SALVAGE_MAX_COMPONENTS,
              "wire carries %d components, the table has %d", NET_VEHICLE_MAX_COMPONENTS, SALVAGE_MAX_COMPONENTS);
        CHECK(NET_VEHICLE_MAX_ENTRIES == MAX_BUGGIES, "wire caps %d cars, MAX_BUGGIES is %d",
              NET_VEHICLE_MAX_ENTRIES, MAX_BUGGIES);
        /* the TOTAL is never sent -- each part travels in its own byte, so what has to fit is
           the largest single component, checked right below */
        CHECK(NET_VEHICLE_HP_MAX * SALVAGE_MAX_COMPONENTS >= SALVAGE_HULL,
              "%d bytes cannot describe a %d hull", NET_VEHICLE_HP_MAX * SALVAGE_MAX_COMPONENTS, SALVAGE_HULL);
        for (int kind = 0; kind <= 1; kind++)
            for (int c = 0; c < SALVAGE_MAX_COMPONENTS; c++)
                CHECK(salvage_def_component_hp(kind, c) <= NET_VEHICLE_HP_MAX,
                      "component %d of kind %d needs %d bytes", c, kind, salvage_def_component_hp(kind, c));
    }

    /* ---- 9. the ground half: a tier is worth an EXISTING item, and you can pick it up ---------- */
    {   vehicle_salvage_loot_reset();
        CHECK(vehicle_salvage_loot_item(SALVAGE_TIER_LOGIC) == FOOD_KEY, "the logic tier item");
        CHECK(vehicle_salvage_loot_item(SALVAGE_TIER_COMPOSITE) == FOOD_RATION_PACK, "the composite tier item");
        CHECK(vehicle_salvage_loot_item(SALVAGE_TIER_REBAR) == FOOD_ENERGY_BAR, "the rebar tier item");
        CHECK(vehicle_salvage_loot_item(SALVAGE_TIER_SCRAP) == FOOD_COFFEE, "the scrap tier item");
        CHECK(vehicle_salvage_loot_points(SALVAGE_TIER_LOGIC) > vehicle_salvage_loot_points(SALVAGE_TIER_COMPOSITE),
              "the board must be worth more than the mesh");
        CHECK(vehicle_salvage_loot_points(SALVAGE_TIER_COMPOSITE) > vehicle_salvage_loot_points(SALVAGE_TIER_REBAR),
              "the mesh must be worth more than the rebar");
        CHECK(vehicle_salvage_loot_points(SALVAGE_TIER_REBAR) > vehicle_salvage_loot_points(SALVAGE_TIER_SCRAP),
              "the rebar must be worth more than the scrap");

        int k = vehicle_salvage_loot_drop(4, 10.0f, 2.0f, 20.0f, SALVAGE_COMP_ENGINE, SALVAGE_TIER_LOGIC, 2, 1000u);
        CHECK(k >= 0, "the drop was refused");
        CHECK(vehicle_salvage_loot_count() == 1, "drop count %d", vehicle_salvage_loot_count());
        int qty = 0, item = -1;
        CHECK(vehicle_salvage_loot_collect(5, 10.0f, 2.0f, 20.0f, &qty, &item) == -1,
              "collected a drop from another scene");
        CHECK(vehicle_salvage_loot_collect(4, 40.0f, 2.0f, 20.0f, &qty, &item) == -1,
              "collected a drop 30 units away");
        CHECK(vehicle_salvage_loot_collect(4, 11.0f, 2.0f, 20.0f, &qty, &item) == SALVAGE_TIER_LOGIC,
              "walking over the board did not pick it up");
        CHECK(qty == 2 && item == FOOD_KEY, "picked up item %d x%d", item, qty);
        CHECK(vehicle_salvage_loot_count() == 0, "the drop is still on the ground after pickup");

        /* it rots */
        vehicle_salvage_loot_drop(4, 0.0f, 0.0f, 0.0f, SALVAGE_COMP_CHASSIS, SALVAGE_TIER_SCRAP, 1, 1000u);
        vehicle_salvage_loot_tick(1000u + VS_LOOT_ROT_MS);
        CHECK(vehicle_salvage_loot_count() == 1, "a part rotted early");
        vehicle_salvage_loot_tick(1000u + VS_LOOT_ROT_MS + 1u);
        CHECK(vehicle_salvage_loot_count() == 0, "a part never rotted");

        /* a full pool must not lose the part it just produced */
        for (int i = 0; i < VS_LOOT_MAX_DROPS; i++)
            vehicle_salvage_loot_drop(4, 0.0f, 0.0f, 0.0f, SALVAGE_COMP_WHEELS, SALVAGE_TIER_SCRAP, 1, 2000u);
        int newest = vehicle_salvage_loot_drop(4, 1.0f, 1.0f, 1.0f, SALVAGE_COMP_ENGINE, SALVAGE_TIER_LOGIC, 1, 2000u);
        CHECK(newest >= 0 && vehicle_salvage_loot_count() == VS_LOOT_MAX_DROPS,
              "full pool holds %d", vehicle_salvage_loot_count());
        CHECK(vehicle_salvage_loot_at(newest) && vehicle_salvage_loot_at(newest)->tier == SALVAGE_TIER_LOGIC,
              "the newest part was the one lost");
        vehicle_salvage_loot_reset();
        CHECK(vehicle_salvage_loot_count() == 0, "reset left drops behind");
    }

    if (fails) { printf("vehicle_salvage: %d FAILED\n", fails); return 1; }
    printf("vehicle_salvage: all checks passed\n");
    return 0;
}
