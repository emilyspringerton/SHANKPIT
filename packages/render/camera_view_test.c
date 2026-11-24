/* camera_view_test.c -- UNIFY-2: round-trip of the lobby camera glue (camera_view.c) through the compiled PARENA
 * camera mods. Expected numbers are derived by hand in the comment beside each check. No SDL/GL. */
#include "camera_view.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(c, m) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL  %s (line %d)\n", m, __LINE__); } } while (0)
#define NEAR(a, b, tol, m) do { g_checks++; long _a = (a), _b = (b); if (labs(_a - _b) > (tol)) { g_fail++; \
    printf("FAIL  %s: got %ld want %ld +-%d (line %d)\n", m, _a, _b, (int)(tol), __LINE__); } } while (0)

static CamViewIn base_in(int dt) {
    CamViewIn in = {0};
    in.dt_ms = dt; in.third = 1; in.allow_fp = 1;
    return in;
}

static void test_defaults(void) {
    CamView cv; camview_init(&cv);
    CHECK(cv.opt_follow == 1 && cv.opt_recenter == 1 && cv.opt_terrain == 1 && cv.opt_smooth == 1, "SHANKPIT defaults 1/1/1/1");
    CHECK(cv.opt_click_move == 0, "click-to-move defaults OFF");
    CHECK(cv.fp == 1 && cv.arm_milli == 0, "starts first person");
}

static void test_zoom(void) {
    CamView cv; camview_init(&cv);
    camview_wheel(&cv, -1, 1);                       /* one notch OUT of first person jumps to the 2000 floor */
    CHECK(cv.target_milli == 2000, "first notch out lands on 2000 (clear of the 800 exit line)");
    camview_wheel(&cv, -1, 1);                       /* 2000 * 1.15 = 2300 */
    NEAR(cv.target_milli, 2300, 2, "second notch out is 1.15x");
    cv.target_milli = 6000; camview_wheel(&cv, 1, 1);/* 6000 / 1.15 = 5217.4 */
    NEAR(cv.target_milli, 5217, 3, "notch in is /1.15");
    /* ease: arm 0 -> target 2000, dt = tau 140: 2000 * (1 - e^-1) = 1264 */
    CamView e; camview_init(&e); e.target_milli = 2000;
    CamViewIn in = base_in(140); CamViewOut out;
    camview_update(&e, &in, &out);
    NEAR(e.arm_milli, 1264, 25, "one tau reaches 63.2% of the way");
    CHECK(out.first_person == 0, "1264 is above the 800 exit line -> third person");
    /* hysteresis: 700 is between enter (600) and exit (800) */
    CamView h; camview_init(&h); h.arm_milli = h.target_milli = 700; h.fp = 1;
    camview_update(&h, &in, &out); CHECK(out.first_person == 1, "700 stays first person");
    h.fp = 0; camview_update(&h, &in, &out); CHECK(out.first_person == 0, "700 stays third person");
    h.arm_milli = h.target_milli = 500; camview_update(&h, &in, &out); CHECK(out.first_person == 1, "500 enters first person");
    /* a server-forced third-person view never reaches first person and floors at 4000 */
    CamView l; camview_init(&l); camview_sync_flag(&l, 1); CHECK(l.arm_milli == 6000, "sync from third flag seeds 6000");
    for (int i = 0; i < 20; i++) camview_wheel(&l, 1, 0);
    CHECK(l.target_milli == 4000, "locked view clamps at 4000");
    in.allow_fp = 0; l.arm_milli = 0; camview_update(&l, &in, &out); CHECK(out.first_person == 0, "allow_fp=0 never first person");
    /* snap (V) */
    CamView s; camview_init(&s); camview_snap(&s, 1); CHECK(s.target_milli == 6000, "V from first person -> last distance 6000");
    s.arm_milli = 6000; s.fp = 0; camview_snap(&s, 1); CHECK(s.target_milli == 0 && s.last_out_milli == 6000, "V from third -> first, remembers 6000");
    camview_snap(&s, 1); CHECK(s.target_milli == 6000, "V again returns");
    /* a remembered custom distance: wheel out to ~9000 then V in then V out */
    CamView r; camview_init(&r); r.target_milli = 9000; r.arm_milli = 9000; r.fp = 0;
    camview_wheel(&r, 0, 1); camview_snap(&r, 1); camview_snap(&r, 1);
    CHECK(r.target_milli == 9000 || r.target_milli == 6000, "snap round trip returns a third-person distance");
}

