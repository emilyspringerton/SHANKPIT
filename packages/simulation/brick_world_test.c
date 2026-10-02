/* brick_world_test.c -- headless end-to-end test of destructible brick through the REAL engine
 * paths: physics.h's update_weapons (hitscan hook), its custom-level slot arrays, trace_map, and the
 * brick_world.h glue (blast hook, match reset, network apply). No SDL, no window.
 *
 *   gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -Ipackages/common \
 *       -Ipackages/simulation -Ipackages/world packages/simulation/brick_world_test.c \
 *       packages/simulation/brick_mod.c packages/world/terrain.c -lm
 */
#include "../common/physics.h"
#include "brick_world.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* physics.h's phys_respawn calls into local_game.h's bot evolution; this test never respawns. */
PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* solid at a point, using the live collision slots (exactly what resolve_collision/trace_map see) */
static int solid_at(float x, float y, float z) {
    for (int i = 1; i < map_count; i++) {
        Box b = map_geo[i];
        if (fabsf(x - b.x) < b.w * 0.5f && fabsf(y - b.y) < b.h * 0.5f && fabsf(z - b.z) < b.d * 0.5f) return 1;
    }
    return 0;
}

static CustomLevelData g_lvl;

static void load_level(void) {
    memset(&g_lvl, 0, sizeof(g_lvl));
    snprintf(g_lvl.materials[0].name, LEVEL_BOXES_MAX_NAME, "brick");
    snprintf(g_lvl.materials[1].name, LEVEL_BOXES_MAX_NAME, "metal");
    g_lvl.material_count = 2;
    /* 0: a brick wall 60 x 20 x 6 (20 x 7 x 2 cells) centred (0,10,0)
       1: a brick floor slab 100 x 2 x 100 (slab-like -> NOT destructible)
       2: a metal pillar (not brick -> NOT destructible)
       3: a brick door box (excluded by the door list) */
    LevelBox *b = g_lvl.boxes;
    b[0] = (LevelBox){ 0, 10, 0, 60, 20, 6, 0.6f, 0.6f, 0.6f, 0.3f, 0 };
    b[1] = (LevelBox){ 0, -1, 0, 100, 2, 100, 0.6f, 0.6f, 0.6f, 0.3f, 0 };
    b[2] = (LevelBox){ 80, 10, 0, 6, 20, 6, 0.6f, 0.6f, 0.6f, 0.3f, 1 };
    b[3] = (LevelBox){ -80, 10, 0, 10, 20, 2, 0.6f, 0.6f, 0.6f, 0.3f, 0 };
    g_lvl.count = 4;
    g_lvl.door_count = 1;
    g_lvl.doors[0].box_index = 3;

    float x[4], y[4], z[4], w[4], h[4], d[4], r[4], g[4], bl[4]; int mi[4];
    for (int i = 0; i < 4; i++) {
        x[i] = b[i].x; y[i] = b[i].y; z[i] = b[i].z; w[i] = b[i].w; h[i] = b[i].h; d[i] = b[i].d;
        r[i] = g[i] = bl[i] = 0.6f; mi[i] = b[i].material_idx;
    }
    char names[2][CUSTOM_LEVEL_MATERIAL_NAME_LEN] = { "brick", "metal" };
    char shaders[2][CUSTOM_LEVEL_MATERIAL_NAME_LEN] = { "standard", "standard" };
    float spec[2] = { 0, 0 }, shin[2] = { 1, 1 }, fric[2] = { 0.3f, 0.3f };
    phys_set_custom_level_materials(names, shaders, spec, shin, fric, 2);
    phys_set_custom_level(x, y, z, w, h, d, r, g, bl, mi, 4, 0, 1);
    phys_set_scene(SCENE_CUSTOM_LEVEL);
    brick_world_init_from_level(&g_lvl);
}

