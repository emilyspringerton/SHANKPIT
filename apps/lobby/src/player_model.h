#ifndef PLAYER_MODEL_H
#define PLAYER_MODEL_H

// Ronin Shell proportions
#define RONIN_TORSO_W 1.35f
#define RONIN_TORSO_H 1.15f
#define RONIN_TORSO_D 0.75f

#define RONIN_SHOULDER_PAD_W 0.45f
#define RONIN_SHOULDER_PAD_H 0.45f
#define RONIN_SHOULDER_PAD_D 0.7f
#define RONIN_SHOULDER_PAD_OFFSET 0.95f

#define RONIN_SLEEVE_W 0.35f
#define RONIN_SLEEVE_H 1.1f
#define RONIN_SLEEVE_D 0.45f
#define RONIN_SLEEVE_OFFSET 0.85f

#define RONIN_LINING_Y_BOTTOM -0.6f
#define RONIN_LINING_Y_TOP -0.52f

#define RONIN_PANTS_W 0.55f
#define RONIN_PANTS_H 1.55f
#define RONIN_PANTS_D 0.7f
#define RONIN_PANTS_OFFSET 0.32f

// Articulated limb proportions (blocky, low-poly style)
#define RONIN_ARM_UPPER_W 0.34f
#define RONIN_ARM_UPPER_H 0.72f
#define RONIN_ARM_UPPER_D 0.40f
#define RONIN_ARM_LOWER_W 0.30f
#define RONIN_ARM_LOWER_H 0.62f
#define RONIN_ARM_LOWER_D 0.34f
#define RONIN_HAND_W 0.24f
#define RONIN_HAND_H 0.16f
#define RONIN_HAND_D 0.20f

#define RONIN_LEG_UPPER_W 0.45f
#define RONIN_LEG_UPPER_H 0.80f
#define RONIN_LEG_UPPER_D 0.54f
#define RONIN_LEG_LOWER_W 0.40f
#define RONIN_LEG_LOWER_H 0.72f
#define RONIN_LEG_LOWER_D 0.46f
#define RONIN_FOOT_W 0.46f
#define RONIN_FOOT_H 0.20f
#define RONIN_FOOT_D 0.74f

// Storm Mask proportions
#define RONIN_HEAD_W 0.65f
#define RONIN_HEAD_H 0.75f
#define RONIN_HEAD_D 0.65f

#define RONIN_FACEPLATE_W 0.55f
#define RONIN_FACEPLATE_H 0.45f
#define RONIN_FACEPLATE_D 0.08f

#define RONIN_VENT_W 0.12f
#define RONIN_VENT_H 0.22f
#define RONIN_VENT_D 0.03f
#define RONIN_VENT_OFFSET_X 0.22f

#define RONIN_HORN_W 0.12f
#define RONIN_HORN_H 0.28f
#define RONIN_HORN_D 0.12f
#define RONIN_HORN_OFFSET_X 0.28f



typedef struct CharacterDefinition {
    const char *id;
    int skin_id;
    float render_scale[3];
    float collider_size[3];
    float collider_offset[3];
    float camera_offset[3];
    float weapon_socket[3];
    float head_socket[3];
    float anim_root_offset[3];
    float selection_offset[3];
    float selection_scale;
    float ground_offset;
    int has_tail;
    int is_humanoid;
} CharacterDefinition;

#endif
