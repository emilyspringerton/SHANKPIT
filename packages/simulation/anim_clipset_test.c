/* anim_clipset_test.c -- the thirteen-clip selector, driving REAL .gband files.
 *
 * The clips are the repo's own (Mike_Idle/Mike_Walk/Mike_Hello), copied into a temp directory under
 * the contract names by this test. That is deliberate: it exercises GOLDENBAND's real loader, the
 * real manifest channel parsing and the real duration/tick-rate values, not a struct built by hand.
 *
 * What is asserted is the behaviour a renderer would notice: the posture loops and wraps, an action
 * holds its last tick, a state change restarts the clip and starts the rules' own crossfade, and a
 * bundle missing a clip falls back to one that exists instead of drawing nothing.
 *
 * Build/run: make test-anim-clipset
 */
#include "anim_clipset.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

static int copy_file(const char *src, const char *dst) {
    FILE *a = fopen(src, "rb");
    if (!a) return 0;
    FILE *b = fopen(dst, "wb");
    if (!b) { fclose(a); return 0; }
    char buf[8192];
    size_t n;
    int okc = 1;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) {
        if (fwrite(buf, 1, n, b) != n) { okc = 0; break; }
    }
    fclose(a);
    fclose(b);
    return okc;
}

/* Build <dir>/<contract>.gband(+.json) for every state from three real clips. */
static int build_bundle(const char *dir, const char *src_dir, const char *idle_src) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", dir, dir);
    if (system(cmd) != 0) return 0;
    int built = 0;
    for (int s = 0; s < ANIM_STATE_COUNT; s++) {
        const char *stem = (s == ANIM_IDLE || s == ANIM_CROUCH_IDLE) ? idle_src : "Mike_Walk";
        char src[512], dst[512];
        int good = 1;
        snprintf(src, sizeof src, "%s/%s.gband", src_dir, stem);
        snprintf(dst, sizeof dst, "%s/%s.gband", dir, anim_state_clip_name(s));
        good = good && copy_file(src, dst);
        snprintf(src, sizeof src, "%s/%s.gband.json", src_dir, stem);
        snprintf(dst, sizeof dst, "%s/%s.gband.json", dir, anim_state_clip_name(s));
        good = good && copy_file(src, dst);
        if (good) built++;
    }
    return built;
}

