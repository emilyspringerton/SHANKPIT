#ifndef DW_MATCH_H
#define DW_MATCH_H

/* dw_match.h -- the DEADWEIGHT SEQUENCER: the game itself, running inside SHANKPIT.
 *
 * PARENA's stdlib/deadweight/card_rules.prn owns every NUMBER (105 cards, costs, powers, credits,
 * the kind triangle, damage, energy, vault, the packed fx words and their condition/amount engine).
 * It deliberately owns no SEQUENCING -- DEADWEIGHT's own core/match.c does that, and that file is not
 * in this tree. This is that sequencer, and it is the piece that turns the rules into a game two
 * people (or two citizens, or a citizen and a person) can sit down and play.
 *
 * A MATCH is: 20 hull, 2 energy, 3 credits, a 16-card deck, a 4-card hand each round, one card each
 * per round, best hull wins, bankruptcy (credits below -6) loses instantly, 100 rounds is a draw on
 * hull then credits. Every one of those numbers is read from the engine, not restated here, so the
 * sequencer cannot drift from the rules.
 *
 * WHAT IS SEQUENCED (the real fx channels, both phases): 01 bonus, 02 pierce, 03 bypass, 04 immune,
 * 05 lifeline, 06 reflect, 07 cancel, 09 halve, 20 heal, 21 selfdamage, 22 true, 23/24 energy,
 * 25/26/27 next-round energy (both seats), 28 armor, 29/30/31 vault and the opponent's vault,
 * 32 burn, 33 regen, 39 equalize hulls, 40 chronobreak, 41 convert energy to armor, 42 cleanse,
 * 43 debt-free.
 *
 * The hand-lifecycle channels the catalog carries are sequenced too: 08 COPY (play the opponent's
 * declared card -- note its only carrier, Hostile Takeover (36), ALSO cancels, so in practice a copy
 * copies a cancelled declaration; a COPY-only card would copy a live one), 34 DISABLE (opponent loses
 * N hand slots next round), 35 BLIND (my meters are
 * hidden -- state the UI reads, no mechanical effect in a perfect-information headless sim),
 * 36 SWAP hands, 37 REDRAW my hand, 38 DISCARD N of the opponent's cards (no catalog card carries 38
 * today; the sequencer handles it anyway).
 *
 * `unsupported_fx` is therefore no longer a description of known gaps -- it is a GUARD: a channel
 * this sequencer does not recognise sets a bit instead of being silently ignored, so a future card
 * cannot quietly play a different game.
 *
 * Determinism: one 32-bit LCG inside the match, advanced only for the round roll and the deck draw,
 * and the hands are dealt from the deck in order -- so the same seed and the same two deck lists
 * replay byte-for-byte, which is what makes a match testable and a citizen's record trustworthy. */

#include <stdint.h>

#define DW_HAND 4
#define DW_DECK 16

typedef struct {
    int hull;
    int energy;
    int vault;          /* credits; -6 or below is bankruptcy */
    int armor;          /* soaks incoming damage until spent */
    int burn;           /* damage per round */
    int burn_rounds;
    int regen;          /* healing per round */
    int regen_rounds;
    int pending_energy; /* banked by channels 25/26/27, applied next round */
    int disabled_next;  /* hand slots the opponent's DISABLE will cost me next round */
    int blind_rounds;   /* my meters are hidden from the opponent for this many rounds */
    int hand[DW_HAND];
} DwSeat;

typedef struct {
    DwSeat seat[2];
    int deck[2][DW_DECK];
    int deck_pos[2];
    int round;
    int roll;               /* 0..99, this round's roll for conditions 18/21 and Dark Pool */
    int last_card[2];       /* what each seat played last round (-1 = pass) */
    int round_start_hull[2]; /* hull at the start of this round (channel 40 restores it) */
    int last_damage[2];     /* hull damage each seat TOOK last round, after every effect */
    int last_burn[2];       /* burn damage each seat took at the end of last round */
    int over;
    int winner;             /* -1 live, 0/1 seat, 2 draw */
    uint32_t rng;
    int kind_plays[3];      /* every card actually played, by kind -- the population's meta tally */
    int unsupported_fx[2];  /* bitmask of unsequenced channels each seat played, per round */
    int rounds_played;
} DwMatch;

/* Builds a match from two 16-card decks. Both hull/energy/vault start from the engine. */
void dw_match_init(DwMatch *m, uint32_t seed, const int deck0[DW_DECK], const int deck1[DW_DECK]);

/* One round. `card0`/`card1` are card ids or -1 to pass; an illegal card is treated as a pass (the
 * rules say it cannot be played, and the sequencer must not invent a different card).
 * Returns 1 when the match ended on this round, 0 while it is still live. */
int dw_match_round(DwMatch *m, int card0, int card1);

/* Plays a whole match with both seats choosing through the PARENA citizen policy (cp_pick), using
 * each seat's `aggression` (0..1000). Returns the winner (-1 if it could not finish). */
int dw_match_play_out(DwMatch *m, int aggression0, int aggression1);

/* Human-readable id of the seat a pid maps to, for logs/tests: 0 or 1. */
int dw_match_seat_for_pid(int seat0_pid, int pid);

#endif
