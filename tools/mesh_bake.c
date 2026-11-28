/* mesh_bake -- turn a glTF/GLB into a level-box skin: an LOD chain (.smsh) and a baked normal map (.tga).
 *   mesh_bake <in.glb|in.gltf> <out_dir> <asset_id> [normal_map_size=512]
 * Writes <out_dir>/<asset_id>.smsh and, if the mesh has UVs, <out_dir>/<asset_id>_n.tga. The asset id is what a level box's "mesh" field names
 * (the normal map is "<asset_id>_n.tga"); NOCK's upload does the same through this code. Exit 0 on success, 1 on any failure (nothing half-written
 * is left claiming success). */
#include "mesh_asset.h"
#include <ctype.h>

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: mesh_bake <in.glb|in.gltf> <out_dir> <asset_id> [normal_map_size]\n"); return 1; }
    const char *id = argv[3];
    for (const char *c = id; *c; c++) if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-') { fprintf(stderr, "mesh_bake: asset id must be [A-Za-z0-9_-]\n"); return 1; }
    if (!*id || strlen(id) > 36) { fprintf(stderr, "mesh_bake: asset id empty or too long\n"); return 1; }
    int size = argc > 4 ? atoi(argv[4]) : 512;
    if (size < 16 || size > 4096) { fprintf(stderr, "mesh_bake: normal map size must be 16..4096\n"); return 1; }
    MeshData m; MeshLods L;
    if (!mesh_gltf_load(argv[1], &m)) { fprintf(stderr, "mesh_bake: cannot read %s as a supported glTF (see mesh_asset.h for the v0 limits)\n", argv[1]); return 1; }
    if (!mesh_lod_build(&m, &L)) { fprintf(stderr, "mesh_bake: LOD build failed\n"); return 1; }
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.smsh", argv[2], id);
    if (!mesh_lods_save(path, &L)) { fprintf(stderr, "mesh_bake: cannot write %s\n", path); return 1; }
    printf("%s: %d LODs (", path, L.n);
    for (int i = 0; i < L.n; i++) printf("%s%d tris", i ? " -> " : "", L.lod[i].nt);
    printf("), original extents %.3f x %.3f x %.3f\n", m.ext[0], m.ext[1], m.ext[2]);
    if (m.uv) {
        unsigned char *rgb = (unsigned char *)malloc((size_t)size * size * 3);
        snprintf(path, sizeof path, "%s/%s_n.tga", argv[2], id);
        if (!rgb || !mesh_bake_normal_map(&m, size, 4, rgb) || !mesh_write_tga(path, size, rgb)) { fprintf(stderr, "mesh_bake: normal map bake failed\n"); return 1; }
        printf("%s: %dx%d object-space normal map\n", path, size, size);
        free(rgb);
    } else printf("no TEXCOORD_0 in the mesh: LODs written, no normal map (unwrap it in Blender and export UVs)\n");
    mesh_free(&m); mesh_lods_free(&L);
    return 0;
}
