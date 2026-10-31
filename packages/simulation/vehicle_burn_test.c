/* vehicle_burn_test.c -- `make test-vehicle-burn`. Asserts the car destruction ladder and the
 * Bedrock Racers fire curve: a healthy car never lights, a damaged one smokes before it burns,
 * flame swells fast and dies slow, the blast happens exactly once at ~5 s, and a scuff is free
 * while a wall at speed costs hull. No PARENA binary needed: this exercises vehicle_burn.h's own
 * defaults, which regen-vehicle-burn-rules installs in the hosts. */
#include <stdio.h>
#include "vehicle_burn.h"
#ifdef SHANKPIT_VEHICLE_BURN_PARENA
#include "vehicle_burn_rules_host.h"
#endif

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

int main(void) {
#ifdef SHANKPIT_VEHICLE_BURN_PARENA
    /* the generated PARENA table must say exactly what this header's defaults say, or the game and
       the test are playing different cars */
    vehicle_burn_rules_install();
    {
        const VehicleBurnRules *g = vehicle_burn_rules();
        CHECK(g->fire_threshold == g_vehicle_burn_defaults.fire_threshold &&
              g->smoke_threshold == g_vehicle_burn_defaults.smoke_threshold &&
              g->rise_milli == g_vehicle_burn_defaults.rise_milli &&
              g->fall_milli == g_vehicle_burn_defaults.fall_milli &&
              g->burn_hp_centi_per_tick == g_vehicle_burn_defaults.burn_hp_centi_per_tick &&
              g->detonate_ticks == g_vehicle_burn_defaults.detonate_ticks &&
              g->light_min_milli == g_vehicle_burn_defaults.light_min_milli &&
              g->light_max_milli == g_vehicle_burn_defaults.light_max_milli &&
              g->blast_radius_milli == g_vehicle_burn_defaults.blast_radius_milli &&
              g->blast_damage == g_vehicle_burn_defaults.blast_damage,
              "PARENA table drifted from vehicle_burn.h's defaults: %d/%d %d/%d %d/%d %d/%d",
              g->fire_threshold, g_vehicle_burn_defaults.fire_threshold,
              g->smoke_threshold, g_vehicle_burn_defaults.smoke_threshold,
              g->burn_hp_centi_per_tick, g_vehicle_burn_defaults.burn_hp_centi_per_tick,
              g->detonate_ticks, g_vehicle_burn_defaults.detonate_ticks);
    }
    CHECK(vehicle_impact_damage(900, 5000) == vehicle_burn_impact_damage(900, 5000),
          "PARENA crash damage %d != header %d", vehicle_impact_damage(900, 5000),
          vehicle_burn_impact_damage(900, 5000));
#endif
    const VehicleBurnRules *r = vehicle_burn_rules();
    CHECK(r->fire_threshold == 75 && r->smoke_threshold == 150, "thresholds %d/%d",
          r->fire_threshold, r->smoke_threshold);
    /* the warn window and the burn clock are the same 5 s: everything between the fire line and
       zero must take exactly detonate_ticks at this rate, or detonate_ticks is a lie */
    CHECK(r->burn_hp_centi_per_tick * r->detonate_ticks == 100 * r->fire_threshold,
          "burn rate %d centi x %d ticks = %d centi-hull, want %d (the fire line)",
          r->burn_hp_centi_per_tick, r->detonate_ticks,
          r->burn_hp_centi_per_tick * r->detonate_ticks, 100 * r->fire_threshold);

    {   /* a healthy car is untouched by 10 s of ticking */
        VehicleBurn v; float hp = 300.0f;
        vehicle_burn_reset(&v, 0, 1u);
        for (int i = 0; i < 600; i++) vehicle_burn_tick(&v, &hp);
        CHECK(hp == 300.0f && v.state == 0 && v.fire_fx == 0.0f && v.smoke == 0.0f,
              "healthy car changed: hp=%.2f state=%d fire=%.3f smoke=%.3f", hp, v.state, v.fire_fx, v.smoke);
    }

    {   /* smoke comes first, and no flame with it */
        VehicleBurn v; float hp = 140.0f;
        vehicle_burn_reset(&v, 0, 2u);
        for (int i = 0; i < 20; i++) vehicle_burn_tick(&v, &hp);
        CHECK(v.state == 1, "140 hull should smoke, state=%d", v.state);
        CHECK(v.smoke > 0.25f && v.fire_fx == 0.0f, "smoke=%.3f fire=%.3f", v.smoke, v.fire_fx);
        CHECK(hp == 140.0f, "smoking costs no hull, hp=%.2f", hp);
    }

    {   /* the flame swells fast -- BR's 0.25 of the gap per tick */
        VehicleBurn v; float hp = 74.0f;
        vehicle_burn_reset(&v, 0, 3u);
        for (int i = 0; i < 4; i++) vehicle_burn_tick(&v, &hp);
        CHECK(v.state == 2 && v.fire_fx > 0.6f, "4 ticks of fire = %.3f", v.fire_fx);
    }

    {   /* it detonates exactly once, at about 5 s */
        VehicleBurn v; float hp = 74.0f;
        int blasts = 0, ticks = 0;
        vehicle_burn_reset(&v, 0, 4u);
        for (ticks = 0; ticks < 2000 && hp > 0.0f; ticks++) blasts += vehicle_burn_tick(&v, &hp);
        CHECK(blasts == 1, "expected one blast, got %d", blasts);
        CHECK(ticks >= 280 && ticks <= 310, "detonated at tick %d (want ~296-300)", ticks);
        for (int i = 0; i < 120; i++) blasts += vehicle_burn_tick(&v, &hp);
        CHECK(blasts == 1, "a dead car blasted twice");
    }

    {   /* the fire dies slow: even repaired, it keeps glowing for a while */
        VehicleBurn v; float hp = 70.0f;
        vehicle_burn_reset(&v, 0, 5u);
        for (int i = 0; i < 6; i++) vehicle_burn_tick(&v, &hp);
        float lit = v.fire_fx;
        hp = 300.0f;    /* repaired */
        for (int i = 0; i < 5; i++) vehicle_burn_tick(&v, &hp);
        CHECK(v.fire_fx > lit * 0.6f, "flame dropped too fast: %.3f -> %.3f", lit, v.fire_fx);
        for (int i = 0; i < 200; i++) vehicle_burn_tick(&v, &hp);
        CHECK(v.fire_fx < 0.01f && v.state == 0, "should be out: fire=%.4f state=%d", v.fire_fx, v.state);
    }

    {   /* the flicker stays in BR's 1.4 .. 2.4 a car is worth */
        VehicleBurn v; float hp = 70.0f;
        vehicle_burn_reset(&v, 0, 6u);
        for (int i = 0; i < 30; i++) vehicle_burn_tick(&v, &hp);
        for (unsigned t = 0; t < 64; t++) {
            float l = vehicle_burn_light(&v, t);
            CHECK(l >= 1.3f && l <= 2.45f, "tick %u light %.3f", t, l);
        }
        CHECK(vehicle_burn_light(NULL, 0) == 0.0f, "no record, no light");
    }

    /* crashes: a scuff is free, a wall at speed is not */
    CHECK(vehicle_burn_impact_damage(900, 300) == 0, "slow bump cost %d", vehicle_burn_impact_damage(900, 300));
    CHECK(vehicle_burn_impact_damage(900, 5000) == 22, "900 kg at 5.0/tick = %d (want 22)",
          vehicle_burn_impact_damage(900, 5000));
    CHECK(vehicle_burn_impact_damage(900, -5000) == 22, "direction must not matter");
    CHECK(vehicle_burn_impact_damage(900, 5000) < vehicle_burn_hull(0) / 4,
          "one crash must not be lethal");

    if (fails) { printf("vehicle_burn: %d FAILED\n", fails); return 1; }
    printf("vehicle_burn: all checks passed\n");
    return 0;
}
