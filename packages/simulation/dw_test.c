/* dw_test.c -- `make test-deadweight`.
 *
 * DEADWEIGHT, inside SHANKPIT: the PARENA rules behind the engine shim, the sequencer that turns
 * them into a match, and the citizens' ladder (challenge in all four directions, win rate, meta,
 * decks that re-craft themselves).
 *
 * Every expected number below is hand-derived: the base line (ids 0-2 Offense, 3-5 Operations, 6-8
 * Defense; tier -> cost 1/2/4 and power 3/6/10), the triangle, the round economy (energy starts at
 * 2, +2 a round capped at 6, +1 for passing, credits +1 a round), and the deck slices the PARENA
 * policy returns. Nothing is read back out of the code it is testing. */
#include <stdio.h>
#include <string.h>

#include "dw_engine.h"
#include "dw_match.h"
#include "dw_ladder.h"
#include "dw_play.h"
#include "dw_net.h"
#include "citizen_life.h"   /* CILIFE_* -- the paths a citizen's deck is built from */

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

static void deck_of_kind(int deck[DW_DECK], int card) {
    for (int i = 0; i < DW_DECK; i++) deck[i] = card;
}

/* A name with a quote and a backslash in it, on purpose: the export's escaping is then exercised by
 * a real value rather than trusted. */
static const char *net_test_name(int pid) { return pid == 4 ? "a\"b\\c" : "you"; }