static void test_click_move(void) {
    CamView cv; camview_init(&cv); cv.fp = 0;
    CHECK(camview_click_dest(&cv, 1, 1, 5000, 0, 1000) == 0, "option off -> rejected");
    cv.opt_click_move = 1;
    CHECK(camview_click_dest(&cv, 1, 1, 5000, 0, 200) == 0, "normal.y 200 (wall) -> rejected");
    CHECK(camview_click_dest(&cv, 1, 0, 5000, 0, 1000) == 0, "no hit -> rejected");
    CHECK(camview_click_dest(&cv, 0, 1, 5000, 0, 1000) == 0, "view not allowed first person (server-forced) -> rejected");
    cv.fp = 1; CHECK(camview_click_dest(&cv, 1, 1, 5000, 0, 1000) == 0, "first person -> rejected"); cv.fp = 0;
    CHECK(camview_click_dest(&cv, 1, 1, 0, -5000, 1000) == 1, "ground hit accepted");

    CamViewIn in = base_in(16); CamViewOut out;
    cv.arm_milli = cv.target_milli = 6000;
    camview_update(&cv, &in, &out);   /* player at origin, yaw 0 (forward = -z), goal 5000 straight ahead */
    CHECK(out.steering == 1, "steering");
    CHECK(out.fwd_permille == 1000 && out.str_permille == 0, "5000 ahead (>1500) -> full forward, no strafe");
    CHECK(out.aim_yaw_mdeg == 0, "heading already 0");

    /* goal to the right (+x): heading atan2(-5000, -0) = -90 deg = 270000; body eases the short way (-90) */
    CamView r; camview_init(&r); r.opt_click_move = 1; r.fp = 0; r.arm_milli = r.target_milli = 6000;
    camview_click_dest(&r, 1, 1, 5000, 0, 1000);
    in = base_in(16); camview_update(&r, &in, &out);
    /* alpha(16, 180) = 1 - e^(-16/180) = 0.0851; step = -90000 * 0.0851 = -7659 -> 360000 - 7659 = 352341 */
    NEAR(out.aim_yaw_mdeg, 352341, 200, "body turns the short way toward +x");

    /* arrival cancels: within 400 milli */
    CamView a; camview_init(&a); a.opt_click_move = 1; a.fp = 0; a.arm_milli = a.target_milli = 6000;
    camview_click_dest(&a, 1, 1, 300, 0, 1000);
    in = base_in(16); camview_update(&a, &in, &out);
    CHECK(out.steering == 0 && a.dest_active == 0, "arrived (300 < 400) -> destination cleared");
    /* WASD cancels */
    CamView w; camview_init(&w); w.opt_click_move = 1; w.fp = 0; w.arm_milli = w.target_milli = 6000;
    camview_click_dest(&w, 1, 1, 0, -5000, 1000);
    in = base_in(16); in.wasd = 1; camview_update(&w, &in, &out);
    CHECK(out.steering == 0 && w.dest_active == 0, "WASD cancels");
    /* entering first person cancels */
    CamView f; camview_init(&f); f.opt_click_move = 1; f.fp = 0; f.arm_milli = 500; f.target_milli = 0;
    camview_click_dest(&f, 1, 1, 0, -5000, 1000);
    in = base_in(16); camview_update(&f, &in, &out);
    CHECK(out.first_person == 1 && f.dest_active == 0, "zooming into first person cancels");
    /* stuck: no movement for 1500 ms cancels. 16 ms frames, position never changes */
    CamView k; camview_init(&k); k.opt_click_move = 1; k.fp = 0; k.arm_milli = k.target_milli = 6000;
    camview_click_dest(&k, 1, 1, 0, -50000, 1000);
    in = base_in(100);
    for (int i = 0; i < 20; i++) camview_update(&k, &in, &out);   /* 2000 ms stationary */
    CHECK(k.dest_active == 0, "stuck >= 1500 ms cancels");
}