static void fire(PlayerState *p, int weapon, int ticks) {
    PlayerState targets[MAX_CLIENTS];
    Projectile projectiles[MAX_PROJECTILES];
    memset(targets, 0, sizeof(targets));
    memset(projectiles, 0, sizeof(projectiles));
    p->current_weapon = weapon;
    for (int t = 0; t < ticks; t++) {
        p->ammo[weapon] = 99;
        update_weapons(p, targets, projectiles, 1, 0, 0, (unsigned int)t * 16u, 3000u);
    }
}

/* brick_world.h installs the AI hooks; this test links no witness_ai.c, so stand-ins for its setters. */
void witness_ai_set_wall_hit_hook(WitnessWallHitFn fn) { (void)fn; }
void witness_ai_set_map_hook(WitnessMapFn fn) { (void)fn; }

int main(void) {
    load_level();
    CHECK(g_brick_active == 1);
    /* only the wall qualifies: slab, metal pillar and the door box are excluded */
    CHECK(g_brick.parent[0].active == 1);
    CHECK(g_brick.parent_count == 1 || !g_brick.parent[1].active);
    for (int i = 1; i < g_brick.parent_count; i++) CHECK(!g_brick.parent[i].active);
    CHECK(g_brick.parent[0].n[0] == 20 && g_brick.parent[0].n[2] == 2);

    PlayerState p;
    memset(&p, 0, sizeof(p));
    p.active = 1; p.scene_id = SCENE_CUSTOM_LEVEL; p.state = STATE_ALIVE;
    p.x = 1.5f; p.y = 10.0f - EYE_HEIGHT; p.z = -40.0f;   /* x = a cell-column centre, not a seam */
    p.yaw = 180.0f; p.pitch = 0.0f;                       /* aim +z, straight at the wall */

    float hx, hy, hz, nx, ny, nz;
    CHECK(solid_at(1.5f, 10.0f, -1.5f) && solid_at(1.5f, 10.0f, 1.5f));
    CHECK(trace_map(1.5f, 10, -40, 1.5f, 10, 40, &hx, &hy, &hz, &nx, &ny, &nz) == 1 && fabsf(hz + 3.0f) < 1e-3f);
    int slots_before = map_count;

    /* 1. a networked client never carves: authority off, shoot a lot, nothing changes */
    brick_world_set_authority(0);
    fire(&p, WPN_MAGNUM, 400);
    CHECK(g_brick.rec_count == 0 && map_count == slots_before && solid_at(1.5f, 10.0f, -1.5f));
    brick_world_set_authority(1);

    /* 2. a magnum hits the same spot until it is through the 6-thick (2-cell) wall.
          Per hit: 45 -> 22 after the 50% brick resistance; an 80 HP cell survives 3, dies on the 4th;
          two cells deep -> 8 hits (neighbours take falloff damage too and may fall earlier). */
    int shots = 0;
    for (int i = 0; i < 60 && (solid_at(1.5f, 10.0f, -1.5f) || solid_at(1.5f, 10.0f, 1.5f)); i++) { fire(&p, WPN_MAGNUM, 26); shots++; }
    CHECK(!solid_at(1.5f, 10.0f, -1.5f) && !solid_at(1.5f, 10.0f, 1.5f));   /* breached at the aim point */
    CHECK(shots >= 4 && shots <= 20);
    CHECK(g_brick.parent[0].removed >= 2);
    CHECK(map_count > slots_before - 1 && g_custom_level_hidden[1] == 1);   /* authored wall hidden, pieces added */
    /* the bullet now flies through the hole to the far side */
    int through = !trace_map(1.5f, 10, -40, 1.5f, 10, 40, &hx, &hy, &hz, &nx, &ny, &nz) || hz > 3.0f;
    CHECK(through);
    /* the rest of the wall is intact: a point well off to the side is still solid */
    CHECK(solid_at(25.5f, 10.0f, 0.5f) && solid_at(-25.5f, 5.0f, 1.0f));
    /* excluded boxes are untouched: floor slab, metal pillar, door box */
    CHECK(solid_at(0.5f, -1.0f, 30.5f) && solid_at(80.5f, 10.5f, 0.5f) && solid_at(-80.5f, 10.5f, 0.5f));
    CHECK(g_custom_level_hidden[2] == 0 && g_custom_level_hidden[3] == 0 && g_custom_level_hidden[4] == 0);
    /* geometric invariant on the live piece set: pieces + gone cells == the wall */
    {
        float vol = 0.0f;
        for (int i = 0; i < g_brick.piece_count; i++) vol += g_brick.piece[i].w * g_brick.piece[i].h * g_brick.piece[i].d;
        const BfParent *wp = &g_brick.parent[0];
        float cell = wp->cell[0] * wp->cell[1] * wp->cell[2];
        CHECK(fabsf(vol + (float)wp->removed * cell - 60.0f * 20.0f * 6.0f) < 60.0f * 20.0f * 6.0f * 1e-3f);
    }

    /* 3. a missile blast on the face of an undamaged stretch: carves a crater, not a pinhole */
    int removed_before = g_brick.parent[0].removed;
    g_phys_blast_normal[0] = 0.0f; g_phys_blast_normal[1] = 0.0f; g_phys_blast_normal[2] = -1.0f;   /* struck the -z face */
    g_phys_map_blast_hook(SCENE_CUSTOM_LEVEL, 20.0f, 10.0f, -3.0f, MISSILE_SPLASH_RADIUS, 130, WPN_MISSILE);
    g_phys_blast_normal[2] = 0.0f;
    CHECK(g_brick.parent[0].removed >= removed_before + 6);
    CHECK(!solid_at(19.5f, 10.0f, -1.5f) && !solid_at(19.5f, 10.0f, 1.5f));   /* a rocket goes right through a 2-cell wall */
    /* blades do nothing to brick */
    int rec_now = g_brick.rec_count;
    fire(&p, WPN_KNIFE, 100); fire(&p, WPN_KATANA, 100);
    CHECK(g_brick.rec_count == rec_now);

    /* 4. network: what the server would send, applied to a freshly reset world, rebuilds the exact
          same geometry (the order and the repeats do not matter) */
    BfPiece snap[BF_MAX_PIECES]; int snap_n = g_brick.piece_count;
    memcpy(snap, g_brick.piece, sizeof(BfPiece) * (size_t)snap_n);
    int removed_total = g_brick.parent[0].removed;
    NetBrickEntry all[2048]; int na = 0;
    for (int round = 0; round < 80 && na + NET_BRICK_MAX_ENTRIES <= 2048; round++) {
        int n = brick_world_net_collect(all + na, NET_BRICK_MAX_ENTRIES, 1000u + (unsigned)round * 300u);
        na += n;
    }
    CHECK(na >= removed_total);
    brick_world_reset_match();                                   /* every wall whole again */
    CHECK(g_brick.rec_count == 0 && g_brick.piece_count == 0 && g_custom_level_hidden[1] == 0);
    CHECK(solid_at(1.5f, 10.0f, -1.5f) && map_count == slots_before);
    brick_world_set_authority(0);                                /* now act as a mirroring client */
    for (int i = na - 1; i >= 0; i -= NET_BRICK_MAX_ENTRIES) {   /* deliver in reverse chunks */
        NetBrickState pkt; memset(&pkt, 0, sizeof(pkt));
        pkt.hdr.type = PACKET_BRICK_STATE;
        int c = 0;
        for (int k = i; k > i - NET_BRICK_MAX_ENTRIES && k >= 0; k--) pkt.e[c++] = all[k];
        pkt.count = (unsigned char)c;
        brick_world_net_apply(&pkt, (int)(sizeof(NetHeader) + 4 + (size_t)c * sizeof(NetBrickEntry)));
    }
    CHECK(g_brick.parent[0].removed == removed_total);
    CHECK(g_brick.piece_count == snap_n && memcmp(g_brick.piece, snap, sizeof(BfPiece) * (size_t)snap_n) == 0);
    CHECK(!solid_at(1.5f, 10.0f, -1.5f) && !solid_at(19.5f, 10.0f, -1.5f));
    /* the mirror never rebroadcasts */
    CHECK(g_brick.net_count == 0);

    /* 5. hostile packets: short, over-count, bad parent, bad key, bad hp -- no crash, no change */
    {
        int serial = brick_world_commit_serial(); int rem = g_brick.parent[0].removed;
        NetBrickState bad; memset(&bad, 0, sizeof(bad));
        bad.count = 250;                                          /* claims far more than arrived */
        brick_world_net_apply(&bad, (int)sizeof(NetHeader) + 4);
        brick_world_net_apply(&bad, 3);
        brick_world_net_apply(&bad, 0);
        bad.count = 3;
        bad.e[0].parent = 77; bad.e[0].hp = 10;                    /* no such parent */
        bad.e[1].parent = 0; bad.e[1].key_lo = 0xFFFF; bad.e[1].key_hi = 0xFFFF; bad.e[1].hp = 10;
        bad.e[2].parent = 0; bad.e[2].key_lo = 1; bad.e[2].hp = 200;   /* hp above max */
        brick_world_net_apply(&bad, (int)(sizeof(NetHeader) + 4 + 3 * sizeof(NetBrickEntry)));
        CHECK(g_brick.parent[0].removed == rem);
        CHECK((int)brick_world_commit_serial() == serial);
    }

    /* 6. hooks are scene-gated: another scene's shots must never touch the level */
    brick_world_set_authority(1);
    brick_world_reset_match();
    int ra = g_brick.rec_count;
    g_phys_map_blast_hook(SCENE_STADIUM, 0.0f, 10.0f, -3.0f, 6.0f, 130, WPN_MISSILE);
    CHECK(g_brick.rec_count == ra);

    /* 7. reload a level: parents re-derived, no stale damage */
    g_phys_map_blast_hook(SCENE_CUSTOM_LEVEL, 0.0f, 10.0f, -3.0f, 6.0f, 130, WPN_MISSILE);
    CHECK(g_brick.rec_count > 0);
    load_level();
    CHECK(g_brick.rec_count == 0 && g_brick.piece_count == 0 && solid_at(1.5f, 10.0f, -1.5f));

    /* 8. persisted damage round trip (F1 snapshot / exit autosave): carve, export the records, build
          the upload body, parse it back the way the loader parses a registry export, reload the level
          with those records and require the SAME holes (and the same geometry) with zero re-shooting */
    {
        load_level();
        brick_world_set_authority(1);
        memset(&p, 0, sizeof(p));
        p.active = 1; p.scene_id = SCENE_CUSTOM_LEVEL; p.state = STATE_ALIVE;
        p.x = 1.5f; p.y = 10.0f - EYE_HEIGHT; p.z = -40.0f; p.yaw = 180.0f; p.pitch = 0.0f;
        for (int i = 0; i < 40 && (solid_at(1.5f, 10.0f, -1.5f) || solid_at(1.5f, 10.0f, 1.5f)); i++) fire(&p, WPN_MAGNUM, 26);
        g_phys_blast_normal[2] = -1.0f;
        g_phys_map_blast_hook(SCENE_CUSTOM_LEVEL, 20.0f, 10.0f, -3.0f, MISSILE_SPLASH_RADIUS, 130, WPN_MISSILE);
        g_phys_blast_normal[2] = 0.0f;
        CHECK(!solid_at(1.5f, 10.0f, -1.5f) && !solid_at(19.5f, 10.0f, 1.5f));
        int removed_a = g_brick.parent[0].removed, pieces_a = g_brick.piece_count;
        static LevelBrickCell cells[LEVEL_BOXES_MAX_BRICK_CELLS];
        int n = brick_world_export_damage(cells, LEVEL_BOXES_MAX_BRICK_CELLS);
        CHECK(n > 0 && n == g_brick.rec_count);

        char *body = level_boxes_build_snapshot_json(7, cells, n);
        CHECK(body != NULL && strstr(body, "\"source_level_id\":7") != NULL);
        char *doc = (char *)malloc(strlen(body) + 16);
        sprintf(doc, "{\"walls\":[],%s", body + 1);
        static CustomLevelData parsed;
        CHECK(level_boxes_parse_json(doc, &parsed) == 1);
        CHECK(parsed.brick_damage_count == n);
        int same = 1;
        for (int i = 0; i < n; i++)
            same &= (parsed.brick_damage[i].wall == cells[i].wall && parsed.brick_damage[i].key == cells[i].key && parsed.brick_damage[i].hp == cells[i].hp);
        CHECK(same);   /* includes 30-bit keys: strtoul, not float */
        free(doc); free(body);

        load_level();                          /* fresh, undamaged */
        CHECK(solid_at(1.5f, 10.0f, -1.5f) && g_brick.rec_count == 0);
        g_lvl.brick_damage_count = parsed.brick_damage_count;
        memcpy(g_lvl.brick_damage, parsed.brick_damage, sizeof(LevelBrickCell) * (size_t)n);
        brick_world_init_from_level(&g_lvl);   /* what the server / lobby do on a snapshot load */
        CHECK(!solid_at(1.5f, 10.0f, -1.5f) && !solid_at(1.5f, 10.0f, 1.5f) && !solid_at(19.5f, 10.0f, 1.5f));
        CHECK(g_brick.parent[0].removed == removed_a && g_brick.piece_count == pieces_a);
        CHECK(solid_at(25.5f, 10.0f, 0.5f));
        /* hostile damage records are ignored, not trusted: bad wall, inactive parent, out-of-grid key */
        load_level();   /* real loads call phys_set_custom_level first, which resets the committed slots */
        g_lvl.brick_damage_count = 3;
        g_lvl.brick_damage[0] = (LevelBrickCell){ 99, 1, 0 };
        g_lvl.brick_damage[1] = (LevelBrickCell){ 1, 1, 0 };
        g_lvl.brick_damage[2] = (LevelBrickCell){ 0, 0x3FFFFFFFu, 0 };
        brick_world_init_from_level(&g_lvl);
        CHECK(g_brick.rec_count == 0 && solid_at(1.5f, 10.0f, -1.5f));
        g_lvl.brick_damage_count = 0;
    }

    /* 9. zombie claws (witness_ai -> brick_world_ai_wall_hit): repeated claw hits on a brick face open a hole */
    {
        load_level();
        brick_world_set_authority(1);
        CHECK(solid_at(1.5f, 10.0f, -1.5f));
        int swings = 0;
        for (; swings < 200 && solid_at(1.5f, 10.0f, -1.5f); swings++)
            brick_world_ai_wall_hit(SCENE_CUSTOM_LEVEL, 1.5f, 10.0f, -2.0f, 0.0f, 0.0f, -1.0f, 28);
        CHECK(!solid_at(1.5f, 10.0f, -1.5f));
        CHECK(swings > 1 && swings < 200);
        printf("zombie claw: hole after %d swings\n", swings);
        /* a hit with no wall behind it is a harmless no-op */
        int rc = g_brick.rec_count;
        brick_world_ai_wall_hit(SCENE_CUSTOM_LEVEL, 500.0f, 10.0f, 500.0f, 0.0f, 0.0f, -1.0f, 28);
        CHECK(g_brick.rec_count == rc);
    }

    if (g_fail) { printf("brick_world_test: %d FAILED\n", g_fail); return 1; }
    printf("brick_world_test: all checks passed (magnum breach in %d bursts)\n", shots);
    return 0;
}
