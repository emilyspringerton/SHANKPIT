#ifndef DW_ENGINE_H
#define DW_ENGINE_H

/* dw_engine.h -- the PARENA WASM ENGINE SHIM: DEADWEIGHT's rules behind ONE C ABI.
 *
 * DEADWEIGHT's entire rules layer is already PARENA -- PARENA/stdlib/deadweight/card_rules.prn (105
 * cards, the kind triangle, every damage/energy/vault number, the packed fx engine) and
 * stdlib/deadweight/citizen_policy.prn (card choice + deck crafting). Those two files compile two
 * ways: to native C (what SHANKPIT links) and to a wasm32 module (what a browser or the Android
 * WebView loads -- `PARENA/scripts/build_d2_wasm.sh` is the proven toolchain, zero imports, no
 * malloc). This header is the seam between the game and whichever of those is running.
 *
 * WHAT A SHIM IS FOR, honestly: SHANKPIT has no wasm interpreter in-tree, so today the active
 * backend is always NATIVE -- the same PARENA source, emitted to C by the same compiler. The seam
 * still earns its keep, because it is what makes the wasm path a DROP-IN instead of a fork:
 *   * one ABI, so a caller (the match sequencer, the ladder, a test) never knows which is running;
 *   * `dw_engine_attach_wasm` rejects an ABI mismatch or a table with a null entry, so a
 *     half-instantiated module cannot silently answer zeroes;
 *   * `dw_engine_selftest` asks the live backend the numbers it claims to own, so "the wasm engine
 *     is running" is a checked fact, not a hope.
 *
 * The function table is deliberately FLAT and INTEGER-ONLY: wasm32 has no struct ABI we can rely on
 * across the boundary, and every one of these is already an I32->I32 function in the PARENA source.
 * Adding an entry means bumping DW_ENGINE_ABI, which is what stops an old module being accepted by
 * a newer host. */

#include <stdint.h>

#define DW_ENGINE_ABI 2   /* v2: the citizen policy layer (win rate, pick, deck crafting) */

typedef struct DwEngine {
    int abi;
    const char *name;

    /* ---- rules (stdlib/deadweight/card_rules.prn) ---- */
    int (*num_cards)(void);
    int (*card_kind)(int id);
    int (*card_cost)(int id);
    int (*card_power)(int id);
    int (*card_credit)(int id);
    int (*card_keyword)(int id);
    int (*card_tier)(int id);
    int (*kind_beats)(int a, int b);
    int (*is_legal_play)(int id, int energy, int vault);
    int (*damage_dealt)(int a, int b);
    int (*energy_after_play)(int e, int played);
    int (*round_start_energy)(int e);
    int (*round_start_vault)(int v);
    int (*round_winner)(int h0, int h1, int v0, int v1);
    int (*match_decided)(int h0, int h1);
    int (*bankrupt)(int v);
    int (*card_substitute)(int id, int roll);
    int (*card_fx_a)(int id);
    int (*card_fx_b)(int id);
    int (*fx_amount)(int x, int dealt, int taken, int mk, int ok, int oc, int mh, int oh,
                     int el, int oe, int mv, int ov, int rnd, int roll);
    int (*fx_ch)(int x);
    int (*fx_phase)(int ch);
    int (*start_hull)(void);
    int (*start_energy)(void);
    int (*start_vault)(void);
    int (*max_rounds)(void);
    int (*hand_size)(void);

    /* ---- the citizen layer (stdlib/deadweight/citizen_policy.prn) ---- */
    int (*cp_winrate)(int wins, int games);
    int (*cp_card_score)(int kind, int power, int cost, int legal, int opp_kind, int aggression);
    int (*cp_pick)(int s0, int s1, int s2, int s3, int h0, int h1, int h2, int h3);
    int (*cp_deck_card)(int slot, int path, int winrate, int meta_kind, int meta_share);
} DwEngine;

enum { DW_BACKEND_NONE = 0, DW_BACKEND_NATIVE = 1, DW_BACKEND_WASM = 2 };

/* The engine compiled into this binary: the PARENA source emitted to C. Always available. */
const DwEngine *dw_engine_native(void);

/* Hand the shim a wasm-backed table (a host that owns an interpreter fills one in). 0 = accepted,
 * -1 = rejected (wrong ABI, wrong size, or a null entry -- a module that cannot answer everything
 * is worse than the native engine it would replace). */
int dw_engine_attach_wasm(const DwEngine *table);
void dw_engine_detach_wasm(void);

/* Whichever backend is active. Never NULL. */
const DwEngine *dw_engine(void);
int dw_engine_backend(void);

/* Asks the ACTIVE backend the numbers it claims to answer. 0 = healthy, non-zero = the first
 * disagreement (a bit per check, see dw_engine.c), so a caller can log what failed rather than
 * trusting a bool. */
int dw_engine_selftest(void);

const char *dw_engine_backend_name(int backend);

#endif
