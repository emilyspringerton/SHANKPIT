#ifndef DW_STAGE_H
#define DW_STAGE_H

/* dw_stage.h -- THE STAGE in the world: a tournament bracket over this ladder's own rating order.
 *
 * Founder, real-time: "can we make a stage for esports tournoments inside of noobtopia", then "publish the
 * stage". NOOBTOPIA has the Roblox stage; this is the same thing in the world that OWNS the ladder, for two
 * reasons: a stage that only exists on Roblox cannot be published by the game's own snapshot (WOTAN reads
 * THAT file, not a Roblox server), and a bracket here means the citizens hold a tournament whether or not
 * anybody is standing at a table.
 *
 * THE SEEDING IS NOT WRITTEN HERE. `stage_slots`, `stage_rounds`, `stage_opponent` and `stage_higher_seed`
 * come from PARENA/stdlib/deadweight/stage.prn, emitted to dw_stage_rules.c -- the same file NOOBTOPIA emits
 * to Luau. Two brackets in two worlds must pair the same field the same way, and the only way to guarantee
 * that is to compute it once.
 *
 * ENTRANTS are the ladder's top four by (rating desc, wins desc, pid asc) -- the SAME order the published
 * page and the Roblox board rank by, so a stage cannot seed differently from the table it stands on.
 *
 * EVERY MATCH IS A REAL LADDER MATCH: it goes through dw_ladder_challenge + dw_ladder_accept, so records,
 * ratings, the meta, the saved file and the published ticker all move. Nothing here re-implements a match.
 *
 * A PERSON'S MATCH IS NOT PLAYED BEHIND THEIR BACK: if either entrant is a human seat, `dw_stage_play`
 * refuses with -2 and waits, exactly like the Roblox room waiting for a prompt. The world plays the matches
 * nobody is in.
 *
 * A DRAW advances the higher seed (dw_stage_rules' stage_higher_seed), and the match is STILL a real draw on
 * the ladder -- both ratings moved by a tenth of the gap. The stage and the table agree about what happened
 * even though only one of them advances. */

#include "dw_ladder.h"

#define DW_STAGE_SIZE 4
#define DW_STAGE_MAX_MATCHES 4
#define DW_STAGE_MAX_ROUNDS 3
#define DW_STAGE_INTERVAL_MS 6000u     /* how often the world plays a stage match nobody is in */
#define DW_STAGE_COOLDOWN_MS 60000u    /* how long a finished stage keeps its champion */

typedef struct {
    int a, b;        /* pids; -1 = an empty slot (a bye, or a round that has not been built yet) */
    int winner;      /* pid; -1 = undecided */
    int played;      /* 1 = a real match was played, 0 = a bye or still open */
} DwStageMatch;

typedef struct {
    int active;              /* a bracket exists */
    int over;                /* a champion exists */
    unsigned int started_ms, ended_ms, last_ms;
    int size;                /* entrants actually seated */
    int slots;               /* the bracket's size: a power of two >= size */
    int rounds;              /* how many rounds that needs */
    int round;               /* 1-based round currently being played */
    int byes;
    int entrants[DW_STAGE_SIZE];
    int rank[DW_STAGE_SIZE]; /* rank[k] is entrant k's seed, 1-based */
    int nentrants;
    DwStageMatch matches[DW_STAGE_MAX_ROUNDS][DW_STAGE_MAX_MATCHES];
    int champion;            /* pid, or -1 */
} DwStage;

/* A stage that has not been opened. Safe to publish: the export renders it as `"stage":null`. */
void dw_stage_reset(DwStage *s);

/* Opens a bracket over the ladder's top DW_STAGE_SIZE members. Returns 1 when a bracket exists, 0 when the
 * ladder cannot field one (fewer than 2 members). The field is taken at the moment of the call, so a stage
 * is the population as it stands rather than a snapshot somebody took earlier. */
int dw_stage_open(DwStage *s, const DwLadder *l, unsigned int now);

/* The next match that needs a result, as (round, index), or 0 when the bracket is over or waiting. */
int dw_stage_next(const DwStage *s, int *round, int *slot);

/* Plays the next match for real, through the ladder. Returns the advancing pid, 0 when there is nothing to
 * play, and -2 when the match involves a human seat (it waits for that person, per the header). */
int dw_stage_play(DwStage *s, DwLadder *l, unsigned int now);

/* "FINAL", "SEMIFINAL", "QUARTERFINAL" -- for a screen or a log line. */
const char *dw_stage_round_name(const DwStage *s);

#endif
