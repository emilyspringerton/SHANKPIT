/* shadow_war_host.c -- see shadow_war_host.h. Host plumbing only: unit power, command
 * multipliers, per-tick damage and the deploy gate are all called from the PARENA-generated
 * shadow_war_rules.c (from PARENA/stdlib/big_o/shadow_war.prn). Elo and the bot policy are
 * plain host math by deliberate choice, named in both .prn and .h header comments. */
#include "shadow_war_host.h"
#include "../reflux/reflux_mod_host.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

int shadow_war_base_ok(int base);
int shadow_war_trait_ok(int trait);
int shadow_war_command_ok(int cmd);
int shadow_war_unit_power(int base, int trait);
int shadow_war_attack_mult_tenths(int cmd);
int shadow_war_defense_mult_tenths(int cmd);
int shadow_war_tick_damage(int attack, int defense);
int shadow_war_side_eliminated(int remaining_power);
int shadow_war_can_deploy(int clone_count);
int shadow_war_max_ticks(void);

static int sw_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int shadow_war_army_power(const ShadowWarArmy *army) {
    int n = sw_clampi(army->unit_count, 0, SHADOW_WAR_MAX_UNITS);
    int total = 0;
    for (int i = 0; i < n; i++) {
        int base = shadow_war_base_ok(army->base[i]) ? army->base[i] : 0;
        int trait = shadow_war_trait_ok(army->trait[i]) ? army->trait[i] : 0;
        total += shadow_war_unit_power(base, trait);
    }
    return total;
}

void shadow_war_resolve(const ShadowWarArmy *a, const ShadowWarArmy *b, ShadowWarBattle *out) {
    memset(out, 0, sizeof(*out));
    int cmd_a = shadow_war_command_ok(a->command) ? a->command : SHADOW_WAR_CMD_HOLD;
    int cmd_b = shadow_war_command_ok(b->command) ? b->command : SHADOW_WAR_CMD_HOLD;
    int a_power = shadow_war_army_power(a);
    int b_power = shadow_war_army_power(b);
    out->a_power_start = a_power;
    out->b_power_start = b_power;

    int a_atk = (a_power * shadow_war_attack_mult_tenths(cmd_a)) / 10;
    int a_def = (a_power * shadow_war_defense_mult_tenths(cmd_a)) / 10;
    int b_atk = (b_power * shadow_war_attack_mult_tenths(cmd_b)) / 10;
    int b_def = (b_power * shadow_war_defense_mult_tenths(cmd_b)) / 10;

    int a_remaining = a_power, b_remaining = b_power;
    int max_ticks = shadow_war_max_ticks();
    int t = 0;
    for (; t < max_ticks; t++) {
        if (shadow_war_side_eliminated(a_remaining) || shadow_war_side_eliminated(b_remaining)) break;
        /* simultaneous: both directions computed from the SAME pre-tick atk/def values above,
         * so a mirror battle (identical armies+command on both sides) drains identically every
         * tick -- order-independent, a real, exact draw, not an artifact of who "goes first". */
        int dmg_to_b = shadow_war_tick_damage(a_atk, b_def);
        int dmg_to_a = shadow_war_tick_damage(b_atk, a_def);
        a_remaining -= dmg_to_a;
        b_remaining -= dmg_to_b;
    }
    out->ticks_run = t;
    out->a_power_end = a_remaining > 0 ? a_remaining : 0;
    out->b_power_end = b_remaining > 0 ? b_remaining : 0;

    int a_dead = shadow_war_side_eliminated(a_remaining);
    int b_dead = shadow_war_side_eliminated(b_remaining);
    if (a_dead && b_dead) out->result = SHADOW_WAR_RESULT_DRAW;
    else if (b_dead) out->result = SHADOW_WAR_RESULT_WIN;
    else if (a_dead) out->result = SHADOW_WAR_RESULT_LOSS;
    else out->result = (a_remaining == b_remaining) ? SHADOW_WAR_RESULT_DRAW
                       : (a_remaining > b_remaining ? SHADOW_WAR_RESULT_WIN : SHADOW_WAR_RESULT_LOSS);
}

