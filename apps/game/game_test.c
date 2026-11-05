/* apps/game/game_test.c -- headless acceptance tests for the clean-room game rules.
 *
 * No SDL, no window, no socket: this is the part of the game that must be true before anything is
 * drawn or sent. It pins the mode table (the game must boot into zombies), the loop/render/net
 * arithmetic, the combat and regen verbs the mod exposes, and the REFLUX contract itself
 * (dispatch rows, poll them back through a cursor, and survive a ring wrap).
 *
 * Run with `make game-test`. Exit 0 = all checks passed, 1 = at least one failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event_log.h"
#include "build_profile.h"
#include "game_api.h"
#include "level_catalog.h"
#include "level_mods_host.h"
#include "reflux_runtime.h"

static int g_checks;
static int g_failures;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("FAIL  %s\n", what);
    }
}

static int near(int a, int b, int tol) {
    int d = a - b;
    if (d < 0) d = -d;
    return d <= tol;
}

static int abs_i(int v) { return v < 0 ? -v : v; }

/* Replays the host's own per-tick regen loop: keep the shield held when damage landed plus ms since,
 * and ask the rule for the value this tick. Feeding the CURRENT shield back in would compound the
 * ramp, which is the bug this test exists to catch. */
static int regen_after(int base, int since_ms, int ticks) {
    int shield = base;
    int t;
    for (t = 0; t < ticks; t++) {
        since_ms += 16;
        shield = gp_regen_shield(base, since_ms);
    }
    return shield;
}

static void test_modes(void) {
    check(gp_mode_default() == gp_mode_zombies(), "boot mode is zombies (the lobby)");
    check(gp_mode_zombies() == 1 && gp_mode_ffa() == 2, "mode ids are 1 and 2");
    check(gp_mode_count() == 2, "two modes");
    check(gp_mode_valid(gp_mode_zombies()) == 1, "zombies is valid");
    check(gp_mode_valid(gp_mode_ffa()) == 1, "ffa is valid");
    check(gp_mode_valid(0) == 0 && gp_mode_valid(3) == 0, "invalid modes are refused");
    check(gp_mode_name_id(gp_mode_ffa()) == gp_mode_ffa(), "name id round-trips");

    check(gp_mode_hp(gp_mode_zombies()) == 100, "zombies hp 100");
    check(gp_mode_shield(gp_mode_zombies()) == 50, "zombies shield 50");
    check(gp_mode_shield(gp_mode_ffa()) == 100, "ffa shield 100");
    check(gp_mode_bots(gp_mode_ffa()) == 5, "ffa fields five bots");
    check(gp_mode_bots(gp_mode_zombies()) == 0, "zombies is wave-driven, not a fixed roster");
    check(gp_mode_frag_limit(gp_mode_ffa()) == 25, "ffa frag limit 25");
    check(gp_mode_frag_limit(gp_mode_zombies()) == 0, "zombies has no frag limit");
    check(gp_mode_spawn_radius(gp_mode_zombies()) == 950, "zombies spawn ring 950");
    check(gp_mode_spawn_radius(gp_mode_ffa()) == 520, "ffa spawn ring 520");
    check(gp_mode_wave_break_ms(gp_mode_zombies()) == 3500, "zombies wave break 3.5 s");
}

