/* citizen_avatar.c -- see citizen_avatar.h. No rules live here: every number comes from the
 * PARENA-generated citizen_avatar_rules.c (stdlib/forgedinface/citizen.prn). This file only
 * composes them into the C shape the game speaks, and folds a 32-bit seed into the int32 domain the
 * rules are written against. */

#include "citizen_avatar.h"
#include "citizen_avatar_host.h"

#include <string.h>

/* The rules are I32 and hash with small multipliers; masking off the top bit keeps the seed inside
 * what they were designed for instead of letting a high seed read as a negative number. */
static int ca_seed(uint32_t seed) { return (int)(seed & 0x7fffffffu); }

static void ca_rgb(unsigned char out[3], int hue, int sat, int val) {
    out[0] = (unsigned char)cav_hsv(hue, sat, val, 0);
    out[1] = (unsigned char)cav_hsv(hue, sat, val, 1);
    out[2] = (unsigned char)cav_hsv(hue, sat, val, 2);
}

void citizen_avatar_build(CitizenAvatar *out, uint32_t seed, int slot, int path,
                          int industry, int commerce, int ambition, int bravery, int sociability) {
    if (!out) return;
    int s = ca_seed(seed);

    out->kit = cav_kit(s, slot);
    out->sex_bias = cav_sex_bias(s, slot, path);

    /* The body. Every one of these reads the persona, which is what makes the look agree with the
     * life: nothing here is a second, independent roll. */
    out->height_mm = cav_height_mm(out->sex_bias, bravery);
    out->bulk_permille = cav_bulk_permille(industry, commerce, path);
    out->shoulder_permille = cav_shoulder_permille(out->sex_bias, bravery);
    out->arm_permille = cav_arm_permille(path, industry);
    out->leg_permille = cav_leg_permille(path, bravery);
    out->head_permille = cav_head_permille(out->sex_bias);

    /* ...and the clothes. One hue per garment, all three from the same kit anchor, so a citizen is
     * dressed rather than randomly coloured. */
    ca_rgb(out->coat, cav_hue(0, s, slot, path, out->kit),
           cav_sat(0, out->kit, path, industry, sociability), cav_val(0, out->kit, path, ambition));
    ca_rgb(out->pants, cav_hue(1, s, slot, path, out->kit),
           cav_sat(1, out->kit, path, industry, sociability), cav_val(1, out->kit, path, ambition));
    ca_rgb(out->accent, cav_hue(2, s, slot, path, out->kit),
           cav_sat(2, out->kit, path, industry, sociability), cav_val(2, out->kit, path, ambition));
}

int citizen_avatar_equal(const CitizenAvatar *a, const CitizenAvatar *b) {
    if (!a || !b) return a == b;
    return memcmp(a, b, sizeof(*a)) == 0;
}

float citizen_avatar_scale(const CitizenAvatar *a, int part) {
    if (!a) return 1.0f;
    switch (part) {
        case CITIZEN_AVATAR_PART_TORSO:    return (float)a->bulk_permille / 1000.0f;
        case CITIZEN_AVATAR_PART_SHOULDER: return (float)a->shoulder_permille / 1000.0f;
        case CITIZEN_AVATAR_PART_ARM:      return (float)a->arm_permille / 1000.0f;
        case CITIZEN_AVATAR_PART_LEG:      return (float)a->leg_permille / 1000.0f;
        case CITIZEN_AVATAR_PART_HEAD:     return (float)a->head_permille / 1000.0f;
        default: return 1.0f;
    }
}
