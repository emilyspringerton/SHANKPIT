/* frontend_rules_test.c -- `make test-frontend-rules`.
 *
 * The same headline values ../PARENA/scripts/frontend_vs0_wasm_test.mjs asserts against the wasm module,
 * asserted here through the generated C. Two targets, one set of hand-derived answers: if a rule drifts,
 * the browser and the game disagree loudly instead of quietly (the same parity method trapx_parity.txt
 * established for TRAPX's street).
 *
 * The C-only checks at the end are the loop the frontend actually runs: a 1700s world buys track, buys a
 * boiler, gets a train, and turns the century over.
 */
#include <stdio.h>
#include "frontend_rules_host.h"

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

int main(void) {
    /* ---- the calendar ---- */
    CHECK(era_count() == 4, "eras %d", era_count());
    CHECK(era_start() == 0, "start era %d", era_start());
    CHECK(era_year(0) == 1700, "era 0 is %d", era_year(0));
    CHECK(era_year(3) == 2000, "era 3 is %d", era_year(3));

    /* ---- the tree: track -> steam -> freight/passenger -> signal -> armored ---- */
    CHECK(tech_count() == 6, "nodes %d", tech_count());
    CHECK(tech_cost(0) == 2 && tech_cost(5) == 8, "costs %d/%d", tech_cost(0), tech_cost(5));
    CHECK(tech_prereq(0) == -1, "track has a prereq");
    CHECK(tech_prereq(1) == 0, "steam does not need track");
    CHECK(tech_prereq(5) == 4, "armored does not need signal");
    CHECK(tech_can_buy(2, 0, 0, 0) == 1, "exactly enough points refused");
    CHECK(tech_can_buy(1, 0, 0, 0) == 0, "one short allowed");
    CHECK(tech_can_buy(5, 1, 0, 0) == 0, "steam without track allowed");
    CHECK(tech_can_buy(5, 1, 0, 1) == 1, "steam after track refused");
    CHECK(tech_can_buy(99, 2, 0, 3) == 0, "freight in the 1700s");
    CHECK(tech_can_buy(99, 2, 1, 3) == 1, "freight in the 1800s refused");
    CHECK(tech_next_mask(0, 0) == 1, "track mask %d", tech_next_mask(0, 0));
    CHECK(tech_next_mask(1, 0) == 1, "rebuying track moved the mask");

    /* ---- the train ---- */
    CHECK(train_unlocked(1) == 0, "track alone is a train");
    CHECK(train_unlocked(2) == 0, "steam alone is a train");
    CHECK(train_unlocked(3) == 1, "track + steam is not a train");
    CHECK(train_capacity(0) == 0, "no train, seats %d", train_capacity(0));
    CHECK(train_capacity(3) == 4, "plain train seats %d", train_capacity(3));
    CHECK(train_capacity(7) == 8, "freight train seats %d", train_capacity(7));
    CHECK(train_capacity(15) == 12, "full train seats %d", train_capacity(15));
    CHECK(train_speed_milli(3, 0) == 700, "1700s speed %d", train_speed_milli(3, 0));
    CHECK(train_speed_milli(11, 0) == 850, "passenger speed %d", train_speed_milli(11, 0));
    CHECK(train_speed_milli(3, 3) == 1000, "2000s speed %d", train_speed_milli(3, 3));
    CHECK(rails_cost_per_tile(0) == 3 && rails_cost_per_tile(3) == 1, "rails %d/%d",
          rails_cost_per_tile(0), rails_cost_per_tile(3));

    /* ---- the century turns on research, not on a timer ---- */
    CHECK(era_can_advance(0, 3) == 1, "a railway does not advance 1700");
    CHECK(era_can_advance(0, 1) == 0, "track alone advanced 1700");
    CHECK(era_can_advance(1, 15) == 1, "freight+passenger do not advance 1800");
    CHECK(era_can_advance(1, 7) == 0, "freight alone advanced 1800");
    CHECK(era_can_advance(2, 15) == 0, "the 1900s advanced without signals");
    CHECK(era_can_advance(2, 31) == 1, "signals do not advance 1900");
    CHECK(era_advance(0, 3) == 1, "era did not advance");
    CHECK(era_advance(0, 1) == 0, "era advanced on track alone");

    /* ---- mining ---- */
    CHECK(pick_strike_ms(0) == 1200 && pick_strike_ms(3) == 450, "swing %d/%d",
          pick_strike_ms(0), pick_strike_ms(3));
    CHECK(strike_yield(0, 0) == 4, "stone on coal %d", strike_yield(0, 0));
    CHECK(strike_yield(0, 3) == 10, "steel on coal %d", strike_yield(0, 3));
    CHECK(strike_yield(3, 0) == 1, "stone on gold %d", strike_yield(3, 0));
    CHECK(strike_yield(3, 3) == 2, "steel on gold %d", strike_yield(3, 3));
    CHECK(vein_deplete(5, 4) == 1, "deplete %d", vein_deplete(5, 4));
    CHECK(vein_deplete(3, 10) == 0, "vein went negative");
    CHECK(vein_deplete(-5, 2) == 0, "negative vein reads non-empty");
    CHECK(vein_ratio_milli(500, 1000) == 500, "half seam %d", vein_ratio_milli(500, 1000));
    CHECK(vein_ratio_milli(0, 0) == 0, "zero capacity crashed or lied");
    CHECK(can_strike(1000, 0, 0) == 1, "full bar refused");
    CHECK(can_strike(999, 0, 0) == 0, "short bar swung");
    CHECK(can_strike(5000, 10, 0) == 0, "mid-swing swung again");
    CHECK(ore_from_variant(0) == 0 && ore_from_variant(3) == 1 && ore_from_variant(5) == 2 &&
          ore_from_variant(6) == 3 && ore_from_variant(7) == 4 && ore_from_variant(8) == 0,
          "the ore variant table is wrong");
    CHECK(xp_per_ore(4) == 10 && xp_per_ore(0) == 1, "xp %d/%d", xp_per_ore(4), xp_per_ore(0));
    CHECK(tool_integrity_step(1000, 10, 0) == 990, "stone wear %d", tool_integrity_step(1000, 10, 0));
    CHECK(tool_integrity_step(1000, 10, 3) == 960, "steel wear %d", tool_integrity_step(1000, 10, 3));
    CHECK(tool_integrity_step(5, 10, 0) == 0, "tool went negative");
    CHECK(smelt_bars(0, 10) == 0, "coal became a bar");
    CHECK(smelt_bars(1, 10) == 10, "iron did not smelt");
    CHECK(smelt_bars(4, 3) == 0, "a gem became a bar");
    CHECK(smelt_fuel_needed(7) == 7, "fuel %d", smelt_fuel_needed(7));
    CHECK(carry_capacity(0) == 20 && carry_capacity(3) == 50, "cart %d/%d",
          carry_capacity(0), carry_capacity(3));

    /* ---- the citizens' day ---- */
    CHECK(cd_phase(359) == 0 && cd_phase(360) == 1, "the morning boundary moved");
    CHECK(cd_phase(719) == 1 && cd_phase(720) == 2, "the afternoon boundary moved");
    CHECK(cd_phase(1079) == 2 && cd_phase(1080) == 3, "the evening boundary moved");
    CHECK(cd_phase(2000) == 3, "past midnight is not evening");
    CHECK(cd_activity(0, 1) == 1, "a crafter is not working in the morning");
    CHECK(cd_activity(0, 3) == 5, "a crafter does not socialise at night");
    CHECK(cd_activity(0, 0) == 0 && cd_activity(4, 0) == 0, "someone is awake at night");
    CHECK(cd_activity(1, 1) == 2, "a trader does not run the route");
    CHECK(cd_activity(1, 3) == 3, "a trader does not drink");
    CHECK(cd_activity(2, 2) == 4, "a duellist does not patrol");
    CHECK(cd_activity(3, 1) == 6, "an endgame player does not travel");
    CHECK(cd_activity(3, 2) == 1, "an endgame player does not work");
    CHECK(cd_activity(4, 1) == 7, "an idle citizen does not drift");
    CHECK(cd_activity_in_public(0) == 0, "sleep is public");
    CHECK(cd_activity_in_public(3) == 1, "drinking is private");
    CHECK(cd_activity_zone_kind(1) == 1 && cd_activity_zone_kind(3) == 3, "zone kinds moved");
    CHECK(cd_hunger_step(0, 1) == 60, "morning hunger %d", cd_hunger_step(0, 1));
    CHECK(cd_hunger_step(500, 0) == 500, "sleeping made them hungry");
    CHECK(cd_hunger_step(1000, 2) == 1000, "hunger overfilled");
    CHECK(cd_energy_step(1000, 0, 0) == 1000, "sleep overfilled energy");
    CHECK(cd_energy_step(500, 1, 1) == 380, "a shift drained %d",
          cd_energy_step(500, 1, 1));
    CHECK(cd_energy_step(100, 2, 2) == 0, "energy went negative");
    CHECK(cd_mood_step(500, 500, 500) == 500, "neutral mood moved");
    CHECK(cd_mood_step(0, 1000, 1000) == 0, "mood went negative");
    CHECK(cd_wage(0, 1) == 6 && cd_wage(1, 2) == 9, "wages %d/%d", cd_wage(0, 1), cd_wage(1, 2));
    CHECK(cd_wage(4, 1) == 1, "an idle citizen earns %d", cd_wage(4, 1));
    CHECK(cd_wage(1, 0) == 0, "sleeping paid");
    CHECK(cd_should_seek_home(0, 1000) == 1, "night did not send them home");
    CHECK(cd_should_seek_home(1, 100) == 1, "exhaustion did not");
    CHECK(cd_should_seek_home(2, 500) == 0, "a rested citizen went home mid-afternoon");
    CHECK(cd_should_socialize(200, 2) == 1, "a low mood did not seek company");
    CHECK(cd_should_socialize(500, 2) == 0, "a fine mood sought company");
    CHECK(cd_should_socialize(200, 0) == 0, "socialising at night");

    /* ---- contacts ---- */
    CHECK(cc_gift_worth(3) == 260, "tech gift %d", cc_gift_worth(3));
    CHECK(cc_affinity_step(100, 3, 0) == 360, "tech gift moved %d", cc_affinity_step(100, 3, 0));
    CHECK(cc_affinity_step(100, 0, 1) == 20, "one insult cost %d", cc_affinity_step(100, 0, 1));
    CHECK(cc_affinity_step(990, 3, 0) == 1000, "affinity overfilled");
    CHECK(cc_affinity_step(10, 0, 2) == 0, "affinity went negative");
    CHECK(cc_contact_chance_milli(1000, 100) == 550, "chance %d", cc_contact_chance_milli(1000, 100));
    CHECK(cc_contact_chance_milli(0, -100) == 0, "a stranger with a record got a number");
    CHECK(cc_contact_share(1000, 100, 549) == 1, "roll 549 did not share");
    CHECK(cc_contact_share(1000, 100, 550) == 0, "roll 550 shared");
    CHECK(cc_service_can_ask(200, 0) == 1 && cc_service_can_ask(199, 0) == 0, "commerce floor moved");
    CHECK(cc_service_can_ask(600, 3) == 1 && cc_service_can_ask(599, 3) == 0, "mercenary floor moved");
    CHECK(cc_service_price_milli(0, 0) == 1000, "list price %d", cc_service_price_milli(0, 0));
    CHECK(cc_service_price_milli(0, 1000) == 800, "friend price %d", cc_service_price_milli(0, 1000));
    CHECK(cc_service_price_milli(3, 1000) == 1800, "mercenary price %d", cc_service_price_milli(3, 1000));
    CHECK(cc_mercenary_strength(1000) == 200 && cc_mercenary_strength(0) == 100, "strength %d/%d",
          cc_mercenary_strength(1000), cc_mercenary_strength(0));
    CHECK(cc_affinity_decay(0) == 0 && cc_affinity_decay(100) == 99, "decay");
    CHECK(cc_known(4, 2) == 1, "bit 2 missing");
    CHECK(cc_add(0, 2) == 4, "adding contact 2 gave %d", cc_add(0, 2));
    CHECK(cc_add(4, 2) == 4, "adding twice changed the book");

    /* ---- the loop the frontend actually runs: 1700 -> a railway -> the 1800s ---- */
    {
        int era = era_start(), known = 0, points = 5;
        CHECK(tech_can_buy(points, 0, era, known) == 1, "the opening move is not track");
        known = tech_next_mask(known, 0); points -= tech_cost(0);
        CHECK(tech_can_buy(points, 1, era, known) == 1, "the boiler is not affordable after track");
        known = tech_next_mask(known, 1); points -= tech_cost(1);
        CHECK(known == 3, "mask after track+steam is %d", known);
        CHECK(train_unlocked(known) == 1, "no train in the 1700s");
        CHECK(train_capacity(known) == 4, "the first train seats %d", train_capacity(known));
        CHECK(era_advance(era, known) == 1, "the railway did not advance the century");
        CHECK(cd_activity(1, 2) == 2, "the traders stopped running the route");
    }

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("frontend rules: all checks passed\n");
    return 0;
}
