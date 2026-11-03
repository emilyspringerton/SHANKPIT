/* dw_stage.c -- see dw_stage.h. The bracket itself: entrants by rating, a fold from PARENA, matches through
 * the ladder, and a champion. No match rules live here -- dw_match owns those and the ladder owns the
 * records, which is why a stage result is a ladder result rather than a parallel universe. */

#include "dw_stage.h"
#include "dw_stage_rules_host.h"   /* the PARENA-emitted fold: one .prn, two worlds */

#include <string.h>

void dw_stage_reset(DwStage *s) {
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->champion = -1;
    for (int r = 0; r < DW_STAGE_MAX_ROUNDS; r++) {
        for (int m = 0; m < DW_STAGE_MAX_MATCHES; m++) {
            s->matches[r][m].a = -1;
            s->matches[r][m].b = -1;
            s->matches[r][m].winner = -1;
        }
    }
    for (int i = 0; i < DW_STAGE_SIZE; i++) s->entrants[i] = -1;
}

/* The ladder's own ranking: rating desc, wins desc, pid asc. Selection rather than a sort, because the
 * field is four and a hand-rolled qsort comparator is more code than four scans. */
static int gather_entrants(DwStage *s, const DwLadder *l) {
    int used[DW_LADDER_MAX_CITIZENS];
    memset(used, 0, sizeof used);
    int n = 0;
    while (n < DW_STAGE_SIZE) {
        int best = -1;
        for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) {
            const DwLadderMember *m = &l->members[i];
            if (!m->active || used[i]) continue;
            if (best < 0) { best = i; continue; }
            const DwLadderMember *b = &l->members[best];
            if (m->rating > b->rating) best = i;
            else if (m->rating == b->rating && m->wins > b->wins) best = i;
            else if (m->rating == b->rating && m->wins == b->wins && m->pid < b->pid) best = i;
        }
        if (best < 0) break;
        used[best] = 1;
        s->entrants[n] = l->members[best].pid;
        s->rank[n] = n + 1;
        n++;
    }
    return n;
}

static void seat(DwStage *s, int round, int slot, int a, int b, int rank_a, int rank_b) {
    if (round < 1 || round > DW_STAGE_MAX_ROUNDS) return;
    if (slot < 0 || slot >= DW_STAGE_MAX_MATCHES) return;
    DwStageMatch *m = &s->matches[round - 1][slot];
    m->a = a;
    m->b = b;
    m->winner = -1;
    m->played = 0;
    /* a slot with one body is a BYE: decided, never played. Recording it as decided is what makes
     * "advance the winner" a single rule for every match in the bracket. */
    if (a >= 0 && b < 0) { m->winner = a; s->byes++; }
    else if (b >= 0 && a < 0) { m->winner = b; s->byes++; }
    (void)rank_a; (void)rank_b;
}

int dw_stage_open(DwStage *s, const DwLadder *l, unsigned int now) {
    if (!s || !l) return 0;
    dw_stage_reset(s);
    s->nentrants = gather_entrants(s, l);
    s->size = s->nentrants;
    if (s->nentrants < 2) return 0;

    s->slots = stage_slots(s->nentrants);
    s->rounds = stage_rounds(s->slots);
    if (s->slots > DW_STAGE_SIZE) s->slots = DW_STAGE_SIZE;

    for (int i = 1; i <= s->slots / 2; i++) {
        int ai = i;                       /* 1-based entrant index */
        int bi = stage_opponent(s->slots, i);
        int a = (ai <= s->nentrants) ? s->entrants[ai - 1] : -1;
        int b = (bi <= s->nentrants) ? s->entrants[bi - 1] : -1;
        seat(s, 1, i - 1, a, b, ai, bi);
    }

    s->active = 1;
    s->round = 1;
    s->champion = -1;
    s->started_ms = now;
    s->last_ms = 0;
    s->ended_ms = 0;
    return 1;
}

static int round_complete(const DwStage *s, int round) {
    int slots = s->slots >> (round - 1);
    for (int i = 0; i < slots / 2; i++) {
        if (s->matches[round - 1][i].winner < 0) return 0;
    }
    return 1;
}