static void test_loop(void) {
    int i;
    check(loop_tick_ms() == 16, "loop tick 16 ms");
    check(loop_tick_ms() == tick_ms(), "loop and frame rules agree on the tick");
    check(loop_max_steps() == frame_budget(), "loop and frame rules agree on the step budget");
    check(loop_arena_half() > 900, "arena contains the spawn ring");
    check(loop_wall_height() > 200, "walls are taller than a body");
    check(loop_horizon() < rp_height(), "horizon is on screen");
    check(rp_focal() > 400, "camera has a sane focal length");

    /* forward at yaw 0 is +Z; at yaw 90 deg it is +X (pi/2 rad = 1571 mrad). */
    check(loop_move_z(0, 0, 100, 0) > 0, "full forward at yaw 0 moves +Z");
    check(near(loop_move_x(0, 0, 100, 0), 0, 1), "full forward at yaw 0 does not drift in X");
    check(loop_move_x(0, 1571, 100, 0) > 0, "full forward at yaw 90 deg moves +X");
    check(loop_move_x(0, 0, 0, 100) > 0, "full right strafe moves +X");

    /* The clamp holds on every tick of a long run in every direction, not just the first. */
    {
        int x = 0, z = 0, ok = 1;
        for (i = 0; i < 4000; i++) {
            x = loop_move_x(x, 700, 100, 30);
            z = loop_move_z(z, 700, 100, 30);
            if (x > loop_arena_half() || x < -loop_arena_half() ||
                z > loop_arena_half() || z < -loop_arena_half()) ok = 0;
        }
        check(ok, "4000 ticks of movement stay inside the arena");
        check(x != 0 || z != 0, "movement actually moved");
    }
    check(loop_clamp(200, 0, 100) == 100 && loop_clamp(-5, 0, 100) == 0, "clamp");

    /* ring: slot 0 faces +Z, one quarter turn faces +X */
    check(near(loop_ring_x(0, 4, 100), 0, 2), "ring slot 0 x ~ 0");
    check(near(loop_ring_z(0, 4, 100), 100, 2), "ring slot 0 z ~ radius");
    check(near(loop_ring_x(1, 4, 100), 100, 2), "ring slot 1 x ~ radius");
    check(loop_ring_x(0, 0, 100) == 0, "empty ring is a refusal, not a divide");

    /* arcs: a single body straight ahead, three symmetric about +Z */
    check(loop_arc_x(0, 1, 100, 90) == 0 && near(loop_arc_z(0, 1, 100, 90), 100, 2),
          "a one-body arc is straight ahead");
    check(near(loop_arc_x(1, 3, 100, 90), 0, 2), "the middle of a three-body arc is ahead");
    check(near(loop_arc_x(0, 3, 100, 90), -70, 2) && near(loop_arc_x(2, 3, 100, 90), 70, 2),
          "a three-body arc spreads symmetrically");

    /* facing: from origin, +Z is yaw 0, +X is 90 deg */
    check(near(loop_face_yaw(0, 0, 0, 100), 0, 30), "facing +Z is yaw 0");
    check(near(abs_i(loop_face_yaw(0, 0, 100, 0)), 1571, 40), "facing +X is yaw 90 deg");
}

static void test_render(void) {
    check(rp_width() == 1024 && rp_height() == 576, "framebuffer is 1024x576");
    check(rp_horizon() == loop_horizon(), "renderer uses the loop's horizon");

    /* dead ahead at yaw 0: centred, feet below the horizon, body height positive */
    check(rp_visible(rp_fwd(0, 100, 0)) == 1, "point 1 m ahead is visible");
    check(rp_visible(rp_fwd(0, -100, 0)) == 0, "point behind the camera is culled");
    check(near(rp_screen_x(0, 200, 0), rp_width() / 2, 1), "centre projection");
    check(rp_feet_y(0, 500, 0) > rp_horizon(), "feet project below the horizon");
    check(rp_feet_y(0, 500, 0) < rp_height(), "feet of a body 5 m out are on screen");
    check(rp_feet_y(0, 900, 0) < rp_feet_y(0, 500, 0), "the floor rises with distance");
    check(rp_body_h(rp_fwd(0, 200, 0), 180) > rp_body_h(rp_fwd(0, 900, 0), 180),
          "a far body is shorter");
    check(rp_body_w(rp_fwd(0, 900, 0), 180) >= 2, "a far body still has a visible width");
    check(rp_screen_x(0, 0, 0) == rp_width() / 2, "degenerate projection is total");
    check(rp_visible(rp_fwd(0, 0, 0)) == 0, "the camera's own body is culled");

    /* sky stops run pale top -> warm horizon, never backwards down the frame */
    check(rp_sky_stop(0) == 2 && rp_sky_stop(100) == 1 && rp_sky_stop(300) == 0,
          "sky stops top -> horizon");
    check(rp_sky_stop(89) >= rp_sky_stop(90) && rp_sky_stop(189) >= rp_sky_stop(190),
          "sky stops never run backwards");
    check(rp_floor_shade(rp_fwd(0, 200, 0)) >= rp_floor_shade(rp_fwd(0, 1200, 0)),
          "near floor is lighter than far floor");
    check(rp_wall_shade() == 100, "wall shade is full");

    /* the 460 art direction itself is intact (three distinct sky stops, floor != wall, distinct hues) */
    check(palette_check() == 0, "460 palette check passes");
    check(sky_top() != sky_mid() && sky_mid() != sky_horizon(), "sky stops are distinct");
    check(floor_color() != wall_color(), "floor and wall are distinct");
    check(player_hue(0) != player_hue(1) && player_hue(0) != player_hue(9), "player hues are distinct");
}

