/* empire_rules_test.c -- `make test-empire-rules`. Asserts the influence economy and the sphere's
 * district geometry through the real generated PARENA table: what a troop costs, why a clone costs
 * more, when placement is refused, how a district flips and pays, and that the rings actually close
 * into a sphere-sized world. */
#include <stdio.h>
#include "empire_rules_host.h"

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

int main(void) {
    /* ---- influence ---- */
    CHECK(empire_influence_start() > 0, "start influence %d", empire_influence_start());
    CHECK(empire_influence_max() > empire_influence_start(), "cap %d must exceed start %d",
          empire_influence_max(), empire_influence_start());
    CHECK(empire_influence_per_second() > 0, "no income");

    /* ---- troop costs, and the clone premium ---- */
    CHECK(empire_troop_kind_count() == 4, "kinds %d", empire_troop_kind_count());
    for (int k = 0; k < empire_troop_kind_count(); k++) {
        int cost = empire_troop_cost(k);
        CHECK(cost > 0, "kind %d costs %d", k, cost);
        CHECK(empire_clone_cost(k) > cost, "clone of %d (%d) must cost more than a recruit (%d)",
              k, empire_clone_cost(k), cost);
    }
    /* the mini fly is the expensive one: it is the hardest body to get anywhere */
    CHECK(empire_troop_cost(3) > empire_troop_cost(0), "mini fly %d should outprice rifleman %d",
          empire_troop_cost(3), empire_troop_cost(0));

    /* ---- the gate the phone's greyed-out row reads ---- */
    CHECK(empire_can_spawn(0, 0, 0) == 0, "broke player may spawn");
    CHECK(empire_can_spawn(empire_troop_cost(0), 0, 0) == 1, "exact change refused");
    CHECK(empire_can_spawn(empire_troop_cost(0) - 1, 0, 0) == 0, "one short allowed");
    CHECK(empire_can_spawn(10000, 99, 0) == 0, "unknown kind allowed");
    CHECK(empire_can_spawn(10000, 0, empire_squad_cap()) == 0, "cap ignored");

    CHECK(empire_clone_can(10000, 0, 0, 0) == 0, "cloned a body that does not exist");
    CHECK(empire_clone_can(empire_clone_cost(0), 0, 0, 1) == 1, "a veteran clone was refused");
    CHECK(empire_clone_can(empire_clone_cost(0) - 1, 0, 0, 1) == 0, "clone discounted");
    CHECK(empire_clone_can(10000, 0, empire_squad_cap(), 1) == 0, "clone ignored the cap");

    /* ---- banking: carried goods become influence only once banked, and the cap is real ---- */
    {
        int rate = empire_influence_per_banked_unit();
        int cap = empire_influence_max();
        int per_rifleman = empire_troop_cost(0) / (rate > 0 ? rate : 1) + 1;
        CHECK(rate > 0, "banking pays nothing");
        CHECK(empire_bank_credit(0, 100, cap) == 0, "banked nothing and was paid");
        CHECK(empire_bank_credit(-5, 100, cap) == 0, "a negative carry was paid out");
        CHECK(empire_bank_credit(10, 100, cap) == 10 * rate, "ten banked paid %d, want %d",
              empire_bank_credit(10, 100, cap), 10 * rate);
        CHECK(empire_bank_credit(10, cap, cap) == 0, "a full pool still paid out");
        CHECK(empire_bank_credit(10, cap - 3, cap) == 3, "the cap was not clamped to the gap: %d",
              empire_bank_credit(10, cap - 3, cap));
        CHECK(cap - 3 + empire_bank_credit(10, cap - 3, cap) <= cap, "credit pushed the pool over its cap");
        /* the round trip that matters: bank a rifleman's worth and you can actually place one */
        CHECK(empire_bank_credit(per_rifleman, 0, cap) >= empire_troop_cost(0),
              "banking %d units (%d) did not buy a %d-cost rifleman", per_rifleman,
              empire_bank_credit(per_rifleman, 0, cap), empire_troop_cost(0));
    }

    /* ---- districts: TRAPX's three-way alignment, with income attached ---- */
    int t = empire_district_threshold_milli();
    CHECK(empire_district_state(t, 0) == 1, "held district not yours");
    CHECK(empire_district_state(0, t) == 2, "rival pressure not theirs");
    CHECK(empire_district_state(t - 1, t - 1) == 0, "contested district claimed by nobody");
    CHECK(empire_influence_from_district(1, 30) == 30, "yours should pay in full");
    CHECK(empire_influence_from_district(2, 30) == 0, "a rival's district paid you");
    CHECK(empire_influence_from_district(0, 30) > 0 &&
          empire_influence_from_district(0, 30) < 30, "neutral should pay a third, got %d",
          empire_influence_from_district(0, 30));

    /* ---- the sphere ---- */
    {
        int rings = empire_sphere_rings();
        int total = 0, middle = 0, pole = 0;
        CHECK(rings >= 3, "rings %d", rings);
        for (int r = 0; r < rings; r++) {
            int n = empire_districts_in_ring(r);
            CHECK(n > 0, "ring %d empty", r);
            total += n;
            if (r == 0) pole = n;
            if (r == rings / 2) middle = n;
        }
        CHECK(empire_districts_in_ring(rings) == 0, "a ring past the last one exists");
        CHECK(empire_districts_in_ring(-1) == 0, "a negative ring exists");
        CHECK(total == 72, "districts total %d (want 72, the number this stack keeps coming back to)", total);
        CHECK(middle > pole, "the equator ring (%d) must hold more than the pole cap (%d) or the map pinches",
              middle, pole);
        CHECK(empire_sphere_radius_milli() > empire_district_radius_milli(), "sphere smaller than a district");
        CHECK(empire_nearby_districts() < total, "the whole world cannot be 'nearby'");
        CHECK(empire_spawn_radius_milli() > 0 && empire_spawn_radius_milli() < empire_district_radius_milli() * 2,
              "spawn radius %d is not a local placement", empire_spawn_radius_milli());
    }

    if (fails) { printf("empire_rules: %d FAILED\n", fails); return 1; }
    printf("empire_rules: all checks passed\n");
    return 0;
}
