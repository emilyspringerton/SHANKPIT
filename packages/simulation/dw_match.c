/* dw_match.c -- see dw_match.h. Every number comes from the engine (dw_engine.h); this file owns
 * only the ORDER effects happen in, which is the thing DEADWEIGHT's own core/match.c owns. */

#include "dw_match.h"
#include "dw_engine.h"

#include <string.h>

/* The phase-1 channels this sequencer handles. Anything else is a GUARD case: it sets a bit in
 * unsupported_fx rather than being silently skipped, so a future card cannot quietly play a
 * different game. */
static int dw_phase1_channel(int ch) {
    return ch == 1 || ch == 2 || ch == 3 || ch == 4 || ch == 5 || ch == 6 || ch == 7 || ch == 8 ||
           ch == 9 || ch == 22;
}

static uint32_t dw_next_rng(DwMatch *m) {
    m->rng = m->rng * 1664525u + 1013904223u;
    return m->rng;
}

static int dw_draw_hand(DwMatch *m, int s) {
    const DwEngine *e = dw_engine();
    int n = e->hand_size();
    if (n > DW_HAND) n = DW_HAND;
    for (int i = 0; i < n; i++) {
        if (m->deck_pos[s] >= DW_DECK) m->deck_pos[s] = 0;   /* deterministic reshuffle */
        m->seat[s].hand[i] = m->deck[s][m->deck_pos[s]++];
    }
    for (int i = n; i < DW_HAND; i++) m->seat[s].hand[i] = -1;
    return n;
}

void dw_match_init(DwMatch *m, uint32_t seed, const int deck0[DW_DECK], const int deck1[DW_DECK]) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
    const DwEngine *e = dw_engine();
    for (int s = 0; s < 2; s++) {
        m->seat[s].hull = e->start_hull();
        m->seat[s].energy = e->start_energy();
        m->seat[s].vault = e->start_vault();
        m->round_start_hull[s] = m->seat[s].hull;
        for (int i = 0; i < DW_DECK; i++) m->deck[s][i] = s == 0 ? deck0[i] : deck1[i];
        m->deck_pos[s] = 0;
        m->last_card[s] = -1;
        /* "no hand yet" is -1 everywhere, never 0: a zero-filled slot is a REAL card id (the cheapest
         * Offense card), which is how a policy reading a pre-deal hand used to play a card nobody held. */
        for (int i = 0; i < DW_HAND; i++) m->seat[s].hand[i] = -1;
    }
    m->winner = -1;
    m->rng = seed ? seed : 0x9e3779b9u;
}

/* ---- effect application ------------------------------------------------------------------------ */

/* One seat's view of the round, so the fx condition engine can be asked about it. */
typedef struct {
    int card;       /* this seat's declared card, -1 = pass */
    int opp_card;
    int dealt;      /* damage this seat deals, before armor */
    int taken;      /* damage this seat takes, before armor */
    int energy_left;
    int ignore_armor;   /* channel 02 */
    int true_damage;    /* channel 22 */
    int immune;         /* channel 04 */
    int lifeline;       /* channel 05 */
    int cancel;         /* channel 07: the opponent's card is treated as a pass */
    int halve;          /* channel 09 */
    int reflect;        /* channel 06 */
    int bypass_floor;   /* channel 03 */
    int bypass_no_reflect;
    int copy;           /* channel 08: play the opponent's declared card instead */
} DwSeatFx;

static int dw_fx_amount_for(const DwMatch *m, const DwSeatFx *f, int s, int word) {
    const DwEngine *e = dw_engine();
    int opp = 1 - s;
    return e->fx_amount(word, f->dealt, f->taken,
                        f->card >= 0 ? e->card_kind(f->card) : -1,
                        f->opp_card >= 0 ? e->card_kind(f->opp_card) : -1,
                        f->opp_card >= 0 ? e->card_cost(f->opp_card) : 0,
                        m->seat[s].hull, m->seat[opp].hull,
                        f->energy_left, m->seat[opp].energy,
                        m->seat[s].vault, m->seat[opp].vault,
                        m->round, m->roll);
}

