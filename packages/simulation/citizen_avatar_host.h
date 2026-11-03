#ifndef CITIZEN_AVATAR_HOST_H
#define CITIZEN_AVATAR_HOST_H

/* citizen_avatar_host.h -- prototypes for the PARENA-generated citizen avatar rules.
 *
 * Source of truth: PARENA/stdlib/forgedinface/citizen.prn, emitted to
 * packages/simulation/citizen_avatar_rules.c by `make regen-citizen-avatar`. Nothing in this file
 * is hand-maintained logic -- it is the same no-shared-header-between-generated-units convention
 * packages/simulation/witness_ai.c already uses for zombies_pheromone_rules.c.
 *
 * Every rule here is a TOTAL INTEGER FUNCTION: no randomness, no state, no allocation. The same
 * arguments give the same number on the server, on the lobby, in a replay and in a test. */

int cav_kit_count(void);
int cav_kit(int seed, int slot);
int cav_sex_bias(int seed, int slot, int path);
int cav_height_mm(int sex_bias, int bravery);
int cav_bulk_permille(int industry, int commerce, int path);
int cav_shoulder_permille(int sex_bias, int bravery);
int cav_arm_permille(int path, int industry);
int cav_leg_permille(int path, int bravery);
int cav_head_permille(int sex_bias);

/* kind: 0 = coat, 1 = pants, 2 = accent. channel: 0 = r, 1 = g, 2 = b. */
int cav_hue(int kind, int seed, int slot, int path, int kit);
int cav_sat(int kind, int kit, int path, int industry, int sociability);
int cav_val(int kind, int kit, int path, int ambition);
int cav_hsv(int hue, int sat, int val, int channel);

#endif