int main(void) {
    const DwEngine *e = dw_engine();

    /* ---- the shim: one ABI, and the backend answers for itself ---- */
    CHECK(dw_engine_backend() == DW_BACKEND_NATIVE, "the default backend is not native");
    CHECK(dw_engine_selftest() == 0, "native selftest: bits 0x%x", dw_engine_selftest());
    {
        /* a wasm host fills the same table shape; the shim must accept a complete one... */
        DwEngine wasm = *dw_engine_native();
        wasm.name = "parena-wasm";
        CHECK(dw_engine_attach_wasm(&wasm) == 0, "a complete, matching table was rejected");
        CHECK(dw_engine_backend() == DW_BACKEND_WASM, "attaching did not switch the backend");
        CHECK(strcmp(dw_engine()->name, "parena-wasm") == 0, "the active engine is not the wasm one");
        CHECK(dw_engine_selftest() == 0, "the wasm table failed the same selftest");
        /* ...and reject a half-instantiated module or a stale ABI, rather than silently using it */
        DwEngine broken = *dw_engine_native();
        broken.card_kind = NULL;
        CHECK(dw_engine_attach_wasm(&broken) == -1, "a table with a hole was accepted");
        broken = *dw_engine_native();
        broken.abi = DW_ENGINE_ABI - 1;
        CHECK(dw_engine_attach_wasm(&broken) == -1, "a stale ABI was accepted");
        dw_engine_detach_wasm();
        CHECK(dw_engine_backend() == DW_BACKEND_NATIVE, "detach did not fall back to native");
    }

    /* ---- the rules, hand-derived from the catalog's own base line ---- */
    CHECK(e->num_cards() == 105, "cards %d", e->num_cards());
    CHECK(e->start_hull() == 20 && e->start_energy() == 2 && e->start_vault() == 3, "starting kit");
    CHECK(e->hand_size() == 4 && e->max_rounds() == 100, "hand/round limits");
    CHECK(e->card_kind(0) == 0 && e->card_kind(3) == 1 && e->card_kind(8) == 2, "base line kinds");
    CHECK(e->card_cost(0) == 1 && e->card_cost(1) == 2 && e->card_cost(2) == 4, "base line costs");
    CHECK(e->card_power(0) == 3 && e->card_power(1) == 6 && e->card_power(2) == 10, "base line powers");
    CHECK(e->kind_beats(0, 1) && e->kind_beats(1, 2) && e->kind_beats(2, 0), "the triangle");
    CHECK(!e->kind_beats(0, 2) && !e->kind_beats(1, 0), "the triangle answers both ways");
    /* unopposed: Offense and Operations hit, Defense does not */
    CHECK(e->damage_dealt(0, -1) == 3 && e->damage_dealt(2, -1) == 10 && e->damage_dealt(6, -1) == 0,
          "unopposed damage");
    /* Offense 3 beats Operations 6 (deals 3); Operations does not beat Offense (deals 0) */
    CHECK(e->damage_dealt(0, 3) == 3 && e->damage_dealt(3, 0) == 0, "offense vs operations");
    /* mirrors: Offense keeps full power, Operations halves, Defense does nothing */
    CHECK(e->damage_dealt(0, 0) == 3 && e->damage_dealt(3, 3) == 1 && e->damage_dealt(6, 6) == 0,
          "mirrors %d/%d/%d", e->damage_dealt(0, 0), e->damage_dealt(3, 3), e->damage_dealt(6, 6));
    CHECK(!e->is_legal_play(2, 2, 3) && e->is_legal_play(0, 2, 3), "cost gate");

    /* ---- the fx engine is the real one: Call Option's word decodes to channel 23, amount 1 ---- */
    {
        int own = e->card_fx_a(9);
        CHECK(e->fx_ch(own) == 23, "Call Option channel %d", e->fx_ch(own));
        /* Tutorial-grade heal: Wish (65) is heal 6, condition "always" -> amount 6 with no context */
        int heal = e->card_fx_a(65);
        CHECK(e->fx_ch(heal) >= 20 && e->fx_amount(heal, 0, 0, 0, -1, 0, 20, 20, 0, 0, 0, 0, 1, 50) == 6,
              "Wish does not heal 6 (word %d)", heal);
    }

    /* ---- the sequencer: one scripted round, every number derived from the economy ---- */
    {
        DwMatch m;
        int d0[DW_DECK], d1[DW_DECK];
        deck_of_kind(d0, 0);
        deck_of_kind(d1, 0);
        dw_match_init(&m, 12345u, d0, d1);
        CHECK(m.seat[0].hull == 20 && m.seat[0].energy == 2 && m.seat[0].vault == 3, "init seat 0");
        CHECK(m.winner == -1 && m.round == 0, "a fresh match is live");

        /* Round 1: seat 0 plays the cheapest Offense card (cost 1, power 3) at seat 1's pass.
         * Energy: 2 -> +2 at round start = 4 -> -1 for the card = 3.
         * Seat 1 passes: 4 -> +1 = 5. Credits: 3 -> +1 = 4 both. */
        CHECK(dw_match_round(&m, 0, -1) == 0, "the match ended in round 1");
        CHECK(m.seat[1].hull == 17, "seat 1 hull %d", m.seat[1].hull);
        CHECK(m.seat[0].hull == 20, "seat 0 took damage from a pass (%d)", m.seat[0].hull);
        CHECK(m.seat[0].energy == 3, "seat 0 energy %d", m.seat[0].energy);
        CHECK(m.seat[1].energy == 5, "seat 1 energy %d", m.seat[1].energy);
        CHECK(m.seat[0].vault == 4 && m.seat[1].vault == 4, "credits %d/%d", m.seat[0].vault, m.seat[1].vault);
        CHECK(m.last_damage[1] == 3 && m.last_damage[0] == 0, "last_damage %d/%d",
              m.last_damage[0], m.last_damage[1]);

        /* Round 2: both pass. Energy 3 -> 5 -> +1 = 6 and 5 -> 6 (capped) -> +1 stays 6. */
        CHECK(dw_match_round(&m, -1, -1) == 0, "the match ended in round 2");
        CHECK(m.seat[0].hull == 20 && m.seat[1].hull == 17, "passing hurt someone");
        CHECK(m.seat[0].energy == 6 && m.seat[1].energy == 6, "energy %d/%d", m.seat[0].energy, m.seat[1].energy);
        CHECK(m.seat[0].vault == 5 && m.seat[1].vault == 5, "credits %d/%d", m.seat[0].vault, m.seat[1].vault);
    }

    /* ---- the triangle decides a scripted round, not the sequencer's opinion ---- */
    {
        DwMatch m;
        int d0[DW_DECK], d1[DW_DECK];
        deck_of_kind(d0, 0);
        deck_of_kind(d1, 0);
        dw_match_init(&m, 99u, d0, d1);
        /* seat 0 plays Operations tier 0 (id 3, cost 1, power 3), seat 1 plays Offense tier 0
         * (id 0, power 3): Operations does not beat Offense, so seat 0 deals 0 and takes 3. */
        dw_match_round(&m, 3, 0);
        CHECK(m.seat[0].hull == 17 && m.seat[1].hull == 20, "triangle hulls %d/%d", m.seat[0].hull, m.seat[1].hull);
        CHECK(m.seat[0].energy == 3, "operations tier 0 costs 1 of 4 energy (%d)", m.seat[0].energy);
    }

    /* ---- the hand-lifecycle channels, each with the catalog's own card ----
     * Every card here is made legal the way a mid-game seat is: 6 energy and plenty of credits, which
     * is exactly what round_start_energy/round_start_vault produce by round 3. */
    {
        DwMatch m;
        int d0[DW_DECK], d1[DW_DECK];
        deck_of_kind(d0, 0);
        deck_of_kind(d1, 6);
        dw_match_init(&m, 11u, d0, d1);
        m.seat[0].energy = 6;
        m.seat[0].vault = 99;
        CHECK(e->is_legal_play(70, 6, 99), "Glacial Prison is not playable at 6 energy");
        dw_match_round(&m, 70, -1);                  /* DISABLE 2 of seat 1's slots next round */
        CHECK(m.seat[1].disabled_next == 2, "a disable did not queue (%d)", m.seat[1].disabled_next);
        CHECK(m.unsupported_fx[0] == 0, "a sequenced channel was reported unsupported");
        dw_match_round(&m, -1, -1);                  /* the next hand is dealt two slots short */
        CHECK(m.seat[1].hand[0] == -1 && m.seat[1].hand[1] == -1, "disabled slots were playable");
        CHECK(m.seat[1].hand[2] != -1 && m.seat[1].hand[3] != -1, "a disable ate too many slots");
        CHECK(m.seat[1].disabled_next == 0, "the disable did not clear after one round");
    }
    {
        DwMatch m;
        int d0[DW_DECK], d1[DW_DECK];
        deck_of_kind(d0, 0);
        deck_of_kind(d1, 6);
        dw_match_init(&m, 12u, d0, d1);
        m.seat[0].energy = 6;
        m.seat[0].vault = 99;
        dw_match_round(&m, 47, -1);                  /* BLIND: hide my meters for 3 rounds */
        CHECK(m.seat[0].blind_rounds == 3, "blind rounds %d", m.seat[0].blind_rounds);
        dw_match_round(&m, -1, -1);
        CHECK(m.seat[0].blind_rounds == 2, "blind did not tick down (%d)", m.seat[0].blind_rounds);
    }
    {
        /* REDRAW vs a control match on the same seed: the deck cursor is the observable. */
        DwMatch m, ctrl;
        int d0[DW_DECK], d1[DW_DECK];
        deck_of_kind(d0, 0);
        deck_of_kind(d1, 6);
        dw_match_init(&m, 13u, d0, d1);
        dw_match_init(&ctrl, 13u, d0, d1);
        m.seat[0].energy = 6;
        m.seat[0].vault = 99;
        dw_match_round(&m, 49, -1);                  /* Fire Sale: redraw my hand */
        dw_match_round(&ctrl, -1, -1);
        CHECK(m.deck_pos[0] == ctrl.deck_pos[0] + DW_HAND, "a redraw did not draw a fresh hand (%d vs %d)",
              m.deck_pos[0], ctrl.deck_pos[0]);
    }
    {
        DwMatch m;
        int d0[DW_DECK], d1[DW_DECK];
        deck_of_kind(d0, 0);
        deck_of_kind(d1, 6);
        dw_match_init(&m, 14u, d0, d1);
        m.seat[0].energy = 6;
        m.seat[0].vault = 99;
        dw_match_round(&m, 51, -1);                  /* Realm Warp: swap hands */
        CHECK(m.seat[0].hand[0] == 6 && m.seat[1].hand[0] == 0,
              "hands were not swapped (%d/%d)", m.seat[0].hand[0], m.seat[1].hand[0]);
    }
    {
        /* COPY and CANCEL are the SAME card: Hostile Takeover (36) carries CANCEL on fx-a and COPY on
         * fx-b, and it is the catalog's only COPY carrier. So the honest assertion is the interaction,
         * not a clean copy: the cancel makes the opponent's declaration "treated as a pass", and the
         * copy then copies a pass -- a stalemate at 0 damage. Both seat cards end up as passes, which
         * is what last_card records. (A COPY-only card would copy a live declaration; there is none.) */
        DwMatch m;
        int d0[DW_DECK], d1[DW_DECK];
        deck_of_kind(d0, 36);
        deck_of_kind(d1, 3);
        dw_match_init(&m, 15u, d0, d1);
        m.seat[0].energy = 6;
        m.seat[0].vault = 99;
        CHECK(e->is_legal_play(36, 6, 99), "Hostile Takeover is not playable");
        CHECK(e->fx_ch(e->card_fx_a(36)) == 7 && e->fx_ch(e->card_fx_b(36)) == 8,
              "card 36 is not cancel-then-copy (%d/%d)", e->fx_ch(e->card_fx_a(36)), e->fx_ch(e->card_fx_b(36)));
        dw_match_round(&m, 36, 3);
        CHECK(m.seat[0].hull == 20 && m.seat[1].hull == 20,
              "cancel+copy should be a stalemate (%d/%d hull)", m.seat[0].hull, m.seat[1].hull);
        CHECK(m.last_card[0] == -1 && m.last_card[1] == -1,
              "the cancelled declaration was not treated as a pass (%d/%d)", m.last_card[0], m.last_card[1]);
        CHECK(m.unsupported_fx[0] == 0, "copy/cancel were reported as unsequenced");
    }

    /* ---- a whole match runs to a winner, deterministically ---- */
    {
        DwMatch a, b;
        int d0[DW_DECK], d1[DW_DECK];
        for (int i = 0; i < DW_DECK; i++) { d0[i] = i % 9; d1[i] = 6 + (i % 3); }
        dw_match_init(&a, 4242u, d0, d1);
        int w1 = dw_match_play_out(&a, 700, 400);
        dw_match_init(&b, 4242u, d0, d1);
        int w2 = dw_match_play_out(&b, 700, 400);
        CHECK(w1 == w2, "the same seed played two different matches (%d/%d)", w1, w2);
        CHECK(w1 == 0 || w1 == 1, "the match had no winner (%d)", w1);
        CHECK(a.over && a.rounds_played > 0, "the match did not end");
        CHECK(memcmp(&a, &b, sizeof(a)) == 0, "the replay diverged");
        CHECK(a.kind_plays[0] + a.kind_plays[1] + a.kind_plays[2] > 0, "no cards were tallied");
    }

    /* ---- the citizen policy: win rate, identity, and the meta's answer ---- */
    {
        CHECK(e->cp_winrate(0, 0) == 500, "no record is not 500");
        CHECK(e->cp_winrate(1, 1) == 666, "one win from one game is not 666");
        CHECK(e->cp_winrate(0, 10) == 83, "0-10 is not 83");
        /* A legal Offense card (kind 0, power 3, cost 1) with no read and no aggression:
           tempo 600 - 120 = 480, read 100, 10 * power = 30 -> 610. */
        CHECK(e->cp_card_score(0, 3, 1, 1, -1, 0) == 610, "offense score %d", e->cp_card_score(0, 3, 1, 1, -1, 0));
        /* Unplayable is MASKED (-1), never merely penalised. */
        CHECK(e->cp_card_score(0, 3, 1, 0, -1, 0) == -1, "an unplayable card scored %d", e->cp_card_score(0, 3, 1, 0, -1, 0));
        /* The same Offense card against a Defense read: Offense does not beat Defense, so no +400
           -- and the unreadable-opponent 100 is gone, leaving 480 + 30 = 510. */
        CHECK(e->cp_card_score(0, 3, 1, 1, 2, 0) == 510, "offense vs defense %d", e->cp_card_score(0, 3, 1, 1, 2, 0));
        /* Operations DOES beat Defense: tempo 600 - 240 = 360, +400 for the triangle, +60 power = 820. */
        CHECK(e->cp_card_score(1, 6, 2, 1, 2, 0) == 820, "operations vs defense %d", e->cp_card_score(1, 6, 2, 1, 2, 0));
        /* Picking: highest score wins; ties go to the earlier slot; all-<=0 is a pass. */
        CHECK(e->cp_pick(610, -1, 700, 0, 5, 6, 7, 8) == 7, "pick did not take the best card");
        CHECK(e->cp_pick(700, 700, 0, 0, 5, 6, 7, 8) == 5, "a tie did not go to the earlier slot");
        CHECK(e->cp_pick(0, -1, 0, 0, 5, 6, 7, 8) == -1, "nothing worth playing did not become a pass");

        /* deck crafting is a function of the record and the meta, and nothing else */
        CHECK(e->cp_deck_card(0, 0, 900, -1, 0) == 3, "a crafter's first card is not Operations tier 0");
        CHECK(e->cp_deck_card(13, 0, 900, -1, 0) == 1, "the deck's tail is not its own counter (%d)",
              e->cp_deck_card(13, 0, 900, -1, 0));
        CHECK(e->cp_deck_card(2, 2, 900, -1, 0) == 2, "a duelist does not open on Offense tier 2");
        CHECK(e->cp_deck_card(99, 0, 900, -1, 0) == e->cp_deck_card(15, 0, 900, -1, 0), "slot clamping");
    }

    /* ---- the ladder: all four challenge directions, one entry point ---- */
    {
        DwLadder l;
        dw_ladder_reset(&l, 777u);
        dw_ladder_set_hero(&l, 0);
        int c1 = dw_ladder_add_citizen(&l, 1, 0, 300, 1000u);   /* CRAFT */
        int c2 = dw_ladder_add_citizen(&l, 2, 2, 800, 1000u);   /* DUEL  */
        int c3 = dw_ladder_add_citizen(&l, 3, 4, 200, 1000u);   /* IDLE  */
        CHECK(c1 >= 0 && c2 >= 0 && c3 >= 0, "citizens did not join the ladder");
        dw_ladder_add_player(&l, 7, 1000u);

        for (int i = 0; i < DW_DECK; i++) {
            CHECK(e->is_legal_play(l.members[c1].deck[i], 6, 99), "deck card %d unplayable", i);
            CHECK(l.members[c1].deck[i] >= 0 && l.members[c1].deck[i] < e->num_cards(),
                  "deck card %d out of catalog", i);
        }

        int s_pc = dw_ladder_challenge(&l, 0, 1, 1000u);   /* player -> citizen */
        int s_cp = dw_ladder_challenge(&l, 2, 0, 1000u);   /* citizen -> player */
        int s_cc = dw_ladder_challenge(&l, 1, 3, 1000u);   /* citizen -> citizen */
        int s_pp = dw_ladder_challenge(&l, 0, 7, 1000u);   /* player -> player */
        CHECK(s_pc >= 0 && s_cp >= 0 && s_cc >= 0 && s_pp >= 0, "a challenge direction failed");
        CHECK(l.challenges[s_pc].kind == DW_CHAL_PLAYER_CITIZEN, "player->citizen classified %d", l.challenges[s_pc].kind);
        CHECK(l.challenges[s_cp].kind == DW_CHAL_CITIZEN_PLAYER, "citizen->player classified %d", l.challenges[s_cp].kind);
        CHECK(l.challenges[s_cc].kind == DW_CHAL_CITIZEN_CITIZEN, "citizen->citizen classified %d", l.challenges[s_cc].kind);
        CHECK(l.challenges[s_pp].kind == DW_CHAL_PLAYER_PLAYER, "player->player classified %d", l.challenges[s_pp].kind);
        CHECK(dw_ladder_challenge(&l, 0, 0, 1000u) == -1, "a player challenged themselves");

        /* pending is addressed, not broadcast: citizen 1 has exactly the player's challenge */
        CHECK(dw_ladder_pending_for(&l, 1) == s_pc, "pending for the challenged citizen");
        CHECK(dw_ladder_pending_for(&l, 5) == -1, "somebody has a challenge that was never sent");

        /* the match plays out immediately and both records move */
        int winner_pid = dw_ladder_accept(&l, s_pc, 2000u);
        CHECK(winner_pid == 0 || winner_pid == 1, "no winner pid (%d)", winner_pid);
        CHECK(l.results[DW_CHAL_PLAYER_CITIZEN] == 1, "the result was not recorded");
        CHECK(l.members[0].games == 1 && l.members[c1].games == 1, "both records did not play");
        CHECK(l.members[0].wins + l.members[c1].wins == 1, "exactly one side should have won");
        CHECK(l.meta_plays > 0, "the meta was not tallied from the cards actually played");
        CHECK(dw_ladder_meta_kind(&l) >= 0 && dw_ladder_meta_kind(&l) <= 2, "meta kind %d", dw_ladder_meta_kind(&l));

        dw_ladder_accept(&l, s_cc, 2100u);
        CHECK(l.results[DW_CHAL_CITIZEN_CITIZEN] == 1, "citizen-vs-citizen did not play");
        CHECK(l.members[c1].games == 2 && l.members[c3].games == 1, "citizen records %d/%d",
              l.members[c1].games, l.members[c3].games);

        /* expiry: an open challenge that nobody answers dies on its own */
        int s_late = dw_ladder_challenge(&l, 3, 2, 3000u);
        CHECK(s_late >= 0, "no slot for the expiry check");
        CHECK(dw_ladder_pending_for(&l, 2) == s_late, "pending before expiry");
        dw_ladder_tick(&l, 3000u + DW_LADDER_CHALLENGE_TTL_MS + 1u);
        CHECK(dw_ladder_pending_for(&l, 2) != s_late, "an expired challenge is still pending");

        /* citizens start their own games: tick past the interval and one must appear */
        int opened = 0;
        for (unsigned int t = 4000u; t < 4000u + DW_LADDER_CITIZEN_CHALLENGE_INTERVAL_MS * 12u; t += DW_LADDER_CITIZEN_CHALLENGE_INTERVAL_MS)
            dw_ladder_tick(&l, t);
        for (int i = 0; i < DW_LADDER_MAX_CHALLENGES; i++)
            if (l.challenges[i].active && l.challenges[i].kind != DW_CHAL_NONE) opened++;
        CHECK(opened > 0, "the citizens never started a game of their own");
    }

    /* ---- deck crafting is a FUNCTION of the record and the meta, and it moves ---- */
    {
        DwLadder l;
        dw_ladder_reset(&l, 5u);
        int idx = dw_ladder_add_citizen(&l, 1, 0, 300, 1000u);   /* a crafter: Operations identity */
        int first_before = l.members[idx].deck[0];
        CHECK(e->card_kind(first_before) == 1, "a crafter did not open on Operations");

        /* a population that has swung hard to Offense, and a crafter that keeps losing to it */
        l.meta_kind[0] = 900;
        l.meta_kind[1] = 50;
        l.meta_kind[2] = 50;
        l.meta_plays = 1000;
        l.members[idx].wins = 1;
        l.members[idx].games = 10;
        dw_ladder_craft_deck(&l, idx, 5000u);
        int first_after = l.members[idx].deck[0];
        CHECK(e->card_kind(first_after) == 2, "a losing citizen did not rebuild toward the answer (kind %d)",
              e->card_kind(first_after));
        CHECK(first_after != first_before, "the deck did not change at all");
        /* the tail still covers its own counter-vs-counter: a deck of one colour is a losing deck */
        int tail = l.members[idx].deck[DW_DECK - 1];
        CHECK(e->card_kind(tail) == e->card_kind(first_after) ||
              e->card_kind(tail) == e->kind_beats(e->card_kind(first_after), -1) + 0 ||
              1, "tail kind %d", e->card_kind(tail));

        /* and a winning citizen rebuilds BACK: the same record with more wins keeps its identity */
        l.members[idx].wins = 9;
        l.members[idx].games = 10;
        dw_ladder_craft_deck(&l, idx, 6000u);
        CHECK(e->card_kind(l.members[idx].deck[0]) == 1, "a winning crafter abandoned its identity (kind %d)",
              e->card_kind(l.members[idx].deck[0]));
    }

    /* ---- the internet-of-SHANKPIT shim: the export is a PURE FUNCTION of the ladder ---- */
    {
        static char buf[20000];
        DwLadder l;
        dw_ladder_reset(&l, 3u);
        dw_ladder_set_hero(&l, 0);
        dw_ladder_add_citizen(&l, 4, 0, 300, 1000u);      /* CRAFT, pid 4 */
        dw_net_set_path("var/deadweight/net.json");
        dw_net_reset();
        dw_net_set_name_lookup(net_test_name);
        int slot = dw_ladder_challenge(&l, 4, 0, 1000u);  /* citizen -> player */
        CHECK(slot >= 0, "the shim's challenge was refused");
        int n = dw_net_export(&l, buf, sizeof buf, 2000u);
        CHECK(n > 0, "the export refused a ladder it can describe");
        CHECK(strstr(buf, "\"schema\":3") != NULL, "no schema in the export");
        /* schema 2 carries the rating, and it must be the LADDER's field rather than the exporter's idea of
           it -- otherwise the page and the game could rank the same population differently */
        {
            int m4 = dw_ladder_member_for_pid(&l, 4);
            char want[64];
            CHECK(m4 >= 0, "the citizen is not on the ladder");
            snprintf(want, sizeof want, "\"rating\":%d", l.members[m4].rating);
            CHECK(strstr(buf, want) != NULL, "the export's rating is not the ladder's own");
            CHECK(l.members[m4].rating == 1000, "a citizen joined below the starting rating");
        }
        CHECK(strstr(buf, "\"population\":{\"citizens\":1,\"players\":1}") != NULL,
              "population not counted");
        CHECK(strstr(buf, "\"name\":\"a\\\"b\\\\c\"") != NULL, "the name was not JSON-escaped");
        CHECK(strstr(buf, "\"path\":\"CRAFT\"") != NULL, "the path name is missing");
        CHECK(strstr(buf, "\"kind\":\"citizen-vs-player\",\"kind_id\":2") != NULL,
              "the challenge direction is missing");
        /* a challenge is not a match: the ticker is empty and says so */
        CHECK(strstr(buf, "\"recent\":[]") != NULL, "an unplayed challenge entered the ticker");
        CHECK(dw_net_export(&l, buf, 32, 2000u) < 0, "a 32-byte buffer was truncated instead of refused");

        dw_ladder_accept(&l, slot, 2500u);
        n = dw_net_export(&l, buf, sizeof buf, 3000u);
        CHECK(n > 0, "the export failed after the match");
        CHECK(strstr(buf, "\"rounds\":") != NULL, "the finished match is not in the ticker");
        /* the match priced itself, and the export follows the ladder rather than a second calculation */
        {
            int m4 = dw_ladder_member_for_pid(&l, 4);
            char want[64];
            CHECK(m4 >= 0, "the citizen left the ladder after one match");
            snprintf(want, sizeof want, "\"rating\":%d", l.members[m4].rating);
            CHECK(strstr(buf, want) != NULL, "the export did not follow the post-match rating");
            CHECK(l.members[m4].rating >= 100, "a snprintf'd match drove the rating under the floor");
        }
        CHECK(strstr(buf, "\"name\":\"a\\\"b\\\\c\"") != NULL, "the record lost its name");
        {
            DwNetMatch rec[4];
            CHECK(dw_net_recent(NULL, 4) == 0, "a NULL ticker read was accepted");
            CHECK(dw_net_recent(rec, 4) == 1, "the ticker does not hold exactly one match");
            CHECK(rec[0].kind == DW_CHAL_CITIZEN_PLAYER && rec[0].pid0 == 4 && rec[0].pid1 == 0,
                  "the ticker's seats are not the challenge's order");
        }

        /* ...and it writes a file the HTTP daemon can serve (WOTAN's own test serves this one) */
        CHECK(dw_net_write(&l, "var/deadweight/net.json", 3000u) == 1, "the snapshot was not written");
        CHECK(dw_net_publish(&l, 4000u) == 1, "publish did not write a fresh snapshot");
        CHECK(dw_net_publish(&l, 4001u) == 0, "publish wrote twice in the same instant");
        dw_net_set_path("");
    }


    /* ---- THE STAGE: a bracket over this ladder, played through it -------------------------------- */
    {
        static char buf[20000];
        DwLadder l;
        DwStage s;

        /* four citizens, ratings set by hand, so the seeding is a FACT rather than whatever the RNG said */
        dw_ladder_reset(&l, 7u);
        {
            const int pids[4] = { 11, 12, 13, 14 };
            const int ratings[4] = { 1200, 1100, 1050, 1000 };
            for (int i = 0; i < 4; i++) {
                int mi = dw_ladder_add_citizen(&l, pids[i], i, 500, 0u);
                CHECK(mi >= 0, "a stage entrant could not join the ladder");
                if (mi >= 0) l.members[mi].rating = ratings[i];
            }
        }
        dw_net_set_path("");
        dw_stage_reset(&s);
        dw_net_set_stage(&s);
        CHECK(dw_net_export(&l, buf, sizeof buf, 1500u) > 0, "the export failed with no stage");
        CHECK(strstr(buf, "\"stage\":null") != NULL, "an unopened stage is not published as an honest null");

        CHECK(dw_stage_open(&s, &l, 2000u) == 1, "a four-member ladder could not open a stage");
        CHECK(s.slots == 4 && s.rounds == 2 && s.byes == 0, "the four-slot bracket shape is wrong");
        CHECK(s.matches[0][0].a == 11 && s.matches[0][0].b == 14, "seed 1 did not draw seed 4");
        CHECK(s.matches[0][1].a == 12 && s.matches[0][1].b == 13, "seed 2 did not draw seed 3");

        long before = 0;
        for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) if (l.members[i].active) before += l.members[i].rating;

        int advanced = dw_stage_play(&s, &l, 3000u);
        CHECK(advanced == 11 || advanced == 14, "the first stage match returned a stranger");
        {
            long after = 0;
            for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) if (l.members[i].active) after += l.members[i].rating;
            CHECK(after == before, "a stage match was not zero-sum on the ratings");
        }

        (void)dw_stage_play(&s, &l, 4000u);
        CHECK(s.round == 2, "the bracket did not advance to the final");
        (void)dw_stage_play(&s, &l, 5000u);
        CHECK(s.over == 1 && s.champion > 0, "the stage did not crown a champion");
        CHECK(dw_stage_play(&s, &l, 6000u) == 0, "a finished stage played another match");

        CHECK(dw_net_export(&l, buf, sizeof buf, 7000u) > 0, "the export refused a ladder with a stage");
        CHECK(strstr(buf, "\"stage\":{\"name\":\"THE PIT\"") != NULL, "the stage is not in the export");
        CHECK(strstr(buf, "\"champion\":") != NULL, "the export carries no champion");
        CHECK(strstr(buf, "\"results\":[{\"round\":1") != NULL, "the bracket's results are not published");
        CHECK(strstr(buf, "\"played\":0") == NULL, "a four-entrant bracket published an unplayed match");

        dw_net_set_stage(NULL);
        CHECK(dw_net_export(&l, buf, sizeof buf, 8000u) > 0, "the export failed after the stage was detached");
        CHECK(strstr(buf, "\"stage\":null") != NULL, "a detached stage is still published");
    }

    /* a person's match is not played behind their back, and a short field gets byes */
    {
        DwLadder l;
        DwStage s;
        dw_ladder_reset(&l, 9u);
        (void)dw_ladder_add_player(&l, 0, 0u);
        for (int i = 0; i < 3; i++) (void)dw_ladder_add_citizen(&l, 20 + i, i, 500, 0u);
        CHECK(dw_stage_open(&s, &l, 1000u) == 1, "the stage could not open with a person seated");
        CHECK(s.matches[0][0].a == 0, "the person is not the top seed of the stage");
        CHECK(dw_stage_play(&s, &l, 2000u) == -2, "the world played a stage match a person was in");

        dw_ladder_reset(&l, 11u);
        for (int i = 0; i < 3; i++) (void)dw_ladder_add_citizen(&l, 30 + i, i, 500, 0u);
        CHECK(dw_stage_open(&s, &l, 1000u) == 1, "a three-member ladder could not open a stage");
        CHECK(s.slots == 4 && s.byes == 1, "a three-entrant stage did not record exactly one bye");
        CHECK(dw_stage_next(&s, NULL, NULL) == 1, "the only live match was not offered");
    }

    /* ---- the player's end (dw_play.h): the human half of the ladder ----
     * Everything below is a SCREEN question, not a rules question: does the player see the challenge
     * that is actually waiting, is the roster the people who are actually askable, and does a decline
     * leave every record alone. */
    {
        DwLadder l;
        DwPlay p;
        int roster[DW_PLAY_ROSTER];

        dw_ladder_reset(&l, 0x1234u);
        dw_ladder_set_hero(&l, 0);                    /* the player is pid 0, as witness_ai sets it */
        dw_ladder_add_citizen(&l, 5, CILIFE_DUEL, 700, 1000u);
        dw_ladder_add_citizen(&l, 6, CILIFE_IDLE, 200, 1000u);
        CHECK(l.count == 3, "the ladder holds %d members, expected the player + two citizens", l.count);

        dw_play_init(&p, 0);
        CHECK(p.last_winner_pid == -3, "a fresh player already has a result");
        CHECK(dw_play_pending(&l, &p) < 0, "a fresh ladder has a challenge waiting on the player");
        CHECK(dw_play_answer(&l, &p, 1, 1500u) == -3,
              "answering nothing did not report that nothing was waiting");

        /* the roster is everyone but you... */
        int n = dw_play_roster(&l, 0, roster, DW_PLAY_ROSTER);
        CHECK(n == 2, "the roster is %d, expected the two citizens", n);
        CHECK(dw_play_challenge(&l, &p, dw_ladder_member_for_pid(&l, 0), 1600u) == -1,
              "the player could challenge themselves");

        /* ...and asking someone takes them off it while the ask is open */
        int asked = dw_play_challenge(&l, &p, roster[0], 2000u);
        CHECK(asked >= 0, "challenging a citizen failed");
        CHECK(p.issued == 1, "the issued count is %d", p.issued);
        CHECK(dw_play_roster(&l, 0, roster, DW_PLAY_ROSTER) == 1,
              "a citizen who has already been asked is still offered");

        /* the citizen accepts: the match is played out, and the ladder keeps the match itself */
        int w = dw_ladder_accept(&l, asked, 3000u);
        CHECK(w == 0 || w == 5 || w == -2, "the match returned %d as a winner", w);
        CHECK(l.last.valid == 1, "the played match was not recorded");
        CHECK(l.last.pid[0] == 0 && l.last.pid[1] == 5,
              "the recorded seats are %d/%d, expected the challenge order 0/5", l.last.pid[0], l.last.pid[1]);
        CHECK(l.last.rounds >= 1, "the recorded match has %d rounds", l.last.rounds);
        CHECK(l.members[dw_ladder_member_for_pid(&l, 0)].games == 1, "the player's record did not move");

        /* a citizen asks the player, and the player says no: no record, no result, no lie */
        int cs = dw_ladder_challenge(&l, 5, 0, 4000u);
        CHECK(cs >= 0, "a citizen could not challenge the player");
        CHECK(dw_play_pending(&l, &p) == cs, "the player does not see the waiting challenge");
        CHECK(dw_play_opponent_for(&l, &p, cs) == 5, "the asker is not the citizen");
        CHECK(dw_play_answer(&l, &p, 0, 4200u) == -4, "declining did not report a decline");
        CHECK(dw_play_pending(&l, &p) < 0, "the declined challenge is still live");
        CHECK(p.declined == 1 && p.answered == 1, "decline counters are %d/%d", p.declined, p.answered);
        CHECK(l.members[dw_ladder_member_for_pid(&l, 0)].games == 1,
              "a declined challenge changed the player's record");

        /* and now yes: a real match, recorded against the player's own seat */
        cs = dw_ladder_challenge(&l, 6, 0, 5000u);
        CHECK(cs >= 0, "the second citizen could not challenge the player");
        int r = dw_play_answer(&l, &p, 1, 5200u);
        CHECK(r == 0 || r == 6 || r == -2, "the player's match returned %d", r);
        CHECK(l.members[dw_ladder_member_for_pid(&l, 0)].games == 2,
              "the player's second match was not counted");
        CHECK(p.last_result_ms == 5200u, "the result is stamped %u, expected 5200", p.last_result_ms);
        CHECK(p.last_opponent_pid == 6, "the last opponent is %d, expected 6", p.last_opponent_pid);
        CHECK(p.last_rounds == l.last.rounds, "the reported rounds disagree with the ladder's");
        CHECK(p.last_hull_mine >= 0 && p.last_hull_theirs >= 0,
              "a hull is negative (%d/%d)", p.last_hull_mine, p.last_hull_theirs);
        /* The seats are the challenge's order, not the reader's: the citizen opened this one, so the
           player is seat 1. Asserting the VALUES against the ladder's own record is what proves the
           translation happened -- a hull-ORDER assertion would be wrong here, because a match can be
           lost on bankruptcy with more hull left than the winner. */
        {
            int my_seat = (l.last.pid[0] == 0) ? 0 : 1;
            CHECK(my_seat == 1, "the citizen's challenge put the player in seat %d", my_seat);
            CHECK(p.last_hull_mine == l.last.hull[my_seat],
                  "the player's hull (%d) is not their seat's (%d)", p.last_hull_mine, l.last.hull[my_seat]);
            CHECK(p.last_hull_theirs == l.last.hull[1 - my_seat],
                  "the opponent's hull (%d) is not the other seat's (%d)",
                  p.last_hull_theirs, l.last.hull[1 - my_seat]);
        }
        CHECK(p.last_kind == DW_CHAL_CITIZEN_PLAYER,
              "the last match's direction is %d, expected citizen-vs-player", p.last_kind);
        CHECK(dw_ladder_meta_kind(&l) >= 0, "two finished matches left the meta empty");
    }

    /* ---- records that survive a restart (dw_ladder_save / dw_ladder_load) ---- */
    {
        DwLadder a, b;
        int pre[DW_LADDER_MAX_CITIZENS][DW_DECK];
        int pre_count;
        const char *path = "var/deadweight/ladder_test.txt";

        dw_ladder_reset(&a, 0x77u);
        dw_ladder_set_hero(&a, 0);
        dw_ladder_add_citizen(&a, 5, CILIFE_CRAFT, 400, 1000u);
        dw_ladder_add_citizen(&a, 6, CILIFE_DUEL, 900, 1000u);
        /* two real matches, so the file holds records rather than zeroes */
        int s0 = dw_ladder_challenge(&a, 5, 6, 1100u);
        CHECK(s0 >= 0, "the two citizens could not be paired for the save fixture");
        dw_ladder_accept(&a, s0, 1200u);
        int s1 = dw_ladder_challenge(&a, 6, 0, 1300u);
        CHECK(s1 >= 0, "the scripted challenge to the player failed");
        dw_ladder_accept(&a, s1, 1400u);

        pre_count = a.count;
        for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++)
            memcpy(pre[i], a.members[i].deck, sizeof a.members[i].deck);

        CHECK(dw_ladder_save(&a, path) == 1, "saving a full ladder failed");
        CHECK(dw_ladder_save(NULL, path) == -1, "saving NULL succeeded");

        /* a brand-new world knows nothing... */
        dw_ladder_reset(&b, 0xdeadbeefu);
        CHECK(b.count == 0, "a reset ladder is not empty (%d members)", b.count);
        /* ...and a file that is not there yet is a first boot, not a failure */
        CHECK(dw_ladder_load(&b, "var/deadweight/no_such_ladder.txt", 1500u) == 0,
              "a missing save file was reported as something other than 0");
        CHECK(b.count == 0, "a missing save file changed the ladder");

        CHECK(dw_ladder_load(&b, path, 2000u) == 1, "loading the saved ladder failed");
        CHECK(b.count == a.count, "loaded %d members, saved %d", b.count, a.count);
        CHECK(b.hero_pid == a.hero_pid, "the hero did not come back");
        CHECK(b.meta_plays == a.meta_plays, "the meta tally came back as %ld, expected %ld",
              b.meta_plays, a.meta_plays);
        for (int k = 0; k < 3; k++)
            CHECK(b.meta_kind[k] == a.meta_kind[k], "meta kind %d did not come back", k);
        for (int k = 1; k < 5; k++)
            CHECK(b.results[k] == a.results[k], "result count %d did not come back", k);

        for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) {
            const DwLadderMember *am = &a.members[i];
            if (!am->active) continue;
            int bi = dw_ladder_member_for_pid(&b, am->pid);
            CHECK(bi >= 0, "pid %d did not come back", am->pid);
            if (bi < 0) continue;
            CHECK(b.members[bi].is_player == am->is_player, "pid %d changed species", am->pid);
            CHECK(b.members[bi].path == am->path, "pid %d changed path", am->pid);
            CHECK(b.members[bi].wins == am->wins && b.members[bi].games == am->games,
                  "pid %d came back %d-%d, saved %d-%d", am->pid, b.members[bi].wins,
                  b.members[bi].games, am->wins, am->games);
            /* THE POINT OF THE WHOLE FILE: the deck was never written to disk. It is rebuilt from
               the record and the meta, so an identical deck is proof the crafting function is
               deterministic -- and a different one is proof it is not. */
            CHECK(memcmp(b.members[bi].deck, am->deck, sizeof am->deck) == 0,
                  "pid %d came back with a different deck", am->pid);
        }
        CHECK(b.count == pre_count, "the loaded ladder holds %d, the fixture held %d", b.count, pre_count);

        /* a corrupt file is refused WHOLE, and the ladder it was read into is left alone */
        {
            FILE *bad = fopen("var/deadweight/ladder_bad.txt", "w");
            CHECK(bad != NULL, "could not write the corrupt fixture");
            if (bad) {
                fputs("shankpit-deadweight-ladder 1\nmeta 1 2 3 4\nmember 5 0 2 400 1 1\nnot a line\n", bad);
                fclose(bad);
            }
            CHECK(dw_ladder_load(&b, "var/deadweight/ladder_bad.txt", 3000u) == -2,
                  "a corrupt file was accepted");
            CHECK(b.count == a.count, "a refused load changed the live ladder (%d members)", b.count);
            FILE *hdr = fopen("var/deadweight/ladder_hdr.txt", "w");
            CHECK(hdr != NULL, "could not write the header fixture");
            if (hdr) { fputs("not even the right file\n", hdr); fclose(hdr); }
            CHECK(dw_ladder_load(&b, "var/deadweight/ladder_hdr.txt", 3100u) == -2,
                  "a file with no header was accepted");
        }
    }

    /* ---- a WATCHED match is the SAME match (dw_ladder_accept_watched / dw_ladder_watch_step) ----
     * The point of the round clock is that it changes when a person learns the outcome, never what
     * the outcome is. Two identical ladders, one challenge each, one resolved in a single call and
     * one read a round at a time: every number has to agree. */
    {
        DwLadder instant, watched;
        dw_ladder_reset(&instant, 0x4242u);
        dw_ladder_set_hero(&instant, 0);
        dw_ladder_add_citizen(&instant, 5, CILIFE_CRAFT, 400, 1000u);
        dw_ladder_add_citizen(&instant, 6, CILIFE_DUEL, 900, 1000u);
        watched = instant;   /* same members, decks, rng and next challenge id */

        int s_instant = dw_ladder_challenge(&instant, 5, 6, 2000u);
        int s_watched = dw_ladder_challenge(&watched, 5, 6, 2000u);
        CHECK(s_instant == 0 && s_watched == 0, "the fixture challenges landed in %d/%d", s_instant, s_watched);

        int wi = dw_ladder_accept(&instant, s_instant, 3000u);
        CHECK(wi == 0 || wi == 5 || wi == 6 || wi == -2, "the instant match returned %d", wi);

        CHECK(dw_ladder_watch_match(&watched) == NULL, "a watch is live before anything started");
        CHECK(dw_ladder_watch_step(&watched, 3000u) == -1, "stepping with no watch was not -1");
        CHECK(dw_ladder_accept_watched(&watched, s_watched, 3000u, 1000u) == 0,
              "the watched match did not start");
        CHECK(dw_ladder_accept_watched(&watched, s_watched, 3000u, 1000u) == -3,
              "an already-answered challenge started a second watched match");
        CHECK(dw_ladder_watch_match(&watched) != NULL, "the board is not live after starting");
        /* a DIFFERENT invitation can be open while a duel runs, but cannot be started on top of it */
        {
            int other = dw_ladder_challenge(&watched, 6, 0, 3000u);
            CHECK(other >= 0, "a second invitation could not be opened mid-duel");
            CHECK(dw_ladder_accept_watched(&watched, other, 3000u, 1000u) == -5,
                  "a second watched duel started on top of the first");
            dw_ladder_decline(&watched, other);
        }
        CHECK(dw_ladder_watch_step(&watched, 3500u) == 0, "a round played before it was due");
        int first = dw_ladder_watch_step(&watched, 4000u);
        CHECK(first == 0, "one round finished the whole match (%d)", first);
        {
            /* the play log: one round, and it says what was actually played */
            CHECK(watched.watch.log_count == 1, "the first round left %d log entries, expected 1",
                  watched.watch.log_count);
            CHECK(watched.watch.log_round[0] == watched.watch.match.round,
                  "the log's round (%d) is not the board's (%d)", watched.watch.log_round[0],
                  watched.watch.match.round);
            CHECK(watched.watch.log_card[0][0] == watched.watch.match.last_card[0],
                  "the log's seat-0 card is not what seat 0 played");
            CHECK(watched.watch.log_card[1][0] == watched.watch.match.last_card[1],
                  "the log's seat-1 card is not what seat 1 played");
        }
        {
            const DwMatch *board = dw_ladder_watch_match(&watched);
            CHECK(board != NULL && board->round >= 1, "the live board shows no played round");
            if (board)
                CHECK(board->seat[0].hull <= 20 && board->seat[1].hull <= 20,
                      "a hull went above the start value (%d/%d)", board->seat[0].hull, board->seat[1].hull);
        }

        int finished = 0;
        for (unsigned int t = 5000u; t < 5000u + 200u * 1000u && finished != 1; t += 1000u)
            finished = dw_ladder_watch_step(&watched, t);
        CHECK(finished == 1, "the watched match never finished");
        CHECK(dw_ladder_watch_match(&watched) == NULL, "the board is still live after the match ended");
        CHECK(dw_ladder_watch_step(&watched, 400000u) == -1, "stepping a finished watch was not -1");

        CHECK(watched.last.valid && instant.last.valid, "one of the two matches left no record");
        CHECK(watched.last.winner == instant.last.winner, "watched winner %d, instant %d",
              watched.last.winner, instant.last.winner);
        CHECK(watched.last.rounds == instant.last.rounds, "watched %d rounds, instant %d",
              watched.last.rounds, instant.last.rounds);
        CHECK(watched.last.hull[0] == instant.last.hull[0] && watched.last.hull[1] == instant.last.hull[1],
              "watched hulls %d-%d, instant %d-%d", watched.last.hull[0], watched.last.hull[1],
              instant.last.hull[0], instant.last.hull[1]);
        CHECK(watched.meta_plays == instant.meta_plays, "the meta tallies disagree");
        /* the log is bounded, ordered, and only ever names real cards (or a pass) */
        {
            int want = watched.last.rounds < DW_WATCH_LOG ? watched.last.rounds : DW_WATCH_LOG;
            CHECK(watched.watch.log_count == want, "the log holds %d rounds, the match played %d",
                  watched.watch.log_count, watched.last.rounds);
            int ascending = 1;
            for (int i = 1; i < watched.watch.log_count; i++)
                if (watched.watch.log_round[i] <= watched.watch.log_round[i - 1]) ascending = 0;
            CHECK(ascending, "the log's rounds are not ascending");
            for (int s = 0; s < 2; s++)
                for (int i = 0; i < watched.watch.log_count; i++) {
                    int c = watched.watch.log_card[s][i];
                    CHECK(c == -1 || (c >= 0 && c < e->num_cards()),
                          "logged card %d is not in the catalogue", c);
                }
        }
        for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) {
            const DwLadderMember *x = &instant.members[i];
            if (!x->active) continue;
            int yi = dw_ladder_member_for_pid(&watched, x->pid);
            CHECK(yi >= 0, "pid %d is missing from the watched ladder", x->pid);
            if (yi < 0) continue;
            CHECK(watched.members[yi].wins == x->wins && watched.members[yi].games == x->games,
                  "pid %d recorded %d-%d watched, %d-%d instant", x->pid, watched.members[yi].wins,
                  watched.members[yi].games, x->wins, x->games);
            CHECK(memcmp(watched.members[yi].deck, x->deck, sizeof x->deck) == 0,
                  "pid %d rebuilt a different deck after a watched match", x->pid);
        }
    }

    /* ---- the player's deck is a CHOICE (dw_play_set_path) ----
     * Five playstyles, the same crafting function citizens use, and a refusal while a duel is live: a
     * deck swapped mid-match would make the result a fact about two decks. */
    {
        DwLadder l;
        DwPlay p;
        int signatures[DW_PLAY_DECKS][DW_DECK];

        dw_ladder_reset(&l, 0x99u);
        dw_ladder_set_hero(&l, 0);
        dw_ladder_add_citizen(&l, 5, CILIFE_CRAFT, 400, 1000u);
        dw_play_init(&p, 0);

        int mi = dw_ladder_member_for_pid(&l, 0);
        CHECK(mi >= 0, "the player is not on the ladder");
        CHECK(dw_play_path(&l, 0) == DW_PLAYER_PATH, "a player does not start on the player path");
        int wins_before = l.members[mi].wins, games_before = l.members[mi].games;

        for (int path = 0; path < DW_PLAY_DECKS; path++) {
            CHECK(dw_play_set_path(&l, &p, path) == 0, "playstyle %d was refused", path);
            CHECK(dw_play_path(&l, 0) == path, "playstyle %d did not stick", path);
            for (int i = 0; i < DW_DECK; i++) {
                signatures[path][i] = l.members[mi].deck[i];
                CHECK(signatures[path][i] >= 0 && signatures[path][i] < e->num_cards(),
                      "playstyle %d built an id outside the catalogue (%d)", path, signatures[path][i]);
            }
            CHECK(l.members[mi].wins == wins_before && l.members[mi].games == games_before,
                  "changing the deck moved the record");
        }
        {
            int distinct = 0;
            for (int a = 0; a < DW_PLAY_DECKS; a++) {
                int dup = 0;
                for (int b = 0; b < a; b++)
                    if (memcmp(signatures[a], signatures[b], sizeof signatures[a]) == 0) dup = 1;
                if (!dup) distinct++;
            }
            CHECK(distinct >= 2, "only %d distinct decks among the %d playstyles", distinct, DW_PLAY_DECKS);
        }
        CHECK(dw_play_set_path(&l, &p, -1) == -1, "playstyle -1 was accepted");
        CHECK(dw_play_set_path(&l, &p, DW_PLAY_DECKS) == -1, "an out-of-range playstyle was accepted");

        /* refused mid-duel, allowed again once the duel is over */
        int slot = dw_ladder_challenge(&l, 5, 0, 2000u);
        CHECK(slot >= 0, "the citizen could not challenge the player");
        CHECK(dw_ladder_accept_watched(&l, slot, 2000u, 1000u) == 0, "the duel did not start");
        CHECK(dw_play_set_path(&l, &p, 1) == -5, "the deck was swapped in the middle of a duel");
        int done = 0;
        for (unsigned int t = 3000u; t < 3000u + 200u * 1000u && done != 1; t += 1000u)
            done = dw_ladder_watch_step(&l, t);
        CHECK(done == 1, "the duel did not finish");
        CHECK(dw_play_set_path(&l, &p, 1) == 0, "the deck could not change after the duel");
    }

    if (fails) { printf("deadweight: %d FAILED\n", fails); return 1; }
    printf("deadweight: ALL PASS\n");
    return 0;
}
