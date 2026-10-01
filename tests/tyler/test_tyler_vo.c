/* test_tyler_vo.c -- headless (no SDL, no window, no audio device) tests for MODE_TYLER, the
 * TYLER VALHANNA cold open: the P0 gates that make the scripted actors actually walk (T6), and
 * (in later steps of the same unit of work) the lines table, voice driver, mixer, wire packet.
 *
 * Run from the repo root (it reads the level JSONs under assets/tyler_levels and, later, assets/tyler_vo):
 *   make test-tyler-vo
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../packages/common/protocol.h"
#include "../../packages/common/physics.h"
#include "../../packages/common/shared_movement.h"
#include "../../packages/common/net_sim.h"
/* Per-binary globals local_game.h expects its host to define (see story_swarm_humanness_test.c). */
int g_story_cutscene_done = 0;
int g_story_outro_requested = 0;
int g_shankpit_is_server = 0;
#include "../../packages/simulation/local_game.h"
#include "../../packages/simulation/tyler_coldopen.h"
#include "../../packages/reflux/reflux_runtime.h"
#include "../../packages/world/level_boxes.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); g_fail++; } else { g_pass++; } } while (0)

/* The real Iceland level into physics.h's custom-level buffers -- the same copy
 * apps/lobby/src/main.c's level_boxes_apply_to_physics does (that one is static in the lobby). */
static int load_level_into_physics(const char *path, CustomLevelData *lvl) {
    if (!level_boxes_load_from_file(path, lvl)) return 0;
    float x[LEVEL_BOXES_MAX], y[LEVEL_BOXES_MAX], z[LEVEL_BOXES_MAX], w[LEVEL_BOXES_MAX], h[LEVEL_BOXES_MAX],
          d[LEVEL_BOXES_MAX], r[LEVEL_BOXES_MAX], g[LEVEL_BOXES_MAX], b[LEVEL_BOXES_MAX];
    int mi[LEVEL_BOXES_MAX];
    for (int i = 0; i < lvl->count; i++) {
        x[i] = lvl->boxes[i].x; y[i] = lvl->boxes[i].y; z[i] = lvl->boxes[i].z;
        w[i] = lvl->boxes[i].w; h[i] = lvl->boxes[i].h; d[i] = lvl->boxes[i].d;
        r[i] = lvl->boxes[i].r; g[i] = lvl->boxes[i].g; b[i] = lvl->boxes[i].b; mi[i] = lvl->boxes[i].material_idx;
    }
    char names[LEVEL_BOXES_MAX_MATERIALS][CUSTOM_LEVEL_MATERIAL_NAME_LEN], shaders[LEVEL_BOXES_MAX_MATERIALS][CUSTOM_LEVEL_MATERIAL_NAME_LEN];
    float spec[LEVEL_BOXES_MAX_MATERIALS], shin[LEVEL_BOXES_MAX_MATERIALS], fric[LEVEL_BOXES_MAX_MATERIALS];
    for (int i = 0; i < lvl->material_count; i++) {
        snprintf(names[i], sizeof names[i], "%.31s", lvl->materials[i].name);
        snprintf(shaders[i], sizeof shaders[i], "%.31s", lvl->materials[i].shader_name);
        spec[i] = lvl->materials[i].specular; shin[i] = lvl->materials[i].shininess; fric[i] = lvl->materials[i].friction;
    }
    phys_set_custom_level_materials(names, shaders, spec, shin, fric, lvl->material_count);
    phys_set_custom_level(x, y, z, w, h, d, r, g, b, mi, lvl->count, lvl->ground_plane_enabled, lvl->ground_plane_squares);
    float sx[LEVEL_BOXES_MAX_SPAWNERS], sy[LEVEL_BOXES_MAX_SPAWNERS], sz[LEVEL_BOXES_MAX_SPAWNERS];
    int st[LEVEL_BOXES_MAX_SPAWNERS], sid[LEVEL_BOXES_MAX_SPAWNERS];
    for (int i = 0; i < lvl->spawner_count; i++) {
        sx[i] = lvl->spawners[i].x; sy[i] = lvl->spawners[i].y; sz[i] = lvl->spawners[i].z;
        st[i] = lvl->spawners[i].team; sid[i] = lvl->spawners[i].id;
    }
    phys_set_custom_level_spawners(sx, sy, sz, st, sid, lvl->spawner_count);
    return 1;
}

static unsigned int g_exit_at = 0;
static int stub_exit(int next_level_id, int target_spawner, unsigned int now_ms) {
    (void)next_level_id; (void)target_spawner; g_exit_at = now_ms; return 1;
}

/* Local MODE_TYLER setup exactly the way lobby_start_tyler_mode does it: local_init_match, the
 * level, spawn the level's own two Characters, wisp override, start the coordinator. */
