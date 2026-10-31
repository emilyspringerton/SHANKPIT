/* skin_registry_test.c -- an imported clip set becomes a drawable body.
 *
 * The chain this proves is the founder's own: NOCK imports a set, the engine finds it, loads it as a
 * kit against its named mesh, and skins vertices from it. Everything runs against the REAL committed
 * set (assets/goldenband/clipsets/mike) and the real Mike gskel/gmesh, so a change to either the
 * binder's output or the loader breaks a check instead of a character silently vanishing.
 *
 * Build/run: make test-skin-registry
 */
#include "skin_registry.h"
#include "../goldenband/gband_skel_npc.h"
#include "../common/mat4.h"

#include <stdio.h>
#include <stdlib.h>
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

/* Fixture commands are disposable, but the C library marks system() warn_unused_result and this TU
   builds with -Werror, so every call reads its status. */
static void run_fixture_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc != 0) printf("note: fixture command failed (%d): %s\n", rc, cmd);
}

/* Counts what a real draw would have submitted, without a GL context. */
static int g_verts = 0;
static void count_skinned(const float *verts, int vert_count, const Mat4 *mvp, const Mat4 *model) {
    (void)verts; (void)mvp; (void)model;
    if (vert_count > 0) g_verts += vert_count;
}

int main(void) {
    const char *clipsets = "assets/goldenband/clipsets";
    char probe[512];
    snprintf(probe, sizeof probe, "%.*s/mike/skin.json", (int)sizeof probe - 16, clipsets);
    FILE *f = fopen(probe, "rb");
    if (!f) {
        printf("SKIP  %s missing -- bind the mike set first (make bind-mike-clips)\n", probe);
        return 0;
    }
    fclose(f);

    printf("== skin_registry: what the engine is offered ==\n");
    SkinRegistry reg;
    int n = skin_registry_scan(&reg, clipsets);
    ok(n >= 1, "the imported set is discovered");
    int mike = -1;
    for (int i = 0; i < reg.count; i++)
        if (strcmp(reg.entries[i].name, "mike") == 0) mike = i;
    ok(mike >= 0, "it is offered under its own name");
    ok(mike >= 0 && strcmp(reg.entries[mike].mesh, "Mike") == 0,
       "skin.json names the mesh, so the clips have a rig to be drawn on");
    ok(mike >= 0 && reg.entries[mike].bound == ANIM_STATE_COUNT,
       "all thirteen contract clips are present");
    ok(mike >= 0 && reg.entries[mike].complete == 1, "and the set reports itself complete");

    printf("== skin_registry: refusals ==\n");
    {
        SkinRegistry empty;
        ok(skin_registry_scan(&empty, "var/definitely_not_here") == 0,
           "a missing directory is zero offers, not an error");
        ok(skin_registry_scan(&empty, "var") == 0, "a directory with no sets offers nothing");
    }

    printf("== the set loads as a real kit ==\n");
    if (mike < 0) {
        printf("\n%d checks, %d failed\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }
    int kit = gband_skel_npc_load_clipset(reg.entries[mike].dir);
    ok(kit >= 0, "gband_skel_npc_load_clipset accepted the bundle");
    ok(gband_skel_npc_kit_ready(kit) == 1, "the kit is ready to draw");
    ok(gband_skel_npc_kit_skel(kit) != NULL, "and it has the mesh's skeleton (the ragdoll pool needs one)");

    printf("== and it actually skins vertices ==\n");
    {
        Mat4 vp, model;
        memset(&vp, 0, sizeof vp);
        memset(&model, 0, sizeof model);
        vp.m[0] = vp.m[5] = vp.m[10] = vp.m[15] = 1.0f;
        model.m[0] = model.m[5] = model.m[10] = model.m[15] = 1.0f;
        g_verts = 0;
        gband_skel_npc_draw(kit, 0, 0.0f, 0.0f, 0.0f, 0.0f, 16.0f,
                            GBAND_SKEL_NPC_ANIM_AUTO, &vp, count_skinned);
        ok(g_verts > 0, "an imported skin produces a skinned mesh (vertices submitted)");

        int idle_verts = g_verts;
        g_verts = 0;
        gband_skel_npc_draw(kit, 0, 0.0f, 0.0f, 0.0f, 0.0f, 16.0f,
                            GBAND_SKEL_NPC_ANIM_GREET, &vp, count_skinned);
        ok(g_verts == idle_verts, "a gesture draws the same body, not a different one");
    }

    printf("== a second imported set is a second skin ==\n");
    {
        /* A real second set, in the real layout: <assets>/clipsets/<name>/ with the mesh one level up,
           so this exercises the same mount-point resolution a NOCK import goes through. */
        ok(system("mkdir -p var/skin_registry_test/goldenband/clipsets/twin && "
                  "cp assets/goldenband/Mike.gskel assets/goldenband/Mike.gmesh "
                  "var/skin_registry_test/goldenband/ && "
                  "cp assets/goldenband/clipsets/mike/*.gband* var/skin_registry_test/goldenband/clipsets/twin/ && "
                  "cp assets/goldenband/clipsets/mike/skin.json "
                  "var/skin_registry_test/goldenband/clipsets/twin/skin.json 2>/dev/null") >= 0,
           "second set fixture written");
        SkinRegistry r2;
        int n2 = skin_registry_scan(&r2, "var/skin_registry_test/goldenband/clipsets");
        ok(n2 == 1 && strcmp(r2.entries[0].name, "twin") == 0,
           "a second set is offered under its own name (that is how NPCs get their own body)");
        int k2 = gband_skel_npc_load_clipset(r2.entries[0].dir);
        ok(k2 >= 0 && k2 != kit, "and loads into its own kit slot, not over the first");
        run_fixture_cmd("rm -rf var/skin_registry_test");
    }

    printf("== a set without a mesh is not a skin ==\n");
    {
        ok(system("mkdir -p var/skin_registry_test/nomesh && cp assets/goldenband/clipsets/mike/anim_idle.gband "
                  "var/skin_registry_test/nomesh/ 2>/dev/null") >= 0, "fixture written");
        SkinRegistry r2;
        skin_registry_scan(&r2, "var/skin_registry_test");
        ok(r2.count == 0 && r2.skipped >= 1, "a directory with no skin.json is skipped, not offered");
        ok(gband_skel_npc_load_clipset("var/skin_registry_test/nomesh") == -1,
           "and the loader refuses it directly");
        run_fixture_cmd("rm -rf var/skin_registry_test");
    }

    gband_skel_npc_shutdown();
    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