static void test_combat(void) {
    /* shield first, then health -- the rule the shield-regen feature shipped */
    check(gp_hit_shield(50, 20, 0) == 30, "20 through a 50 shield leaves 30");
    check(gp_hit_health(100, 50, 20, 0) == 100, "a shielded body takes no health damage");
    check(gp_hit_shield(100, 130, 0) == 0, "an overkill shot empties the shield");
    check(gp_hit_health(100, 100, 130, 0) == 70, "the remainder carries into health");

    check(gp_hit_killed(10, 0, 45, 0) == 1, "a 45 hit kills a 10 hp body");
    check(gp_hit_killed(100, 50, 45, 0) == 0, "a shielded body survives a magnum round");
    check(gp_hit_killed(0, 0, 45, 0) == 0, "a corpse is never killed twice");
    check(gp_hit_killed(10, 0, 30, 1) == 1, "a head shot kills");
    check(combat_dmg_dealt(100, 70) == 30, "damage dealt is before minus after");
    check(combat_dmg_dealt(70, 100) == 0, "damage dealt never goes negative");

    check(gp_weapon_damage(2) == 20, "rifle does 20");
    check(gp_weapon_damage(1) == 45, "magnum does 45");
    check(gp_weapon_damage(4) == 101, "sniper does 101");
    check(gp_weapon_damage(99) == 40, "an unknown slot falls back to melee, not zero");

    /* scoring: light body 1, heavy 3, head doubles; FFA pays flat frags */
    check(gp_kill_score(gp_mode_zombies(), 55, 0) == 1, "zombies light kill is 1");
    check(gp_kill_score(gp_mode_zombies(), 100, 0) == 3, "zombies heavy kill is 3");
    check(gp_kill_score(gp_mode_zombies(), 55, 1) == 2, "a head shot doubles it");
    check(gp_kill_score(gp_mode_ffa(), 100, 0) == 1, "ffa frag is 1");
    check(gp_kill_score(gp_mode_ffa(), 100, 1) == 2, "ffa headshot frag is 2");
    check(gp_mode_respawn_ticks(gp_mode_ffa(), 100) == 180, "a 100 hp body waits 180 ticks");
    check(gp_mode_respawn_ticks(gp_mode_zombies(), 55) == 90, "a light body waits 90 ticks");

    /* waves */
    check(gp_mode_wave_size(gp_mode_zombies(), 0) == 3, "wave 0 is three shamblers");
    check(gp_mode_wave_size(gp_mode_zombies(), 3) == 6, "wave 3 is six");
    check(gp_mode_wave_size(gp_mode_zombies(), 20) == 10, "waves cap at ten");
    check(gp_mode_wave_size(gp_mode_ffa(), 4) == 0, "ffa has no waves");
    check(gp_mode_wave_hp(gp_mode_zombies(), 0) == 55, "wave 0 shambler has 55 hp");
    check(gp_mode_wave_hp(gp_mode_zombies(), 3) == 70, "wave 3 shambler has 70 hp");
    check(gp_mode_wave_hp(gp_mode_zombies(), 99) == 120, "wave hp caps at 120");
}

static void test_regen(void) {
    int base = gp_regen_shield(0, gp_regen_delay_ms() - 1);
    check(gp_regen_delay_ms() == 4000, "regen delay is 4 s");
    check(gp_regen_interval_ms() == 1000, "regen ticks once a second");
    check(gp_regen_waiting(gp_regen_delay_ms() - 1) == 1, "still waiting just before 4 s");
    check(gp_regen_waiting(gp_regen_delay_ms()) == 0, "no longer waiting at 4 s");
    check(base == 0, "nothing comes back before 4 s");
    check(regen_after(0, 4400, 10) > 0, "regen actually ramps");
    check(regen_after(0, 4400, 10) == gp_regen_shield(0, 4400), "regen is idempotent per elapsed ms");
    check(gp_regen_shield(0, 20000) <= shield_max(), "regen never passes the shield cap");
    check(gp_regen_shield(80, 20000) <= shield_max(), "a damaged-high shield stays capped");
    check(gp_regen_shield(30, 4000) > 0, "a 30 base gets its first tick at 4 s");
}

