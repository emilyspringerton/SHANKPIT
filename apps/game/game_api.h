/* apps/game/game_api.h -- the hand-written contract for the generated PARENA rules.
 *
 * Every declaration here is emitted by `parena build` from the packages/game rules (plus the
 * sim/render rule modules they compose) into build/game_rules.c at build time. This header is the same
 * "hand-written header, generated implementation" split packages/simulation/shield_regen.h uses;
 * the C names are the hyphen-to-underscore mangle VS0 emits, including `?` -> `_`.
 */
#ifndef SHANKPIT_GAME_API_H
#define SHANKPIT_GAME_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- packages/game/core_loop.prn ---------------------------------------------------------------- */
int loop_tick_ms(void);
int loop_max_steps(void);
int loop_arena_half(void);
int loop_wall_height(void);
int loop_speed(void);
int loop_body_radius(void);
int loop_cam_height(void);
int loop_focal(void);
int loop_horizon(void);
double loop_sin(int md);
double loop_cos(int md);
double loop_sin_deg(int deg);
double loop_cos_deg(int deg);
double loop_to_f(int n);
int loop_to_i(double v);
double loop_atan2(int dz, int dx);
int loop_clamp(int v, int lo, int hi);
int loop_clamp_pos(int v);
int loop_move_x(int x, int yaw, int fwd, int strafe);
int loop_move_z(int z, int yaw, int fwd, int strafe);
int loop_ring_deg(int slot, int count);
int loop_ring_x(int slot, int count, int radius);
int loop_ring_z(int slot, int count, int radius);
int loop_face_yaw(int ax, int az, int bx, int bz);
int loop_arc_deg(int slot, int count, int spread);
int loop_arc_x(int slot, int count, int radius, int spread);
int loop_arc_z(int slot, int count, int radius, int spread);

/* ---- packages/game/render_plan.prn --------------------------------------------------------------- */
int rp_width(void);
int rp_height(void);
int rp_focal(void);
int rp_cam_h(void);
int rp_near(void);
int rp_horizon(void);
double rp_fwd(int dx, int dz, int yaw);
double rp_right(int dx, int dz, int yaw);
int rp_visible(double f);
int rp_screen_x(int dx, int dz, int yaw);
int rp_feet_y(int dx, int dz, int yaw);
int rp_body_h(double f, int h);
int rp_body_w(double f, int h);
int rp_sky_stop(int y);
int rp_floor_shade(double f);
int rp_wall_shade(void);
int rp_hud_scale(void);
int rp_nearest_yaw(int ax, int az, int bx, int bz);

/* ---- packages/game/net_rules.prn ----------------------------------------------------------------- */
int net_magic(void);
int net_version(void);
int net_max_bodies(void);
int net_port(void);
int net_tick_ms(void);
int net_stale_ms(void);
int net_packet_connect(void);
int net_packet_cmd(void);
int net_packet_snapshot(void);
int net_packet_welcome(void);
int net_packet_level_mods(void);
int net_packet_mod_request(void);
int net_mod_request_size(void);
int net_packet_caps(void);
int net_caps_size(void);
int net_welcome_size(void);
int net_max_peers(void);
int net_first_client_slot(void);
int net_interest_all(void);
int net_interest_match(int peer_level, int level);
int net_mod_policy_off(void);
int net_mod_policy_owner(void);
int net_mod_policy_peer(void);
int net_mod_policy_valid(int policy);
int net_mod_policy_allows(int policy, int is_owner);
int net_mod_request_cooldown_ms(void);
int net_mod_request_authorized(int policy, int is_owner, int token_ok, int now_ms, int next_ms);
int net_cmd_size(void);
int net_snapshot_size(void);
int net_level_mod_bytes(void);
int net_level_mods_header_bytes(void);
int net_max_level_mods(void);
int net_level_mods_size(void);
int net_body_bytes(void);
int net_header_bytes(void);
int net_quant(int v);
int net_body_flags_alive(void);
int net_body_flags_bot(void);
int net_valid_mode(int mode);
int net_valid_packet(int type);
int net_valid_body_count(int count);
int net_slot_valid(int slot);
int net_valid_level_count(int count);

/* ---- packages/game/gameplay_mod.prn (the REFLUX gameplay core mod) -------------------------------- */
int gp_mode_zombies(void);
int gp_mode_ffa(void);
int gp_mode_default(void);
int gp_mode_valid(int mode);
int gp_mode_count(void);
int gp_mode_hp(int mode);
int gp_mode_shield(int mode);
int gp_mode_bots(int mode);
int gp_mode_wave_size(int mode, int wave);
int gp_mode_wave_hp(int mode, int wave);
int gp_mode_wave_break_ms(int mode);
int gp_mode_respawn_ticks(int mode, int hp);
int gp_mode_frag_limit(int mode);
int gp_mode_spawn_radius(int mode);
int gp_mode_name_id(int mode);
int gp_kill_score(int mode, int hp, int head);
int gp_hit_shield(int shield, int dmg, int head);
int gp_hit_health(int hp, int shield, int dmg, int head);
int gp_hit_killed(int hp, int shield, int dmg, int head);
int gp_regen_shield(int shield, int since_ms);
int gp_regen_waiting(int since_ms);
int gp_regen_delay_ms(void);
int gp_regen_interval_ms(void);
int gp_weapon_damage(int weapon);
int gp_evt_hit(void);
int gp_evt_ko(void);
int gp_evt_mode(void);
int gp_evt_is_gameplay_(int action_type);

/* ---- packages/game/level_mods.prn (the core per-level REFLUX module registry) -------------------- */
int lm_mod_core_sim(void);
int lm_mod_level(void);
int lm_mod_weather_render(void);
int lm_mod_weather_audio(void);
int lm_mod_sky(void);
int lm_mod_count(void);
int lm_mod_valid(int mod);
int lm_env_surface(void);
int lm_env_underground(void);
int lm_env_moon(void);
int lm_env_valid(int env);
int lm_bit(int mod);
int lm_all_mask(void);
int lm_mandatory_mask(void);
int lm_mandatory(int mod);
int lm_enabled(int mask, int mod);
int lm_default_mask(int env);
int lm_sanitize(int mask);
int lm_set(int mask, int mod, int on);
int lm_next_revision(int revision);
int lm_evt_registered(void);
int lm_evt_mod_set(void);
int lm_evt_state(void);
int lm_evt_unregistered(void);
int lm_evt_first(void);
int lm_evt_last(void);
int lm_evt_is_level_(int action_type);

/* ---- packages/game/build_profiles.prn (what each BUILD contains) --------------------------------- */
int bp_target_native(void);
int bp_target_llvm(void);
int bp_target_wasm_core(void);
int bp_target_native_core(void);
int bp_target_count(void);
int bp_target_valid(int target);
int bp_net_none(void);
int bp_net_host_c(void);
int bp_net_parena(void);
int bp_target_capabilities(int target);
int bp_target_net(int target);

/* ---- composed sim/render rules the client reads directly ------------------------------------------ */
int tick_ms(void);
int frame_budget(void);
int frame_dt_ms(int dt);
int frame_steps(int acc, int dt);
int frame_acc(int acc, int dt);
int combat_score_for(int hp_max, int head);
int combat_evt_first(void);
int combat_evt_last(void);
int combat_respawn_ticks(int hp_max);
int combat_dmg_dealt(int before, int after);
int sky_top(void);
int sky_mid(void);
int sky_horizon(void);
int floor_color(void);
int wall_color(void);
int player_hue(int c);
int palette_check(void);
int shield_max(void);

#ifdef __cplusplus
}
#endif

#endif /* SHANKPIT_GAME_API_H */
