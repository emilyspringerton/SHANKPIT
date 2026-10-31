/* material_rules_host.h -- the material animation + light style vocabulary, from PARENA.
 *
 * The rules live in PARENA/stdlib/shankpit/material_rules.prn, generated into material_rules.c (do
 * not edit it; `make regen-material-rules`). Pure queries, no table to install: these are timings and
 * index maths, and a renderer that installed different ones than the server would desync a flicker. */
#ifndef SHANKPIT_MATERIAL_RULES_HOST_H
#define SHANKPIT_MATERIAL_RULES_HOST_H

/* PARENA-generated (material_rules.c) */
int mat_kind_count(void);
int mat_scroll_speed_milli(int kind);
int mat_scroll_u_milli(int kind, int tick);
int mat_scroll_v_milli(int kind, int tick);
int mat_frame_index(int fps, int frames, int tick);
int mat_light_style_count(void);
int mat_reflection_strength_permille(int kind);   /* 0 mirror, 1 metal, 2 glass; 0 for everything else */
int light_style_level_permille(int style, int tick);
int mat_emissive_milli(int light_style, int tick, int room_light_milli);
int tint_low_health_milli(int hp, int max_hp);
int tint_submerged_milli(int depth_milli);
int tint_acid_milli(int acid_permille);
int tint_shield_milli(int shield, int max_shield);
int palette_index_256(int index);
int vlm_axis_index_milli(int pos_milli, int origin_milli, int extent_milli, int res);
int vlm_ambient_floor_milli(void);

#endif /* SHANKPIT_MATERIAL_RULES_HOST_H */