static void test_net(void) {
    check(net_magic() == 1397247312, "magic is 'SHKP'");
    check(net_cmd_size() == 20, "cmd is 20 bytes");
    check(net_snapshot_size() == 156, "snapshot is 156 bytes");
    check(net_snapshot_size() == net_header_bytes() + net_max_bodies() * net_body_bytes(),
          "snapshot size is header + bodies");
    check(net_snapshot_size() <= 512, "snapshot fits one comfortable datagram");
    check(net_quant(40000) == 30000 && net_quant(-40000) == -30000, "positions clamp to i16 range");
    check(net_quant(1234) == 1234, "ordinary positions pass through");
    check(net_valid_mode(gp_mode_zombies()) == 1 && net_valid_mode(gp_mode_ffa()) == 1, "modes on the wire");
    check(net_valid_mode(0) == 0 && net_valid_mode(7) == 0, "invalid modes on the wire");
    check(net_valid_body_count(1) == 1 && net_valid_body_count(net_max_bodies()) == 1, "counts");
    check(net_valid_body_count(0) == 0 && net_valid_body_count(999) == 0, "bad counts refused");
    check(net_slot_valid(0) == 1 && net_slot_valid(11) == 1 && net_slot_valid(12) == 0, "slots");
    check(net_body_flags_alive() == 1 && net_body_flags_bot() == 2, "flags are bits");
    check(net_port() == 7777 && net_tick_ms() == 33, "port and snapshot cadence");
    check(net_packet_level_mods() == 4, "level mods are packet type 4");
    check(net_packet_mod_request() == 5, "mod requests are packet type 5");
    check(net_mod_request_size() == 12, "a mod request fits one small datagram");
    check(net_packet_caps() == 6, "capability advertisements are packet type 6");
    check(net_caps_size() == 12, "a capability advertisement carries caps and a level");
    check(net_packet_welcome() == 3 && net_welcome_size() == 8, "welcome names the assigned slot");
    check(net_max_peers() == 4 && net_first_client_slot() == 8,
          "four client slots sit above seven bot slots");
    check(net_interest_match(net_interest_all(), 7) == 1, "interest 0 watches every level");
    check(net_interest_match(7, 7) == 1 && net_interest_match(7, 8) == 0,
          "a peer watching level 7 receives only level 7");
    check(net_level_mods_size() == 72, "level mods packet is one fixed datagram");
    check(net_level_mods_size() ==
              net_level_mods_header_bytes() + net_max_level_mods() * net_level_mod_bytes(),
          "level mods size is header + rows");
    check(net_valid_packet(5) == 1 && net_valid_packet(6) == 1 && net_valid_packet(7) == 0,
          "packets 0..6 on the wire, 7 refused");
    check(net_valid_level_count(1) == 1 && net_valid_level_count(net_max_level_mods()) == 1,
          "a level mods count is bounded");
    check(net_valid_level_count(0) == 0 && net_valid_level_count(9) == 0, "bad counts refused");
}

static void test_reflux(void) {
    RefluxLog log;
    int cursor = 0;
    int hits = 0, kos = 0, modes = 0;
    int i;

    reflux_log_reset(&log);
    check(reflux_log_length(&log) == 0, "empty log");

    /* The gameplay mod names the rows; the host dispatches them; a subscriber polls. This is the
     * exact path apps/game/host.c takes, just without the socket and the pixels. */
    reflux_log_dispatch(&log, gp_evt_hit(), 0, 3, 20);
    reflux_log_dispatch(&log, gp_evt_hit(), 3, 0, 20);
    reflux_log_dispatch(&log, gp_evt_ko(), 3, 0, 2);
    reflux_log_dispatch(&log, gp_evt_mode(), gp_mode_zombies(), gp_mode_ffa(), 0);

    for (i = cursor; i < reflux_log_length(&log); i++) {
        const RefluxAction *row = reflux_log_at(&log, i);
        check(row != NULL, "row exists");
        if (!row) continue;
        check(gp_evt_is_gameplay_(row->action_type) == 1, "row is in the gameplay family");
        if (row->action_type == gp_evt_hit()) hits++;
        if (row->action_type == gp_evt_ko()) {
            kos++;
            check(row->a == 3 && row->b == 0 && row->c == 2, "KO payload is (victim, attacker, weapon)");
        }
        if (row->action_type == gp_evt_mode()) {
            modes++;
            check(row->a == gp_mode_zombies() && row->b == gp_mode_ffa(), "mode payload is (old, new)");
        }
    }
    check(hits == 2 && kos == 1 && modes == 1, "the family is readable by cursor");
    check(reflux_log_at(&log, 4) == NULL, "out-of-range read is NULL");

    /* The ring's real bound: after it wraps, the oldest rows are gone and the accessors say so
     * instead of silently pretending the cursor is still valid. */
    reflux_log_reset(&log);
    for (i = 0; i < REFLUX_LOG_CAPACITY + 44; i++) {
        reflux_log_dispatch(&log, gp_evt_hit(), i, 1, 2);
    }
    check(reflux_log_length(&log) == REFLUX_LOG_CAPACITY, "ring saturates at capacity");
    check(log.total_dispatched == REFLUX_LOG_CAPACITY + 44, "total counts past the ring");
    check(reflux_log_at(&log, 0) != NULL, "oldest retained row is readable");
    check(reflux_log_at(&log, 0)->a == 44, "oldest retained row is the 45th dispatch");
    check(reflux_log_at(&log, REFLUX_LOG_CAPACITY - 1)->a == REFLUX_LOG_CAPACITY + 43,
          "newest row is the last dispatch");
    check(reflux_log_at(&log, REFLUX_LOG_CAPACITY) == NULL, "one past the end is NULL");
    check(gp_evt_is_gameplay_(gp_evt_mode()) == 1, "mode change is in the family");
    check(gp_evt_is_gameplay_(1) == 0 && gp_evt_is_gameplay_(130 + 2) == 0,
          "a foreign action is not in the family");
    check(gp_evt_hit() == combat_evt_first() && gp_evt_ko() == combat_evt_last(),
          "the mod reuses combat's own 129/130, not a private pair");
}

