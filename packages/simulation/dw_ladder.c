/* dw_ladder.c -- see dw_ladder.h. The decks come from PARENA's citizen policy; the matches come from
 * dw_match.c; this file only keeps the records, the meta tally and the challenge book. */

#include "dw_ladder.h"
#include "dw_match.h"
#include "dw_engine.h"
#include "dw_net.h"   /* the publish side: every finished match goes on the wire's ticker */

#include <stdio.h>
#include <string.h>

#define DW_LADDER_FORMAT "shankpit-deadweight-ladder 1"

/* The autosave target, set once by the host (witness_ai.c reads SHANKPIT_LADDER_FILE). Empty means
 * no autosave, which is the default: a normal run writes nothing at all, the same discipline
 * dw_net.c's own snapshot already follows. */
static char g_ladder_path[240];

void dw_ladder_set_save_path(const char *path) {
    if (!path) { g_ladder_path[0] = '\0'; return; }
    snprintf(g_ladder_path, sizeof g_ladder_path, "%s", path);
}

static uint32_t dw_ladder_rng(DwLadder *l) {
    l->rng = l->rng * 1664525u + 1013904223u;
    return l->rng;
}

int dw_ladder_reset(DwLadder *l, uint32_t seed) {
    if (!l) return -1;
    memset(l, 0, sizeof(*l));
    l->next_challenge_id = 1;
    l->hero_pid = -1;
    l->rng = seed ? seed : 0x9e3779b9u;
    return 0;
}

int dw_ladder_member_for_pid(const DwLadder *l, int pid) {
    if (!l || pid < 0) return -1;
    for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++)
        if (l->members[i].active && l->members[i].pid == pid) return i;
    return -1;
}

int dw_ladder_citizen_winrate(const DwLadder *l, int member_index) {
    if (!l || member_index < 0 || member_index >= DW_LADDER_MAX_CITIZENS) return 500;
    const DwLadderMember *m = &l->members[member_index];
    return dw_engine()->cp_winrate(m->wins, m->games);
}

int dw_ladder_meta_kind(const DwLadder *l) {
    if (!l || l->meta_plays <= 0) return -1;
    int best = 0;
    for (int k = 1; k < 3; k++) if (l->meta_kind[k] > l->meta_kind[best]) best = k;
    return best;
}

int dw_ladder_meta_share(const DwLadder *l) {
    if (!l || l->meta_plays <= 0) return 0;
    int best = dw_ladder_meta_kind(l);
    if (best < 0) return 0;
    long permille = (long)l->meta_kind[best] * 1000L / l->meta_plays;
    return (int)permille;
}

int dw_ladder_craft_deck(DwLadder *l, int idx, unsigned int now) {
    if (!l || idx < 0 || idx >= DW_LADDER_MAX_CITIZENS || !l->members[idx].active) return -1;
    const DwEngine *e = dw_engine();
    DwLadderMember *m = &l->members[idx];
    int winrate = e->cp_winrate(m->wins, m->games);
    int meta_kind = dw_ladder_meta_kind(l);
    int meta_share = dw_ladder_meta_share(l);
    for (int slot = 0; slot < DW_DECK; slot++)
        m->deck[slot] = e->cp_deck_card(slot, m->path, winrate, meta_kind, meta_share);
    m->last_craft_ms = now;
    return m->deck[0];
}

static int dw_ladder_free_member(const DwLadder *l) {
    for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) if (!l->members[i].active) return i;
    return -1;
}

int dw_ladder_add_citizen(DwLadder *l, int pid, int path, int aggression, unsigned int now) {
    if (!l) return -1;
    int existing = dw_ladder_member_for_pid(l, pid);
    if (existing >= 0) return existing;   /* respawn of a body already on the ladder */
    int idx = dw_ladder_free_member(l);
    if (idx < 0) return -1;
    DwLadderMember *m = &l->members[idx];
    memset(m, 0, sizeof(*m));
    m->active = 1;
    m->is_player = 0;
    m->pid = pid;
    m->path = path;
    m->aggression = aggression < 0 ? 0 : (aggression > 1000 ? 1000 : aggression);
    l->count++;
    dw_ladder_craft_deck(l, idx, now);
    return idx;
}

