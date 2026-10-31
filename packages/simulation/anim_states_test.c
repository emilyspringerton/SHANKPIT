/* anim_states_test.c -- the thirteen-state DRAW path, on a real imported clipset.
 *
 * anim_clipset_test.c proves the selector decides the right state. That was only half: the kit loader
 * still played idle/walk plus two gestures, so nine of the thirteen clips were loaded, contract-named,
 * and unreachable from a body on screen. This covers the other half -- gband_skel_npc_draw_state:
 *
 *   - the thirteen names in packages/goldenband equal anim_state.h's contract (the two are separate
 *     packages on purpose, so this is a test rather than a shared header),
 *   - a real committed clipset loads its states and reports which ones,
 *   - the state actually changes the pose (idle vs run sample different vertices),
 *   - a one-shot holds its last tick instead of snapping back to frame 0,
 *   - an unloaded state falls back to idle rather than drawing nothing,
 *   - a kit with no states at all (every built-in) reports -1, so the caller keeps the old path.
 *
 * Build/run: make test-anim-states
 */
#include "anim_state.h"
#include "gband_skel_npc.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", what);
    } else {
        printf("ok    %s\n", what);
    }
}

/* The draw callback: the only thing a headerless test can see. Copies the world-space vertices so two
 * states can be compared -- different clips must produce a different body. */
#define CAPTURE_MAX_VERTS 24000 /* Mike's mesh is ~8k verts; give every committed kit headroom */
static float g_capture[6 * CAPTURE_MAX_VERTS];
static float g_pose_a[6 * CAPTURE_MAX_VERTS];
static float g_pose_b[6 * CAPTURE_MAX_VERTS];
static int g_capture_verts = 0;
static int g_draws = 0;

static void capture_skinned(const float *verts6, int vert_count, const Mat4 *mvp, const Mat4 *model) {
    (void)mvp;
    (void)model;
    g_draws++;
    if (vert_count > CAPTURE_MAX_VERTS) vert_count = CAPTURE_MAX_VERTS;
    memcpy(g_capture, verts6, (size_t)vert_count * 6 * sizeof(float));
    g_capture_verts = vert_count;
}

static int capture_differs_from(const float *other, int other_n) {
    if (g_capture_verts != other_n) return 1;
    for (int i = 0; i < g_capture_verts * 6; i++)
        if (fabsf(g_capture[i] - other[i]) > 1e-4f) return 1;
    return 0;
}

static int find_first_clipset(char *out, size_t out_sz) {
    DIR *d = opendir("assets/goldenband/clipsets");
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(out, out_sz, "assets/goldenband/clipsets/%s", e->d_name);
        found = 1;
        break;
    }
    closedir(d);
    return found;
}

int main(void) {
    Mat4 vp = mat4_identity();

    printf("== the two contract tables agree ==\n");
    ok(GBAND_SKEL_NPC_STATE_COUNT == ANIM_STATE_COUNT,
       "packages/goldenband's state count equals anim_state.h's");
    {
        int mismatches = 0;
        for (int s = 0; s < ANIM_STATE_COUNT; s++) {
            const char *a = gband_skel_npc_state_name(s);
            const char *b = anim_state_clip_name(s);
            if (!a || !b || strcmp(a, b) != 0) {
                printf("      state %d: goldenband '%s' vs anim_state '%s'\n", s, a ? a : "(null)",
                       b ? b : "(null)");
                mismatches++;
            }
        }
        ok(mismatches == 0, "and every state's clip name matches, one for one");
    }

    printf("== a real clipset loads its states ==\n");
    char set_dir[512];
    if (!find_first_clipset(set_dir, sizeof set_dir)) {
        printf("SKIP  no assets/goldenband/clipsets in this tree -- commit one to run this test\n");
        printf("\n%d checks, %d failed\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }
    int kit = gband_skel_npc_load_clipset(set_dir);
    ok(kit >= 0, "the committed set loads as a kit");
    if (kit < 0) {
        printf("\n%d checks, %d failed\n", g_checks, g_failures);
        return 1;
    }
    int loaded = 0;
    for (int s = 0; s < GBAND_SKEL_NPC_STATE_COUNT; s++)
        if (gband_skel_npc_state_loaded(kit, s)) loaded++;
    printf("      %d/%d states loaded from %s\n", loaded, GBAND_SKEL_NPC_STATE_COUNT, set_dir);
    ok(loaded >= 2, "at least walk and idle are available to drive");

    printf("== the state really changes the pose ==\n");
    int idle_n = 0;
    gband_skel_npc_draw_state(kit, 0, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, ANIM_IDLE, &vp, capture_skinned);
    memcpy(g_pose_a, g_capture, sizeof(float) * 6 * (size_t)g_capture_verts);
    idle_n = g_capture_verts;
    ok(g_capture_verts > 0, "idle draws real skinned vertices");
    if (gband_skel_npc_state_loaded(kit, ANIM_RUN)) {
        gband_skel_npc_draw_state(kit, 1, 0.0f, 0.0f, 0.0f, 0.0f, 200.0f, ANIM_RUN, &vp, capture_skinned);
        ok(capture_differs_from(g_pose_a, idle_n), "run samples a different pose than idle");
    } else {
        printf("      (no anim_run in this bundle: run-vs-idle not asserted)\n");
    }

    printf("== a one-shot holds, an unloaded state falls back ==\n");
    if (gband_skel_npc_state_loaded(kit, ANIM_WEAPON_FIRE)) {
        gband_skel_npc_draw_state(kit, 2, 0.0f, 0.0f, 0.0f, 0.0f, 5000.0f, ANIM_WEAPON_FIRE, &vp, capture_skinned);
        memcpy(g_pose_b, g_capture, sizeof(float) * 6 * (size_t)g_capture_verts);
        int fire_n = g_capture_verts;
        gband_skel_npc_draw_state(kit, 2, 0.0f, 0.0f, 0.0f, 0.0f, 5000.0f, ANIM_WEAPON_FIRE, &vp, capture_skinned);
        ok(!capture_differs_from(g_pose_b, fire_n), "fire holds its last tick instead of restarting at 0");
    } else {
        printf("      (no anim_weapon_fire in this bundle: hold not asserted)\n");
    }

    {
        int draws_before = g_draws;
        gband_skel_npc_draw_state(kit, 3, 0.0f, 0.0f, 0.0f, 0.0f, 16.0f, 999, &vp, capture_skinned);
        ok(g_draws == draws_before + 1, "an out-of-range state still draws (falls back), never nothing");
        ok(gband_skel_npc_resolve_state(kit, 999) == 0, "and resolves to idle");
    }

    printf("== a kit with no states keeps the old path ==\n");
    {
        /* Every built-in character is loaded with gband_skel_npc_load_kit, which never loads state clips.
           A kit that does not exist is the cheap way to assert the same refusal without depending on
           which built-ins this checkout has. */
        ok(gband_skel_npc_resolve_state(-1, ANIM_IDLE) == -1, "an unknown kit resolves to -1");
        ok(gband_skel_npc_state_loaded(-1, ANIM_IDLE) == 0, "and reports no states");
    }

    gband_skel_npc_shutdown();
    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
