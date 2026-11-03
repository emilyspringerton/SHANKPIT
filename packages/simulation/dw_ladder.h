#ifndef DW_LADDER_H
#define DW_LADDER_H

/* dw_ladder.h -- the citizens' DEADWEIGHT ladder: who plays whom, and what they build.
 *
 * Founder real-time, 2026-11-03: "I WANT CITIZENS TO BE ABLE TO CHALLENGE EACHOTHER TO GAMES OF
 * DEADWEIGHT AND CITIZENS CAN CHALLENGE PLAYERS AND PLAYERS CAN CHALLENGE CITIZENS AND PLAYERS CAN
 * CHALLENGE PLAYERS CITIZENS WILL CRAFT THEIR OWN DECKS BASED ON THEIR WIN RATE AND THE CURRENT
 * META." That is exactly the surface here, one entry point (`dw_ladder_challenge`) for all four
 * directions, classified by who the two pids resolve to rather than by four parallel APIs that can
 * disagree with each other.
 *
 * CITIZENS CRAFT THEIR OWN DECKS. A deck is not stored as a preference file: it is a FUNCTION of
 * the citizen's own record (wins/games) and the POPULATION'S meta (the kind mix of every card
 * actually played in this ladder), evaluated by PARENA's cp_deck_card. So a citizen that keeps
 * losing to the current top deck rebuilds toward the answer by itself, and one that keeps winning
 * keeps its identity -- and because it is a function, a restart with the same record and the same
 * meta reproduces the same 16 cards. `dw_ladder_craft_deck` is the only writer.
 *
 * A MATCH IS PLAYED OUT TO A WINNER the moment a challenge is accepted: this is a card game with no
 * real-time component, so there is nothing for a tick loop to dribble out -- the duel runs to
 * completion in one deterministic call (dw_match_play_out) and the records update. A later, watched
 * version (someone watching the board round by round) is a scheduling change on top of this, not a
 * different rules engine.
 *
 * Players have records too, and get decks the same way (their path is a stand-in until a player can
 * choose a deck at all -- named as the next step, not pretended otherwise). */

#include <stdint.h>
#include "dw_match.h"   /* DW_DECK, DW_HAND -- the deck a member carries is a match deck */

#define DW_LADDER_MAX_CITIZENS 48
#define DW_LADDER_MAX_CHALLENGES 16
#define DW_LADDER_CHALLENGE_TTL_MS 45000u
#define DW_LADDER_CITIZEN_CHALLENGE_INTERVAL_MS 4000u

enum {
    DW_CHAL_NONE = 0,
    DW_CHAL_CITIZEN_CITIZEN = 1,
    DW_CHAL_CITIZEN_PLAYER = 2,
    DW_CHAL_PLAYER_CITIZEN = 3,
    DW_CHAL_PLAYER_PLAYER = 4
};

typedef struct {
    int active;
    int is_player;      /* 1 = a human seat (the hero, a coop peer), 0 = an ambient citizen */
    int pid;
    int path;           /* CILIFE_* for citizens; DW_PLAYER_PATH for people */
    int aggression;     /* 0..1000, from the citizen's own traits; 500 for people */
    int wins, games;
    int deck[DW_DECK];
    unsigned int last_craft_ms;
} DwLadderMember;

typedef struct {
    int active;
    unsigned int id;
    int from_pid, to_pid;
    int kind;           /* DW_CHAL_* */
    unsigned int opened_ms;
    unsigned int expires_ms;
} DwChallenge;

/* The match that finished most recently, in enough detail for a player's own screen to say what
 * happened ("Ada beat you 3-0 in 9 rounds") without replaying it. Challenges only carry the two
 * pids and an expiry; a RESULT is a fact about a match that has already happened, so it gets its own
 * record rather than being folded back into the challenge slot that was just closed. */
typedef struct {
    int valid;
    int kind;           /* DW_CHAL_* -- the direction the match was opened from */
    int pid[2];         /* the challenge's order: seat 0 opened it */
    int winner;         /* 0/1 seat, 2 draw */
    int rounds;
    int hull[2];
    unsigned int at_ms;
} DwLadderLast;

typedef struct {
    DwLadderMember members[DW_LADDER_MAX_CITIZENS];
    int count;
    DwChallenge challenges[DW_LADDER_MAX_CHALLENGES];
    unsigned int next_challenge_id;
    int meta_kind[3];   /* every card played in this ladder, by kind */
    long meta_plays;
    int results[5];     /* matches completed, by DW_CHAL_* */
    DwLadderLast last;  /* the most recent finished match, for the player's own screen */
    int hero_pid;       /* -1 when nobody is playing */
    uint32_t rng;
    unsigned int now_ms;
    unsigned int last_citizen_challenge_ms;
} DwLadder;