int dw_ladder_add_player(DwLadder *l, int pid, unsigned int now) {
    if (!l) return -1;
    int existing = dw_ladder_member_for_pid(l, pid);
    if (existing >= 0) {
        l->members[existing].is_player = 1;
        return existing;
    }
    int idx = dw_ladder_free_member(l);
    if (idx < 0) return -1;
    DwLadderMember *m = &l->members[idx];
    memset(m, 0, sizeof(*m));
    m->active = 1;
    m->is_player = 1;
    m->pid = pid;
    m->path = DW_PLAYER_PATH;
    m->aggression = 500;
    l->count++;
    dw_ladder_craft_deck(l, idx, now);
    return idx;
}

void dw_ladder_remove_pid(DwLadder *l, int pid) {
    if (!l) return;
    int idx = dw_ladder_member_for_pid(l, pid);
    if (idx >= 0) {
        l->members[idx].active = 0;
        if (l->count > 0) l->count--;
    }
    for (int i = 0; i < DW_LADDER_MAX_CHALLENGES; i++)
        if (l->challenges[i].active &&
            (l->challenges[i].from_pid == pid || l->challenges[i].to_pid == pid))
            l->challenges[i].active = 0;
}

void dw_ladder_set_hero(DwLadder *l, int pid) {
    if (!l) return;
    l->hero_pid = pid;
    if (pid >= 0) dw_ladder_add_player(l, pid, l->now_ms);
}

int dw_ladder_challenge(DwLadder *l, int from_pid, int to_pid, unsigned int now) {
    if (!l || from_pid == to_pid) return -1;
    int from_m = dw_ladder_member_for_pid(l, from_pid);
    int to_m = dw_ladder_member_for_pid(l, to_pid);
    if (from_m < 0 || to_m < 0) return -1;
    int from_player = l->members[from_m].is_player;
    int to_player = l->members[to_m].is_player;
    int slot = -1;
    for (int i = 0; i < DW_LADDER_MAX_CHALLENGES; i++) if (!l->challenges[i].active) { slot = i; break; }
    if (slot < 0) return -1;
    DwChallenge *c = &l->challenges[slot];
    c->active = 1;
    c->id = l->next_challenge_id++;
    c->from_pid = from_pid;
    c->to_pid = to_pid;
    c->kind = from_player ? (to_player ? DW_CHAL_PLAYER_PLAYER : DW_CHAL_PLAYER_CITIZEN)
                          : (to_player ? DW_CHAL_CITIZEN_PLAYER : DW_CHAL_CITIZEN_CITIZEN);
    c->opened_ms = now;
    c->expires_ms = now + DW_LADDER_CHALLENGE_TTL_MS;
    return slot;
}

int dw_ladder_pending_for(const DwLadder *l, int pid) {
    if (!l) return -1;
    for (int i = 0; i < DW_LADDER_MAX_CHALLENGES; i++)
        if (l->challenges[i].active && l->challenges[i].to_pid == pid) return i;
    return -1;
}

void dw_ladder_decline(DwLadder *l, int slot) {
    if (!l || slot < 0 || slot >= DW_LADDER_MAX_CHALLENGES) return;
    l->challenges[slot].active = 0;
}

