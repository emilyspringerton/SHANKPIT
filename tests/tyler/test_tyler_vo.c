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
#include "../../packages/simulation/tyler_voice_lines.h"   /* GENERATED: g_tyler_voice_clip_ms, subtitles */
#include <stdint.h>


/* ---- compact SHA-256 (FIPS 180-4), just enough to verify assets/tyler_vo/MANIFEST.sha256 ---- */
typedef struct { uint32_t h[8]; uint8_t buf[64]; uint64_t len; size_t fill; } Sha256;
static const uint32_t K256[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(Sha256 *c, const uint8_t *p) {
    uint32_t w[64], a, b, cc, d, e, f, g, h, t1, t2; int i;
    for (i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (i = 16; i < 64; i++) { uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3), s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10); w[i] = w[i-16] + s0 + w[i-7] + s1; }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void sha_init(Sha256 *c) { static const uint32_t i0[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memcpy(c->h, i0, sizeof i0); c->len = 0; c->fill = 0; }
static void sha_update(Sha256 *c, const uint8_t *d, size_t n) {
    c->len += n;
    while (n) { size_t k = 64 - c->fill; if (k > n) k = n; memcpy(c->buf + c->fill, d, k); c->fill += k; d += k; n -= k; if (c->fill == 64) { sha_block(c, c->buf); c->fill = 0; } }
}
static void sha_final(Sha256 *c, char out_hex[65]) {
    uint64_t bits = c->len * 8; uint8_t pad = 0x80; sha_update(c, &pad, 1); pad = 0; while (c->fill != 56) sha_update(c, &pad, 1);
    uint8_t lb[8]; for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i)); sha_update(c, lb, 8);
    for (int i = 0; i < 8; i++) snprintf(out_hex + 8 * i, 9, "%08x", c->h[i]);
}

/* A committed clip: whole file bytes + the decoded facts the engine's loader will check. */
typedef struct { uint8_t *bytes; long size; int rate, channels, bits; long samples; } WavInfo;
static int wav_read(const char *path, WavInfo *w) {
    memset(w, 0, sizeof *w);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); w->size = ftell(f); fseek(f, 0, SEEK_SET);
    w->bytes = (uint8_t *)malloc((size_t)w->size);
    if (!w->bytes || fread(w->bytes, 1, (size_t)w->size, f) != (size_t)w->size) { fclose(f); return 0; }
    fclose(f);
    if (w->size < 44 || memcmp(w->bytes, "RIFF", 4) || memcmp(w->bytes + 8, "WAVEfmt ", 8)) return 0;
    w->channels = w->bytes[22] | w->bytes[23] << 8;
    w->rate = w->bytes[24] | w->bytes[25] << 8 | w->bytes[26] << 16;
    w->bits = w->bytes[34] | w->bytes[35] << 8;
    if (memcmp(w->bytes + 36, "data", 4)) return 0;
    w->samples = (long)(w->bytes[40] | w->bytes[41] << 8 | w->bytes[42] << 16 | (uint32_t)w->bytes[43] << 24) / 2;
    return 1;
}

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


