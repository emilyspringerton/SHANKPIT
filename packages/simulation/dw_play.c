/* dw_play.c -- see dw_play.h. No rules live here: every number is the ladder's or the match's. */

#include "dw_play.h"

#include <string.h>

static int dw_play_open_from(const DwLadder *l, int from_pid, int to_pid) {
    for (int i = 0; i < DW_LADDER_MAX_CHALLENGES; i++) {
        const DwChallenge *c = &l->challenges[i];
        if (c->active && c->from_pid == from_pid && c->to_pid == to_pid) return i;
    }
    return -1;
}

void dw_play_init(DwPlay *p, int my_pid) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->my_pid = my_pid;
    p->pending_slot = -1;
    p->last_winner_pid = -3;
    p->last_opponent_pid = -1;
    p->last_kind = DW_CHAL_NONE;
}

int dw_play_pending(DwLadder *l, DwPlay *p) {
    if (!l || !p) return -1;
    p->pending_slot = dw_ladder_pending_for(l, p->my_pid);
    return p->pending_slot;
}

int dw_play_opponent_for(DwLadder *l, DwPlay *p, int slot) {
    if (!l || !p) return -1;
    if (slot < 0 || slot >= DW_LADDER_MAX_CHALLENGES) return -1;
    const DwChallenge *c = &l->challenges[slot];
    if (!c->active) return -1;
    return c->from_pid == p->my_pid ? c->to_pid : c->from_pid;
}

int dw_play_answer(DwLadder *l, DwPlay *p, int accept, unsigned int now) {
    if (!l || !p) return -3;
    int slot = dw_ladder_pending_for(l, p->my_pid);
    if (slot < 0) return -3;
    int opponent = dw_play_opponent_for(l, p, slot);

    if (!accept) {
        dw_ladder_decline(l, slot);
        p->pending_slot = -1;
        p->answered++;
        p->declined++;
        return -4;
    }

    int winner = dw_ladder_accept(l, slot, now);
    if (winner == -3) return -3;   /* the slot died between the check and the play */

    p->pending_slot = -1;
    p->answered++;
    p->last_winner_pid = winner;
    p->last_opponent_pid = opponent;
    p->last_result_ms = now;
    if (l->last.valid) {
        /* the ladder records the match in the CHALLENGE's seat order (seat 0 opened it). A screen
         * speaks from one person's side, so translate once, here, instead of at every call site. */
        int my_seat = (l->last.pid[0] == p->my_pid) ? 0 : 1;
        p->last_kind = l->last.kind;
        p->last_rounds = l->last.rounds;
        p->last_hull_mine = l->last.hull[my_seat];
        p->last_hull_theirs = l->last.hull[1 - my_seat];
    }
    return winner;
}

int dw_play_challenge(DwLadder *l, DwPlay *p, int member_index, unsigned int now) {
    if (!l || !p) return -1;
    if (member_index < 0 || member_index >= DW_LADDER_MAX_CITIZENS) return -1;
    const DwLadderMember *m = &l->members[member_index];
    if (!m->active) return -1;
    if (m->pid == p->my_pid) return -1;
    int slot = dw_ladder_challenge(l, p->my_pid, m->pid, now);
    if (slot >= 0) p->issued++;
    return slot;
}

int dw_play_roster(const DwLadder *l, int my_pid, int *out, int cap) {
    if (!l || !out || cap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < DW_LADDER_MAX_CITIZENS && n < cap; i++) {
        const DwLadderMember *m = &l->members[i];
        if (!m->active) continue;
        if (m->pid == my_pid) continue;                       /* you cannot ask yourself */
        if (dw_play_open_from(l, my_pid, m->pid) >= 0) continue;   /* already asked, still waiting */
        out[n++] = i;
    }
    return n;
}