/* Phase 1. Pass A runs before damage is computed (cancel/halve/immune/lifeline/bypass), pass B
 * after it (bonus/pierce/reflect/true) -- which is what lets a cancel change the damage rather than
 * being outvoted by it. */
static void dw_phase1(DwMatch *m, DwSeatFx fx[2], int pass) {
    const DwEngine *e = dw_engine();
    for (int s = 0; s < 2; s++) {
        if (fx[s].card < 0) continue;
        for (int w = 0; w < 2; w++) {
            int word = w == 0 ? e->card_fx_a(fx[s].card) : e->card_fx_b(fx[s].card);
            if (word == 0) continue;
            int ch = e->fx_ch(word);
            if (e->fx_phase(ch) != 1) continue;
            if (!dw_phase1_channel(ch)) { m->unsupported_fx[s] |= 1 << (ch % 32); continue; }
            int amt = dw_fx_amount_for(m, &fx[s], s, word);
            if (pass == 0) {
                if (ch == 7 && amt > 0) fx[s].cancel = 1;
                if (ch == 9 && amt > 0) fx[s].halve = 1;
                if (ch == 4 && amt > 0) fx[s].immune = 1;
                if (ch == 5 && amt > 0) fx[s].lifeline = 1;
                if (ch == 3 && amt > fx[s].bypass_floor) fx[s].bypass_floor = amt;
                if (ch == 8 && amt > 0) fx[s].copy = 1;
            } else {
                if (ch == 1 && amt > 0) fx[s].dealt += amt;
                if (ch == 2 && amt > 0) fx[s].ignore_armor = 1;
                if (ch == 6 && amt > 0) fx[s].reflect += amt;
                if (ch == 22 && amt > 0) fx[s].true_damage = 1;
                if (ch == 3 && amt > 0) fx[s].bypass_no_reflect = 1;
            }
        }
    }
}

/* Phase 2: everything that changes the seats rather than the exchange. */
static void dw_phase2(DwMatch *m, const DwSeatFx fx[2]) {
    const DwEngine *e = dw_engine();
    for (int s = 0; s < 2; s++) {
        int opp = 1 - s;
        if (fx[s].card < 0) continue;
        for (int w = 0; w < 2; w++) {
            int word = w == 0 ? e->card_fx_a(fx[s].card) : e->card_fx_b(fx[s].card);
            if (word == 0) continue;
            int ch = e->fx_ch(word);
            if (e->fx_phase(ch) != 2) continue;
            int amt = dw_fx_amount_for(m, &fx[s], s, word);
            if (amt <= 0 && ch != 43) continue;
            switch (ch) {
                case 20: { int h = m->seat[s].hull + amt; m->seat[s].hull = h > e->start_hull() ? e->start_hull() : h; } break;
                case 21: m->seat[s].hull -= amt; break;
                case 23: { int en = m->seat[s].energy + amt; m->seat[s].energy = en > 6 ? 6 : en; } break;
                case 24: { int en = m->seat[opp].energy - amt; m->seat[opp].energy = en < 0 ? 0 : en; } break;
                case 25: m->seat[s].pending_energy += amt; break;
                case 26: m->seat[s].pending_energy -= amt; break;
                case 27: m->seat[opp].pending_energy -= amt; break;
                case 28: m->seat[s].armor += amt; break;
                case 29: m->seat[s].vault += amt; break;
                case 30: m->seat[s].vault -= amt; break;
                case 31: m->seat[opp].vault -= amt; break;
                case 32: m->seat[opp].burn += amt / 16; m->seat[opp].burn_rounds += amt % 16; break;
                case 33: m->seat[s].regen += amt / 16; m->seat[s].regen_rounds += amt % 16; break;
                case 34: { int n = m->seat[opp].disabled_next + amt;
                           m->seat[opp].disabled_next = n > DW_HAND ? DW_HAND : n; } break;
                case 35: m->seat[s].blind_rounds += amt; break;
                case 36: { int tmp[DW_HAND];
                           memcpy(tmp, m->seat[s].hand, sizeof(tmp));
                           memcpy(m->seat[s].hand, m->seat[opp].hand, sizeof(tmp));
                           memcpy(m->seat[opp].hand, tmp, sizeof(tmp)); } break;
                case 37: dw_draw_hand(m, s); break;
                case 38: { int left = amt;
                           for (int i = 0; i < DW_HAND && left > 0; i++)
                               if (m->seat[opp].hand[i] >= 0) { m->seat[opp].hand[i] = -1; left--; }
                         } break;
                case 39: { /* equalize hulls: both to the average, rounded down */
                    int avg = (m->seat[0].hull + m->seat[1].hull) / 2;
                    m->seat[0].hull = avg; m->seat[1].hull = avg;
                } break;
                case 40: /* chronobreak: back to the hull held at the start of this round */
                    if (m->round_start_hull[s] > 0) m->seat[s].hull = m->round_start_hull[s];
                    break;
                case 41: { int use = m->seat[s].energy < amt ? m->seat[s].energy : amt;
                           m->seat[s].energy -= use; m->seat[s].armor += use; } break;
                case 42: m->seat[s].burn = 0; m->seat[s].burn_rounds = 0; break;
                case 43: if (m->seat[s].vault < 0) m->seat[s].vault = 0; break;
                default: m->unsupported_fx[s] |= 1 << (ch % 32); break;
            }
        }
    }
}

