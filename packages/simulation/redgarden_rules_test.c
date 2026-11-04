/* redgarden_rules_test.c -- hand-derived checks for the REDGARDEN rule mods SHANKPIT now hosts.
 *
 * The point of this file is not "the functions exist". It is that the numbers SHANKPIT will run are the
 * numbers REDGARDEN already ships. Every expectation below is either
 *   (a) a constant quoted from REDGARDEN/packages/simulation/arena_game.h (the macro is named at the check),
 *   (b) one step of arithmetic over such a constant (8000 ms of an 8000 ms channel is a full capture), or
 *   (c) a product of one, computed by hand in the comment.
 *
 * If REDGARDEN retunes a hero or a capture timer and the port is not updated, this goes red -- which is the
 * whole reason the port is a rule mod with a parity test rather than a copy of the C.
 */
#include <stdio.h>
#include "redgarden_rules_host.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s\n", msg); } \
} while (0)

#define CHECK_INT(got, want, msg) do { \
    int g_ = (got), w_ = (want); \
    if (g_ == w_) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s -- got %d, want %d\n", msg, g_, w_); } \
} while (0)

int main(void) {
    /* ---- teams and mana: ARENA_TEAM_SIZE 10, ARENA_MAX_HEROES 20, MP 100/20/20/45 --------------- */
    CHECK_INT(rg_team_size(), 10, "team size is REDGARDEN's ARENA_TEAM_SIZE");
    CHECK_INT(rg_max_heroes(), 20, "max heroes is team * 2 (ARENA_MAX_HEROES)");
    CHECK_INT(rg_mp_max(), 100, "mana pool is ARENA_MP_MAX");
    CHECK_INT(rg_mp_cost(0), 20, "Q costs ARENA_MP_COST_Q");
    CHECK_INT(rg_mp_cost(1), 20, "W costs ARENA_MP_COST_W");
    CHECK_INT(rg_mp_cost(2), 45, "R costs ARENA_MP_COST_R");
    CHECK_INT(rg_mp_cost(3), 0, "an unknown slot costs nothing (total function)");
    CHECK_INT(rg_mp_after_cast(50, 2), 5, "R from 50 mana leaves 5");
    CHECK_INT(rg_mp_after_cast(10, 2), 0, "a cast cannot take mana below zero");
    CHECK_INT(rg_mp_regen(0, 1000, 15), 15, "one second of fountain mana regen at 15/sec");
    CHECK_INT(rg_mp_regen(95, 1000, 15), 100, "regen is capped at ARENA_MP_MAX");
    CHECK_INT(rg_mp_regen(-5, 1000, 15), 15, "negative mana is clamped before regen");

    /* ---- auto-attack: range 1.6, damage 8, cooldown 700, windup = cooldown/4 -------------------- */
    CHECK_INT(rg_hero_speed_milli(), 4000, "ARENA_HERO_SPEED 4.0 as permille");
    CHECK_INT(rg_attack_range_milli(), 1600, "ARENA_ATTACK_RANGE 1.6 as permille");
    CHECK_INT(rg_attack_damage(), 8, "ARENA_ATTACK_DAMAGE");
    CHECK_INT(rg_attack_cooldown_ms(), 700, "ARENA_ATTACK_COOLDOWN_MS");
    CHECK_INT(rg_attack_windup_ms(), 175, "windup is 25% of 700 (ARENA_ATTACK_WINDUP_MS)");

    /* ---- nodes and towers: 5 nodes, capture radius 5.0, channel 8000, tower 420 hp ------------- */
    CHECK_INT(rg_node_count(), 5, "ARENA_NODE_COUNT 5");
    CHECK_INT(rg_node_capture_radius_milli(), 5000, "ARENA_NODE_CAPTURE_RADIUS 5.0 as permille");
    CHECK_INT(rg_node_capture_channel_ms(), 8000, "ARENA_NODE_CAPTURE_CHANNEL_MS");
    CHECK_INT(rg_tower_hp(), 420, "ARENA_TOWER_MAX_HP");
    CHECK_INT(rg_tower_aggro_radius_milli(), 6000, "ARENA_TOWER_AGGRO_RADIUS 6.0 as permille");
    CHECK_INT(rg_tower_projectile_speed_milli(), 14000, "ARENA_TOWER_PROJECTILE_SPEED 14.0");

    CHECK_INT(rg_capture_channel_ms(0), 8000, "no bonus: the full 8s channel");
    CHECK_INT(rg_capture_channel_ms(3000), 5000, "a 5s neutral-creep bonus leaves 3s? no -- 8000-3000");
    CHECK_INT(rg_capture_channel_ms(7000), 2000, "a huge bonus stops at the 2s floor");
    CHECK_INT(rg_capture_progress_milli(4000, 0), 500, "half of an 8s channel is 500 permille");
    CHECK_INT(rg_capture_progress_milli(8000, 0), 1000, "a finished channel is 1000 permille");
    CHECK_INT(rg_capture_progress_milli(9000, 0), 1000, "progress is capped at 1000, not 1025");
    CHECK_INT(rg_capture_progress_milli(4000, 3000), 800, "4000 of the 5000 ms bonused channel");

    /* ---- fountains: 15 hp/s and 15 mp/s, both capped at the max --------------------------------- */
    CHECK_INT(rg_fountain_hp(90, 100, 1000), 100, "a second in the fountain tops a 90 hp hero off");
    CHECK_INT(rg_fountain_hp(0, 100, 500), 7, "half a second heals 15/2 = 7 (integer)");
    CHECK_INT(rg_fountain_mana_per_sec(), 15, "ARENA_FOUNTAIN_MANA_PER_SEC");

    /* ---- creeps: neutral 80hp/6dmg/30s, team 26hp/4dmg/12s -------------------------------------- */
    CHECK_INT(rg_creep_neutral_hp(), 80, "ARENA_CREEP_NEUTRAL_HP");
    CHECK_INT(rg_creep_team_hp(), 26, "ARENA_CREEP_TEAM_HP (the S170-161 tone-down)");
    CHECK_INT(rg_creep_neutral_damage(), 6, "ARENA_CREEP_NEUTRAL_DAMAGE");
    CHECK_INT(rg_creep_team_damage(), 4, "ARENA_CREEP_TEAM_DAMAGE");
    CHECK_INT(rg_creep_aggro_radius_milli(), 4000, "ARENA_CREEP_AGGRO_RADIUS 4.0");
    CHECK_INT(rg_creep_respawn_ms(0), 30000, "the neutral prize respawns in 30s");
    CHECK_INT(rg_creep_respawn_ms(1), 12000, "a team guardian respawns in 12s");
    CHECK_INT(rg_creep_kill_heal(1, 1), 20, "killing your own team's guardian heals 20");
    CHECK_INT(rg_creep_kill_heal(0, 1), 0, "the neutral prize heals nothing");
    CHECK_INT(rg_creep_kill_capture_bonus_ms(0, 0), 5000, "neutral bonus is ARENA_CREEP_NEUTRAL_KILL_CAPTURE_BONUS_MS");
    CHECK_INT(rg_creep_kill_capture_bonus_ms(1, 1), 0, "your own guardian gives no capture bonus");
    CHECK_INT(rg_creep_kill_capture_bonus_ms(1, 0), 1500, "farming the enemy guardian gives 1.5s");

    /* ---- death and the clock: waves of 30s, match cap 12 min ------------------------------------ */
    CHECK_INT(rg_respawn_wave_ms(), 30000, "ARENA_RESPAWN_WAVE_MS");
    CHECK_INT(rg_respawn_wait_ms(0, 0), 30000, "dying at t=0 waits for the first wave");
    CHECK_INT(rg_respawn_wait_ms(0, 10000), 20000, "ten seconds after dying, twenty to wait");
    CHECK_INT(rg_respawn_wait_ms(31000, 32000), 28000, "dying inside wave 1 waits for the wave at 60000");
    CHECK_INT(rg_match_time_left_ms(0), 720000, "ARENA_MATCH_MAX_DURATION_MS is 12 minutes");
    CHECK_INT(rg_match_time_left_ms(720000), 0, "the clock stops at zero, not negative");

    /* ---- ability kits: REDGARDEN's five, quoted slot by slot ------------------------------------ */
    CHECK_INT(rg_hero_count(), 6, "six heroes encoded (five kits plus the ranged one)");
    CHECK_INT(rg_hero_attack_range_milli(4), 8880, "Gary's 6.0 * 2.0 * 0.74 = 8.88 range");
    CHECK_INT(rg_hero_attack_range_milli(0), 1600, "everyone else is melee 1.6");
    CHECK_INT(rg_hero_attack_damage(4), 7, "ARENA_GARY_ATTACK_DAMAGE");
    CHECK_INT(rg_hero_attack_cooldown_ms(4), 900, "ARENA_GARY_ATTACK_COOLDOWN_MS");
    CHECK_INT(rg_hero_attack_windup_ms(4), 225, "Gary's windup is 900/4");
    CHECK_INT(rg_ability_cooldown_ms(0, 0), 4000, "Unicorn Q is ARENA_UNICORN_Q_COOLDOWN_MS");
    CHECK_INT(rg_ability_cooldown_ms(0, 1), 0, "Unicorn W is a toggle, so it has no cooldown");
    CHECK_INT(rg_ability_cooldown_ms(1, 2), 18000, "Duck R is ARENA_DUCK_R_COOLDOWN_MS");
    CHECK_INT(rg_ability_cooldown_ms(5, 2), 30000, "Doc R is ARENA_DOC_WHEEL_R_COOLDOWN_MS");
    CHECK_INT(rg_ability_cooldown_ms(9, 0), 0, "an unknown hero has no cooldowns, not a garbage one");
    CHECK_INT(rg_ability_damage(0, 0), 12, "Unicorn Q damage 12");
    CHECK_INT(rg_ability_damage(1, 2), 20, "Duck R damage 20");
    CHECK_INT(rg_ability_damage(2, 0), 9, "Ghost Q damage 9");
    CHECK_INT(rg_ability_damage(3, 0), 0, "Frog's kit does no damage, and that is the kit");
    CHECK_INT(rg_ability_dps_pooled(2, 2), 24, "Ghost R: 6 dps over 4000 ms is 24 total");
    CHECK_INT(rg_ability_range_milli(1, 2), 9000, "Duck R reaches ARENA_DUCK_R_RANGE 9.0");
    CHECK_INT(rg_ability_range_milli(2, 0), 7000, "Ghost Q reaches ARENA_GHOST_Q_RANGE 7.0");
    CHECK_INT(rg_ability_range_milli(5, 2), 6000, "Doc R's zone is ARENA_DOC_WHEEL_R_RADIUS 6.0");
    CHECK_INT(rg_ability_range_milli(0, 1), 0, "a self toggle has no ground range");
    CHECK_INT(rg_ability_duration_ms(3, 2), 5000, "Frog R vanish is ARENA_FROG_R_VANISH_MS");
    CHECK_INT(rg_ability_duration_ms(2, 0), 1500, "Ghost Q silences for ARENA_GHOST_Q_SILENCE_MS");
    CHECK(rg_ability_is_toggle(0, 1) == 1, "Unicorn W is a toggle");
    CHECK(rg_ability_is_toggle(1, 1) == 0, "Duck W is an instant, not a toggle");
    CHECK_INT(rg_ability_toggle_regen_per_sec(0, 1), 6, "Spaghetti Vent regens 6/sec while on");
    CHECK_INT(rg_ability_silence_ms(2, 0), 1500, "silence is a reading of Ghost Q, not a HUD constant");
    CHECK_INT(rg_ability_slow_chance_percent(1, 1), 50, "Smoke Bomb slows on a 50% roll");
    CHECK_INT(rg_ability_slow_ms(1, 1), 2000, "the slow lasts 2s");
    CHECK_INT(rg_ability_slow_percent(1, 1), 30, "the slow is 30%");

    /* Doc's heal scales from 14 at full health to 28 at death's door (ARENA_DOC_WHEEL_Q_HEAL_*). */
    CHECK_INT(rgh_ability_heal_for_hp(5, 0, 100, 100), 14, "heal at full target health is the base 14");
    CHECK_INT(rgh_ability_heal_for_hp(5, 0, 0, 100), 28, "heal at zero target health is 28");
    CHECK_INT(rgh_ability_heal_for_hp(5, 0, 50, 100), 21, "half health is the midpoint, 21");
    CHECK_INT(rgh_ability_heal_for_hp(5, 0, 0, 0), 14, "a zero max_hp cannot divide: the base is returned");
    CHECK_INT(rg_ability_heal_base(1, 0), 0, "a non-healer has no heal");

    /* ---- cast legality, CDR and toggle drain ---------------------------------------------------- */
    CHECK(rg_can_cast(50, 0, 0, 2, 45) == 1, "an affordable, ready, unsilenced R is castable");
    CHECK(rg_can_cast(40, 0, 0, 2, 45) == 0, "40 mana cannot pay a 45 R");
    CHECK(rg_can_cast(50, 0, 100, 0, 20) == 0, "silence blocks a cast even with mana");
    CHECK(rg_can_cast(50, 250, 0, 0, 20) == 0, "a cooldown that is not spent blocks a cast");
    CHECK_INT(rg_cooldown_after_cast(4000, 25), 3000, "25% CDR takes 4000 to 3000");
    CHECK_INT(rg_cooldown_after_cast(4000, 100), 0, "100% CDR is instant, never negative");
    CHECK_INT(rg_cooldown_after_cast(4000, 150), 0, "CDR above 100 is clamped");
    CHECK_INT(rg_toggle_sustain_ms(100, 5), 20000, "a full bar sustains a 5/sec drain for 20s");
    CHECK_INT(rg_toggle_sustain_ms(100, 0), 0, "no drain means 'not a toggle', not infinity");

    /* ---- the AI genome: four traits, bounded, and the decision order that matters --------------- */
    CHECK_INT(rg_gene_count(), 4, "four genes: aggression, caution, greed, loyalty");
    CHECK_INT(rg_gene_caution(), 1, "caution is gene 1");
    CHECK(rg_genome_valid(0, 0, 0, 0) == 1, "an all-zero genome is legal");
    CHECK(rg_genome_valid(100, 100, 100, 100) == 1, "an all-max genome is legal");
    CHECK(rg_genome_valid(101, 0, 0, 0) == 0, "aggression 101 is not a genome");
    CHECK_INT(rg_genome_clamp(300), 100, "a corrupt gene is clamped, not rejected");
    CHECK_INT(rg_genome_clamp(-3), 0, "a negative gene is clamped to zero");

    /* Caution 0 breaks off at 150 permille, caution 100 at 450; the roll adds up to 39 permille. */
    CHECK_INT(rg_retreat_threshold_milli(0, 0), 150, "a reckless actor fights down to 15% health");
    CHECK_INT(rg_retreat_threshold_milli(100, 0), 450, "a careful one breaks off at 45%");
    CHECK_INT(rg_retreat_threshold_milli(100, 999), 489, "the roll adds its 39 permille of doubt");
    CHECK(rg_should_retreat(200, 50, 0) == 1, "20% health with caution 50 (300 threshold) retreats");
    CHECK(rg_should_retreat(200, 0, 0) == 0, "the same 20% health with caution 0 keeps fighting");

    CHECK_INT(rg_desired_range_milli(5000, 0), 5000, "zero aggression holds its weapon's range");
    CHECK_INT(rg_desired_range_milli(5000, 100), 4000, "max aggression closes 1000 permille");
    CHECK_INT(rg_desired_range_milli(1000, 100), 800, "closing never goes below 800 permille");
    CHECK(rg_should_engage(4000, 5000, 0, 0) == 1, "in range on a low roll engages");
    CHECK(rg_should_engage(6000, 5000, 0, 0) == 0, "out of range does not engage");

    CHECK_INT(rg_target_score(0, 0, 0, 0), 100, "a hero is worth 100 before traits");
    CHECK_INT(rg_target_score(3, 0, 100, 0), 440, "max greed makes a node worth 440");
    CHECK_INT(rg_target_score(2, 0, 0, 100), 420, "max loyalty makes a tower worth 420");
    CHECK_INT(rg_target_score(9, 100, 100, 100), 0, "an unknown kind scores 0, not a wild number");

    /* rg_decide: dead holds, hurt retreats, far approaches, starved attacks, rich casts. */
    CHECK_INT(rg_decide(0, 1000, 1000, 1600, 50, 50, 50, 50, 500), 0, "a dead actor holds");
    CHECK_INT(rg_decide(100, 1000, 1000, 1600, 50, 100, 50, 50, 0), 4, "10% health with max caution retreats");
    CHECK_INT(rg_decide(900, 1000, 9000, 1600, 50, 50, 50, 50, 500), 1, "out of range approaches");
    CHECK_INT(rg_decide(900, 0, 1000, 1600, 0, 0, 0, 0, 800), 2, "no mana, late roll: attack");
    CHECK_INT(rg_decide(900, 1000, 1000, 1600, 0, 0, 0, 0, 0), 3, "full mana on an early roll casts");
    CHECK_INT(rg_decide(900, 1000, 1000, 1600, 0, 0, 100, 0, 100), 5, "greedy on a low roll goes farming");
    CHECK_INT(rg_jitter_milli(100, 500), 50, "max trait at a mid roll wobbles by 50");
    CHECK_INT(rg_jitter_milli(-5, 999), 0, "a negative trait wobbles by nothing");

    /* ---- session rules: slots, phases, bandwidth, drops ----------------------------------------- */
    CHECK(rg_lobby_valid(2) == 1, "the live-verified 1v1 lobby is valid");
    CHECK(rg_lobby_valid(3) == 0, "an odd lobby is refused, not rounded");
    CHECK(rg_lobby_valid(20) == 1, "10v10 is valid");
    CHECK(rg_lobby_valid(22) == 0, "more slots than ARENA_MAX_HEROES is invalid");
    CHECK_INT(rg_team_of_slot(0), 0, "slot 0 is team 0");
    CHECK_INT(rg_team_of_slot(9), 0, "slot 9 is still team 0");
    CHECK_INT(rg_team_of_slot(10), 1, "slot 10 opens team 1");
    CHECK_INT(rg_team_of_slot(19), 1, "slot 19 is team 1");

    CHECK_INT(rg_snapshot_hz(), 20, "16 ms frames, every 3rd frame: 1000/48 = 20 Hz");
    CHECK_INT(rg_snapshot_bytes(20, 5, 5, 4), 590, "a full match snapshot is 590 bytes by hand");
    CHECK_INT(rg_snapshot_bytes_per_sec(590), 11800, "which is 11.8 KB/s per client at 20 Hz");
    CHECK(rg_fits_budget(11800, 2048) == 0, "11800 B/s does NOT fit a 2 KB/s session ceiling");
    CHECK(rg_fits_budget(590, 2048) == 1, "one snapshot per second does");

    CHECK_INT(rg_phase_from_match(0, 20, 20, 0, 720000), 1, "a full, valid lobby goes live");
    CHECK_INT(rg_phase_from_match(0, 19, 20, 0, 720000), 0, "a nearly-full lobby waits");
    CHECK_INT(rg_phase_from_match(1, 20, 20, 1, 100), 2, "a winner ends a live match");
    CHECK_INT(rg_phase_from_match(1, 20, 20, 0, 0), 2, "so does the clock running out");
    CHECK(rg_slot_reclaimable(4999) == 0, "a slot is held inside the 5s grace");
    CHECK(rg_slot_reclaimable(5000) == 1, "and reclaimed after it");
    CHECK(rg_ticket_expired(0, 30000) == 0, "a 30s-old ticket is still valid");
    CHECK(rg_ticket_expired(0, 30001) == 1, "31s is expired");
    CHECK(rg_spectator_allowed(1) == 1, "observers may watch a live match");
    CHECK(rg_spectator_allowed(0) == 0, "not a lobby");

    printf("redgarden rules: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 1 - 1 : 1;
}