int main(void) {
    const char *assets = "assets/goldenband";
    char probe[512];
    snprintf(probe, sizeof probe, "%s/Mike_Idle.gband", assets);
    FILE *f = fopen(probe, "rb");
    if (!f) {
        printf("SKIP  %s not present -- run from the repo root\n", probe);
        return 0;
    }
    fclose(f);

    const char *dir = "var/anim_clipset_test/full";
    int built = build_bundle(dir, assets, "Mike_Idle");
    ok(built == ANIM_STATE_COUNT, "a 13-clip bundle was assembled from real .gband files");

    printf("== anim_clipset: load ==\n");
    AnimClipSet set;
    int n = anim_clipset_load(&set, dir);
    ok(n == ANIM_STATE_COUNT, "all thirteen clips loaded through gseq_clip_load");
    ok(anim_clipset_all_loaded(&set), "the set reports itself complete");
    ok(set.clips[ANIM_IDLE].channel_count > 0, "channels came from the manifest, not a guess");
    ok(set.clips[ANIM_IDLE].clip.duration_ticks > 1 && set.clips[ANIM_IDLE].clip.tick_rate > 0,
       "the real duration and tick rate are in the clip");

    printf("== anim_clipset: postures loop ==\n");
    {
        PlayerState p;
        memset(&p, 0, sizeof p);
        p.on_ground = 1;
        unsigned int d = set.clips[ANIM_IDLE].clip.duration_ticks;
        /* 100 ms, not 16: these clips are 30 Hz, so a single 16 ms frame is zero whole ticks and
         * "it advanced" would be testing nothing. */
        int st = anim_clipset_tick(&set, &p, 0, 0, 100);
        ok(st == ANIM_IDLE, "a standing player is IDLE");
        ok(set.tick > 0, "the clip advanced with dt");
        anim_clipset_tick(&set, &p, 0, 0, (int)((d + 5) * 1000 / (int)set.clips[ANIM_IDLE].clip.tick_rate));
        ok(set.tick < d, "a posture wraps by duration instead of holding the last frame");
        ok(set.one_shot_finished == 0, "a looping clip is never 'finished'");
    }

    printf("== anim_clipset: the rules choose the clip ==\n");
    {
        PlayerState p;
        memset(&p, 0, sizeof p);
        p.on_ground = 1;
        p.vx = 0.5f;
        anim_clipset_tick(&set, &p, 0, 0, 16);
        ok(set.wanted == ANIM_RUN && set.state == ANIM_RUN, "0.5 units/tick selects RUN, and its clip is the one sampled");

        p.is_shooting = 1;
        anim_clipset_tick(&set, &p, 0, 0, 16);
        ok(set.wanted == ANIM_WEAPON_FIRE && set.state == ANIM_WEAPON_FIRE, "firing wins over locomotion");
        ok(anim_state_additive(set.state) == 1, "and the rules mark it additive (upper body only)");

        p.katana_slash_timer = 10;
        anim_clipset_tick(&set, &p, 0, 0, 16);
        ok(set.wanted == ANIM_MELEE_SWING && set.state == ANIM_MELEE_SWING, "a swing outranks firing");
        ok(set.tick == 0, "entering a state restarts the clip at tick 0");
        ok(anim_clipset_blend_weight(&set) < 1.0f, "entering a state starts the crossfade");

        unsigned int d = set.clips[ANIM_MELEE_SWING].clip.duration_ticks;
        anim_clipset_tick(&set, &p, 0, 0, (int)((d + 50) * 1000 / (int)set.clips[ANIM_MELEE_SWING].clip.tick_rate));
        ok(set.tick == d - 1 && set.one_shot_finished == 1, "a one-shot holds its last tick instead of wrapping");
        ok(anim_clipset_blend_weight(&set) == 1.0f, "the crossfade completes and stays complete");

        p.katana_slash_timer = 0;
        p.is_shooting = 0;
        anim_clipset_tick(&set, &p, 0, 0, 16);
        ok(set.state == ANIM_RUN && set.tick == 0, "the action releases back to locomotion, at the clip's start");
    }

    printf("== anim_clipset: airborne and crouch ==\n");
    {
        PlayerState p;
        memset(&p, 0, sizeof p);
        p.on_ground = 0;
        p.jump_timer = 3;
        anim_clipset_tick(&set, &p, 0, 0, 16);
        ok(set.state == ANIM_JUMP_START, "the first ticks off the ground are JUMP_START");
        p.jump_timer = 40;
        anim_clipset_tick(&set, &p, 0, 0, 16);
        ok(set.state == ANIM_IN_AIR, "past the window it is IN_AIR");
        p.on_ground = 1;
        p.crouching = 1;
        p.vx = 0.0f;
        anim_clipset_tick(&set, &p, 0, 0, 16);
        ok(set.state == ANIM_CROUCH_IDLE, "crouching owns the pose even standing still");
    }

    printf("== anim_clipset: a bundle with holes still animates ==\n");
    {
        const char *part = "var/anim_clipset_test/partial";
        char cmd[512];
        snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", part, part);
        ok(system(cmd) == 0, "partial bundle directory created");
        char src[512], dst[512];
        snprintf(src, sizeof src, "%s/Mike_Idle.gband", assets);
        snprintf(dst, sizeof dst, "%s/anim_idle.gband", part);
        ok(copy_file(src, dst), "partial bundle holds a real idle clip");
        snprintf(src, sizeof src, "%s/Mike_Idle.gband.json", assets);
        snprintf(dst, sizeof dst, "%s/anim_idle.gband.json", part);
        ok(copy_file(src, dst), "and its manifest");

        AnimClipSet s2;
        int got = anim_clipset_load(&s2, part);
        ok(got == 1, "only the idle loaded");
        ok(!anim_clipset_all_loaded(&s2), "the set reports itself incomplete");
        PlayerState p;
        memset(&p, 0, sizeof p);
        p.on_ground = 1;
        p.vx = 0.5f;
        int st = anim_clipset_tick(&s2, &p, 0, 0, 16);
        ok(s2.wanted == ANIM_RUN, "the rules still want RUN");
        ok(st == ANIM_IDLE && s2.state == ANIM_IDLE, "with no run clip it plays the idle rather than nothing");
        ok(anim_clipset_sample(&s2) != NULL, "and there is a real pose to draw");
        anim_clipset_free(&s2);
    }

    printf("== anim_clipset: nothing loaded ==\n");
    {
        const char *empty = "var/anim_clipset_test/empty";
        char cmd[512];
        snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", empty, empty);
        ok(system(cmd) == 0, "empty directory created");
        AnimClipSet s3;
        ok(anim_clipset_load(&s3, empty) == 0, "an empty bundle loads nothing");
        PlayerState p;
        memset(&p, 0, sizeof p);
        p.on_ground = 1;
        ok(anim_clipset_tick(&s3, &p, 0, 0, 16) == -1, "tick reports -1, so a caller knows not to draw a pose");
        ok(anim_clipset_sample(&s3) == NULL, "and there is no pose");
        ok(anim_clipset_clip(&s3) == NULL, "nor a clip");
    }

    anim_clipset_free(&set);
    /* The bundles are this test's own artifacts (copies of real assets), not content: leaving them
       behind means an untracked var/anim_clipset_test/ in every working tree after a test run. */
    if (system("rm -rf var/anim_clipset_test") != 0) printf("warn: could not remove var/anim_clipset_test\n");
    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