/* ---- a short simulated round: waves spawn, enemies close, hits and KOs are dispatched ----------- */
static void test_round_sim(void) {
    int tick;
    int player_hp = gp_mode_hp(gp_mode_zombies());
    int player_shield = gp_mode_shield(gp_mode_zombies());
    int sp = 0, sh = player_shield;
    int base = player_shield;
    int since = 0;
    int damage_seen = 0;
    int wave = 0;
    int bots = 0;
    int kos = 0;

    reflux_host_reset();
    /* wave 0 ring */
    bots = gp_mode_wave_size(gp_mode_zombies(), wave);
    check(bots == 3, "wave 0 spawns three bodies");
    check(gp_mode_wave_hp(gp_mode_zombies(), wave) == 55, "wave 0 bodies have 55 hp");

    for (tick = 0; tick < 240; tick++) {
        /* one shambler lands a rifle hit on the player every 12 ticks, as the host's bot AI does */
        if (tick % 12 == 0) {
            int killed = gp_hit_killed(player_hp, sh, 20, 0);
            int hp = gp_hit_health(player_hp, sh, 20, 0);
            int after = gp_hit_shield(sh, 20, 0);
            reflux_host_dispatch(gp_evt_hit(), 1, 0, 20);
            sp = sh - after;
            damage_seen += sp;
            sh = after;
            player_hp = hp;
            base = sh;
            since = 0;
            if (killed) reflux_host_dispatch(gp_evt_ko(), 0, 1, 2);
        }
        since += 16;
        sh = gp_regen_shield(base, since);
    }
    check(damage_seen > 0, "a shambler actually took shield off the player");
    check(player_hp <= 100, "hp stayed bounded");
    check(sh >= 0 && sh <= shield_max(), "shield stayed bounded");

    /* the subscriber the host runs: count KOs without the dispatcher knowing */
    for (tick = 0; tick < reflux_host_total_dispatched(); tick++) {
        int idx = reflux_host_index_for_abs(tick);
        if (idx >= 0 && reflux_host_action_type_at(idx) == gp_evt_ko()) kos++;
    }
    check(kos >= 1, "the round dispatched at least one KO");
    check(reflux_host_log_size() > 0, "the host log is live");
}

/* The core per-level module registry: environment defaults, the mandatory core, and live updates that
 * travel through the shared REFLUX log without touching a second level. */
