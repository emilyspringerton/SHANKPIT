#ifndef DW_CITIZEN_HOST_H
#define DW_CITIZEN_HOST_H

/* dw_citizen_host.h -- prototypes for the PARENA-generated DEADWEIGHT citizen policy
 * (PARENA/stdlib/deadweight/citizen_policy.prn -> packages/simulation/dw_citizen_rules.c).
 *
 * The policy is import-free by design: it takes a card's kind/power/cost and its legality as
 * arguments, which the host resolves from the same card_rules engine it plays with. So these
 * signatures carry facts, not card ids. */

int cp_winrate_permille(int wins, int games);
int cp_kind_identity(int path);
int cp_kind_answer(int meta_kind);
int cp_deck_kind(int path, int winrate, int meta_kind, int meta_share);
int cp_card_score(int kind, int power, int cost, int legal, int opp_kind, int aggression);
int cp_pick(int s0, int s1, int s2, int s3, int h0, int h1, int h2, int h3);
int cp_deck_card(int slot, int path, int winrate, int meta_kind, int meta_share);

#endif