int dw_ladder_accept(DwLadder *l, int slot, unsigned int now) {
    if (!l || slot < 0 || slot >= DW_LADDER_MAX_CHALLENGES) return -3;
    DwChallenge *c = &l->challenges[slot];
    if (!c->active) return -3;
    int i0 = dw_ladder_member_for_pid(l, c->from_pid);
    int i1 = dw_ladder_member_for_pid(l, c->to_pid);
    if (i0 < 0 || i1 < 0) { c->active = 0; return -3; }

    DwLadderMember *a = &l->members[i0];
    DwLadderMember *b = &l->members[i1];
    /* the seats are the CHALLENGE's order: seat 0 is whoever opened it, which is what makes a result
     * readable as "Kaito beat you" without a second lookup */
    DwMatch match;
    uint32_t seed = (uint32_t)c->id * 2654435761u ^ (uint32_t)((a->pid + 1) * 7919) ^ (uint32_t)(b->pid + 3);
    dw_match_init(&match, seed, a->deck, b->deck);
    int winner = dw_match_play_out(&match, a->aggression, b->aggression);
    int kind = c->kind;
    c->active = 0;

    /* the population's meta is the cards that were actually played, not the decks that were built */
    for (int k = 0; k < 3; k++) l->meta_kind[k] += match.kind_plays[k];
    l->meta_plays += (long)match.kind_plays[0] + match.kind_plays[1] + match.kind_plays[2];
    l->results[kind]++;

    int pid = -2;
    if (winner == 0 || winner == 1) {
        DwLadderMember *w = winner == 0 ? a : b;
        DwLadderMember *lose = winner == 0 ? b : a;
        w->wins++;
        w->games++;
        lose->games++;
        pid = w->pid;
    } else {
        a->games++;
        b->games++;
    }

    /* ...and the match itself is kept, in the seats' own order, so a screen can report what happened
     * rather than only who won (dw_play.h reads this). Written before the decks are re-crafted, so
     * nothing about this record can depend on the post-match state it caused. */
    l->last.valid = 1;
    l->last.kind = kind;
    l->last.pid[0] = a->pid;
    l->last.pid[1] = b->pid;
    l->last.winner = winner == 0 ? 0 : (winner == 1 ? 1 : 2);
    l->last.rounds = match.rounds_played;
    l->last.hull[0] = match.seat[0].hull;
    l->last.hull[1] = match.seat[1].hull;
    l->last.at_ms = now;

    /* every finished match goes into the published ticker (dw_net.h). Done here rather than in a
     * caller because this is the ONE place a match stops being live, so nothing can finish a match
     * and forget to tell the internet about it. */
    dw_net_note_match(kind, a->pid, b->pid, winner == 0 ? 0 : (winner == 1 ? 1 : 2),
                      match.rounds_played, match.seat[0].hull, match.seat[1].hull, now);

    /* both records changed, so both decks are re-crafted from the new record and the new meta --
     * this is the "citizens craft their own decks based on their win rate and the current meta"
     * loop closing, and it happens whether they won or lost */
    dw_ladder_craft_deck(l, i0, now);
    dw_ladder_craft_deck(l, i1, now);
    l->now_ms = now;
    /* a finished match is the only thing worth a write: records moved, and the file is the record */
    if (g_ladder_path[0]) dw_ladder_save(l, g_ladder_path);
    return pid;
}

int dw_ladder_save(const DwLadder *l, const char *path) {
    if (!l || !path || !path[0]) return -1;
    char tmp[260];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", DW_LADDER_FORMAT);
    fprintf(f, "meta %d %d %d %ld\n", l->meta_kind[0], l->meta_kind[1], l->meta_kind[2], l->meta_plays);
    fprintf(f, "results %d %d %d %d\n", l->results[1], l->results[2], l->results[3], l->results[4]);
    fprintf(f, "hero %d\n", l->hero_pid);
    for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) {
        const DwLadderMember *m = &l->members[i];
        if (!m->active) continue;
        fprintf(f, "member %d %d %d %d %d %d\n", m->pid, m->is_player, m->path, m->aggression,
                m->wins, m->games);
    }
    int ok = fflush(f) == 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return -1; }
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 1;
}

