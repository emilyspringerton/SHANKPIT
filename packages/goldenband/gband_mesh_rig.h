// gband_mesh_rig.h — S144-02 Stage B: real vertex-weighted skinned mesh for
// SHANKPIT, ported from REDGARDEN/GFD's S144-07 (same module, same GOLDENBAND
// .gband/.gskel/.gmesh asset format). See
// GoblinFoxDragon/docs2/GOLDENBAND_INTEGRATION_NORTHSTAR.md §2 Phase 2.
//
// CPU skinning, deliberately (see the northstar doc for why): this module
// computes fully-skinned, flattened (non-indexed) triangle-list vertex data
// each frame -- pos.xyz + normal.xyz per vertex -- and hands it to the
// caller via a callback, the same "never touch main.c's GL function
// pointers directly" boundary packages/render/gl_shader.h's DynamicVBO
// already established for Stage A.
//
// No box-rig fallback exists in SHANKPIT (unlike REDGARDEN's gband_rig.c) --
// callers should fall back to the plain procedural box body (this repo's
// pre-existing draw_player_skin_tyler) on gband_mesh_rig_ready() == 0,
// never draw nothing.
#ifndef GOLDENBAND_GBAND_MESH_RIG_H
#define GOLDENBAND_GBAND_MESH_RIG_H

#include "../common/mat4.h"

// gband_mesh_rig_init loads <asset_dir>/<mesh_name>.gskel + .gmesh, plus
// <mesh_name>_idle.gband/<mesh_name>_walk.gband from asset_dir -- a real
// Blender-exported skeleton's bones carry their own real rest rotation
// (confirmed live in REDGARDEN: ~178 degrees on the founder's actual arm
// bones) that a clip authored against a flat/identity-rotation skeleton
// doesn't account for, so clips are scoped per mesh_name rather than
// shared with any other rig's clips. Returns 1 on success, 0 on failure --
// callers should fall back to the plain procedural box body on failure,
// never draw nothing.
int gband_mesh_rig_init(const char *asset_dir, const char *mesh_name);

void gband_mesh_rig_shutdown(void);

int gband_mesh_rig_ready(void);

// gband_mesh_rig_draw animates, skins, and draws the mesh for one hero slot
// (independent animation clock per slot, keyed by hero_slot -- callers pass
// a stable per-player index, e.g. PlayerState.id). draw_skinned receives
// this frame's flattened pos+normal vertex data (vert_count vertices, 6
// floats each) plus the already-computed mvp/model matrices -- the caller
// uploads it to a dynamic VBO and draws.
//
// hero_y (SHANKPIT-specific addition vs. the REDGARDEN/GFD original this
// was ported from): those callers always draw at ground level (Y=0 baked
// into hero_world_t), safe for their flatter arena. SHANKPIT has real
// jumping/verticality -- rendering a jumping player's mesh pinned to Y=0
// would visibly desync it from the camera and from the existing box body's
// own p->y-driven position, so this port threads the real height through.
void gband_mesh_rig_draw(int hero_slot, float hero_x, float hero_y, float hero_z, float facing_rad, float dt_ms,
                          const Mat4 *vp,
                          void (*draw_skinned)(const float *verts6, int vert_count,
                                                const Mat4 *mvp, const Mat4 *model));

#endif // GOLDENBAND_GBAND_MESH_RIG_H
