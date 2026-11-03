#ifndef ZOMBIE_BRAIN_H
#define ZOMBIE_BRAIN_H

/* zombie_brain.h -- THE OG TRAIN-MODE NET, driving zombies.
 *
 * Founder, real-time: "FOR ZOMBIES MODE CAN WE MERGE IN THE OG TRAIN MODE NEURAL NET OPPONENTS SO THE
 * ZOMBIES ARE MORE DYNAMIC?"
 *
 * WHAT THE OG NET IS, checked rather than assumed: packages/simulation/brain_weights.h is auto-generated
 * from `shank_brain_horde.pth`, architecture 8 -> 256 -> 128 -> 4, and packages/simulation/neural_net.h's
 * `bot_brain_forward` runs it. Its 8 inputs are the ones `bot_ai.h`'s `bot_think` builds -- self x/z (÷500),
 * yaw as sin/cos, target x/z (÷500), distance (÷500), own health (÷100) -- and its 4 outputs are
 * [forward, strafe, yaw-rate, shoot-trigger]. The checkpoint's own name is why this merge is a merge: it was
 * trained on a HORDE, not on a duelist.
 *
 * SO THIS MODULE IS A SHIM, NOT A SECOND AI. It feeds the net from a zombie's own local frame, then maps the
 * four outputs onto what a zombie can actually do -- walk, turn, lunge -- and hands that back as an INTENT.
 * `witness_ai.c` remains the authority: pheromone steering, hero lock, hunger infighting, melee range, the
 * attack cooldown and the damage numbers all still decide whether anything happens. The net changes what the
 * body WANTS, never what the world allows.
 *
 * WHY ONE NET DOES NOT MAKE FIVE IDENTICAL BRAINS. The same output vector is shaped per class by
 * PARENA/stdlib/shankpit/zombie_brain_rules.prn: a BRUTE reads ranges 1200 permille (so it commits late and
 * hard) and may only weave 350 millidegrees, a RUNNER reads 900 and weaves 1500. That asymmetry is most of
 * what makes a charge feel heavy, and it is data, not a fork.
 *
 * OFF IS OFF. `zombie_brain_enabled` defaults to 1 in MODE_ZOMBIES and 0 in MODE_SURVIVAL (see
 * witness_ai.c), and `zombie_brain_think` answers `used_net = 0` with a zeroed intent, which leaves the
 * hand-tuned path below it EXACTLY as it was. There is deliberately no duplicated heuristic in here: one
 * implementation of the old behaviour, not two that drift.
 *
 * DETERMINISTIC. Same context, same intent -- float math on fixed weights, a per-body phase derived from
 * (class, level) rather than an RNG, and NaN refused at the door instead of propagated. That is what makes a
 * replay (and the test) possible.
 *
 * NOT A TRAINING PATH. Nothing here learns, samples or explores; it is a forward pass with the weights the
 * checkpoint shipped. The Go bot pool's own native inference (docs/BOT_TRAINING_NORTHSTAR.md §12 step 1) is
 * still separate, unbuilt work. */

#include "zombie_taxonomy.h"

/* How much WEAVE the net can add to the target bearing at yaw-rate 1.0, in degrees. This is an OFFSET added
 * to the bearing the hand-tuned chase would have taken, not a turn rate -- see the mapping note below. The
 * per-class cap in zombie_brain_rules.prn is applied on top of it, so a RUNNER weaves 1.5 deg and a BRUTE
 * 0.35 deg on the same input. */
#define ZOMBIE_BRAIN_YAW_GAIN_DEG 12.0f

/* The floor on a zombie's forward commitment: even a net that wants to stand still or back away pushes this
 * body forward at 55% of its class pace. There is no retreat affordance on a zombie, and this is the number
 * that keeps one from acquiring one. */
#define ZOMBIE_BRAIN_MIN_COMMIT 0.55f

/* How much of the per-body phase (up to ±450 milli) becomes strafe. Small on purpose: a drift, not a jink. */
#define ZOMBIE_BRAIN_PHASE_GAIN 0.20f

/* Everything the net is allowed to see about one zombie. Fractions are 0..1; the shim does the ÷500 and
 * ÷100 scaling the checkpoint was trained with, so callers never have to remember it.
 *
 * WHAT THE NET IS, AND IS NOT, ALLOWED TO DECIDE -- measured, 2026-11-03, not assumed. The 8 inputs are
 * ABSOLUTE world coordinates of both bodies (that is what shank_brain_horde.pth was trained on), so the net
 * cannot generalize to a map it has not seen; and its own prefered forward channel at range is backwards.
 * Fed raw and applied as a controller it walks a zombie away from a hero 100 units ahead in all three frames
 * tried (absolute; the zombie's own frame; its own frame with true yaw -- in the last one it simply spins,
 * yaw -360 to -900 over 600 ticks). Therefore:
 *
 *   the BEARING to the target decides WHERE (keep doing what the hand-tuned chase did, which converges)
 *   the NET decides HOW (weave on top of that bearing, lateral jink, forward commitment, when to lunge)
 *
 * That split is the merge: a horde that moves with the OG train-mode net's own texture, without a single body
 * that cannot walk toward its prey. */
typedef struct {
    float x, z;             /* the body's own position */
    float yaw_deg;          /* where it is facing */
    float health_frac;      /* own health, 0..1 */
    float target_x, target_z;
    float target_dist;      /* flat distance to the target, units */
    int cls;                /* ZCLASS_* */
    int level;              /* 1..ZOMBIE_MAX_LEVEL */
} ZombieBrainCtx;

typedef struct {
    float fwd;              /* ZOMBIE_BRAIN_MIN_COMMIT..1 -- a commitment, never a direction */
    float strafe;           /* -1..1 */
    float yaw_delta_deg;    /* a WEAVE to add to the target bearing, already capped by the class's own cap */
    int attack;             /* 1 = the net wants to lunge (the caller still checks range + cooldown) */
    int used_net;           /* 0 = no opinion; leave the hand-tuned path alone */
    int phase_milli;        /* the body's own drift, 0..899, for a screen or a test to read */
} ZombieIntent;

extern int zombie_brain_enabled;

/* The mapping on its own: net outputs + class -> intent. Separated so the thresholds and turn caps can be
 * tested against hand-picked outputs instead of whatever the weights happen to produce today. */
ZombieIntent zombie_brain_map(const float outputs[4], int cls, int level);

/* The whole shim: build the observation, run the net, shape the answer. */
ZombieIntent zombie_brain_think(const ZombieBrainCtx *c);

#endif
