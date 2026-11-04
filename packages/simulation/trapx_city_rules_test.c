/* trapx_city_rules_test.c -- `make test-trapx-city`.
 *
 * Asserts the city-scale TRAPX engine through the real generated PARENA table: population drift, money
 * flow and the faction siphon, intel handoff, pressure spread/decay/conversion, trains and ships, the
 * power grid, casino conversion, local scrutiny and crime, federal oversight, neighborhood evolution, the
 * Spire's triggers, and the automata overlay.
 *
 * Every expected value below is hand-derived from stdlib/trapx/city_engine_rules.prn, not read back out
 * of the C. The last block is the one that matters most: the spec's own feedback loop (money -> casino ->
 * corruption -> worse money) asserted as behaviour, not as individual formulas.
 */
#include <stdio.h>
#include "trapx_city_rules_host.h"

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

int main(void) {
    /* ---- units and the clock ---- */
    CHECK(ce_one() == 1000, "one is %d permille", ce_one());
    CHECK(ce_tick_ms() == 60000, "tick %d ms", ce_tick_ms());
    CHECK(ce_automata_period_ticks() == 30, "automata period %d", ce_automata_period_ticks());
    CHECK(ce_pop_max() == 4000, "pop max %d", ce_pop_max());
    CHECK(ce_clamp(5, 0, 3) == 3, "clamp high");
    CHECK(ce_clamp(-5, 0, 3) == 0, "clamp low");

    /* ---- 1. population drift: base 50, +stability*0.05, -corruption*0.1, -crime*0.05 ---- */
    CHECK(ce_pop_growth_milli(1000, 0, 0, 0) == 100,
          "a clean stable cell grows %d, want 100", ce_pop_growth_milli(1000, 0, 0, 0));
    CHECK(ce_pop_growth_milli(0, 1000, 0, 0) == -50,
          "a fully corrupt cell grows %d, want -50", ce_pop_growth_milli(0, 1000, 0, 0));
    CHECK(ce_pop_growth_milli(0, 1000, 1000, 0) == -100,
          "corrupt AND lawless %d, want -100", ce_pop_growth_milli(0, 1000, 1000, 0));
    CHECK(ce_pop_growth_milli(0, 0, 0, 100) == 150, "train boost not added: %d",
          ce_pop_growth_milli(0, 0, 0, 100));
    CHECK(ce_pop_growth_milli(0, 0, 0, 500) == 300, "a huge boost was not clamped to 300");
    CHECK(ce_pop_growth_milli(1000, 0, 0, 1000) == 300, "growth was not clamped to 300");
    CHECK(ce_pop_growth_milli(0, 1000, 1000, -300) == -300, "decline was not clamped to -300");

    /* the add. 0.1 people/tick truncates to nothing in a single tick -- the host keeps the remainder. */
    CHECK(ce_pop_step(100, 100, 4000) == 100, "a tenth of a person moved a whole one");
    CHECK(ce_pop_step(100, 2000, 4000) == 102, "two people did not arrive");
    CHECK(ce_pop_step(3999, 5000, 4000) == 4000, "pop blew past its ceiling");
    CHECK(ce_pop_step(3, -5000, 4000) == 0, "pop went negative");

    /* ---- 6a. economy: population * stability * (1 - corruption) ---- */
    CHECK(ce_money_flow(1000, 1000, 0) == 1000, "clean flow %d", ce_money_flow(1000, 1000, 0));
    CHECK(ce_money_flow(1000, 800, 500) == 400, "half-rotten flow %d", ce_money_flow(1000, 800, 500));
    CHECK(ce_money_flow(100, 800, 500) == 40, "flow does not scale with pop");
    CHECK(ce_money_flow(1000, 1000, 2000) == 0, "corruption past 1000 was not clamped");

    CHECK(ce_faction_siphon(400, 500) == 200, "half the flow is %d", ce_faction_siphon(400, 500));
    CHECK(ce_faction_siphon(400, 1000) == 400, "all of the flow is not all of it");
    CHECK(ce_faction_siphon(400, 2000) == 400, "influence share past 1000 was not clamped");

    /* ---- 3. intel: chance = 0.1 + instability*0.2 ---- */
    CHECK(ce_intel_chance_milli(0) == 100, "calm chance %d", ce_intel_chance_milli(0));
    CHECK(ce_intel_chance_milli(1000) == 300, "unstable chance %d", ce_intel_chance_milli(1000));
    CHECK(ce_intel_transfer(0, 99) == 1, "roll 99 should hand off at a 10%% chance");
    CHECK(ce_intel_transfer(0, 100) == 0, "roll 100 should not");
    CHECK(ce_intel_transfer(1000, 299) == 1, "roll 299 should hand off at 30%%");
    CHECK(ce_intel_transfer(1000, 300) == 0, "roll 300 should not");

    /* ---- 4. spread, decay, conversion ---- */
    CHECK(ce_faction_spread_force(1000) == 10, "full influence spreads %d", ce_faction_spread_force(1000));
    CHECK(ce_faction_spread_force(500) == 5, "half influence spreads %d", ce_faction_spread_force(500));
    CHECK(ce_faction_convert(600, 600) == 1, "exactly at threshold did not convert");
    CHECK(ce_faction_convert(599, 600) == 0, "one under threshold converted");
    CHECK(ce_pressure_decay(1000) == 950, "decay of 1000 is %d", ce_pressure_decay(1000));
    CHECK(ce_pressure_decay(100) == 95, "decay of 100 is %d", ce_pressure_decay(100));

    /* ---- 5. trains and ships ---- */
    CHECK(ce_train_boost(100, 0) == 100, "undisrupted train lost its boost");
    CHECK(ce_train_boost(100, 500) == 50, "half-disrupted train kept %d", ce_train_boost(100, 500));
    CHECK(ce_train_boost(100, 1000) == 0, "dead line still delivered");
    CHECK(ce_train_boost(100, 5000) == 0, "disruption past 1000 was not clamped");

    CHECK(ce_ship_falloff(0) == 1000 && ce_ship_falloff(1) == 500 && ce_ship_falloff(2) == 250 &&
          ce_ship_falloff(3) == 125 && ce_ship_falloff(4) == 62 && ce_ship_falloff(5) == 31,
          "the falloff table is not halving");
    CHECK(ce_ship_falloff(-1) == 1000, "negative distance has no falloff");
    CHECK(ce_ship_falloff(99) == 31, "far ships did not reach the floor");
    CHECK(ce_ship_resource_shock(1000, 2) == 250, "shock at two cells %d", ce_ship_resource_shock(1000, 2));
    CHECK(ce_ship_resource_shock(1000, 4) == 62, "shock at four cells %d", ce_ship_resource_shock(1000, 4));

    /* ---- 5b. the grid ---- */
    CHECK(ce_infra_stability_step(500, 500, 1000) == 503, "a healthy node did not firm the block up");
    CHECK(ce_infra_stability_step(500, 2000, 1000) == 490, "overload %d", ce_infra_stability_step(500, 2000, 1000));
    CHECK(ce_infra_stability_step(500, 5000, 1000) == 460, "heavy overload %d", ce_infra_stability_step(500, 5000, 1000));
    CHECK(ce_infra_stability_step(500, 20000, 1000) == 460, "the overload penalty was not capped");
    CHECK(ce_infra_stability_step(5, 2000, 1000) == 0, "an overloaded block went negative");

    /* ---- 7. casinos ---- */
    CHECK(ce_casino_inflow(1000, 500) == 500, "inflow %d", ce_casino_inflow(1000, 500));
    CHECK(ce_casino_processed(500, 2000) == 1000, "a 2x multiplier produced %d", ce_casino_processed(500, 2000));
    CHECK(ce_casino_banker_share(1000, 700) == 700, "banker share %d", ce_casino_banker_share(1000, 700));
    CHECK(ce_casino_oversight_pressure(1000, 100) == 100, "oversight %d", ce_casino_oversight_pressure(1000, 100));
    CHECK(ce_casino_corruption(1000, 300) == 300, "corruption %d", ce_casino_corruption(1000, 300));
    CHECK(ce_casino_scrutiny(800, 1000, 500) == 300, "money did not buy blind eyes: %d",
          ce_casino_scrutiny(800, 1000, 500));
    CHECK(ce_casino_scrutiny(100, 1000, 500) == 0, "scrutiny went negative");

    /* ---- 8. scrutiny and crime ---- */
    CHECK(ce_scrutiny(500, 0, 0, 0) == 500, "untouched scrutiny %d", ce_scrutiny(500, 0, 0, 0));
    CHECK(ce_scrutiny(500, 1000, 0, 0) == 0, "full corruption did not zero the beat");
    CHECK(ce_scrutiny(500, 0, 1000, 0) == 0, "full casino influence did not zero the beat");
    CHECK(ce_scrutiny(500, 200, 500, 100) == 100, "mixed scrutiny %d", ce_scrutiny(500, 200, 500, 100));
    CHECK(ce_crime_step(100, 100) == 104, "crime under a dead beat %d", ce_crime_step(100, 100));
    CHECK(ce_crime_step(100, 250) == 101, "thinner beat did not add crime");
    CHECK(ce_crime_step(100, 299) == 100, "a one-point gap added crime");
    CHECK(ce_crime_step(100, 300) == 98, "a policed block did not clear crime");
    CHECK(ce_crime_step(1000, 0) == 1000, "crime blew past 1000");

    /* ---- 9. federal oversight ---- */
    CHECK(ce_federal_pressure_step(0, 0, 0, 0) == 0, "idle pressure moved");
    CHECK(ce_federal_pressure_step(0, 10000, 0, 0) == 99, "casino money at 0.01 gave %d",
          ce_federal_pressure_step(0, 10000, 0, 0));
    CHECK(ce_federal_pressure_step(1000, 0, 1, 0) == 999, "a scandal plus decay gave %d",
          ce_federal_pressure_step(1000, 0, 1, 0));
    CHECK(ce_federal_pressure_step(0, 0, 0, 10) == 99, "ten leaks gave %d",
          ce_federal_pressure_step(0, 0, 0, 10));
    CHECK(ce_federal_investigation(800) == 1, "threshold not reached");
    CHECK(ce_federal_investigation(799) == 0, "one under threshold opened an investigation");
    CHECK(ce_federal_seizure(1000) == 300, "seizure %d", ce_federal_seizure(1000));
    CHECK(ce_federal_arrest(1000) == 200, "arrest %d", ce_federal_arrest(1000));
    CHECK(ce_investigation_shock() == 60, "shock %d", ce_investigation_shock());

    /* ---- 10. neighborhoods ---- */
    CHECK(ce_neighborhood_stage(700, 300, 0) == 2, "stable + monied is not a boom");
    CHECK(ce_neighborhood_stage(700, 299, 0) == 1, "stable and poor is a boom");
    CHECK(ce_neighborhood_stage(299, 5000, 0) == 0, "unstable is not decay");
    CHECK(ce_neighborhood_stage(900, 900, 800) == 0, "rot does not outrank stability");
    CHECK(ce_neighborhood_stage(500, 100, 100) == 1, "ordinary block is not stable");
    CHECK(ce_neighborhood_grows(2) == 1 && ce_neighborhood_grows(1) == 0, "grows()");
    CHECK(ce_neighborhood_decays(0) == 1 && ce_neighborhood_decays(1) == 0, "decays()");

    /* ---- 11. the Spire ---- */
    CHECK(ce_spire_event(0, 800, 900, 0) == 1, "unwatched money did not trigger a crash");
    CHECK(ce_spire_event(200, 800, 900, 0) == 0, "a watching city still crashed");
    CHECK(ce_spire_event(500, 0, 200, 0) == 2, "a dead block did not reroute the line");
    CHECK(ce_spire_event(0, 800, 200, 800) == 3, "an intel dump did not outrank a crash");
    CHECK(ce_spire_event(500, 0, 900, 0) == 0, "nothing should have happened");

    /* ---- 12. automata ---- */
    CHECK(ce_automata_step(3, 4, 100) == 3, "an emptied cell was not neutralized first");
    CHECK(ce_automata_step(3, 4, 200) == 2, "four corrupt neighbours did not corrupt");
    CHECK(ce_automata_step(3, 4, 199) == 3, "pop 199 is not empty");
    CHECK(ce_automata_step(0, 4, 500) == 2, "rot did not spread");
    CHECK(ce_automata_step(3, 0, 500) == 1, "three neighbours did not convert");
    CHECK(ce_automata_step(2, 3, 500) == 0, "a quiet cell changed hands");

    /* ---- the spec's own feedback loop, as behaviour -------------------------------------------
     * A block worth 640 a tick, a casino on it taking half the flow and doubling it, and what that does
     * back to the block one tick later. Hand-derived: 640 -> 320 inflow -> 640 processed -> 192
     * corruption -> money 486. That drop IS the doctrine: the money mitochondria feed on what feeds them.
     */
    {
        int pop = 1000, stability = 800, corruption = 200, crime = 100;
        int scrutiny = 500;

        int growth = ce_pop_growth_milli(stability, corruption, crime, 0);
        CHECK(growth == 65, "hand-derived growth is 65, got %d", growth);

        int money = ce_money_flow(pop, stability, corruption);
        CHECK(money == 640, "hand-derived money is 640, got %d", money);

        int taken = ce_faction_siphon(money, 500);
        CHECK(taken == 320, "the faction took %d, want 320", taken);

        int inflow = ce_casino_inflow(money, 500);
        int processed = ce_casino_processed(inflow, 2000);
        int new_corruption = corruption + ce_casino_corruption(processed, 300);
        CHECK(processed == 640, "processed %d", processed);
        CHECK(new_corruption == 392, "corruption after one tick %d", new_corruption);

        int next_money = ce_money_flow(pop, stability, new_corruption);
        CHECK(next_money == 486, "money after one tick %d", next_money);
        CHECK(next_money < money, "the casino did not tax the block that feeds it");

        scrutiny = ce_scrutiny(scrutiny, new_corruption, 500, 0);
        CHECK(scrutiny == 0, "scrutiny after one tick %d", scrutiny);
        CHECK(ce_crime_step(crime, scrutiny) == 106, "crime under a bought beat %d", ce_crime_step(crime, scrutiny));
        CHECK(ce_federal_pressure_step(0, processed, 0, 0) == 5, "the take drew %d federal heat",
              ce_federal_pressure_step(0, processed, 0, 0));
        CHECK(ce_spire_event(ce_federal_pressure_step(0, processed, 0, 0), 800, 500, 0) == 1,
              "the Spire did not notice unobserved casino money");
    }

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("trapx city engine: all checks passed\n");
    return 0;
}