#define DW_PLAYER_PATH 2   /* DUEL: a person came to play */

/* Resets the ladder. `seed` drives citizen-initiated challenges only -- decks and matches carry
 * their own seeds, so a match replay does not depend on how many people wandered past. */
int dw_ladder_reset(DwLadder *l, uint32_t seed);

/* Registers a citizen (or, with is_player=1, a human seat). A citizen's path/aggression come from
 * its own persona in citizen_life.h. The deck is crafted immediately, from a 0-0 record and an
 * empty meta, so nobody starts with no cards. Returns the member index, or -1 when full. */
int dw_ladder_add_citizen(DwLadder *l, int pid, int path, int aggression, unsigned int now);
int dw_ladder_add_player(DwLadder *l, int pid, unsigned int now);
void dw_ladder_remove_pid(DwLadder *l, int pid);
void dw_ladder_set_hero(DwLadder *l, int pid);

int dw_ladder_member_for_pid(const DwLadder *l, int pid);
int dw_ladder_citizen_winrate(const DwLadder *l, int member_index);

/* Re-crafts one member's deck from its record and the current meta. Returns the deck's first card,
 * or -1 if the member does not exist. */
int dw_ladder_craft_deck(DwLadder *l, int member_index, unsigned int now);

/* The four directions, one call. Returns the challenge slot, or -1 (unknown pid, member full, the
 * two are the same person, or no free slot). A player challenging a citizen and a citizen
 * challenging a player are the SAME relationship seen from the two ends -- the kind records which
 * end opened it, because that is what a UI needs to say ("Kaito challenges you" vs "you challenged
 * Kaito"). */
int dw_ladder_challenge(DwLadder *l, int from_pid, int to_pid, unsigned int now);

/* The first open challenge addressed TO this pid, or -1. */
int dw_ladder_pending_for(const DwLadder *l, int pid);

/* Accepts and plays the match to a winner. Returns the winning SEAT's pid (or -2 on a draw), or -3
 * if the slot was not a live challenge. Both records are updated and both decks re-crafted, because
 * the match changed both records and the population's meta. */
int dw_ladder_accept(DwLadder *l, int slot, unsigned int now);
void dw_ladder_decline(DwLadder *l, int slot);

/* Expiry, plus citizens starting games of their own: every interval, one citizen challenges another
 * citizen, or the hero with a lower rate. Citizens do not need a player present to play. */
void dw_ladder_tick(DwLadder *l, unsigned int now);

/* ---- records that survive a restart ------------------------------------------------------------
 * A ladder whose records vanish with the process is a ladder nobody can be told they are losing on:
 * the whole "citizens craft decks from their win rate" loop resets to an empty table every boot, and
 * the player's own record is a lie by morning. This is that gap, closed with the smallest file that
 * can hold the facts.
 *
 * WHAT IS SAVED: every member (pid, whether it is a person, its path, aggression, wins, games), the
 * population's meta tally, the per-direction results, and the hero. WHAT IS NOT: the DECKS (a deck is
 * a FUNCTION of the record and the meta -- see dw_ladder_craft_deck -- so re-crafting on load both
 * restores them exactly and PROVES the function is deterministic), and the CHALLENGE BOOK (a
 * challenge has a 45-second life; a restart killing them is honest, and a saved one would be a
 * promise about a clock that did not run).
 *
 * The file is line-oriented text, not JSON: it is written and read by the same C module, and a format
 * nobody else parses does not earn a parser. Unknown or malformed lines refuse the WHOLE file rather
 * than half-loading a corrupt record -- a wrong win count is worse than an empty one. Write is
 * write-then-rename, so a crash mid-save cannot leave a truncated ladder behind. */
int dw_ladder_save(const DwLadder *l, const char *path);

/* Loads over a ladder (typically a freshly reset one). Returns 1 loaded, 0 when there is no file yet
 * (not an error -- first boot), -1 on an IO failure, -2 when the file exists but is not this format
 * (the ladder is left exactly as it was). */
int dw_ladder_load(DwLadder *l, const char *path, unsigned int now);

/* Names the file every finished match is written to. "" or NULL stops autosaving. */
void dw_ladder_set_save_path(const char *path);

/* The population's current meta: the most-played kind (0/1/2, -1 with no data) and its share in
 * permille of all cards played. */
int dw_ladder_meta_kind(const DwLadder *l);
int dw_ladder_meta_share(const DwLadder *l);

const char *dw_challenge_kind_name(int kind);

#endif
