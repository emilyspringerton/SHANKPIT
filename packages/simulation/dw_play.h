#ifndef DW_PLAY_H
#define DW_PLAY_H

/* dw_play.h -- THE PLAYER'S END of the citizens' DEADWEIGHT ladder.
 *
 * `dw_ladder.h` already answers "who plays whom" for all four directions, but it answers it in pids
 * and member indices: the vocabulary of the simulation. A person sitting at the game has a screen,
 * and a screen has rows. This module is the one layer between them, and it exists so that the UI
 * code never has to hold a pointer into the ladder or guess which end of a challenge it is:
 *
 *   - what is waiting on me                (dw_play_pending)
 *   - who is asking                        (dw_play_opponent_for)
 *   - yes / no                             (dw_play_answer)
 *   - who could I ask                      (dw_play_roster)
 *   - ask them                             (dw_play_challenge)
 *
 * WHY A SEPARATE MODULE, given the ladder is one call away. Because the mistakes here are not rule
 * mistakes, they are SCREEN mistakes: showing a challenge that has already expired, offering a
 * citizen the player has already asked, reading a result from the wrong seat, or reporting
 * "you lost" after a decline. None of that can be tested through a renderer, and all of it can be
 * tested through this interface -- which is why it exists instead of being inlined into a draw
 * function, and why `make test-deadweight` drives it directly.
 *
 * A MATCH IS STILL PLAYED OUT INSTANTLY. There is no board to watch yet (that is a scheduling
 * change named in docs/DEADWEIGHT_IN_SHANKPIT_NEXT_STEPS.md, not a rules change). What this module
 * gives a screen is the RESULT: the two hulls, the round count and who won, from the ladder's own
 * record of the match it just finished.
 *
 * Names are deliberately absent. A pid's name belongs to the citizen that owns it
 * (witness_ai_citizen_name), and this module never invents one -- the roster is indices, and the
 * host resolves them. */

#include "dw_ladder.h"

#define DW_PLAY_ROSTER DW_LADDER_MAX_CITIZENS

typedef struct {
    int my_pid;
    int pending_slot;        /* the cached dw_ladder_pending_for result; -1 when nothing waits */
    int last_winner_pid;     /* the last match: winner's pid, -2 a draw, -3 no match yet */
    int last_opponent_pid;
    int last_kind;           /* DW_CHAL_*, the direction the last match was opened from */
    int last_rounds;
    int last_hull_mine;      /* the last match, read from MY seat rather than seat 0 */
    int last_hull_theirs;
    unsigned int last_result_ms;
    int answered;            /* challenges this player has answered, accepted or declined */
    int declined;
    int issued;              /* challenges this player has opened */
} DwPlay;

/* `my_pid` is the player's own ladder pid -- the ladder's hero_pid, not the render slot. */
void dw_play_init(DwPlay *p, int my_pid);

/* Refreshes and returns the slot of the first challenge addressed to you, or -1. */
int dw_play_pending(DwLadder *l, DwPlay *p);

/* The pid at the OTHER end of a challenge -- the asker, when you are the one being asked. */
int dw_play_opponent_for(DwLadder *l, DwPlay *p, int slot);

/* Answers the waiting challenge. `accept` plays the match out to a winner; `!accept` closes it.
 * Returns the winning pid (0..n), -2 for a draw, -3 when nothing was waiting, or -4 when the
 * challenge was declined. A decline touches no record and no result: nothing happened is the truth,
 * and a screen that prints "you lost" after a no is a bug this return value exists to prevent. */
int dw_play_answer(DwLadder *l, DwPlay *p, int accept, unsigned int now);

/* Opens a challenge against members[member_index]. Returns the challenge slot, or -1 (not on the
 * ladder, yourself, or no free slot). */
int dw_play_challenge(DwLadder *l, DwPlay *p, int member_index, unsigned int now);

/* Who this player could ask RIGHT NOW, as member indices: everyone still on the ladder except
 * themselves, minus anyone they already have an open challenge with (asking twice is what a cursor
 * resting on a row would do by accident). Returns the count written to `out`, capped by `cap`. */
int dw_play_roster(const DwLadder *l, int my_pid, int *out, int cap);

#endif
