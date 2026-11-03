/* dw_engine.c -- see dw_engine.h. The native backend is the PARENA-generated C, wrapped in the
 * exact function-table shape a wasm host would fill in, so the two are interchangeable BY
 * CONSTRUCTION rather than by convention. */

#include "dw_engine.h"
#include "dw_rules_host.h"
#include "dw_citizen_host.h"

#include <stddef.h>

static const DwEngine DW_NATIVE = {
    DW_ENGINE_ABI,
    "parena-native",
    num_cards, card_kind, card_cost, card_power, card_credit, card_keyword, card_tier,
    kind_beats, is_legal_play, damage_dealt, energy_after_play, round_start_energy,
    round_start_vault, round_winner, match_decided, bankrupt, card_substitute,
    card_fx_a, card_fx_b, fx_amount, fx_ch, fx_phase,
    start_hull, start_energy, start_vault, max_rounds, hand_size,
    cp_winrate_permille, cp_card_score, cp_pick, cp_deck_card
};

/* A wasm module is instantiated, not guaranteed: every entry is checked, by name, rather than by
 * poking bytes, so a hole cannot be mistaken for a valid function. */

static const DwEngine *g_wasm = NULL;

const DwEngine *dw_engine_native(void) { return &DW_NATIVE; }

#define DW_REQUIRE_FN(f) do { if (!(t)->f) return 0; } while (0)

static int dw_engine_table_complete(const DwEngine *t) {
    if (!t) return 0;
    DW_REQUIRE_FN(num_cards); DW_REQUIRE_FN(card_kind); DW_REQUIRE_FN(card_cost);
    DW_REQUIRE_FN(card_power); DW_REQUIRE_FN(card_credit); DW_REQUIRE_FN(card_keyword);
    DW_REQUIRE_FN(card_tier); DW_REQUIRE_FN(kind_beats); DW_REQUIRE_FN(is_legal_play);
    DW_REQUIRE_FN(damage_dealt); DW_REQUIRE_FN(energy_after_play); DW_REQUIRE_FN(round_start_energy);
    DW_REQUIRE_FN(round_start_vault); DW_REQUIRE_FN(round_winner); DW_REQUIRE_FN(match_decided);
    DW_REQUIRE_FN(bankrupt); DW_REQUIRE_FN(card_substitute); DW_REQUIRE_FN(card_fx_a);
    DW_REQUIRE_FN(card_fx_b); DW_REQUIRE_FN(fx_amount); DW_REQUIRE_FN(fx_ch); DW_REQUIRE_FN(fx_phase);
    DW_REQUIRE_FN(start_hull); DW_REQUIRE_FN(start_energy); DW_REQUIRE_FN(start_vault);
    DW_REQUIRE_FN(max_rounds); DW_REQUIRE_FN(hand_size); DW_REQUIRE_FN(cp_winrate);
    DW_REQUIRE_FN(cp_card_score); DW_REQUIRE_FN(cp_pick); DW_REQUIRE_FN(cp_deck_card);
    return 1;
}

int dw_engine_attach_wasm(const DwEngine *table) {
    if (!table) return -1;
    if (table->abi != DW_ENGINE_ABI) return -1;
    if (!dw_engine_table_complete(table)) return -1;
    g_wasm = table;
    return 0;
}

void dw_engine_detach_wasm(void) { g_wasm = NULL; }

const DwEngine *dw_engine(void) { return g_wasm ? g_wasm : &DW_NATIVE; }

int dw_engine_backend(void) { return g_wasm ? DW_BACKEND_WASM : DW_BACKEND_NATIVE; }

const char *dw_engine_backend_name(int backend) {
    return backend == DW_BACKEND_WASM ? "wasm" : backend == DW_BACKEND_NATIVE ? "native" : "none";
}

int dw_engine_selftest(void) {
    const DwEngine *e = dw_engine();
    int bad = 0;
    if (!e) return 1 << 0;
    if (e->num_cards() != 105) bad |= 1 << 1;
    if (e->start_hull() != 20) bad |= 1 << 2;
    if (e->start_energy() != 2) bad |= 1 << 3;
    if (e->start_vault() != 3) bad |= 1 << 4;
    if (e->hand_size() != 4) bad |= 1 << 5;
    /* the kinds and costs of the base line: id 0 is the cheapest Offense card, id 8 the fattest
     * Defense one, and the triangle must answer in one direction only */
    if (e->card_kind(0) != 0 || e->card_kind(3) != 1 || e->card_kind(6) != 2) bad |= 1 << 6;
    if (e->card_cost(0) != 1 || e->card_power(0) != 3) bad |= 1 << 7;
    if (!e->kind_beats(0, 1) || e->kind_beats(1, 0)) bad |= 1 << 8;
    if (e->damage_dealt(0, -1) != e->card_power(0)) bad |= 1 << 9;
    return bad;
}
