#ifndef SHADOW_WAR_HOST_H
#define SHADOW_WAR_HOST_H
/* shadow_war_host.h -- BIG_O engine merge follow-up (EMILY/BACKLOG.md SECTION 536 follow-up,
 * "continue full game"): the basement's async shadow war, closing the one system
 * SHANKPIT docs2/specs/BIGO_ENGINE_MERGE_NORTHSTAR.md Sec.3 and BIG_O/NORTHSTAR.md Sec.1/4
 * both still named as genuinely unbuilt. Every rule (unit power, command multipliers, per-tick
 * damage, the deploy gate) is PARENA -- generated into shadow_war_rules.c from
 * PARENA/stdlib/big_o/shadow_war.prn (see that file's own header comment for why big_o/, not
 * shankpit/, despite BIGO_ENGINE_MERGE_NORTHSTAR.md Sec.4's "ownership going forward" note).
 * This file is the glue: army/battle data shapes, the
 * N-tick resolution loop, a deterministic bot, and Elo (plain host math on purpose -- see
 * shadow_war.prn's own header comment for why Elo specifically isn't PARENA).
 *
 * v0 scope, named plainly (same discipline every other phase in BIGO_ENGINE_MERGE_NORTHSTAR.md
 * already uses):
 *  - An army IS the player's current BP_CLONES roster (base/trait per clone) -- "quality" from
 *    SHIP_PLAN.md's own W1 sketch has no real source in the lab yet (no amplification/tiering
 *    mechanic on a spliced clone exists), so it is not modeled. Named gap, not guessed at.
 *  - One command (ADVANCE/HOLD/SCATTER) per side for the WHOLE battle, not a per-tick sequence --
 *    the real point of "async" is nobody has to be online to issue a second order mid-fight.
 *  - Aggregate army power only, no individual unit death/survival tracking -- a battle is two
 *    HP-pool-like numbers draining each other over N ticks, not a grid/positional sim (SHIP_PLAN's
 *    own "small grid" phrasing reads as the PHEROMONE PLACEMENT grid, a separate, not-yet-built
 *    concern, not the battle resolution itself -- see shadow_war_test.c's own header comment).
 *  - Bot opponent only this pass (BIG_O/NORTHSTAR.md Sec.4's own "bot opponent available" V0
 *    cut) -- real two-human-player async PvP needs cross-session persistence (IDUNA), which
 *    SHIP_PLAN.md itself scopes as separate Phase-2/IDUNA integration work, not W1's deliverable.
 *  - Deploying EXPENDS every current clone, win or lose -- no per-unit survivors return this
 *    version. A real, simple v1 rule, not an oversight.
 *  - Elo lives on Phone.shadow_war_elo (session-local, SHANKPIT-native, not yet persisted to
 *    IDUNA) -- same "real and tested but not yet wired to the live backend" shape phase 1's own
 *    day_night_clock carried before its later server-authoritative follow-up landed. */
#include "../common/phone.h"

#define SHADOW_WAR_MAX_UNITS BP_CLONES   /* an army is exactly the player's current clone roster */

enum { SHADOW_WAR_CMD_ADVANCE = 0, SHADOW_WAR_CMD_HOLD = 1, SHADOW_WAR_CMD_SCATTER = 2 };
enum { SHADOW_WAR_RESULT_LOSS = 0, SHADOW_WAR_RESULT_WIN = 1, SHADOW_WAR_RESULT_DRAW = 2 };

#define SHADOW_WAR_ELO_DEFAULT 1200
#define SHADOW_WAR_ELO_K 32

typedef struct {
    int base[SHADOW_WAR_MAX_UNITS];
    int trait[SHADOW_WAR_MAX_UNITS];
    int unit_count;
    int command;   /* SHADOW_WAR_CMD_*, fixed for the whole battle */
} ShadowWarArmy;

typedef struct {
    int result;                           /* SHADOW_WAR_RESULT_*, from army A's own side */
    int ticks_run;
    int a_power_start, b_power_start;
    int a_power_end, b_power_end;          /* both clamped >= 0 */
} ShadowWarBattle;

/* Deterministic N-tick resolution (shadow-war-max-ticks, 20). Both sides' commands are read once
 * up front. Each tick, damage is computed for BOTH directions from the SAME pre-tick power
 * values, then applied simultaneously -- order-independent, so a mirror battle (identical armies,
 * identical command) is a real, exact draw. Stops early if either side is eliminated
 * (shadow-war-side-eliminated). Never crashes on a malformed army: unit_count is clamped to
 * [0, SHADOW_WAR_MAX_UNITS] and any out-of-range base/trait is clamped to 0 before scoring. */
void shadow_war_resolve(const ShadowWarArmy *a, const ShadowWarArmy *b, ShadowWarBattle *out);

int shadow_war_army_power(const ShadowWarArmy *army);

/* Host-side, deterministic, no RNG: presses the advantage (ADVANCE) when clearly ahead, digs in
 * (HOLD) when clearly behind, and SCATTERs when the two armies are close -- the same two armies
 * always choose the same command. Not yet Elo/strength-scaled into a real bot pool the way
 * SLOWBOT_LEAGUE's own league bots are -- a real, separate, future refinement. */
int shadow_war_bot_choose_command(int bot_power, int enemy_power);

/* A deterministic "rival cell" roster sized to match `unit_count`, cycling base 0/1/2 and trait
 * 0/1/2 so it's a mixed army rather than one stacked type. unit_count is clamped to
 * [1, SHADOW_WAR_MAX_UNITS] (a bot always fields at least one unit). */
void shadow_war_bot_army(int unit_count, ShadowWarArmy *out);

/* Standard Elo (K=SHADOW_WAR_ELO_K by default; the caller may pass another K). score_x1000 is
 * 1000 (win) / 500 (draw) / 0 (loss) -- fixed-point so the call boundary stays integer. Uses the
 * real logistic expectation (pow(10, x/400)) internally -- plain host math, see shadow_war.prn's
 * header comment for why this one piece is not PARENA. Returns the new rating, rounded to the
 * nearest int; never negative (floored at 100, same spirit as a chess federation floor). */
int shadow_war_elo_update(int my_elo, int opp_elo, int score_x1000, int k);

typedef struct {
    int attempted;     /* 1 any time this was called with a real request to deploy */
    int ok;            /* 1 if a battle actually ran (enough clones); 0 = gate refused it */
    int result;        /* SHADOW_WAR_RESULT_*, only meaningful if ok */
    int old_elo, new_elo;
    char msg[48];       /* same one-line HUD convention as LabStationResult.msg */
} ShadowWarDeployResult;

/* Sends the phone's ENTIRE current clone roster to fight a deterministic bot army of the same
 * size. On success: clones are expended (clone_count -> 0), *inout_elo updated via
 * shadow_war_elo_update, and REFLUX_ACTION_SHADOW_WAR_RESOLVED dispatched (a = player_id, b =
 * result, c = new elo) -- player_id is the caller's own slot index (the local sandbox's hero is
 * always 0; taken as a real parameter rather than hardcoded so this module stays agnostic of who
 * is calling it). *inout_elo == 0 is treated as "never fought" and lazily seeded to
 * SHADOW_WAR_ELO_DEFAULT (1200) before use -- avoids needing a separate Phone-init call site.
 * Always fills *out. */
void shadow_war_deploy(Phone *p, int *inout_elo, int player_id, ShadowWarDeployResult *out);

#endif