static void test_level_mods(void) {
    int surface = lm_default_mask(lm_env_surface());
    int under = lm_default_mask(lm_env_underground());
    int moon = lm_default_mask(lm_env_moon());
    int mask;

    check(lm_mod_core_sim() == 1 && lm_mod_level() == 2 && lm_mod_sky() == 5,
          "module ids are stable");
    check(lm_mod_count() == 5, "five modules");
    check(lm_env_valid(lm_env_surface()) == 1 && lm_env_valid(lm_env_moon()) == 1,
          "surface and moon are environments");
    check(lm_env_valid(3) == 0, "an unknown environment is refused");

    /* The founder's own two cases, as masks rather than prose. */
    check(lm_enabled(surface, lm_mod_weather_render()) == 1, "surface draws weather");
    check(lm_enabled(under, lm_mod_weather_render()) == 0, "underground disables weather render");
    check(lm_enabled(under, lm_mod_weather_audio()) == 1, "underground still hears the weather");
    check(lm_enabled(under, lm_mod_core_sim()) == 1, "underground keeps the core sim");
    check(lm_enabled(under, lm_mod_level()) == 1, "underground keeps its level module");
    check(lm_enabled(moon, lm_mod_weather_render()) == 0, "moon disables weather render");
    check(lm_enabled(moon, lm_mod_weather_audio()) == 0, "moon disables weather audio too");
    check(lm_enabled(moon, lm_mod_core_sim()) == 1, "moon keeps the core sim");

    /* Mandatory modules survive every path, including a hostile or empty mask. */
    check(lm_set(under, lm_mod_core_sim(), 0) == under, "core sim cannot be turned off");
    check(lm_set(under, lm_mod_level(), 0) == under, "the level module cannot be turned off");
    check(lm_sanitize(0) == lm_mandatory_mask(), "an empty mask returns to the mandatory set");
    check(lm_set(under, 99, 1) == under, "an unknown module is ignored");
    mask = lm_set(under, lm_mod_weather_render(), 1);
    check(lm_enabled(mask, lm_mod_weather_render()) == 1, "a module can be turned on");
    check(lm_set(mask, lm_mod_weather_render(), 0) == under, "and off again");

    /* The C header and the PARENA rules must name the same rows. */
    check(lm_evt_registered() == REFLUX_ACTION_LEVEL_REGISTERED, "registered row number agrees");
    check(lm_evt_mod_set() == REFLUX_ACTION_LEVEL_MOD_SET, "mod-set row number agrees");
    check(lm_evt_state() == REFLUX_ACTION_LEVEL_STATE, "state row number agrees");
    check(lm_evt_is_level_(REFLUX_ACTION_LEVEL_MOD_SET) == 1, "the family predicate sees its rows");
    check(lm_evt_is_level_(gp_evt_mode()) == 0, "a mode row is not a level update");
    check(reflux_action_is_level_mod(REFLUX_ACTION_LEVEL_STATE) == 1,
          "the C predicate agrees with the PARENA one");

    /* Live registry: two levels, updates through the log, no bleed between them. */
    reflux_host_reset();
    level_mods_reset();
    check(level_mods_register(7, lm_env_underground()) == under, "level 7 opens underground");
    check(level_mods_register(7, lm_env_surface()) == under, "re-registering does not reset a live level");
    check(level_mods_register(8, lm_env_moon()) == moon, "level 8 opens on the moon");
    check(level_mods_count() == 2, "two live levels");
    check(level_mods_revision(7) == 1, "a fresh level starts at revision 1");
    check(reflux_host_total_dispatched() == 2, "each registration dispatches one row");

    /* request is the server's event; sync is the level's subscriber. The mask must not move between
     * the two, or the log would not be the thing carrying the update. */
    check(level_mods_request(7, lm_mod_weather_render(), 1) == 1, "an update row is dispatched");
    check(level_mods_mask(7) == under, "request alone does not mutate the level");
    check(level_mods_sync(7) >= 1, "sync applies the unseen update");
    check(level_mods_enabled(7, lm_mod_weather_render()) == 1, "underground can opt back into weather");
    check(level_mods_revision(7) == 2, "an accepted change bumps the revision");
    check(level_mods_mask(8) == moon, "level 8 is untouched by level 7's update");
    check(level_mods_sync(7) == 0, "a second sync applies nothing");
    check(level_mods_revision(7) == 2, "a repeated sync does not bump the revision");

    check(level_mods_set(7, lm_mod_weather_render(), 0) == 1, "sync-mode set turns weather off");
    check(level_mods_mask(7) == under, "level 7 is back to its underground mask");
    check(level_mods_revision(7) == 3, "the second accepted change bumps again");
    check(level_mods_request(7, lm_mod_core_sim(), 0) == 0, "disabling the core sim is refused");

    /* A stale full-state row (older revision) is ignored rather than undoing a newer update. */
    reflux_host_dispatch(lm_evt_state(), 7, under, 1);
    check(level_mods_sync(7) == 0, "a stale state row applies nothing");
    check(level_mods_mask(7) == under && level_mods_revision(7) == 3,
          "a stale state row cannot roll a live level back");
    check(level_mods_set(8, lm_mod_weather_audio(), 1) == 1, "the moon bunker can add weather audio");
    check(level_mods_mask(8) == (moon | lm_bit(lm_mod_weather_audio())), "level 8 carries the new bit");
    check(level_mods_mask(7) == under, "level 7 is still untouched");

    /* The network relay applies the same authoritative state: forward-only, sanitised, and never
     * able to strip the mandatory modules out of a live level. */
    check(level_mods_apply_state(8, surface, 4) == 1, "a network state applies forward");
    check(level_mods_mask(8) == surface && level_mods_revision(8) == 4,
          "network state moves the mask and revision");
    check(level_mods_apply_state(8, moon, 2) == 0, "a stale network state is ignored");
    check(level_mods_mask(8) == surface && level_mods_revision(8) == 4,
          "a replayed packet cannot roll the level back");
    check(level_mods_apply_state(8, 0, 5) == 1 && level_mods_mask(8) == lm_mandatory_mask(),
          "a hostile network mask is sanitised to the mandatory set");
    check(level_mods_apply_state(99, surface, 9) == 0, "state for an unregistered level is ignored");

    /* A log reset (a new round) must not leave a cursor reading another level's rows. */
    level_mods_publish(7);
    check(reflux_host_action_type_at(reflux_host_index_for_abs(reflux_host_total_dispatched() - 1)) ==
              lm_evt_state(),
          "publish emits a full-state row");
    reflux_host_reset();
    check(level_mods_sync(7) == 0, "sync after a reset is a harmless no-op");
    check(level_mods_mask(7) == under, "and it does not corrupt the live mask");

    level_mods_reset();
    check(level_mods_count() == 0, "reset clears the registry");
}