static int setup_local_tyler(TylerColdOpenState *st, CustomLevelData *lvl, int *tyler, int *hana) {
    local_init_match(1, MODE_TYLER);
    if (!load_level_into_physics("assets/tyler_levels/tyler_1986_iceland.json", lvl)) return 0;
    scene_load(SCENE_CUSTOM_LEVEL);
    local_state.players[0].scene_id = SCENE_CUSTOM_LEVEL;
    story_ai_reset(&local_state);
    int ids[2] = { -1, -1 }, n = 0;
    for (int i = 0; i < lvl->character_count && n < 2; i++) {
        const LevelCharacter *lc = &lvl->characters[i];
        int id = story_ai_spawn_enemy(&local_state, (AIRole)lc->role, lc->kit, lc->x, lc->y, lc->z);
        if (id > 0) ids[n++] = id;
    }
    if (n != 2) return 0;
    for (int i = 1; i < MAX_CLIENTS; i++) local_state.players[i].scene_id = SCENE_CUSTOM_LEVEL;
    phys_respawn(&local_state.players[0], 0);
    local_state.players[0].state = STATE_SPECTATOR;      /* the wisp */
    local_state.players[0].forced_kit = AI_KIT_AUTO;
    reflux_host_reset();
    *tyler = ids[0]; *hana = ids[1];
    tyler_coldopen_start(st, ids[0], ids[1], 1);
    return 1;
}

/* T6: in MODE_TYLER the scripted actors get real movement input AND really walk. Before the
 * story_ai.c gate accepted MODE_TYLER, in_fwd was exactly 0.0 for the whole cold open. */
static void test_t6_actors_walk(void) {
    TylerColdOpenState st; CustomLevelData lvl; int ty, ha;
    memset(&lvl, 0, sizeof lvl);
    if (!setup_local_tyler(&st, &lvl, &ty, &ha)) { CHECK(0, "T6 setup failed (level/characters)"); return; }
    CHECK(local_state.game_mode == MODE_TYLER && local_state.story_phase == STORY_PHASE_PLAYING,
          "T6 local_init_match(MODE_TYLER) -> game_mode %d, story_phase %d (want PLAYING)", local_state.game_mode, local_state.story_phase);
    CHECK(local_state.players[ty].is_bot && local_state.players[ha].is_bot, "T6 Tyler/Hana are NPC bots");
    float tx0 = local_state.players[ty].x, tz0 = local_state.players[ty].z;
    float peak_fwd_tyler = 0.0f, peak_fwd_hana = 0.0f, max_move_tyler = 0.0f;
    int tyler_shot = 0, tyler_dead = 0;
    int beat_seen[8] = {0}, beat2_fwd = 0, beat1_seen = 0;
    float b1x = 0, b1z = 0, unnamed_drift = 0;
    unsigned int now = 1;
    for (int t = 0; t < 2400 && !st.done; t++) {              /* 2400 * 16 ms = 38 s > the 32.9 s cold open */
        now += 16;
        tyler_coldopen_tick(&st, now, 23, stub_exit);
        local_update(0.0f, 0.0f, 0.0f, 0.0f, 0, -1, 0, 0, 0, 0, 0, NULL, now);
        PlayerState *T = &local_state.players[ty], *H = &local_state.players[ha];
        if (fabsf(T->in_fwd) > peak_fwd_tyler) peak_fwd_tyler = fabsf(T->in_fwd);
        if (fabsf(H->in_fwd) > peak_fwd_hana) peak_fwd_hana = fabsf(H->in_fwd);
        if (st.current_beat == 2 && fabsf(T->in_fwd) > 0.0f) beat2_fwd++;
        if (st.current_beat >= 0 && st.current_beat < 8) beat_seen[st.current_beat] = 1;
        float d = sqrtf((T->x - tx0) * (T->x - tx0) + (T->z - tz0) * (T->z - tz0));
        if (d > max_move_tyler) max_move_tyler = d;
        if (T->in_shoot || H->in_shoot) tyler_shot = 1;
        /* Tyler is not named in beat 1 (Hana's): he must hold exactly where beat 0 left him. */
        if (st.current_beat == 1 && st.beat_triggered) {
            if (!beat1_seen) { beat1_seen = 1; b1x = T->x; b1z = T->z; }
            float dd = sqrtf((T->x - b1x) * (T->x - b1x) + (T->z - b1z) * (T->z - b1z));
            if (dd > unnamed_drift) unnamed_drift = dd;
        }
        if (T->state == STATE_DEAD) tyler_dead = 1;
    }
    CHECK(st.done && g_exit_at > 0, "T6 the cold open ran to its exit (done=%d exit_at=%u)", st.done, g_exit_at);
    CHECK(peak_fwd_tyler > 0.2f, "T6 Tyler's movement input peaked at %.3f in a travel beat (was exactly 0.000 before the gate fix)", peak_fwd_tyler);
    CHECK(peak_fwd_hana > 0.2f, "T6 Hana's movement input peaked at %.3f", peak_fwd_hana);
    CHECK(beat2_fwd > 20, "T6 Tyler had nonzero forward input on %d ticks of beat 2 (travel to the printer)", beat2_fwd);
    CHECK(max_move_tyler > 4.0f, "T6 Tyler physically moved %.2f units from his spawn (the NPC branch applies the input)", max_move_tyler);
    CHECK(!tyler_shot && !tyler_dead, "T6 neither actor ever fired and Tyler never died (no combat/ally AI in MODE_TYLER)");
    CHECK(beat1_seen && unnamed_drift < 0.5f, "T6 an actor a beat does not name holds still (Tyler drifted %.2f units through Hana's beat 1)", unnamed_drift);
    for (int b = 0; b < 8; b++) CHECK(beat_seen[b], "T6 beat %d ran", b);
    /* Tyler really reaches the printer marker (beat 2/4: (4, 1, -4.5)) -- within the AI's own 4-unit arrival radius. */
    PlayerState *T = &local_state.players[ty];
    float dxm = T->x - 4.0f, dzm = T->z - (-2.6f);  /* final beat marker */
    CHECK(sqrtf(dxm * dxm + dzm * dzm) < 5.0f, "T6 Tyler ends near the exit-button marker (%.1f, %.1f)", T->x, T->z);
}