int dw_ladder_load(DwLadder *l, const char *path, unsigned int now) {
    if (!l || !path || !path[0]) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;   /* nothing saved yet: a first boot, not a failure */

    char line[256];
    if (!fgets(line, sizeof line, f) || strncmp(line, DW_LADDER_FORMAT, strlen(DW_LADDER_FORMAT)) != 0) {
        fclose(f);
        return -2;
    }

    int mk[3] = { 0, 0, 0 };
    long plays = -1;
    int res[5] = { 0, 0, 0, 0, 0 };
    int hero = -1;
    struct { int pid, is_player, path, aggression, wins, games; } pend[DW_LADDER_MAX_CITIZENS];
    int np = 0;

    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "meta %d %d %d %ld", &mk[0], &mk[1], &mk[2], &plays) == 4) continue;
        if (sscanf(line, "results %d %d %d %d", &res[1], &res[2], &res[3], &res[4]) == 4) continue;
        if (sscanf(line, "hero %d", &hero) == 1) continue;
        if (np < DW_LADDER_MAX_CITIZENS &&
            sscanf(line, "member %d %d %d %d %d %d", &pend[np].pid, &pend[np].is_player,
                   &pend[np].path, &pend[np].aggression, &pend[np].wins, &pend[np].games) == 6) {
            np++;
            continue;
        }
        fclose(f);
        return -2;   /* one unreadable line refuses the file, rather than half-loading a record */
    }
    fclose(f);
    if (plays < 0) return -2;   /* no meta line: the header's contract was not met */

    /* the meta goes in FIRST, because every deck re-crafted below is a function of it */
    l->meta_kind[0] = mk[0];
    l->meta_kind[1] = mk[1];
    l->meta_kind[2] = mk[2];
    l->meta_plays = plays;
    for (int k = 1; k < 5; k++) l->results[k] = res[k];
    l->hero_pid = hero;

    for (int i = 0; i < np; i++) {
        int idx = dw_ladder_member_for_pid(l, pend[i].pid);
        if (idx < 0) {
            idx = dw_ladder_free_member(l);
            if (idx < 0) continue;   /* full: a smaller ladder than the file, not a crash */
            l->count++;
        }
        DwLadderMember *m = &l->members[idx];
        m->active = 1;
        m->pid = pend[i].pid;
        m->is_player = pend[i].is_player;
        m->path = pend[i].path;
        m->aggression = pend[i].aggression;
        m->wins = pend[i].wins;
        m->games = pend[i].games;
        /* the deck was never saved: it is rebuilt from the record and the meta, which is exactly the
           rule the living ladder follows after every match */
        dw_ladder_craft_deck(l, idx, now);
    }

    /* the player's seat must be on the ladder even if the file predates it (hero_pid set, no member) */
    if (l->hero_pid >= 0 && dw_ladder_member_for_pid(l, l->hero_pid) < 0)
        dw_ladder_add_player(l, l->hero_pid, now);
    return 1;
}

void dw_ladder_tick(DwLadder *l, unsigned int now) {
    if (!l) return;
    l->now_ms = now;
    for (int i = 0; i < DW_LADDER_MAX_CHALLENGES; i++) {
        DwChallenge *c = &l->challenges[i];
        if (!c->active) continue;
        if ((int)(now - c->expires_ms) >= 0) c->active = 0;
        /* a challenge addressed to somebody who has left the world is dead too */
        else if (dw_ladder_member_for_pid(l, c->to_pid) < 0) c->active = 0;
    }

    if (l->count < 2) return;
    if (l->last_citizen_challenge_ms != 0 &&
        now - l->last_citizen_challenge_ms < DW_LADDER_CITIZEN_CHALLENGE_INTERVAL_MS) return;

    /* a citizen starts a game of its own. The hero is the more interesting opponent, so the citizen
     * picks them at 40% when they are there -- a citizens-only table is still a table either way. */
    int candidate = -1;
    int tries = DW_LADDER_MAX_CITIZENS;
    while (tries-- > 0) {
        int pick = (int)(dw_ladder_rng(l) % (uint32_t)DW_LADDER_MAX_CITIZENS);
        if (l->members[pick].active && !l->members[pick].is_player) { candidate = pick; break; }
    }
    if (candidate < 0) return;
    int target = -1;
    int want_hero = (l->hero_pid >= 0) && ((int)(dw_ladder_rng(l) % 100u) < 40);
    if (want_hero) {
        target = dw_ladder_member_for_pid(l, l->hero_pid);
    } else {
        int tries2 = DW_LADDER_MAX_CITIZENS;
        while (tries2-- > 0) {
            int pick = (int)(dw_ladder_rng(l) % (uint32_t)DW_LADDER_MAX_CITIZENS);
            if (!l->members[pick].active) continue;
            if (pick == candidate) continue;
            target = pick;
            break;
        }
    }
    if (target < 0) return;
    if (dw_ladder_challenge(l, l->members[candidate].pid, l->members[target].pid, now) >= 0)
        l->last_citizen_challenge_ms = now;
}

const char *dw_challenge_kind_name(int kind) {
    switch (kind) {
        case DW_CHAL_CITIZEN_CITIZEN: return "citizen-vs-citizen";
        case DW_CHAL_CITIZEN_PLAYER:  return "citizen-vs-player";
        case DW_CHAL_PLAYER_CITIZEN:  return "player-vs-citizen";
        case DW_CHAL_PLAYER_PLAYER:   return "player-vs-player";
        default: return "none";
    }
}