/* A level manifest is the server's own config: it carries each level's environment and optional mod
 * override, and registers the whole catalog or nothing. */
static void test_level_catalog(void) {
    const char *path = "/tmp/shank_level_manifest_test.json";
    FILE *f = fopen(path, "wb");

    check(f != NULL, "manifest test file opens");
    if (f) {
        fputs("{\"levels\":["
              "{\"id\":21,\"name\":\"BUNKER\",\"env\":1},"
              "{\"id\":22,\"name\":\"MOON\",\"env\":2,\"mods\":0}]}",
              f);
        fclose(f);
    }

    level_catalog_reset();
    level_mods_reset();
    reflux_host_reset();
    check(level_catalog_load(path) == 2, "manifest loads two levels");
    check(level_catalog_count() == 2, "catalog keeps both levels");
    check(strcmp(level_catalog_name(21), "BUNKER") == 0, "catalog keeps the level name");
    check(strcmp(level_catalog_name(999), "") == 0, "an unknown level has no name");
    check(level_mods_env(21) == lm_env_underground(), "level 21 carries its environment");
    check(level_mods_mask(21) == lm_default_mask(lm_env_underground()),
          "level 21 opens with the bunker mask");
    check(level_mods_enabled(21, lm_mod_weather_render()) == 0,
          "the bunker level has no weather render");
    check(level_mods_mask(22) == lm_mandatory_mask(),
          "a manifest mods override is sanitised, not trusted");

    /* Duplicate ids refuse the whole manifest and leave the registry empty. */
    f = fopen(path, "wb");
    if (f) {
        fputs("{\"levels\":["
              "{\"id\":21,\"name\":\"A\",\"env\":1},"
              "{\"id\":21,\"name\":\"B\",\"env\":2}]}",
              f);
        fclose(f);
    }
    level_catalog_reset();
    level_mods_reset();
    check(level_catalog_load(path) < 0, "duplicate ids refuse the manifest");
    check(level_mods_count() == 0, "a refused manifest registers nothing");

    level_catalog_reset();
    level_mods_reset();
    check(level_catalog_load("/tmp/shank_no_such_manifest.json") == -1,
          "a missing manifest is refused");
    remove(path);
}

/* The JSONL tail: every level-family row appears exactly once, with its real payload. */
static void test_event_log(void) {
    const char *path = "/tmp/shank_event_log_test.jsonl";
    char buf[2048] = {0};
    size_t n = 0;
    FILE *f;

    remove(path);
    reflux_host_reset();
    level_mods_reset();
    check(event_log_open(path) == 1, "event log opens");
    event_log_reset_cursor();

    level_mods_register(31, lm_env_underground()); /* registered */
    level_mods_request(31, lm_mod_sky(), 1);       /* mod_set */
    level_mods_apply(31, lm_mod_sky(), 1);         /* applies, does not dispatch */
    level_mods_publish(31);                        /* state */

    check(event_log_sync(77) == 3, "three level rows stream to the log");
    check(event_log_sync(78) == 0, "a second sync writes nothing");
    event_log_close();

    f = fopen(path, "rb");
    check(f != NULL, "event log file exists");
    if (f) {
        n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
    }
    check(strstr(buf, "\"event\":\"registered\"") != NULL, "registered row is in the stream");
    check(strstr(buf, "\"event\":\"mod_set\"") != NULL, "mod_set row is in the stream");
    check(strstr(buf, "\"event\":\"state\"") != NULL, "state row is in the stream");
    check(strstr(buf, "\"tick\":77") != NULL, "the caller's tick is recorded");
    check(strstr(buf, "\"level\":31") != NULL, "the level id is recorded");
    remove(path);
}

/* Replay rebuilds the registry from the stream: deltas go through the live set path, state is
 * forward-only, and a log_reset starts a fresh registry. */