static void dw_end_of_round_dots(DwMatch *m) {
    for (int s = 0; s < 2; s++) {
        int b = 0;
        if (m->seat[s].burn_rounds > 0 && m->seat[s].burn > 0) {
            b = m->seat[s].burn;
            m->seat[s].burn_rounds--;
            if (m->seat[s].burn_rounds <= 0) { m->seat[s].burn_rounds = 0; m->seat[s].burn = 0; }
        }
        m->last_burn[s] = b;
        m->seat[s].hull -= b;
        if (m->seat[s].regen_rounds > 0 && m->seat[s].regen > 0) {
            int h = m->seat[s].hull + m->seat[s].regen;
            m->seat[s].hull = h > dw_engine()->start_hull() ? dw_engine()->start_hull() : h;
            m->seat[s].regen_rounds--;
            if (m->seat[s].regen_rounds <= 0) { m->seat[s].regen_rounds = 0; m->seat[s].regen = 0; }
        }
    }
}

/* BEGIN: the clock, the roll, the economy, the deal and DISABLE. Split from the exchange so a policy
 * can choose from the hand it will actually play -- see the header for the bug this fixes. */
static int dw_round_begin(DwMatch *m) {
    if (!m) return 1;
    if (m->over) return 1;
    if (m->started) return 1;   /* already begun: the missing call is FINISH, not another BEGIN */
    const DwEngine *e = dw_engine();
    if (m->round >= e->max_rounds()) { m->over = 1; m->winner = e->round_winner(m->seat[0].hull, m->seat[1].hull, m->seat[0].vault, m->seat[1].vault); return 1; }

    m->round++;
    m->rounds_played++;
    m->roll = (int)(dw_next_rng(m) % 100u);

    /* Start of round: energy refills and banks, credits accrue, hands are dealt, chronobreak
     * remembers the hull it has to be able to restore. */
    for (int s = 0; s < 2; s++) {
        int en = e->round_start_energy(m->seat[s].energy) + m->seat[s].pending_energy;
        m->seat[s].pending_energy = 0;
        m->seat[s].energy = en < 0 ? 0 : en;
        if (m->seat[s].energy > 6) m->seat[s].energy = 6;
        m->seat[s].vault = e->round_start_vault(m->seat[s].vault);
        m->round_start_hull[s] = m->seat[s].hull;
        dw_draw_hand(m, s);
        /* DISABLE: the slots the opponent's last card cost me. Applied AFTER the deal, because the
         * point of the mechanic is that you were dealt a hand you cannot fully use. */
        if (m->seat[s].disabled_next > 0) {
            int n = m->seat[s].disabled_next;
            if (n > DW_HAND) n = DW_HAND;
            for (int i = 0; i < n; i++) m->seat[s].hand[i] = -1;
            m->seat[s].disabled_next = 0;
        }
        if (m->seat[s].blind_rounds > 0) m->seat[s].blind_rounds--;
    }

    m->started = 1;
    return 0;
}

