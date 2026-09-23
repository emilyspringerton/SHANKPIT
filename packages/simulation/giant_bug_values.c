#include "giant_bug_values.h"

/* Prototypes for the PARENA-generated network (giant_bug_brain.c, do-not-edit-by-hand -- see
 * that file's own header). No shared .h between generated units in this codebase, same real
 * precedent every other PARENA-generated module here already establishes. */
int bug_hidden_0(int, int, int, int, int, int, int, int);
int bug_hidden_1(int, int, int, int, int, int, int, int);
int bug_hidden_2(int, int, int, int, int, int, int, int);
int bug_hidden_3(int, int, int, int, int, int, int, int);
int bug_hidden_4(int, int, int, int, int, int, int, int);
int bug_hidden_5(int, int, int, int, int, int, int, int);
int bug_hidden_6(int, int, int, int, int, int, int, int);
int bug_hidden_7(int, int, int, int, int, int, int, int);
int bug_attack_drive(int, int, int, int, int, int, int, int);
int bug_flee_drive(int, int, int, int, int, int, int, int);
int bug_swarm_drive(int, int, int, int, int, int, int, int);

#define BUG_STRENGTH_BASELINE 1.0f
#define BUG_SPEED_BASELINE 1.0f
#define BUG_EAT_TRANSFER_PCT 0.35f /* real, hand-picked fraction of prey's own values absorbed */

void giant_bug_state_init(GiantBugState *b, uint32_t now_ms) {
    b->hunger = 0.3f;
    b->swarm_density = 0.0f;
    b->heat_scent = 0.0f;
    b->molt_pressure = 0.0f;
    b->ground_vibration = 0.0f;
    b->pain = 0.0f;
    b->hive_signal = 0.0f;
    b->light_aversion = 0.0f;
    b->strength = BUG_STRENGTH_BASELINE;
    b->speed = BUG_SPEED_BASELINE;
    b->mood_change_at_ms = now_ms;
}

void giant_bug_tick(GiantBugState *b, uint32_t now_ms, int has_target, int nearby_bug_count) {
    (void)now_ms;
    b->hunger += has_target ? -0.002f : 0.001f;
    if (b->hunger < 0.0f) b->hunger = 0.0f;
    if (b->hunger > 1.0f) b->hunger = 1.0f;

    b->swarm_density = nearby_bug_count > 4 ? 1.0f : (float)nearby_bug_count / 4.0f;

    b->molt_pressure += 0.0005f; /* one-way, like zombie_values.h's own decay -- never decreases */
    if (b->molt_pressure > 1.0f) b->molt_pressure = 1.0f;
}

void giant_bug_eat_zombie(GiantBugState *b, const ZombieState *prey, uint32_t now_ms) {
    (void)now_ms;
    if (!b || !prey) return;

    float strength_gain = prey->aggression * BUG_EAT_TRANSFER_PCT;
    uint32_t prey_delay = zombie_reaction_delay_ms(prey, 1000);
    float speed_proxy = prey_delay > 0 ? (1000.0f / (float)prey_delay) : 1.0f;
    float speed_gain = speed_proxy * BUG_EAT_TRANSFER_PCT;

    b->strength += strength_gain;
    b->speed += speed_gain;
    b->pain = 0.0f; /* a good meal */
}

static int bug_pct(float v) {
    int p = (int)(v * 100.0f + 0.5f);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}

static void bug_hidden_all(const GiantBugState *b, int h[8]) {
    int hunger = bug_pct(b->hunger), swarm = bug_pct(b->swarm_density), heat = bug_pct(b->heat_scent),
        molt = bug_pct(b->molt_pressure), vibe = bug_pct(b->ground_vibration), pain = bug_pct(b->pain),
        hive = bug_pct(b->hive_signal), light = bug_pct(b->light_aversion);
    h[0] = bug_hidden_0(hunger, swarm, heat, molt, vibe, pain, hive, light);
    h[1] = bug_hidden_1(hunger, swarm, heat, molt, vibe, pain, hive, light);
    h[2] = bug_hidden_2(hunger, swarm, heat, molt, vibe, pain, hive, light);
    h[3] = bug_hidden_3(hunger, swarm, heat, molt, vibe, pain, hive, light);
    h[4] = bug_hidden_4(hunger, swarm, heat, molt, vibe, pain, hive, light);
    h[5] = bug_hidden_5(hunger, swarm, heat, molt, vibe, pain, hive, light);
    h[6] = bug_hidden_6(hunger, swarm, heat, molt, vibe, pain, hive, light);
    h[7] = bug_hidden_7(hunger, swarm, heat, molt, vibe, pain, hive, light);
}

int giant_bug_attack_drive(const GiantBugState *b) {
    int h[8]; bug_hidden_all(b, h);
    return bug_attack_drive(h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
}

int giant_bug_flee_drive(const GiantBugState *b) {
    int h[8]; bug_hidden_all(b, h);
    return bug_flee_drive(h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
}

int giant_bug_swarm_drive(const GiantBugState *b) {
    int h[8]; bug_hidden_all(b, h);
    return bug_swarm_drive(h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
}
