#ifndef SHANKPIT_WALK_RULES_H
#define SHANKPIT_WALK_RULES_H

/* walk_rules.h -- where an NPC drifts, and when, in SHANKPIT. The movement half of the NOOBTOPIA merge.
 *
 * The DECISIONS are PARENA's, once: stdlib/noobtopia/walk_rules.prn -> packages/simulation/walk_rules.c (generated
 * by scripts/gen_noobtopia_rules.sh, do not edit), and the same .prn -> NOOBTOPIA/src/generated/walk_rules.lua.
 * This file is the thin host: it holds a position and a credit, and asks the rules what to do. Position lives here
 * because the rules must never need a clock or a coordinate system -- see MONOREPO/docs/THREE_WAY_MERGE.md.
 *
 * The integration rule is deliberately the same integer algorithm NOOBTOPIA's src/wander.lua runs, so a seeded noob
 * walks the same path in both worlds:
 *   * a target is chosen only when walk_should_choose() says the idle has elapsed;
 *   * the target is the current position plus one leg along one of 8 compass headings, rejected if it lands outside
 *     the agent's leash (each of the 8 headings is tried once);
 *   * movement is one WHOLE unit per tick, gated by a permille accumulator -- so a STARTLED noob (0 permille) is
 *     frozen, a TIRED one (400) steps twice per five ticks, and nothing ever moves half a unit.
 *
 * Header-only and stateless except the agent itself: the caller owns the seed, so a server and every client that
 * runs the same seed see the same wander. */

#include <stdint.h>

/* PARENA-generated (packages/simulation/walk_rules.c). */
int walk_heading_count(void);
int walk_mod(int count, int seed);
int walk_jitter(int span, int seed);
int walk_idle_ticks(int mood, int seed);
int walk_should_choose(int mood, int since_ticks, int seed);
int walk_leg_length(int mood);
int walk_heading(int seed);
int walk_heading_dx(int heading);
int walk_heading_dy(int heading);
int walk_target_dx(int heading, int leg);
int walk_target_dy(int heading, int leg);
int walk_mood_speed_permille(int mood);
int walk_within_leash(int dx, int dy, int leash);

/* Moods, matching humanness.c and ambient_life.prn. */
#define WALK_IDLE 0
#define WALK_CURIOUS 1
#define WALK_BORED 2
#define WALK_TIRED 3
#define WALK_STARTLED 4

/* The seed mixer, shared with NOOBTOPIA's Wander.nextSeed().
 *
 * MINSTD (48271 mod 2^31-1) rather than the 1103515245 mixer ambient_life.h uses, and the reason is measured, not
 * stylistic: this seed drives a path that must match across C and Luau, and Luau numbers are IEEE doubles, exact
 * only to 2^53. `seed * 1103515245` reaches ~2.4e18 (61 bits) for a seed near 2^31, so the low bits are rounded
 * away -- seed 2147483646 gives 2087949151 exactly but 2087949312 once forced through double arithmetic. Plain
 * `lua` 5.4 keeps 64-bit integers and hides this; Roblox would not. `seed * 48271` peaks near 1.0e14, exact in
 * both. Done in int64 here so the C multiply cannot wrap first. */
static inline int walk_agent_next_seed(int seed) {
    int64_t s = (int64_t)(seed < 0 ? 0 : seed);
    return (int)((s * 48271) % 2147483647);
}

typedef struct {
    int x, y;               /* position in whole world units */
    int home_x, home_y;     /* where it is allowed to drift from */
    int target_x, target_y; /* the current leg's destination */
    int dx, dy;             /* unit heading of the current leg, -1/0/1 per axis */
    int leg;                /* how long this leg is, in units */
    int since_choice;       /* ticks since the last target was chosen */
    int credit;             /* permille accumulator: moves a whole unit every 1000 */
    int mood;
    int seed;
    int leash;
    int walking;            /* 1 while a leg is in progress */
} WalkAgent;

static inline void walk_agent_init(WalkAgent *a, int home_x, int home_y, int mood, int seed, int leash) {
    a->x = a->home_x = home_x;
    a->y = a->home_y = home_y;
    a->target_x = home_x;
    a->target_y = home_y;
    a->dx = a->dy = 0;
    a->leg = 0;
    a->since_choice = 0;
    a->credit = 0;
    a->mood = mood;
    a->seed = seed;
    a->leash = leash;
    a->walking = 0;
}

/* Pick a target whose whole leg stays inside the leash, or stand still if all 8 do not (a leash shorter than the
 * leg means this NPC never moves -- visible in the test, not a silent hang). */
static inline void walk_agent_choose(WalkAgent *a) {
    int leg = walk_leg_length(a->mood);
    for (int attempt = 0; attempt < walk_heading_count(); attempt++) {
        int h = walk_heading(a->seed + attempt * 7);
        int tx = walk_target_dx(h, leg);
        int ty = walk_target_dy(h, leg);
        if (walk_within_leash(a->x + tx - a->home_x, a->y + ty - a->home_y, a->leash) != 1) continue;
        a->walking = 1;
        a->dx = walk_heading_dx(h);
        a->dy = walk_heading_dy(h);
        a->leg = leg;
        a->target_x = a->x + tx;
        a->target_y = a->y + ty;
        a->since_choice = 0;
        a->seed = walk_agent_next_seed(a->seed);
        return;
    }
}

/* One tick. Returns 1 if the agent actually moved a whole unit this tick -- NOT "is mid-leg": a STARTLED noob is
 * nominally walking and has a target, but at 0 permille it never spends a credit, so it returns 0 every tick while
 * standing still. `a->walking` is the leg-in-progress flag for a caller that wants to draw it committed to a walk. */
static inline int walk_agent_tick(WalkAgent *a) {
    a->since_choice++;
    if (!a->walking && walk_should_choose(a->mood, a->since_choice, a->seed) == 1) walk_agent_choose(a);
    if (!a->walking) return 0;

    a->credit += walk_mood_speed_permille(a->mood);
    int stepped = 0;
    while (a->credit >= 1000 && a->walking) {
        a->credit -= 1000;
        a->x += a->dx;
        a->y += a->dy;
        stepped = 1;
        if (a->x == a->target_x && a->y == a->target_y) a->walking = 0;
    }
    return stepped;
}

#endif
