#ifndef ZOMBIE_TAXONOMY_H
#define ZOMBIE_TAXONOMY_H

/* zombie_taxonomy.h -- zombies are a POPULATION, not one monster with a health bar.
 *
 * Founder real-time, 2026-11-01: "as per big_o when you kill zombies they may drop bioslury which
 * can be taken to the lab to possibly harvest dna; dna should depend on the stats of the zombie
 * slain zombies should have different stats different levels and different classes as per GFD".
 *
 * The classes are urban-flavoured (TRAPX_NORTHSTAR.md's own conceit: GFD mechanics, city skin) and
 * deliberately few -- five is enough for a player to read at a glance and for the drop table to
 * mean something:
 *
 *   SHAMBLER  the stock case. Slow, thin, common by day.
 *   WALKER    the one the engine already had: 100 hp, a steady pace.
 *   RUNNER    fast and fragile. The one that catches you reloading.
 *   BRUTE     slow, fat, hits like a truck. Worth a canister on its own.
 *   ALPHA     rare, night-only, and the reason to carry a hammer.
 *
 * STATS ARE DERIVED, NEVER ROLLED INDEPENDENTLY. A level 7 BRUTE is the same body every time,
 * whoever spawns it and whatever the RNG did that second -- which is what lets the lab's DNA mean
 * something ("this strand came off a level 9 Alpha") and what makes a test possible at all.
 * Randomness lives in exactly two places: which class a spawn is (weighted by time of day), and
 * its level. Everything downstream of those two numbers is a function.
 *
 * DNA is the point of the drop, and it is a read of the body: its grade rises with level and with
 * class, and the trait it carries is the class's own identity -- a RUNNER yields SPEED, a BRUTE
 * yields ARMOR, an ALPHA yields SCENT. The phone's LAB trait list is the vocabulary
 * (packages/common/phone.h's BP_TRAITS order), so nothing here invents a second one.
 */

#include <stdint.h>

enum { ZCLASS_SHAMBLER = 0, ZCLASS_WALKER, ZCLASS_RUNNER, ZCLASS_BRUTE, ZCLASS_ALPHA, ZCLASS_COUNT };

#define ZOMBIE_MAX_LEVEL 12

/* The phone's own BP_TRAITS order: SPEED, ARMOR, SCENT. */
enum { ZTRAIT_SPEED = 0, ZTRAIT_ARMOR, ZTRAIT_SCENT, ZTRAIT_COUNT };

typedef struct {
    int cls;              /* ZCLASS_* */
    int level;            /* 1..ZOMBIE_MAX_LEVEL */
    int max_health;
    int melee_damage;
    int move_permille;    /* 1000 = the stock pace the old single zombie walked at */
    int sense_bonus;      /* added to the base sense radius, units */
    int slurry_permille;  /* chance a kill leaves a canister, 0..1000 */
    int dna_quality;      /* 0..100, what the lab can read off the strand */
    int dna_trait;        /* ZTRAIT_* -- the class's own identity */
} ZombieProfile;

/* The whole body from (class, level). `out` is always filled; an out-of-range class reads as
 * SHAMBLER and a level outside 1..ZOMBIE_MAX_LEVEL clamps, so a bad number is a weaker zombie
 * rather than an uninitialised struct. */
void zombie_taxonomy_profile(ZombieProfile *out, int cls, int level);

/* Deterministic roll for one spawn: class and level from a seed, weighted by night (0 = day,
 * 1 = night). Night is when RUNNERs and the occasional ALPHA appear; day is shamblers and walkers.
 * Same seed, same body -- tests and replays depend on it. */
void zombie_taxonomy_roll(ZombieProfile *out, uint32_t seed, int night);

const char *zombie_taxonomy_class_name(int cls);
const char *zombie_taxonomy_trait_name(int trait);

/* Does THIS body leave a canister? A pure function of the seed and the profile's own
 * slurry_permille, so a kill is not a second RNG source nobody can reproduce. */
int zombie_taxonomy_slurry_roll(uint32_t seed, const ZombieProfile *p);

#endif