/* FINISH: the declared cards, every effect channel, and the end of the match. */
static int dw_round_finish(DwMatch *m, int card0, int card1) {
    const DwEngine *e = dw_engine();
    m->started = 0;

    int card[2];
    card[0] = card0;
    card[1] = card1;
    for (int s = 0; s < 2; s++) {
        card[s] = e->card_substitute(card[s], m->roll);
        if (card[s] >= 0 && !e->is_legal_play(card[s], m->seat[s].energy, m->seat[s].vault))
            card[s] = -1;   /* unplayable is a pass, never a different card */
    }
    for (int s = 0; s < 2; s++) {
        int credit = card[s] >= 0 ? e->card_credit(card[s]) : 0;
        m->seat[s].energy = e->energy_after_play(m->seat[s].energy, card[s]);
        m->seat[s].vault -= credit;
        if (card[s] >= 0) m->kind_plays[e->card_kind(card[s])]++;
    }

    DwSeatFx fx[2];
    memset(fx, 0, sizeof(fx));
    for (int s = 0; s < 2; s++) {
        fx[s].card = card[s];
        fx[s].opp_card = card[1 - s];
        fx[s].energy_left = m->seat[s].energy;
    }

    /* Pass A: cancels and flags, before any damage exists. */
    dw_phase1(m, fx, 0);
    for (int s = 0; s < 2; s++) {
        if (fx[s].cancel) {
            fx[1 - s].card = -1;
            fx[1 - s].opp_card = fx[s].card;
            card[1 - s] = -1;
        }
    }

    /* COPY (08): play the opponent's declared card instead. Resolved AFTER the cancels, because a
     * cancel makes the opponent's card "treated as a pass" and a copy of a pass is a pass. */
    for (int s = 0; s < 2; s++) {
        if (!fx[s].copy) continue;
        fx[s].card = fx[1 - s].card;
        card[s] = fx[s].card;
        fx[1 - s].opp_card = fx[s].card;
    }

    /* The exchange, as the rules compute it (triangle only; riders are the fx words). */
    for (int s = 0; s < 2; s++) fx[s].dealt = e->damage_dealt(card[s], card[1 - s]);
    for (int s = 0; s < 2; s++) fx[s].taken = fx[1 - s].dealt;

    /* Pass B: the riders that change the damage itself. */
    dw_phase1(m, fx, 1);

    int dealt[2];
    for (int s = 0; s < 2; s++) {
        int d = fx[s].dealt;
        if (fx[s].bypass_floor > d) d = fx[s].bypass_floor;   /* channel 03: at least AA lands */
        dealt[s] = d;
    }
    for (int s = 0; s < 2; s++) {
        int d = dealt[s];
        if (fx[1 - s].immune) d = 0;                   /* the receiver is immune: nothing lands */
        if (!fx[s].ignore_armor && !fx[s].true_damage && d > 0) {
            int use = m->seat[1 - s].armor < d ? m->seat[1 - s].armor : d;
            m->seat[1 - s].armor -= use;
            d -= use;
        }
        if (fx[0].halve || fx[1].halve) d /= 2;
        m->last_damage[1 - s] = d;                      /* damage seat s deals is TAKEN by 1-s */
        m->seat[1 - s].hull -= d;
        /* reflect returns damage to the attacker */
        if (fx[1 - s].reflect > 0 && !fx[s].bypass_no_reflect) {
            m->seat[s].hull -= fx[1 - s].reflect;
            m->last_damage[s] += fx[1 - s].reflect;
        }
    }

    dw_phase2(m, fx);
    dw_end_of_round_dots(m);

    /* Lifeline: survive lethal at 1 hull (checked after everything that could have killed it). */
    for (int s = 0; s < 2; s++) {
        if (fx[s].lifeline && m->seat[s].hull <= 0) m->seat[s].hull = 1;
    }

    m->last_card[0] = card[0];
    m->last_card[1] = card[1];

    /* ---- the match ends by hull, by bankruptcy, or by the round cap ---- */
    int b0 = e->bankrupt(m->seat[0].vault);
    int b1 = e->bankrupt(m->seat[1].vault);
    if (b0 || b1) {
        m->over = 1;
        m->winner = (b0 && b1) ? e->round_winner(m->seat[0].hull, m->seat[1].hull, m->seat[0].vault, m->seat[1].vault)
                               : (b0 ? 1 : 0);
        return 1;
    }
    if (e->match_decided(m->seat[0].hull, m->seat[1].hull)) {
        m->over = 1;
        m->winner = e->round_winner(m->seat[0].hull, m->seat[1].hull, m->seat[0].vault, m->seat[1].vault);
        return 1;
    }
    if (m->round >= e->max_rounds()) {
        m->over = 1;
        m->winner = e->round_winner(m->seat[0].hull, m->seat[1].hull, m->seat[0].vault, m->seat[1].vault);
        return 1;
    }
    return 0;
}