int shadow_war_bot_choose_command(int bot_power, int enemy_power) {
    if (bot_power > enemy_power + 2) return SHADOW_WAR_CMD_ADVANCE;
    if (bot_power < enemy_power - 2) return SHADOW_WAR_CMD_HOLD;
    return SHADOW_WAR_CMD_SCATTER;
}

void shadow_war_bot_army(int unit_count, ShadowWarArmy *out) {
    memset(out, 0, sizeof(*out));
    int n = sw_clampi(unit_count, 1, SHADOW_WAR_MAX_UNITS);
    out->unit_count = n;
    for (int i = 0; i < n; i++) {
        out->base[i] = i % 3;
        out->trait[i] = (i / 3) % 3;
    }
}

int shadow_war_elo_update(int my_elo, int opp_elo, int score_x1000, int k) {
    double expected = 1.0 / (1.0 + pow(10.0, (double)(opp_elo - my_elo) / 400.0));
    double score = (double)score_x1000 / 1000.0;
    double updated = (double)my_elo + (double)k * (score - expected);
    int rounded = (int)(updated >= 0.0 ? updated + 0.5 : updated - 0.5);
    return rounded < 100 ? 100 : rounded;
}

void shadow_war_deploy(Phone *p, int *inout_elo, int player_id, ShadowWarDeployResult *out) {
    memset(out, 0, sizeof(*out));
    out->attempted = 1;

    if (!shadow_war_can_deploy(p->clone_count)) {
        /* refused before touching *inout_elo at all -- "0 = never fought" must mean never
         * actually BATTLED, not merely "the station was poked once with nothing to send". */
        snprintf(out->msg, sizeof out->msg, "WAR TERMINAL: NO CLONES TO DEPLOY");
        return;
    }
    if (*inout_elo == 0) *inout_elo = SHADOW_WAR_ELO_DEFAULT;
    out->old_elo = *inout_elo;
    out->new_elo = *inout_elo;

    ShadowWarArmy mine, bot;
    memset(&mine, 0, sizeof(mine));
    mine.unit_count = sw_clampi(p->clone_count, 0, SHADOW_WAR_MAX_UNITS);
    for (int i = 0; i < mine.unit_count; i++) { mine.base[i] = p->clones[i]; mine.trait[i] = p->clone_traits[i]; }

    shadow_war_bot_army(mine.unit_count, &bot);
    int mine_power = shadow_war_army_power(&mine);
    int bot_power = shadow_war_army_power(&bot);
    mine.command = shadow_war_bot_choose_command(mine_power, bot_power);  /* no live player order input yet (v0) -- see shadow_war_host.h */
    bot.command = shadow_war_bot_choose_command(bot_power, mine_power);

    ShadowWarBattle battle;
    shadow_war_resolve(&mine, &bot, &battle);

    p->clone_count = 0;   /* every clone sent to war is expended, win or lose -- see shadow_war_host.h */

    int score_x1000 = battle.result == SHADOW_WAR_RESULT_WIN ? 1000 : (battle.result == SHADOW_WAR_RESULT_DRAW ? 500 : 0);
    *inout_elo = shadow_war_elo_update(out->old_elo, SHADOW_WAR_ELO_DEFAULT, score_x1000, SHADOW_WAR_ELO_K);
    out->new_elo = *inout_elo;
    out->ok = 1;
    out->result = battle.result;

    reflux_dispatch(REFLUX_ACTION_SHADOW_WAR_RESOLVED, player_id, battle.result, out->new_elo);

    const char *verb = battle.result == SHADOW_WAR_RESULT_WIN ? "WON" : (battle.result == SHADOW_WAR_RESULT_DRAW ? "DRAW" : "LOST");
    snprintf(out->msg, sizeof out->msg, "WAR %s  %d v %d  ELO %d->%d", verb, mine_power, bot_power, out->old_elo, out->new_elo);
}