static void test_follow(void) {
    CamViewOut out;
    /* style 0 Free: a moving player with a body heading does not drag the camera */
    CamView cv; camview_init(&cv); cv.opt_click_move = 1; cv.fp = 0; cv.arm_milli = cv.target_milli = 6000;
    cv.opt_follow = 0; cv.body_yaw_mdeg = 90000;
    camview_click_dest(&cv, 1, 1, -100000, 0, 1000);
    CamViewIn in = base_in(100); in.px_milli = 0; camview_update(&cv, &in, &out);
    in.px_milli = 1000; camview_update(&cv, &in, &out);
    CHECK(out.yaw_mdeg == 0, "Free never recenters");
    /* style 1 Behind when moving: 10 u/s, idle long -> recenters toward the body heading 90000.
       alpha(100, 450) = 1 - e^(-0.2222) = 0.1993 -> 17940 */
    CamView m; camview_init(&m); m.opt_click_move = 1; m.fp = 0; m.arm_milli = m.target_milli = 6000;
    m.body_yaw_mdeg = 90000; camview_click_dest(&m, 1, 1, -100000, 0, 1000);
    in = base_in(100); in.px_milli = 0; camview_update(&m, &in, &out);      /* seeds prev position, speed 0 */
    in.px_milli = 1000; camview_update(&m, &in, &out);                      /* 10 u/s, eased: 10000*(1-e^-1)=6321 > 500 */
    NEAR(out.yaw_mdeg, 17940, 400, "Behind-when-moving recenters at the normal rate");
    /* manual orbit pauses it */
    CamView p; camview_init(&p); p.opt_click_move = 1; p.fp = 0; p.arm_milli = p.target_milli = 6000;
    p.body_yaw_mdeg = 90000; camview_click_dest(&p, 1, 1, -100000, 0, 1000);
    in = base_in(100); in.px_milli = 0; camview_update(&p, &in, &out);
    in.px_milli = 1000; in.manual = 1; camview_update(&p, &in, &out);
    CHECK(out.yaw_mdeg == 0, "manual orbit input pauses auto-recenter");
    /* standing still under Behind-when-moving: no recenter; Always-behind does */
    CamView s; camview_init(&s); s.opt_click_move = 1; s.fp = 0; s.arm_milli = s.target_milli = 6000;
    s.body_yaw_mdeg = 90000; camview_click_dest(&s, 1, 1, -100000, 0, 1000);
    in = base_in(100); camview_update(&s, &in, &out); camview_update(&s, &in, &out);
    CHECK(out.yaw_mdeg == 0, "standing still: Behind-when-moving does not recenter");
    s.opt_follow = 2; camview_update(&s, &in, &out); CHECK(out.yaw_mdeg > 10000, "Always behind recenters while standing");
    /* recenter speed: fast tau 180 -> alpha(100,180) = 0.4262 -> 38358 vs slow tau 900 -> 0.1052 -> 9468 */
    CamView q; camview_init(&q); q.opt_click_move = 1; q.opt_follow = 2; q.fp = 0; q.arm_milli = q.target_milli = 6000;
    q.body_yaw_mdeg = 90000; q.opt_recenter = 2; camview_click_dest(&q, 1, 1, -100000, 0, 1000);
    in = base_in(100); camview_update(&q, &in, &out); NEAR(out.yaw_mdeg, 38358, 600, "fast recenter");
    CamView z; camview_init(&z); z.opt_click_move = 1; z.opt_follow = 2; z.fp = 0; z.arm_milli = z.target_milli = 6000;
    z.body_yaw_mdeg = 90000; z.opt_recenter = 0; camview_click_dest(&z, 1, 1, -100000, 0, 1000);
    camview_update(&z, &in, &out); NEAR(out.yaw_mdeg, 9468, 300, "slow recenter");
    /* WASD mouselook (no destination): facing == camera, nothing to recenter toward */
    CamView n; camview_init(&n); n.opt_follow = 2; n.fp = 0; n.arm_milli = n.target_milli = 6000;
    in = base_in(100); in.yaw_mdeg = 123000; camview_update(&n, &in, &out);
    CHECK(out.yaw_mdeg == 123000, "no destination: yaw untouched");
}