/* Builds the next round from the current one's winners, in bracket order. Returns 1 when a round was built,
 * 0 when the current round is unfinished, and 2 when the bracket has a champion. */
static int advance_round(DwStage *s) {
    if (s->round < 1 || s->round > DW_STAGE_MAX_ROUNDS) return 0;
    if (!round_complete(s, s->round)) return 0;
    int slots = s->slots >> (s->round - 1);
    int n = slots / 2;
    int winners[DW_STAGE_MAX_MATCHES];
    for (int i = 0; i < n; i++) winners[i] = s->matches[s->round - 1][i].winner;
    if (n <= 1) {
        s->champion = winners[0];
        s->over = 1;
        s->ended_ms = s->last_ms;
        return 2;
    }
    int next = s->round + 1;
    for (int i = 0; i < n / 2; i++) {
        int a = winners[i * 2], b = winners[i * 2 + 1];
        seat(s, next, i, a, b, 0, 0);
    }
    s->round = next;
    return 1;
}

int dw_stage_next(const DwStage *s, int *round, int *slot) {
    if (!s || !s->active || s->over) return 0;
    DwStage probe = *s;                 /* advancing is a state change; probe a copy so this stays a query */
    for (int guard = 0; guard < DW_STAGE_MAX_ROUNDS + 1; guard++) {
        if (probe.round < 1 || probe.round > DW_STAGE_MAX_ROUNDS) return 0;
        if (!round_complete(&probe, probe.round)) break;
        if (advance_round(&probe) == 2) return 0;
        if (advance_round(&probe) == 0) break;
    }
    int r = probe.round;
    int slots = probe.slots >> (r - 1);
    for (int i = 0; i < slots / 2; i++) {
        const DwStageMatch *m = &probe.matches[r - 1][i];
        if (m->a >= 0 && m->b >= 0 && m->winner < 0) {
            if (round) *round = r;
            if (slot) *slot = i;
            return 1;
        }
    }
    return 0;
}

/* Ranking lookup for the draw tie-break: a pid's seed, or 999 when it is not in this bracket. */
static int rank_of(const DwStage *s, int pid) {
    for (int i = 0; i < DW_STAGE_SIZE; i++) {
        if (s->entrants[i] == pid) return s->rank[i];
    }
    return 999;
}

int dw_stage_play(DwStage *s, DwLadder *l, unsigned int now) {
    if (!s || !l || !s->active || s->over) return 0;
    int round = 0, slot = 0;
    if (!dw_stage_next(s, &round, &slot)) {
        /* the round may only need advancing (a bye finished it): do that, then look again */
        if (advance_round(s) == 2) return 0;
        if (!dw_stage_next(s, &round, &slot)) return 0;
    }
    DwStageMatch *m = &s->matches[round - 1][slot];
    if (m->a < 0 || m->b < 0 || m->winner >= 0) return 0;

    /* a person's match waits for the person: the world cannot play a game on somebody's behalf */
    int ia = dw_ladder_member_for_pid(l, m->a);
    int ib = dw_ladder_member_for_pid(l, m->b);
    if (ia < 0 || ib < 0) { m->winner = (ia < 0) ? m->b : m->a; return m->winner; }
    if (l->members[ia].is_player || l->members[ib].is_player) return -2;

    int ch = dw_ladder_challenge(l, m->a, m->b, now);
    if (ch < 0) return 0;
    int winner = dw_ladder_accept(l, ch, now);

    int advanced;
    if (winner == -2) {
        /* the match was a DRAW, which the ladder recorded honestly; the stage still needs a body */
        int side = stage_higher_seed(rank_of(s, m->a), rank_of(s, m->b));
        advanced = (side == 0) ? m->a : m->b;
    } else {
        advanced = winner;
    }
    m->winner = advanced;
    m->played = (winner != -2) ? 1 : 1;   /* a draw was still played, it just did not produce a winner */
    s->last_ms = now;
    advance_round(s);
    return advanced;
}

const char *dw_stage_round_name(const DwStage *s) {
    if (!s) return "STAGE";
    int left = s->slots >> (s->round > 0 ? (s->round - 1) : 0);
    if (left <= 2) return "FINAL";
    if (left == 4) return "SEMIFINAL";
    return "QUARTERFINAL";
}