/* T6b: the gate is an allow-list, not a blanket "any mode": deathmatch levels with characters must
 * still see story_ai_tick as a no-op (their live behavior is unchanged). */
static void test_t6b_other_modes_unchanged(void) {
    TylerColdOpenState st; CustomLevelData lvl; int ty, ha;
    memset(&lvl, 0, sizeof lvl);
    if (!setup_local_tyler(&st, &lvl, &ty, &ha)) { CHECK(0, "T6b setup failed"); return; }
    local_state.game_mode = MODE_DEATHMATCH;
    local_state.players[ty].in_fwd = 0.0f;
    story_ai_trigger_scripted(ty, 4.0f, 1.0f, -4.5f, 3000, 100);
    story_ai_tick(&local_state, 116);
    CHECK(local_state.players[ty].in_fwd == 0.0f, "T6b MODE_DEATHMATCH: story_ai_tick still a no-op (in_fwd %.3f)", local_state.players[ty].in_fwd);
    local_state.game_mode = MODE_TYLER;
    story_ai_tick(&local_state, 132);
    CHECK(local_state.players[ty].in_fwd > 0.0f, "T6b MODE_TYLER: story_ai_tick drives the scripted actor (in_fwd %.3f)", local_state.players[ty].in_fwd);
}

/* T6c: a scripted actor walks TOWARD its marker. story_ai.c's ai_angle_to used a yaw convention
 * 180 degrees opposite the sim's movement basis, so a marker at z=-30 sent the actor to z=+50. */
static void test_t6c_walks_toward_marker(void) {
    local_init_match(1, MODE_TYLER);
    story_ai_reset(&local_state);
    local_state.players[0].state = STATE_SPECTATOR; local_state.players[0].x = 100; local_state.players[0].z = 100;
    int id = story_ai_spawn_enemy(&local_state, AI_ROLE_STORY_ALLY, AI_KIT_STAN, 0, 0, 0);
    PlayerState *p = &local_state.players[id];
    unsigned int now = 1;
    story_ai_trigger_scripted(id, 0.0f, 0.0f, -30.0f, 5000, now);
    for (int t = 0; t < 200; t++) { now += 16; local_update(0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, NULL, now); }
    CHECK(p->z < -15.0f && fabsf(p->x) < 5.0f, "T6c marker at (0,-30): actor ended at (%.1f, %.1f) -- toward it, not away (was z=+50)", p->x, p->z);
    /* ...and it stopped at its arrival radius instead of overshooting */
    CHECK(p->z > -33.0f, "T6c actor did not overshoot the marker (z=%.1f)", p->z);
    /* facing: yaw 0 looks down -Z in the sim basis; the actor ends facing the marker direction */
    float fx = -sinf(p->yaw * 0.0174533f), fz = -cosf(p->yaw * 0.0174533f);
    CHECK(fz < -0.9f, "T6c actor faces -Z toward its marker (forward=(%.2f,%.2f))", fx, fz);
}

int main(void) {
    srand(7);
    test_t6_actors_walk();
    test_t6b_other_modes_unchanged();
    test_t6c_walks_toward_marker();
    printf("%s: %d passed, %d failed\n", g_fail ? "FAILED" : "ALL PASS", g_pass, g_fail);
    return g_fail != 0;
}