static void test_terrain_and_smooth(void) {
    CamViewOut out; CamViewIn in;
    /* slope 160 permille uphill: 160-60 = 100 -> 100*22 = 2200 mdeg, uphill -> -2200. Eased one tau (350 ms):
       -2200 * 0.63212 = -1391. */
    CamView t; camview_init(&t); t.fp = 0; t.arm_milli = t.target_milli = 6000;
    in = base_in(350); in.slope_permille = 160; camview_update(&t, &in, &out);
    NEAR(out.pitch_off_mdeg, -1391, 40, "uphill raises the look, eased");
    /* wall pull-in shortened the arm to half: the offset is scaled to half so the two never fight */
    t.pitch_off_mdeg = -2200; t.arm_fraction_permille = 500; in.dt_ms = 0;
    camview_update(&t, &in, &out); NEAR(out.pitch_off_mdeg, -1100, 5, "wall pull-in halves the terrain offset");
    t.arm_fraction_permille = 0; camview_update(&t, &in, &out); CHECK(out.pitch_off_mdeg == 0, "fully pulled in: no terrain offset");
    /* downhill (-160) is positive (look further down) */
    CamView d; camview_init(&d); d.fp = 0; d.arm_milli = d.target_milli = 6000;
    in = base_in(350); in.slope_permille = -160; camview_update(&d, &in, &out);
    NEAR(out.pitch_off_mdeg, 1391, 40, "downhill lowers the look");
    /* inside the 6% dead zone: nothing */
    CamView z; camview_init(&z); z.fp = 0; z.arm_milli = z.target_milli = 6000;
    in = base_in(350); in.slope_permille = 50; camview_update(&z, &in, &out); CHECK(out.pitch_off_mdeg == 0, "dead zone");
    /* terrain Off: no offset, rigid focus */
    CamView o; camview_init(&o); o.opt_terrain = 0; o.fp = 0; o.arm_milli = o.target_milli = 6000;
    in = base_in(350); in.slope_permille = 400; in.py_milli = 0; camview_update(&o, &in, &out);
    in.py_milli = 2000; camview_update(&o, &in, &out);
    CHECK(out.pitch_off_mdeg == 0 && out.focus_dy_milli == 0, "terrain Off: no pitch offset, focus rigid");
    /* step up 2000 (gap < 3000, tau 220): focus covers 63.2% in one tau -> dy = -2000*(1-0.632) = -736 */
    CamView f; camview_init(&f); f.fp = 0; f.arm_milli = f.target_milli = 6000;
    in = base_in(220); in.py_milli = 0; camview_update(&f, &in, &out);
    in.py_milli = 2000; camview_update(&f, &in, &out);
    NEAR(out.focus_dy_milli, -736, 30, "stairs: focus lags the step");
    /* first person ignores the options entirely */
    CamView p; camview_init(&p); p.opt_follow = 2; p.fp = 1; p.arm_milli = 0;
    in = base_in(100); in.slope_permille = 500; in.yaw_mdeg = 5000; camview_update(&p, &in, &out);
    CHECK(out.pitch_off_mdeg == 0 && out.yaw_mdeg == 5000 && out.focus_dy_milli == 0, "first person ignores follow/terrain");
    /* smoothing: off = exact target; low (tau 60) one tau = 63.2% of 1000 = 632; high (tau 160) at dt 160 = 632 */
    int cur[3] = {0, 0, 0}, tgt[3] = {1000, -1000, 0}, res[3];
    CamView s; camview_init(&s);
    s.opt_smooth = 0; camview_smooth_pos(&s, 16, cur, tgt, res); CHECK(res[0] == 1000 && res[1] == -1000 && res[2] == 0, "smoothing off is exact");
    s.opt_smooth = 1; camview_smooth_pos(&s, 60, cur, tgt, res); NEAR(res[0], 632, 12, "low lag, one tau"); NEAR(res[1], -632, 12, "per-axis");
    s.opt_smooth = 2; camview_smooth_pos(&s, 60, cur, tgt, res); NEAR(res[0], 1000 - (long)(1000 * exp(-60.0 / 160.0)), 15, "high lag lags more");
    CHECK(res[0] < 400, "high lag is slower than low");
}

int main(void) {
    test_defaults(); test_zoom(); test_click_move(); test_follow(); test_terrain_and_smooth();
    if (g_fail) { printf("camera_view_test: %d of %d checks FAILED\n", g_fail, g_checks); return 1; }
    printf("camera_view_test: ok: %d checks, 0 failed\n", g_checks);
    return 0;
}
