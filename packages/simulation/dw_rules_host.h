#ifndef DW_RULES_HOST_H
#define DW_RULES_HOST_H

/* dw_rules_host.h -- prototypes for the PARENA-generated DEADWEIGHT rules
 * (PARENA/stdlib/deadweight/card_rules.prn -> packages/simulation/dw_rules.c).
 *
 * Same no-shared-header-between-generated-units convention as zombies_pheromone_rules.c and
 * citizen_avatar_rules.c. Nothing in this file is hand-maintained logic. */

int num_cards(void);
int card_kind(int id);
int card_cost(int id);
int card_power(int id);
int card_credit(int id);
int card_keyword(int id);
int card_tier(int id);
int kind_beats(int a, int b);
int is_legal_play(int id, int energy, int vault);
int damage_dealt(int a, int b);
int energy_after_play(int e, int played);
int round_start_energy(int e);
int round_start_vault(int v);
int round_winner(int h0, int h1, int v0, int v1);
int match_decided(int h0, int h1);
int bankrupt(int v);
int card_substitute(int id, int roll);
int card_fx_a(int id);
int card_fx_b(int id);
int fx_amount(int x, int dealt, int taken, int mk, int ok, int oc, int mh, int oh,
              int el, int oe, int mv, int ov, int rnd, int roll);
int fx_ch(int x);
int fx_phase(int ch);
int start_hull(void);
int start_energy(void);
int start_vault(void);
int max_rounds(void);
int hand_size(void);

#endif
