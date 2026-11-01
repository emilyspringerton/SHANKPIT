#ifndef CITIZEN_LIFE_H
#define CITIZEN_LIFE_H

/* citizen_life.h -- the citizens have their own lives, and every one of them is a different player.
 *
 * Founder real-time, 2026-11-01: "citizens to for that matter - tcitizens could be spies they could
 * secrety be another player like think of all of the citizens are all living their life and they
 * play the game in different ways like how different mmo players play different ways like some just
 * do crafting some just the econemy some just pvp some just endgame etc in that same way the
 * citicitizens will all have their own lives and priorites and perosonalities like dwarf fortress
 * think of the citizens like a mix between the citizens in gta and the sims and dwarf fortress".
 *
 * The design in one sentence: a citizen is a PLAYER with a persona -- five traits, a "how I play
 * this game" path, a home, somewhere they are trying to be, and (rarely) a secret.
 *
 * PATHS are the MMO-player archetypes the founder named, and they are what the citizen spends the
 * day doing rather than a job title:
 *   CRAFT     sits and works. Slow, territorial, does not travel far.
 *   TRADE     moves between posts. The economy player: always somewhere, never for long.
 *   DUEL      looks for trouble. Fastest on its feet, widest range, the one that walks toward a
 *             zombie instead of away.
 *   ENDGAME   the group-content player: long trips to a fixed, far post and long stays.
 *   IDLE      the AFK/social one. Drifts, stands, watches.
 *
 * TRAITS (0..100, dwarf-fortress-flavoured) are the persona the path is chosen from and later
 * behaviour can read: industry, commerce, ambition, bravery, sociability.
 *
 * SPIES are the founder's own "secretly another player": a small share of citizens are not
 * background at all but an account wearing a body -- they get a player handle, they WATCH rather
 * than live (their goals bias to a vantage point), and a host can tell one apart
 * (citizen_life_is_spy) so it can be revealed, reported, greeted or hunted. The handle is
 * deterministic per body, so the same citizen is the same "player" across a session.
 *
 * Deliberately no rendering and no dialogue here, and no relationship to witness_sim's own
 * vigilance/arrogance model -- that is the bureaucratic-horror register (who saw what), this is
 * what the body does with its afternoon. Both read the same PlayerState; neither owns the other.
 */

#include "../common/protocol.h"
#include <stdint.h>

enum { CILIFE_CRAFT = 0, CILIFE_TRADE, CILIFE_DUEL, CILIFE_ENDGAME, CILIFE_IDLE, CILIFE_PATH_COUNT };

enum {
    CILIFE_GOAL_IDLE = 0,   /* standing where it is */
    CILIFE_GOAL_POST,       /* walking to its own spot */
    CILIFE_GOAL_WATCH       /* a spy at a vantage point */
};

#define CILIFE_HANDLE_LEN 16

typedef struct {
    int industry, commerce, ambition, bravery, sociability;   /* 0..100 */
    int path;                 /* CILIFE_* */
    int spy;                  /* 1 = secretly another player */
    char handle[CILIFE_HANDLE_LEN];   /* the "player" behind this body, always set */
    int goal;                 /* CILIFE_GOAL_* */
    float goal_x, goal_z;
    float home_x, home_z;
    unsigned int goal_until_ms;
    uint32_t rng;             /* this citizen's own stream -- never shared, never global */
} CitizenLife;

/* Seeds a persona from a seed, slot and the archetype's own vigilance/arrogance (so a swaggering
 * citizen and a timid one are not the same person). `out` is always filled. */
void citizen_life_roll(CitizenLife *out, uint32_t seed, int slot, int arrogance, int vigilance,
                       float home_x, float home_z);

/* One tick of THEIR life: pick a new goal when the current one is done, then walk toward it (or
 * stand). Fleeing is the caller's business -- witness_ai checks a hunting zombie first and only
 * gives a citizen its afternoon when it is not running for its life. */
void citizen_life_tick(CitizenLife *l, PlayerState *p, unsigned int now_ms);

int citizen_life_is_spy(const CitizenLife *l);
const char *citizen_life_handle(const CitizenLife *l);
const char *citizen_life_path_name(int path);

#endif