static void test_event_replay(void) {
    const char *path = "/tmp/shank_event_replay_test.jsonl";
    FILE *f = fopen(path, "wb");

    check(f != NULL, "replay test stream opens");
    if (f) {
        fputs("{\"seq\":0,\"tick\":0,\"event\":\"registered\",\"level\":41,\"env\":1,\"mask\":11}\n", f);
        fputs("{\"seq\":1,\"tick\":1,\"event\":\"mod_set\",\"level\":41,\"mod\":3,\"on\":1}\n", f);
        fputs("{\"seq\":2,\"tick\":2,\"event\":\"state\",\"level\":41,\"mask\":11,\"revision\":3}\n", f);
        fputs("{\"seq\":3,\"tick\":3,\"event\":\"state\",\"level\":41,\"mask\":31,\"revision\":2}\n", f);
        fputs("{\"seq\":4,\"tick\":4,\"event\":\"log_reset\"}\n", f);
        fputs("{\"seq\":0,\"tick\":5,\"event\":\"registered\",\"level\":42,\"env\":2,\"mask\":3}\n", f);
        fclose(f);
    }

    reflux_host_reset();
    level_mods_reset();
    check(event_log_replay(path) == 4, "replay applies registered, mod_set, state and reset rows");
    check(level_mods_count() == 1, "the post-reset stream leaves one live level");
    check(level_mods_env(42) == lm_env_moon(), "replayed registration keeps the environment");
    check(level_mods_mask(42) == lm_default_mask(lm_env_moon()), "replayed level has its moon mask");
    check(level_mods_env(41) < 0, "the reset level is gone, not half-restored");

    check(event_log_replay("/tmp/shank_no_such_event_log.jsonl") == -1,
          "a missing replay stream is refused");
    remove(path);
}

/* A build that has only ported core sim + level must not believe an incoming mask that enables a
 * module it cannot run, and must not dispatch an update it will refuse. */
static void test_capabilities(void) {
    int core_level = lm_bit(lm_mod_core_sim()) | lm_bit(lm_mod_level());

    level_mods_reset();
    reflux_host_reset();
    check(level_mods_capabilities() == lm_all_mask(), "a native build supports every module by default");
    check(level_mods_set_capabilities(core_level) == core_level, "a core build narrows capabilities");
    check(level_mods_register(61, lm_env_surface()) == core_level,
          "a capped build opens at its own mask, not the surface default");
    check(level_mods_set(61, lm_mod_weather_render(), 1) == 0,
          "a capped build refuses to enable an unported module");
    check(level_mods_apply_state(61, lm_all_mask(), 2) == 0 && level_mods_mask(61) == core_level,
          "incoming state cannot enable an unported module");
    check(level_mods_revision(61) == 2, "a newer capped state still advances the revision");
    check(level_mods_set_capabilities(lm_all_mask()) == lm_all_mask(), "capabilities restore for native tests");
    level_mods_reset();
}

/* The build profile table is the one place that says what a target contains, including whether it
 * links a network transport at all. */
static void test_build_profiles(void) {
    check(bp_target_count() == 4, "four build profiles");
    check(bp_target_capabilities(bp_target_native()) == lm_all_mask(),
          "native admits every level module");
    check(bp_target_capabilities(bp_target_llvm()) == lm_all_mask(),
          "LLVM native admits every level module");
    check(bp_target_capabilities(bp_target_wasm_core()) == 3,
          "WASM core admits core sim + level");
    check(bp_target_net(bp_target_native()) == bp_net_host_c(),
          "native networking is host C today");
    check(bp_target_net(bp_target_wasm_core()) == bp_net_none(),
          "WASM core links no transport");
    check(bp_target_net(bp_target_native_core()) == bp_net_none(),
          "native core links no transport");
    check(bp_target_valid(-1) == 0 && bp_target_valid(99) == 0,
          "unknown targets are refused");
    check(bp_target_capabilities(99) == 0 && bp_target_net(99) == bp_net_none(),
          "unknown targets admit nothing");
    check(build_profile_target() == bp_target_native(),
          "the test client applies its own compiled-in target");
    check(build_profile_net() == bp_net_host_c(),
          "the test client reports the profile's transport");
    check(build_profile_apply() == lm_all_mask() && level_mods_capabilities() == lm_all_mask(),
          "applying the native profile sets the registry mask");
}

int main(void) {
    test_modes();
    test_loop();
    test_render();
    test_combat();
    test_regen();
    test_net();
    test_reflux();
    test_level_mods();
    test_level_catalog();
    test_event_log();
    test_event_replay();
    test_capabilities();
    test_build_profiles();
    test_round_sim();

    printf("%s: %d checks, %d failure(s)\n",
           g_failures == 0 ? "game-test OK" : "game-test FAILED", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
