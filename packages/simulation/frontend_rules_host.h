/* frontend_rules_host.h -- the GFD-frontend VS0 rule surface, from PARENA.
 *
 * Four modules, one slice: the 1700s start and its train tree (era_tech_rules.prn), what pays for it
 * (mining_rules.prn), who lives in it (citizen_day_rules.prn), and who will talk to you (TRAPX's
 * citizen_contact_rules.prn). Generated into packages/simulation/<name>_rules.c by
 * `scripts/gen_frontend_rules.sh` (`make regen-frontend-rules`).
 *
 * The SAME four .prn files are compiled to one wasm module by
 * ../PARENA/scripts/build_frontend_vs0_wasm.sh and asserted there by
 * ../PARENA/scripts/frontend_vs0_wasm_test.mjs -- that is the point of the slice, not a side effect: the
 * browser frontend and the C host cannot disagree about when a train exists.
 *
 * All I32; permille 1000 = 1.0 where a rate is meant. No RNG: a caller that wants a die rolls it and
 * passes the result (cc_contact_share takes roll_milli).
 *
 * Parity note, found live 2026-11-04: PARENA's wasm emitter evaluates BOTH arms of an `if`, so a division
 * guarded only by a branch traps in wasm while it is fine in C. Every variable divisor in these four
 * modules is therefore guarded at the division itself (see vein_ratio_milli) -- keep that rule when adding
 * functions here. */
#ifndef SHANKPIT_FRONTEND_RULES_HOST_H
#define SHANKPIT_FRONTEND_RULES_HOST_H

/* ---- 1700s start + the train tree (PARENA/stdlib/shankpit/era_tech_rules.prn) ----
 * era 0..3 (1700/1800/1900/2000); node 0 TRACK 1 STEAM 2 FREIGHT 3 PASSENGER 4 SIGNAL 5 ARMORED;
 * `known` is a bitmask. */
int era_count(void);
int era_start(void);
int era_year(int era);
int era_known(int era);
int tech_count(void);
int tech_earliest_era(int node);
int tech_cost(int node);
int tech_prereq(int node);
int tech_known(int known, int node);
int tech_prereq_met(int known, int node);
int tech_can_buy(int points, int node, int era, int known);
int tech_next_mask(int known, int node);
int tech_bit(int node);
int train_unlocked(int known);
int train_capacity(int known);
int train_speed_milli(int known, int era);
int rails_cost_per_tile(int era);
int era_can_advance(int era, int known);
int era_advance(int era, int known);

/* ---- traditional mining (PARENA/stdlib/shankpit/mining_rules.prn) ----
 * ore 0 COAL 1 IRON 2 COPPER 3 GOLD 4 GEM; pick 0 STONE 1 COPPER 2 IRON 3 STEEL. */
int ore_kinds(void);
int pick_tiers(void);
int pick_strike_ms(int tier);
int pick_yield_milli(int tier);
int ore_base_yield(int ore);
int strike_yield(int ore, int tier);
int vein_deplete(int amount, int taken);
int vein_empty(int amount);
int vein_ratio_milli(int amount, int capacity);
int stamina_cost_milli(int tier);
int can_strike(int stamina_milli, int cooldown_ms, int tier);
int ore_from_variant(int variant);
int xp_per_ore(int ore);
int tool_integrity_step(int integrity, int strikes, int tier);
int smelt_bars(int ore, int amount);
int smelt_fuel_needed(int bars);
int carry_capacity(int tier);
int pick_known(int tier);
int ore_known(int ore);

/* ---- the citizens' own day (PARENA/stdlib/shankpit/citizen_day_rules.prn) ----
 * path mirrors citizen_life.h's CILIFE_* (0 CRAFT 1 TRADE 2 DUEL 3 ENDGAME 4 IDLE);
 * phase 0 NIGHT 1 MORNING 2 AFTERNOON 3 EVENING;
 * activity 0 SLEEP 1 WORK 2 TRADE_ROUTE 3 DRINK 4 PATROL 5 SOCIAL 6 TRAVEL 7 IDLE. */
int cd_day_minutes(void);
int cd_phase(int minute);
int cd_phase_count(void);
int cd_activity_count(void);
int cd_activity(int path, int phase);
int cd_activity_in_public(int activity);
int cd_activity_zone_kind(int activity);
int cd_hunger_step(int hunger, int phase);
int cd_energy_step(int energy, int phase, int activity);
int cd_mood_step(int mood, int hunger, int energy);
int cd_wage(int path, int activity);
int cd_should_seek_home(int phase, int energy);
int cd_should_socialize(int mood, int phase);
int cd_path_known(int path);
int cd_phase_known(int phase);
int cd_activity_known(int activity);

/* ---- citizen contacts, TRAPX (PARENA/stdlib/trapx/citizen_contact_rules.prn) ----
 * gift 0 NONE 1 SMALL 2 GOOD 3 TECH; service 0 COMMERCE 1 BORROW_TECH 2 REPAIR 3 MERCENARY. */
int cc_affinity_max(void);
int cc_gift_count(void);
int cc_gift_worth(int gift);
int cc_affinity_step(int affinity, int gift, int insult);
int cc_contact_chance_milli(int affinity, int trust);
int cc_contact_share(int affinity, int trust, int roll_milli);
int cc_service_count(void);
int cc_service_min_affinity(int service);
int cc_service_can_ask(int affinity, int service);
int cc_service_price_milli(int service, int affinity);
int cc_mercenary_strength(int affinity);
int cc_affinity_decay(int affinity);
int cc_known(int book, int contact_id);
int cc_add(int book, int contact_id);
int cc_gift_known(int gift);
int cc_service_known(int service);

#endif /* SHANKPIT_FRONTEND_RULES_HOST_H */
