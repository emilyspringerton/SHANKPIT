// gband_skel_npc.c — see gband_skel_npc.h.
#include "gband_skel_npc.h"
#include "gband.h"
#include "gskel.h"
#include "gmesh.h"
#include "gseq.h"
#include "gpose.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static GSkel g_skel;
static GMesh g_mesh;
static GSeqClip g_clip;
static GSeq g_seq;
static int g_ready = 0;

static float *g_out_buf; /* owned, 6 floats per output vertex (pos+normal) */
static uint32_t g_out_capacity_verts;

int gband_skel_npc_init(const char *asset_dir, const char *mesh_name, const char *clip_name) {
    char path[512], gband_path[512], manifest_path[512];

    snprintf(path, sizeof(path), "%s/%s.gskel", asset_dir, mesh_name);
    if (!gskel_init(path, &g_skel)) return 0;
    snprintf(path, sizeof(path), "%s/%s.gmesh", asset_dir, mesh_name);
    if (!gmesh_init(path, &g_mesh)) return 0;

    snprintf(gband_path, sizeof(gband_path), "%s/%s.gband", asset_dir, clip_name);
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s.gband.json", asset_dir, clip_name);
    if (!gseq_clip_load(gband_path, manifest_path, &g_clip)) { gmesh_free(&g_mesh); return 0; }

    memset(&g_seq, 0, sizeof(g_seq));
    g_seq.steps[0].clip_index = 0;
    g_seq.steps[0].duration_seconds = -1.0f; /* the clip's own real duration */
    g_seq.step_count = 1;
    g_seq.blend_seconds = 0.0f; /* nothing to cross-fade into with a single, looping clip */
    g_seq.loop = 1;

    g_out_capacity_verts = g_mesh.index_count;
    g_out_buf = (float *)malloc((size_t)g_out_capacity_verts * 6 * sizeof(float));
    if (!g_out_buf) { gseq_clip_free(&g_clip); gmesh_free(&g_mesh); return 0; }

    g_ready = 1;
    return 1;
}

void gband_skel_npc_shutdown(void) {
    if (!g_ready) return;
    gseq_clip_free(&g_clip);
    gmesh_free(&g_mesh);
    free(g_out_buf);
    g_out_buf = NULL;
    g_ready = 0;
}

int gband_skel_npc_ready(void) { return g_ready; }

#define MAX_NPC_SLOTS 32
static GSeqPlayer g_players[MAX_NPC_SLOTS];
static int g_players_init[MAX_NPC_SLOTS];

void gband_skel_npc_draw(int npc_slot, float npc_x, float npc_y, float npc_z, float facing_rad, float dt_ms,
                          const Mat4 *vp,
                          void (*draw_skinned)(const float *verts6, int vert_count,
                                                const Mat4 *mvp, const Mat4 *model)) {
    if (!g_ready) return;
    if (npc_slot < 0 || npc_slot >= MAX_NPC_SLOTS) return;

    GSeqPlayer *player = &g_players[npc_slot];
    if (!g_players_init[npc_slot]) {
        gseq_player_init(player, &g_seq, &g_clip);
        g_players_init[npc_slot] = 1;
    }
    gseq_player_advance(player, dt_ms / 1000.0f);

    float pose_rot[GSKEL_MAX_JOINTS * 4];
    float pose_trans[GSKEL_MAX_JOINTS * 3];
    gseq_player_sample_pose(player, &g_skel, pose_rot, pose_trans);

    float skin[GSKEL_MAX_JOINTS][16];
    gpose_compute_skin_matrices(&g_skel, pose_rot, pose_trans, skin);

    /* Bake the NPC's own world transform (position + facing) into every joint's skin matrix,
       the same "vertices already come out world-space" contract gband_mesh_rig_draw's own
       comment documents -- mvp collapses to just vp for the draw call below. */
    Mat4 npc_world_t = mat4_translate(npc_x, npc_y, npc_z);
    Mat4 npc_rot = mat4_rotate_y(facing_rad);
    Mat4 npc_world = mat4_multiply(&npc_world_t, &npc_rot);
    for (uint32_t j = 0; j < g_skel.joint_count; j++) {
        Mat4 sj;
        memcpy(sj.m, skin[j], sizeof(sj.m));
        Mat4 baked = mat4_multiply(&npc_world, &sj);
        memcpy(skin[j], baked.m, sizeof(baked.m));
    }

    uint32_t vert_count = gpose_skin_mesh(&g_mesh, skin, g_out_buf);
    if (vert_count > g_out_capacity_verts) vert_count = g_out_capacity_verts; /* defensive */

    Mat4 identity = mat4_identity();
    Mat4 mvp = mat4_multiply(vp, &identity);
    draw_skinned(g_out_buf, (int)vert_count, &mvp, &identity);
}
