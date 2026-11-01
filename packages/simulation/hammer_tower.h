#ifndef HAMMER_TOWER_H
#define HAMMER_TOWER_H

/* hammer_tower.h -- WPN_HAMMER's E power: build a mini tower.
 *
 * Founder real-time, 2026-11-01: "want to build in the E power for 8 the hammer it should build a
 * mini tower there should be like a 3 second cast time you have to stay still for 2.6 seconds or
 * it doesnt cast you caan start to run a bit early like ffxi but when it casts you stop when you
 * first start casting you stop running if you are auto running make it like a torb gattling gun
 * but not that strong for now have it auto target zombies and the men if the men are in combat or
 * citizens if the citizens are in combat".
 *
 * Two halves, deliberately split because they live in different layers:
 *
 *  1. THE CAST -- the timer, the still requirement, the FFXI-style early release and the root are
 *     in update_weapons (packages/common/physics.h), next to every other weapon's ability, so the
 *     offline sim and the dedicated server run the exact same numbers and there is only one copy
 *     of the rule. A finished cast raises PlayerState.hammer_tower_ready; it never spawns anything
 *     itself, because physics.h cannot see the NPC population (`common` must not depend on
 *     `simulation`).
 *
 *  2. THE TOWER -- this module. hammer_tower_tick is called from local_game.h's local_update,
 *     which is the authoritative per-tick path for both a local match and apps/server, so a tower
 *     built in single player and one built on a dedicated server behave identically. It owns its
 *     own fixed pool, does its own damage, and is deliberately NOT on the wire -- the same real,
 *     pre-existing boundary projectiles already have in this engine (a networked client does not
 *     receive projectiles either), so a tower is a server-side body today rather than a
 *     replicated one.
 *
 * TARGETING is the founder's rule, in priority order:
 *   - zombies (and Giant Zombie Bugs -- a feral giant variant of the same population) are always
 *     worth shooting, at any time;
 *   - The Men only once they are IN COMBAT;
 *   - citizens only once they are IN COMBAT.
 * "In combat" is witness_ai_npc_in_combat (witness_ai.c): a live hunt (SILENCING/ENGAGE/PANIC), or
 * a Man with a dispatch target, or somebody who has been shot at or hurt. An innocent bystander is
 * never shot, which is the whole reason the rule exists.
 *
 * It never shoots the player, a bot with no NPC role, or a teammate, so a PvP match sees no change.
 *
 * Deliberately weak, per "not that strong for now": 4 damage a round at ~6 rounds/s is ~24 dps,
 * about a quarter of a rifle, and roughly four seconds to put down one 100 hp zombie. It is a
 * pressure tool and a distraction, not a second player. All of these are plain constants below
 * rather than a PARENA table on purpose -- this is a first pass that wants tuning by feel, and
 * these are exactly the numbers that get tuned.
 */

#include "../common/protocol.h"

#define HAMMER_TOWER_MAX 6                 /* total live towers in the world */
#define HAMMER_TOWER_LIFETIME_MS 25000u    /* how long a built tower stands */
#define HAMMER_TOWER_RANGE 40.0f           /* acquisition + fire range, flat (x,z) */
#define HAMMER_TOWER_INTERVAL_MS 160u      /* between rounds -- a gatling cadence */
#define HAMMER_TOWER_DAMAGE 4              /* per round: deliberately weak */
#define HAMMER_TOWER_WINDUP_MS 500u        /* spins up before the first round, like a gatling */
#define HAMMER_TOWER_TURN_DEG_PER_TICK 12.0f
#define HAMMER_TOWER_AIM_TOLERANCE_DEG 7.0f
#define HAMMER_TOWER_FORWARD_OFFSET 6.0f   /* where it lands, in front of the caster */
#define HAMMER_TOWER_HEIGHT 2.2f           /* muzzle height above the caster's feet */
#define HAMMER_TOWER_DEATH_RESPAWN_MS 2000u

typedef struct {
    int active;
    int owner_id;
    int scene_id;
    float x, y, z;
    float yaw;              /* aim, engine yaw convention (degrees, atan2(dx, dz)) */
    float spin;             /* barrel rotation, degrees -- presentation only */
    int target_id;          /* -1 = nothing worth shooting right now */
    int shots;              /* rounds fired this life, for tests and a future HUD */
    unsigned int born_ms;
    unsigned int acquired_ms;   /* when the current target was first picked */
    unsigned int last_shot_ms;
} HammerTower;

/* Clears the pool. Called on every match start (local_game.h's local_init_match). */
void hammer_tower_reset(void);

/* Places a tower for owner_id in front of (x, y, z) facing yaw. Returns the pool index, or -1
 * when the pool is full (a full pool refuses rather than stealing somebody else's tower). */
int hammer_tower_spawn(ServerState *s, int owner_id, float x, float y, float z, float yaw,
                       unsigned int now_ms);

/* One server tick: expiry, target acquisition, rotation, and fire. Safe to call every tick, and a
 * no-op with no towers standing. */
void hammer_tower_tick(ServerState *s, unsigned int now_ms);

/* Where a tower would land for this player -- the caster's feet, HAMMER_TOWER_FORWARD_OFFSET along
 * their facing. Split out so the host and the test agree on the placement rule. Returns 0 only for
 * a NULL player. */
int hammer_tower_place_for_owner(const PlayerState *p, float *out_x, float *out_y, float *out_z);

int hammer_tower_count(void);
const HammerTower *hammer_tower_at(int index);   /* NULL if that slot is empty */

#endif
