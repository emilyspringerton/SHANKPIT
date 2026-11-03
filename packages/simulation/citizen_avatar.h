#ifndef CITIZEN_AVATAR_H
#define CITIZEN_AVATAR_H

/* citizen_avatar.h -- a citizen's BODY, derived from the persona it already has.
 *
 * citizen_life.h gives every ambient citizen a persona: five traits, one of five paths, a handle, a
 * home. Every one of those citizens was drawn with the same box mannequin. This is the other half
 * of that body: a look that is a FUNCTION of the persona, not a separate roll, so the same citizen
 * looks the same in the lobby, on the server, in a replay, and in a test -- and so its clothes agree
 * with how it plays the game (a DUEL player is lean and long-armed because it walks toward the
 * zombie; a CRAFT player is heavier and shorter-legged because it stands at a bench).
 *
 * Authoring lives in PARENA (stdlib/forgedinface/citizen.prn -- FORGEDINFACE's proportion rig,
 * flattened to what a low-poly box body can express). This header is the C shape the game and the
 * renderer speak, and citizen_avatar.c is only the composition: which rule feeds which field.
 *
 * PROPORTIONS ARE PERMILLE OF THE REFERENCE BODY -- SHANKPIT's Ronin Shell, apps/lobby/src/
 * player_model.h -- where 1000 is exactly the mannequin that already exists. So a neutral citizen
 * is the current model unchanged, and the renderer multiplies its existing constants by
 * citizen_avatar_scale() instead of learning a second set of dimensions.
 *
 * NO RENDERING HERE and no GL: nothing in this module knows what a box is. */

#include <stdint.h>

typedef struct {
    int sex_bias;            /* 0 = masculine build, 1000 = feminine, 500 = androgynous */
    int height_mm;           /* standing height, 1500..1980 */
    int bulk_permille;       /* 780..1250 -- torso across and through */
    int shoulder_permille;   /* 850..1300 -- the widest, most readable part of the silhouette */
    int arm_permille;        /* 900..1120 -- upper+lower arm length */
    int leg_permille;        /* 900..1150 -- upper+lower leg length */
    int head_permille;       /* 880..1120 */
    int kit;                 /* 0..cav_kit_count()-1, the shell/palette family */
    unsigned char coat[3];   /* RGB, 0..255 */
    unsigned char pants[3];
    unsigned char accent[3]; /* the PATH's own colour: the one garment that reads at a distance */
} CitizenAvatar;

/* Builds the avatar for one citizen. `path` is a CILIFE_* value (citizen_life.h), the five traits
 * are 0..100, and `seed` is the citizen's own stream (CitizenLife.rng) -- never a global. `out` is
 * always filled when non-NULL; out-of-range inputs clamp rather than producing a broken body. */
void citizen_avatar_build(CitizenAvatar *out, uint32_t seed, int slot, int path,
                          int industry, int commerce, int ambition, int bravery, int sociability);

/* Same body or not -- the determinism assertion, in one call. */
int citizen_avatar_equal(const CitizenAvatar *a, const CitizenAvatar *b);

/* Parts, for a renderer that owns the absolute dimensions. */
enum {
    CITIZEN_AVATAR_PART_TORSO = 0,   /* uses bulk_permille */
    CITIZEN_AVATAR_PART_SHOULDER,    /* uses shoulder_permille */
    CITIZEN_AVATAR_PART_ARM,         /* uses arm_permille */
    CITIZEN_AVATAR_PART_LEG,         /* uses leg_permille */
    CITIZEN_AVATAR_PART_HEAD,        /* uses head_permille */
    CITIZEN_AVATAR_PART_COUNT
};

/* A multiplier on the reference body for that part: 1.0 is the existing mannequin. An unknown part
 * returns 1.0, so a caller can never accidentally scale a body to zero. */
float citizen_avatar_scale(const CitizenAvatar *a, int part);

#endif