/* T2: the committed assets, the generated lines table and the coordinator's real timeline agree. */
static void test_t2_assets_and_timeline(void) {
    /* manifest: sha256 + bytes + duration of every committed clip */
    FILE *mf = fopen("assets/tyler_vo/MANIFEST.sha256", "r");
    CHECK(mf != NULL, "T2 assets/tyler_vo/MANIFEST.sha256 exists");
    int manifest_rows = 0;
    char ln[512];
    while (mf && fgets(ln, sizeof ln, mf)) {
        if (ln[0] == '#' || ln[0] == '\n') continue;
        char sha[80], name[128]; long bytes, dur;
        if (sscanf(ln, "%64s %ld %ld %127s", sha, &bytes, &dur, name) != 4) { CHECK(0, "T2 bad manifest line: %s", ln); continue; }
        char path[256]; snprintf(path, sizeof path, "assets/tyler_vo/%s", name);
        WavInfo w;
        if (!wav_read(path, &w)) { CHECK(0, "T2 %s unreadable / not a canonical WAV", path); continue; }
        Sha256 c; char hex[65]; sha_init(&c); sha_update(&c, w.bytes, (size_t)w.size); sha_final(&c, hex);
        CHECK(strcmp(hex, sha) == 0, "T2 %s sha256 matches the manifest (clip was not re-rendered behind the table's back)", name);
        CHECK(w.size == bytes, "T2 %s is %ld bytes (manifest %ld)", name, w.size, bytes);
        CHECK(w.rate == 22050 && w.channels == 1 && w.bits == 16, "T2 %s is 22050 Hz mono PCM16 (%d Hz, %d ch, %d bit)", name, w.rate, w.channels, w.bits);
        long ms = (w.samples * 1000L + 11025L) / 22050L;
        CHECK(labs(ms - dur) <= 1, "T2 %s decodes to %ld ms, manifest says %ld ms", name, ms, dur);
        free(w.bytes); manifest_rows++;
    }
    if (mf) fclose(mf);
    CHECK(manifest_rows == g_tyler_voice_line_count, "T2 manifest lists %d clips, the lines table has %d", manifest_rows, g_tyler_voice_line_count);

    /* every table row: file on disk, duration matches within 1 ms, per-beat end == the generated clip_ms */
    unsigned int beat_end[TYLER_COLDOPEN_MAX_BEATS] = {0};
    for (int i = 0; i < g_tyler_voice_line_count; i++) {
        const TylerVoiceLine *L = &g_tyler_voice_lines[i];
        WavInfo w;
        int ok = wav_read(L->file, &w);
        CHECK(ok, "T2 line %d: %s readable", i, L->file);
        if (ok) {
            long ms = (w.samples * 1000L + 11025L) / 22050L;
            CHECK(labs(ms - (long)L->dur_ms) <= 1, "T2 line %d: %s is %ld ms on disk vs %u ms in the table", i, L->file, ms, L->dur_ms);
            free(w.bytes);
        }
        CHECK(L->beat >= 0 && L->beat < TYLER_COLDOPEN_MAX_BEATS && (L->speaker == TYLER_ACTOR_TYLER || L->speaker == TYLER_ACTOR_HANA), "T2 line %d has a sane beat/speaker", i);
        if (L->offset_ms + L->dur_ms > beat_end[L->beat]) beat_end[L->beat] = L->offset_ms + L->dur_ms;
    }
    for (int b = 0; b < TYLER_COLDOPEN_MAX_BEATS; b++)
        CHECK(beat_end[b] == g_tyler_voice_clip_ms[b], "T2 beat %d: max(offset+dur) %u == g_tyler_voice_clip_ms %u", b, beat_end[b], g_tyler_voice_clip_ms[b]);
    CHECK(g_tyler_voice_clip_ms[6] == 0, "T2 beat 6 is the silent action beat");

    /* subtitles and VO share one source: the sentence beat 0's HUD box used to omit is now there */
    CHECK(strstr(g_tyler_coldopen_beats[0].subtitle, "Compte les sorties") != NULL, "T2 beat 0 subtitle carries Hana's second sentence (it was spoken but never subtitled)");
    CHECK(strstr(g_tyler_coldopen_beats[0].subtitle, "[EN:") != NULL, "T2 beat 0 subtitle keeps the [EN: ...] gloss");
    for (int b = 0; b < TYLER_COLDOPEN_MAX_BEATS; b++) {
        const char *sub = g_tyler_coldopen_beats[b].subtitle;
        int ascii = 1; for (const char *p = sub; *p; p++) if ((unsigned char)*p >= 128) ascii = 0;
        CHECK(sub[0] != '\0' && ascii && strlen(sub) < TYLER_COLDOPEN_SUBTITLE_LEN, "T2 beat %d subtitle is non-empty ASCII and fits (%zu chars)", b, strlen(sub));
    }

    /* the REAL coordinator on a simulated 16 ms clock: every voiced line ends well before the next beat */
    TylerColdOpenState st; CustomLevelData lvl; int ty, ha;
    memset(&lvl, 0, sizeof lvl);
    if (!setup_local_tyler(&st, &lvl, &ty, &ha)) { CHECK(0, "T2 setup failed"); return; }
    unsigned int beat_t[TYLER_COLDOPEN_MAX_BEATS + 1] = {0}; int nb = 0, cursor = 0;
    unsigned int now = 1; g_exit_at = 0;
    for (int t = 0; t < 4000 && !st.done; t++) {
        now += 16;
        tyler_coldopen_tick(&st, now, 23, stub_exit);
        for (int n = reflux_host_log_size(); cursor < n; cursor++)
            if (reflux_host_action_type_at(cursor) == REFLUX_ACTION_TYLER_BEAT && nb < TYLER_COLDOPEN_MAX_BEATS) beat_t[nb++] = now;
    }
    CHECK(nb == TYLER_COLDOPEN_MAX_BEATS && st.done, "T2 coordinator dispatched %d beats and exited (done=%d)", nb, st.done);
    beat_t[TYLER_COLDOPEN_MAX_BEATS] = g_exit_at;
    for (int i = 0; i < g_tyler_voice_line_count; i++) {
        const TylerVoiceLine *L = &g_tyler_voice_lines[i];
        long end = (long)beat_t[L->beat] + (long)L->offset_ms + (long)L->dur_ms;
        long slack = (long)beat_t[L->beat + 1] - end;
        CHECK(slack >= 470, "T2 line %d (beat %d) ends %ld ms before the next beat starts (hold pad is 500)", i, L->beat, slack);
    }
}

int main(void) {
    srand(7);
    test_t6_actors_walk();
    test_t6b_other_modes_unchanged();
    test_t6c_walks_toward_marker();
    test_t2_assets_and_timeline();
    printf("%s: %d passed, %d failed\n", g_fail ? "FAILED" : "ALL PASS", g_pass, g_fail);
    return g_fail != 0;
}
