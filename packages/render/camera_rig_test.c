/* camera_rig_test.c -- headless checks for camera_rig.h + the PARENA camera rules (#456/#457/#458).
 * make test-camera-rig */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "camera_rig.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static CamActor actor(int id, float x, float z, int team) {
    CamActor a; memset(&a, 0, sizeof(a));
    a.id = id; a.active = 1; a.team = team; a.x = x; a.z = z; a.health = 100;
    return a;
}

int main(void) {
    /* ---- 1. the rig file round-trips exactly ---- */
    CameraRig a, b;
    camrig_default(&a);
    a.cam[2].fov = 48.5f;
    a.cam[1].subject = 3;
    char s1[4096], s2[4096];
    int n1 = camrig_serialize(&a, s1, sizeof(s1));
    CHECK(n1 > 0);
    CHECK(camrig_parse(&b, s1) == 4);
    int n2 = camrig_serialize(&b, s2, sizeof(s2));
    CHECK(n1 == n2 && strcmp(s1, s2) == 0);                 /* parse(serialize(x)) serializes to the same bytes */
    CHECK(b.cam[1].subject == 3 && b.cam[2].kind == CAM_FIXED && b.cam[1].kind == CAM_ORBIT);
    CHECK(fabsf(b.cam[2].fov - 48.5f) < 1e-3f && strcmp(b.cam[0].name, "Hero") == 0);
    CHECK(camrig_serialize(&a, s2, 40) == -1);              /* too small a buffer is an error, not a truncation */
    printf("rig file: %d bytes, round trip exact\n", n1);

    /* ---- 2. bad input is rejected and leaves the rig alone; values are clamped ---- */
    CameraRig keep = a;
    CHECK(camrig_parse(&keep, "") == 0);
    CHECK(camrig_parse(&keep, "{\"camrig_version\":2,\"cameras\":[{\"name\":\"x\"}]}") == 0);   /* wrong version */
    CHECK(camrig_parse(&keep, "{\"camrig_version\":1,\"cameras\":[]}") == 0);                   /* no cameras */
    CHECK(camrig_parse(&keep, "{\"camrig_version\":1}") == 0);
    CHECK(keep.count == 4 && strcmp(keep.cam[0].name, "Hero") == 0);
    CameraRig c;
    CHECK(camrig_parse(&c, "{\"camrig_version\":1,\"hold_ms\":99999999,\"cameras\":[{\"name\":\"Q\",\"kind\":\"bogus\",\"skill\":99999,\"shake\":-5,\"radius\":9999,\"fov\":400}]}") == 1);
    CHECK(c.cam[0].kind == CAM_FIXED && c.cam[0].skill == 1000 && c.cam[0].shake == 0 && c.cam[0].radius == 500.0f && c.cam[0].fov == 170.0f);
    CHECK(c.hold_ms == 600000);
    {   /* more cameras than the cap are dropped, never overflowed */
        char big[8192]; int off = snprintf(big, sizeof(big), "{\"camrig_version\":1,\"cameras\":[");
        for (int i = 0; i < CAMRIG_MAX + 5; i++) off += snprintf(big + off, sizeof(big) - (size_t)off, "%s{\"name\":\"c%d\"}", i ? "," : "", i);
        snprintf(big + off, sizeof(big) - (size_t)off, "]}");
        CHECK(camrig_parse(&c, big) == CAMRIG_MAX);
    }
    {   /* a hostile name cannot break the file */
        CameraRig h; camrig_default(&h);
        snprintf(h.cam[0].name, sizeof(h.cam[0].name), "ev\"il},{x");
        char hs[4096]; CHECK(camrig_serialize(&h, hs, sizeof(hs)) > 0);
        CameraRig hp; CHECK(camrig_parse(&hp, hs) == 4);
    }

    /* ---- 3. operators lag, and skill matters ---- */
    {
        CameraRig rook, vet;
        camrig_default(&rook); camrig_default(&vet);
        for (int i = 0; i < 4; i++) { rook.cam[i].skill = 0; vet.cam[i].skill = 1000; rook.auto_cut = vet.auto_cut = 0; }
        rook.director_subject = vet.director_subject = 1;
        CamActor act[2] = { actor(1, 0, 0, 0), actor(2, 200, 200, 1) };
        unsigned int t = 1000;
        camrig_update(&rook, act, 2, t, 16); camrig_update(&vet, act, 2, t, 16);          /* prime on the subject */
        act[0].x = 40.0f;                                                                 /* the subject teleports 40 units */
        float gap0 = 40.0f;
        for (int i = 0; i < 6; i++) { t += 16; camrig_update(&rook, act, 2, t, 16); camrig_update(&vet, act, 2, t, 16); }
        float rook_err = fabsf(rook.cam[0].s_aim[0] - 40.0f), vet_err = fabsf(vet.cam[0].s_aim[0] - 40.0f);
        CHECK(rook_err < gap0 && rook_err > 0.5f * gap0);          /* the rookie is still well behind after ~100 ms */
        CHECK(vet_err < rook_err);                                 /* the veteran has caught up further */
        for (int i = 0; i < 400; i++) { t += 16; camrig_update(&rook, act, 2, t, 16); camrig_update(&vet, act, 2, t, 16); }
        CHECK(fabsf(rook.cam[0].s_aim[0] - 40.0f) < 0.5f && fabsf(vet.cam[0].s_aim[0] - 40.0f) < 0.5f);   /* both converge, no overshoot */
        printf("operators: after 100 ms rookie off by %.1f, veteran off by %.1f (of %.0f)\n", rook_err, vet_err, gap0);
    }

    /* ---- 4. the director follows the action and does not flicker ---- */
    {
        CameraRig r; camrig_default(&r);
        /* three players: 1 and 2 are in a firefight (close, one shooting, one just got a kill); 3 is idle far away */
        CamActor act[3] = { actor(1, 0, 0, 0), actor(2, 6, 0, 1), actor(3, 150, 150, 2) };
        act[0].shooting = 1; act[0].vx = 4.0f; act[0].last_kill_ms = 800;
        unsigned int t = 1000;
        int dir_at_end = -1;
        for (int i = 0; i < 625; i++) { t += 16; camrig_update(&r, act, 3, t, 16); }   /* ~10 s */
        dir_at_end = r.director_subject;
        CHECK(dir_at_end == 1 || dir_at_end == 2);                 /* never the idle player */
        CHECK(r.cuts >= 1);
        CHECK(r.cuts <= 3);                                        /* hold + hysteresis: no machine-gun cutting */
        printf("director: watching player %d, %d cut(s) in 10 s, program cam %s\n", dir_at_end, r.cuts, r.cam[r.program].name);

        /* the action moves: player 3 starts a fight; after the hold, the director goes there */
        act[0].shooting = 0; act[0].vx = 0; act[0].last_kill_ms = 0;
        act[1] = actor(4, 150, 140, 3);                            /* player 2 is replaced by a new enemy beside player 3... */
        act[2].x = 150; act[2].z = 143; act[2].shooting = 1; act[2].last_kill_ms = (unsigned int)t - 100;   /* ...who is shooting */
        act[0].x = 400; act[0].z = -400;                           /* and player 1 has wandered off, alone */
        int cuts_before = r.cuts;
        for (int i = 0; i < 400; i++) { t += 16; camrig_update(&r, act, 3, t, 16); }
        CHECK(r.director_subject == 3);
        CHECK(r.cuts > cuts_before);
    }

    /* ---- 5. no ping-pong between two equal fights ---- */
    {
        CameraRig r; camrig_default(&r);
        CamActor act[4] = { actor(1, 0, 0, 0), actor(2, 5, 0, 1), actor(3, 100, 100, 2), actor(4, 105, 100, 3) };
        unsigned int t = 1000;
        for (int i = 0; i < 1250; i++) { t += 16; camrig_update(&r, act, 4, t, 16); }   /* 20 s of two identical fights */
        CHECK(r.cuts <= 2);
        printf("two equal fights over 20 s: %d cut(s)\n", r.cuts);
    }

    /* ---- 6. zoom operator + deterministic shake ---- */
    {
        CameraRig r; camrig_default(&r);
        r.auto_cut = 0; r.director_subject = 1;
        CamActor act[1] = { actor(1, 0, 0, 0) };
        camrig_update(&r, act, 1, 1000, 16);
        CamView near_v, far_v, again;
        CHECK(camrig_view(&r, 0, 5000, &near_v));
        CHECK(near_v.fov > 60.0f);                                  /* hero cam is close: wide */
        CHECK(camrig_view(&r, 2, 5000, &far_v));                    /* Wide tripod, ~65 units out */
        CHECK(far_v.fov < near_v.fov);                              /* tightens on distant action */
        CHECK(camrig_view(&r, 0, 5000, &again));
        CHECK(memcmp(&near_v, &again, sizeof(CamView)) == 0);       /* same time, same frame */
        CamView later; camrig_view(&r, 0, 5131, &later);
        CHECK(near_v.eye[0] != later.eye[0] || near_v.eye[1] != later.eye[1]);   /* shake actually moves it */
        CHECK(!camrig_view(&r, 99, 0, &again));
    }

    if (g_fail) { printf("camera_rig_test: %d FAILED\n", g_fail); return 1; }
    printf("camera_rig_test OK\n");
    return 0;
}
