/* trapx_city_rules_host.h -- TRAPX's city-scale engine, from PARENA.
 *
 * The rules live in PARENA/stdlib/trapx/city_engine_rules.prn, generated into trapx_city_rules.c
 * (do not edit it; `make regen-trapx-city`). trapx_rules_host.h is the OTHER half -- the street's own
 * alertness/trust/heat/Field Office/K9 rules. This header is the city that keeps running whether or not
 * anyone is playing: population, money, corruption, casinos, scrutiny, federal oversight, neighborhoods,
 * the Spire, and the automata overlay.
 *
 * TICK ORDER IS THE CONTRACT, and it is the pasted spec's own order:
 *   1 population  2 citizens  3 items+intel  4 factions  5 infrastructure  6 economy
 *   7 casinos     8 local scrutiny  9 federal oversight  10 neighborhoods  11 Spire events
 *   12 automata overlay, every ce_automata_period_ticks(), NOT every tick.
 *
 * All values are I32; 1000 = 1.0 permille. Nothing here rolls a die: where the spec rolls (intel
 * handoff), the host pre-rolls a 0..999 value and passes it in. The district conversion threshold is a
 * PARAMETER -- pass empire_district_threshold_milli() so the sim and the EMPIRE phone app agree.
 *
 * Deliberately NOT in LOBBY_SRC/SERVER_SRC yet: nothing consumes these rules in a running host (see
 * docs/EMPIRE_NORTHSTAR.md §6), and adding a file to the hand-copied source lists early is how those
 * lists drift. The test is the only consumer today. */
#ifndef SHANKPIT_TRAPX_CITY_RULES_HOST_H
#define SHANKPIT_TRAPX_CITY_RULES_HOST_H

/* PARENA-generated (trapx_city_rules.c) */
int ce_one(void);
int ce_clamp(int v, int lo, int hi);
int ce_tick_ms(void);
int ce_automata_period_ticks(void);
int ce_pop_max(void);

/* 1. population */
int ce_pop_growth_milli(int stability, int corruption, int crime, int train_boost_milli);
int ce_pop_step(int pop, int growth_milli, int max_pop);

/* 6a. economy */
int ce_money_flow(int pop, int stability, int corruption);
int ce_faction_siphon(int money_flow, int influence_share);

/* 3. items and intel */
int ce_intel_chance_milli(int instability);
int ce_intel_transfer(int instability, int roll_milli);

/* 4. faction spread */
int ce_faction_spread_force(int influence);
int ce_faction_convert(int pressure, int threshold);
int ce_pressure_decay(int pressure);

/* 5. trains and ships */
int ce_train_boost(int boost_milli, int disruption_milli);
int ce_ship_falloff(int distance_steps);
int ce_ship_resource_shock(int shock_milli, int distance_steps);

/* 5b. power grid */
int ce_infra_stability_step(int stability, int load_milli, int capacity_milli);

/* 7. casinos */
int ce_casino_inflow(int neighbor_money, int throughput_milli);
int ce_casino_processed(int inflow, int multiplier_milli);
int ce_casino_banker_share(int processed, int banker_boost_milli);
int ce_casino_oversight_pressure(int processed, int oversight_multiplier_milli);
int ce_casino_corruption(int processed, int corruption_multiplier_milli);
int ce_casino_scrutiny(int scrutiny, int processed, int decay_milli);

/* 8. local scrutiny */
int ce_scrutiny(int base, int corruption, int casino_influence, int enforcement_bias);
int ce_crime_step(int crime, int scrutiny);

/* 9. federal oversight */
int ce_federal_pressure_step(int pressure, int casino_money, int scandals, int leaks);
int ce_federal_investigation(int pressure);
int ce_federal_seizure(int faction_money);
int ce_federal_arrest(int faction_influence);
int ce_investigation_shock(void);

/* 10. neighborhoods: 0 DECAY, 1 STABLE, 2 BOOM */
int ce_neighborhood_stage(int avg_stability, int avg_wealth, int avg_corruption);
int ce_neighborhood_grows(int stage);
int ce_neighborhood_decays(int stage);

/* 11. Spire: 0 NONE, 1 MARKET_CRASH, 2 REROUTE_TRAINS, 3 LEAK_INTEL */
int ce_spire_event(int federal_pressure, int casino_power, int avg_stability, int intel_held);

/* 12. automata: 0 KEEP, 1 CONVERT, 2 CORRUPT, 3 NEUTRALIZE */
int ce_automata_step(int aligned_neighbors, int corrupt_neighbors, int population);

#endif /* SHANKPIT_TRAPX_CITY_RULES_HOST_H */
