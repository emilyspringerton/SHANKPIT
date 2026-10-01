/* bullet_hole_test.c -- headless test for packages/world/bullet_hole.h: shot detection, per-gun
 * slot/size/ray mapping, ring-buffer behavior, and that every checked-in NOCK export decodes
 * (via PARENA's own png_decode) to a pure-white-cornered, dark-centered RGBA decal and that the
 * four guns' decals differ.
 *
 * Build and run (from SHANKPIT/):
 *   gcc -Wall -Wextra -O2 -Ipackages/world -o /tmp/bullet_hole_test packages/world/bullet_hole_test.c \
 *       packages/world/png_decode_gen.c packages/world/parena_runtime.c -lm && /tmp/bullet_hole_test
 */
#include "bullet_hole.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    /* slot mapping: bullet guns map to distinct slots, melee/missile/flashlight to -1 */
    assert(bullet_hole_slot_for_weapon(WPN_MAGNUM) == 0);
    assert(bullet_hole_slot_for_weapon(WPN_AR) == 1);
    assert(bullet_hole_slot_for_weapon(WPN_SHOTGUN) == 2);
    assert(bullet_hole_slot_for_weapon(WPN_SNIPER) == 3);
    assert(bullet_hole_slot_for_weapon(WPN_KNIFE) == -1);
    assert(bullet_hole_slot_for_weapon(WPN_KATANA) == -1);
    assert(bullet_hole_slot_for_weapon(WPN_MISSILE) == -1);
    assert(bullet_hole_slot_for_weapon(WPN_FLASHLIGHT) == -1);

    /* shotgun fires a pellet spread, everything else one ray */
    assert(bullet_hole_rays_for_weapon(WPN_SHOTGUN) == 8);
    assert(bullet_hole_rays_for_weapon(WPN_AR) == 1);
    assert(bullet_hole_rays_for_weapon(WPN_SNIPER) == 1);

    /* shot detection from ammo drops */
    BulletHoleTrack t = {0};
    assert(bullet_hole_shots_fired(&t, WPN_AR, 30) == 0);          /* first sample */
    assert(bullet_hole_shots_fired(&t, WPN_AR, 30) == 0);          /* no change */
    assert(bullet_hole_shots_fired(&t, WPN_AR, 29) == 1);          /* one shot */
    assert(bullet_hole_shots_fired(&t, WPN_AR, 27) == 2);          /* two shots between samples */
    assert(bullet_hole_shots_fired(&t, WPN_AR, 30) == 0);          /* reload: ammo rose */
    assert(bullet_hole_shots_fired(&t, WPN_MAGNUM, 8) == 0);       /* weapon switch */
    assert(bullet_hole_shots_fired(&t, WPN_MAGNUM, 7) == 1);
    assert(bullet_hole_shots_fired(&t, WPN_MAGNUM, 0) == BULLET_HOLE_MAX_SHOTS_PER_OBSERVE); /* capped */
    assert(bullet_hole_shots_fired(&t, WPN_KNIFE, 0) == 0);
    assert(bullet_hole_shots_fired(&t, WPN_KNIFE, 0) == 0);        /* melee never fires holes */

    /* spread: a spread-less gun keeps its direction; a shotgun's pellets diverge; always unit */
    BulletHoleBuf b; memset(&b, 0, sizeof(b)); b.rng = 12345;
    float dx = 0, dy = 0, dz = -1;
    bullet_hole_spread_dir(&b, WPN_MAGNUM, &dx, &dy, &dz);
    assert(dx == 0.0f && dy == 0.0f && fabsf(dz + 1.0f) < 1e-6f);
    int moved = 0;
    for (int i = 0; i < 8; i++) {
        float px = 0, py = 0, pz = -1;
        bullet_hole_spread_dir(&b, WPN_SHOTGUN, &px, &py, &pz);
        assert(fabsf(sqrtf(px * px + py * py + pz * pz) - 1.0f) < 1e-4f);
        if (fabsf(px) > 0.01f || fabsf(py) > 0.01f) moved++;
    }
    assert(moved >= 6);

    /* ring buffer: fills, then overwrites oldest; per-hole jitter keeps size within 0.85..1.15x */
    bullet_hole_clear(&b);
    for (int i = 0; i < BULLET_HOLE_MAX + 5; i++)
        bullet_hole_add(&b, i % BULLET_HOLE_SLOTS, 7, (float)i, 0, 0, 0, 1, 0);
    assert(b.count == BULLET_HOLE_MAX);
    assert(b.holes[0].x == (float)BULLET_HOLE_MAX); /* slot 0 overwritten by hole #256 */
    for (int i = 0; i < BULLET_HOLE_MAX; i++) {
        float base = bullet_hole_half_size(b.holes[i].slot);
        assert(b.holes[i].half >= base * 0.849f && b.holes[i].half <= base * 1.151f);
        assert(b.holes[i].scene_id == 7);
    }
    bullet_hole_clear(&b);
    assert(b.count == 0 && b.next == 0);

    /* the four checked-in NOCK exports decode, are white-cornered/dark-centered, and differ */
    unsigned char *px[BULLET_HOLE_SLOTS]; int w[BULLET_HOLE_SLOTS], h[BULLET_HOLE_SLOTS];
    for (int s = 0; s < BULLET_HOLE_SLOTS; s++) {
        assert(bullet_hole_decode_embedded(s, &px[s], &w[s], &h[s]));
        assert(w[s] == 64 && h[s] == 64);
        const unsigned char *c0 = px[s];                                  /* (0,0) */
        const unsigned char *c1 = px[s] + ((63 * 64 + 63) * 4);           /* (63,63) */
        const unsigned char *mid = px[s] + ((32 * 64 + 32) * 4);
        assert(c0[0] == 255 && c0[1] == 255 && c0[2] == 255);
        assert(c1[0] == 255 && c1[1] == 255 && c1[2] == 255);
        assert(mid[0] < 60 && mid[1] < 60 && mid[2] < 60);
    }
    for (int a = 0; a < BULLET_HOLE_SLOTS; a++)
        for (int c = a + 1; c < BULLET_HOLE_SLOTS; c++)
            assert(memcmp(px[a], px[c], 64 * 64 * 4) != 0);
    /* sniper's halo is the widest, shotgun's the tightest: count non-white pixels */
    long dark[BULLET_HOLE_SLOTS];
    for (int s = 0; s < BULLET_HOLE_SLOTS; s++) {
        dark[s] = 0;
        for (int i = 0; i < 64 * 64; i++) if (px[s][i * 4] < 250) dark[s]++;
    }
    assert(dark[3] > dark[0] && dark[0] > dark[1] && dark[1] > dark[2]);
    for (int s = 0; s < BULLET_HOLE_SLOTS; s++) free(px[s]);

    /* destructible brick: decals on a destroyed cell are removed, others stay */
    {
        static BulletHoleBuf buf;
        bullet_hole_clear(&buf);
        bullet_hole_add(&buf, 0, 9, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f);   /* inside the cell */
        bullet_hole_add(&buf, 1, 9, 2.9f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f);   /* inside, near the edge */
        bullet_hole_add(&buf, 1, 9, 6.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f);   /* a different cell */
        /* cell centre (1.5,1.5,0) half extents 1.5: spans x 0..3 */
        assert(bullet_hole_remove_in_box(&buf, 1.5f, 1.5f, 0.0f, 1.5f, 1.5f, 1.5f) == 2);
        assert(buf.holes[0].scene_id < 0 && buf.holes[1].scene_id < 0 && buf.holes[2].scene_id == 9);
        assert(bullet_hole_remove_in_box(&buf, 1.5f, 1.5f, 0.0f, 1.5f, 1.5f, 1.5f) == 0);   /* idempotent */
    }

    printf("bullet_hole_test: all checks passed (marked pixels: magnum=%ld ar=%ld shotgun=%ld sniper=%ld)\n",
           dark[0], dark[1], dark[2], dark[3]);
    return 0;
}