/* Both halves in one call, for a scripted caller (a replay, a test, a UI that has already taken its
 * clicks). The deal-then-resolve order is the game's, so a scripted round and a policy round agree. */
int dw_match_round(DwMatch *m, int card0, int card1) {
    if (dw_round_begin(m)) return 1;
    return dw_round_finish(m, card0, card1);
}

int dw_match_step_policy(DwMatch *m, int aggression0, int aggression1) {
    /* BEGIN first: the policy has to see the hand of the round it is playing. */
    if (dw_round_begin(m)) return 1;
    const DwEngine *e = dw_engine();
    int pick[2];
    int aggr[2] = { aggression0, aggression1 };
    for (int s = 0; s < 2; s++) {
        int opp_kind = m->last_card[1 - s] >= 0 ? e->card_kind(m->last_card[1 - s]) : -1;
        int sc[DW_HAND];
        for (int i = 0; i < DW_HAND; i++) {
            int c = m->seat[s].hand[i];
            sc[i] = c < 0 ? 0
                    : e->cp_card_score(e->card_kind(c), e->card_power(c), e->card_cost(c),
                                       e->is_legal_play(c, m->seat[s].energy, m->seat[s].vault),
                                       opp_kind, aggr[s]);
        }
        pick[s] = e->cp_pick(sc[0], sc[1], sc[2], sc[3],
                             m->seat[s].hand[0], m->seat[s].hand[1], m->seat[s].hand[2],
                             m->seat[s].hand[3]);
    }
    return dw_round_finish(m, pick[0], pick[1]);
}

int dw_match_play_out(DwMatch *m, int aggression0, int aggression1) {
    if (!m) return -1;
    const DwEngine *e = dw_engine();
    int guard = e->max_rounds() + 1;
    while (!m->over && guard-- > 0)
        if (dw_match_step_policy(m, aggression0, aggression1)) break;
    return m->over ? m->winner : -1;
}

int dw_match_seat_for_pid(int seat0_pid, int pid) {
    return pid == seat0_pid ? 0 : 1;
}
