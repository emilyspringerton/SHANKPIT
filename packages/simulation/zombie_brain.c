/* zombie_brain.c -- see zombie_brain.h. The bridge between the OG train-mode net and a zombie's body.
 *
 * This is the ONE translation unit in the C tree that includes neural_net.h (checked: nothing else does), so
 * the header's non-static `bot_brain_forward`/`dense_layer` have exactly one definition per binary. If that
 * ever stops being true, the fix is to make those functions `static inline`, not to copy the forward pass. */

#include "zombie_brain.h"
#include "zombie_brain_rules_host.h"
#include "neural_net.h"

#include <math.h>
#include <string.h>

/* On by default. witness_ai.c sets this from the game mode and SHANKPIT_ZOMBIE_BRAIN, so a world can turn the
 * whole thing off without a rebuild -- and a test can prove the off path is the old path. */
int zombie_brain_enabled = 1;

static float clampf(float v, float lo, float hi) {
    if (!(v == v)) return 0.0f;          /* NaN: refuse it here rather than let it reach the physics */
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float milli(int v) { return (float)v / 1000.0f; }

ZombieIntent zombie_brain_map(const float outputs[4], int cls, int level) {
    ZombieIntent it;
    memset(&it, 0, sizeof it);

    float turn_cap_deg = milli(zb_turn_cap(cls));
    if (turn_cap_deg < 0.0f) turn_cap_deg = 0.0f;

    /* THE FORWARD CHANNEL IS A COMMITMENT, NOT A DIRECTION, and that is a measured decision rather than a
     * taste. Fed raw and applied the way bot_ai.h applies it, every class walks a zombie steadily BACKWARDS
     * from a hero standing 100 units ahead: fwd -0.80..-0.85, 86 units of retreat in 600 ticks, zero lunges --
     * in the absolute world frame, in the zombie's own frame, and in its own frame with the true yaw. A rifle
     * duelist is trained to kite at range; a zombie has no ranged attack to kite with, so obeying that channel
     * as a direction produces a monster that moonwalks out of its own fight. Its MAGNITUDE still carries the
     * net's opinion about how hard to press, so |fwd| becomes this body's commitment between the floor and 1.0,
     * and the caller keeps the heading (see witness_ai.c: the bearing decides where, the net decides how). */
    it.fwd = ZOMBIE_BRAIN_MIN_COMMIT +
             (1.0f - ZOMBIE_BRAIN_MIN_COMMIT) * fabsf(clampf(outputs[0], -1.0f, 1.0f));
    it.strafe = clampf(outputs[1], -1.0f, 1.0f);
    it.yaw_delta_deg = clampf(outputs[2] * ZOMBIE_BRAIN_YAW_GAIN_DEG, -turn_cap_deg, turn_cap_deg);

    /* the net's shoot channel is where a zombie's LUNGE comes from. Its threshold is per class, so a BRUTE
     * commits on a hint and a SHAMBLER needs convincing. */
    int threshold = zb_attack_threshold(cls);
    it.attack = (clampf(outputs[3], -1.0f, 1.0f) * 1000.0f) >= (float)threshold;

    /* a deterministic per-body drift, so a horde of one class does not strafe in lockstep */
    it.phase_milli = zb_phase_milli(cls, level);
    float drift = ((float)it.phase_milli - 450.0f) * milli(1000) * ZOMBIE_BRAIN_PHASE_GAIN / 1000.0f;
    it.strafe = clampf(it.strafe + drift, -1.0f, 1.0f);

    it.used_net = 1;
    return it;
}

ZombieIntent zombie_brain_think(const ZombieBrainCtx *c) {
    ZombieIntent none;
    memset(&none, 0, sizeof none);
    if (!c || !zombie_brain_enabled) return none;

    float in[8];
    float out[4];

    /* The class's own reading of the world, applied BEFORE the net sees it (see zombie_brain_rules.prn for why
     * this is data and not a fork of the brain). */
    float dist = clampf(c->target_dist, 0.0f, 5000.0f) * milli(zb_distance_bias(c->cls));
    float health = clampf(c->health_frac, 0.0f, 1.0f) * milli(zb_health_bias(c->cls));
    float rad = clampf(c->yaw_deg, -3600.0f, 3600.0f) * 0.017453292519943295f;

    /* EXACTLY the 8 features shank_brain_horde.pth was trained on; changing the order silently breaks the
     * weights, so it is written out rather than looped over. */
    in[0] = clampf(c->x / 500.0f, -50.0f, 50.0f);
    in[1] = clampf(c->z / 500.0f, -50.0f, 50.0f);
    in[2] = sinf(rad);
    in[3] = cosf(rad);
    in[4] = clampf(c->target_x / 500.0f, -50.0f, 50.0f);
    in[5] = clampf(c->target_z / 500.0f, -50.0f, 50.0f);
    in[6] = clampf(dist / 500.0f, 0.0f, 10.0f);
    in[7] = clampf(health, 0.0f, 1.0f);

    bot_brain_forward(in, out);
    return zombie_brain_map(out, c->cls, c->level);
}
