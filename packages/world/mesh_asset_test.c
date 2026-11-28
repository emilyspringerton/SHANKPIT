#define _POSIX_C_SOURCE 200809L
/* mesh_asset_test.c -- glTF -> LOD chain -> baked normal map -> .smsh, on a procedural UV sphere written out as a real glTF.   make test-mesh-asset */
#include "mesh_asset.h"
#include <unistd.h>
#include <assert.h>
#include <stdio.h>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void b64(const unsigned char *d, size_t n, char *o) {
    static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = d[i] << 16 | (i + 1 < n ? d[i+1] << 8 : 0) | (i + 2 < n ? d[i+2] : 0);
        o[k++] = A[v >> 18 & 63]; o[k++] = A[v >> 12 & 63]; o[k++] = i + 1 < n ? A[v >> 6 & 63] : '='; o[k++] = i + 2 < n ? A[v & 63] : '=';
    }
    o[k] = 0;
}

/* a UV sphere, radius 1.5 on a 2x1x1-stretched frame so the original extents are checkable: SEG x RING quads */
#define SEG 48
#define RING 32
static size_t make_sphere_gltf(char *out, size_t cap, int with_uv, int with_normals) {
    static float pos[(SEG+1)*(RING+1)*3], nrm[(SEG+1)*(RING+1)*3], uv[(SEG+1)*(RING+1)*2];
    static uint32_t idx[SEG*RING*6];
    int nv = 0, ni = 0;
    for (int r = 0; r <= RING; r++) for (int s = 0; s <= SEG; s++) {
        float th = (float)r / RING * 3.14159265f, ph = (float)s / SEG * 6.2831853f;
        float x = sinf(th) * cosf(ph), y = cosf(th), z = sinf(th) * sinf(ph);
        pos[nv*3] = 2.0f * x; pos[nv*3+1] = y; pos[nv*3+2] = z;                 /* stretched: extents 4 x 2 x 2 */
        float nx = x / 2.0f, ny = y, nz = z, l = sqrtf(nx*nx + ny*ny + nz*nz);  /* the true ellipsoid normal */
        nrm[nv*3] = nx / l; nrm[nv*3+1] = ny / l; nrm[nv*3+2] = nz / l;
        uv[nv*2] = (float)s / SEG; uv[nv*2+1] = 1.0f - (float)r / RING;
        nv++;
    }
    for (int r = 0; r < RING; r++) for (int s = 0; s < SEG; s++) {
        uint32_t a = (uint32_t)(r*(SEG+1)+s), b = a + 1, c = a + SEG + 1, d = c + 1;
        idx[ni++] = a; idx[ni++] = c; idx[ni++] = b; idx[ni++] = b; idx[ni++] = c; idx[ni++] = d;
    }
    size_t pb = (size_t)nv*12, nb = (size_t)nv*12, ub = (size_t)nv*8, ib = (size_t)ni*4, total = pb + nb + ub + ib;
    unsigned char *bin = (unsigned char *)malloc(total); size_t o = 0;
    memcpy(bin + o, pos, pb); o += pb; memcpy(bin + o, nrm, nb); o += nb; memcpy(bin + o, uv, ub); o += ub; memcpy(bin + o, idx, ib);
    char *enc = (char *)malloc(total * 2 + 8); b64(bin, total, enc);
    int n = snprintf(out, cap,
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%zu,\"uri\":\"data:application/octet-stream;base64,%s\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%zu},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":%d,\"type\":\"VEC2\"},{\"bufferView\":3,\"componentType\":5125,\"count\":%d,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0%s%s},\"indices\":3,\"mode\":4}]}]}",
        total, enc, pb, pb, nb, pb + nb, ub, pb + nb + ub, ib, nv, nv, nv, ni,
        with_normals ? ",\"NORMAL\":1" : "", with_uv ? ",\"TEXCOORD_0\":2" : "");
    free(bin); free(enc);
    return (size_t)n;
}

int main(void) {
    static char gl[1 << 20];
    size_t gn = make_sphere_gltf(gl, sizeof gl, 1, 1);
    MeshData m;
    CHECK(mesh_gltf_load_buffer((const unsigned char *)gl, gn, &m));
    CHECK(m.nv == (SEG+1)*(RING+1) && m.nt == SEG*RING*2);
    CHECK(fabsf(m.ext[0] - 4.0f) < 1e-3f && fabsf(m.ext[1] - 2.0f) < 1e-3f && fabsf(m.ext[2] - 2.0f) < 1e-3f);   /* original extents kept */
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < m.nv; i++) { if (m.pos[i*3] < lo) lo = m.pos[i*3]; if (m.pos[i*3] > hi) hi = m.pos[i*3]; }
    CHECK(fabsf(lo + 0.5f) < 1e-3f && fabsf(hi - 0.5f) < 1e-3f);                                                  /* normalised: longest axis -> [-.5,.5] */
    int unit = 1; for (int i = 0; i < m.nv; i++) { float l = sqrtf(m.nrm[i*3]*m.nrm[i*3] + m.nrm[i*3+1]*m.nrm[i*3+1] + m.nrm[i*3+2]*m.nrm[i*3+2]); if (fabsf(l - 1) > 1e-3f) unit = 0; }
    CHECK(unit);

    /* LOD chain: strictly fewer triangles each level, all still valid, uvs preserved */
    MeshLods L;
    CHECK(mesh_lod_build(&m, &L));
    CHECK(L.n == 3);
    for (int i = 1; i < L.n; i++) CHECK(L.lod[i].nt < L.lod[i-1].nt && L.lod[i].nt >= 1);
    for (int i = 0; i < L.n; i++) {
        int ok = L.lod[i].uv != NULL;
        for (int k = 0; k < L.lod[i].nt * 3; k++) if (L.lod[i].idx[k] >= (uint32_t)L.lod[i].nv) ok = 0;
        for (int v = 0; v < L.lod[i].nv * 3; v++) if (!(fabsf(L.lod[i].pos[v]) <= 0.5001f)) ok = 0;                  /* stays inside the unit box */
        CHECK(ok);
    }
    printf("  lod tris: %d -> %d -> %d\n", L.lod[0].nt, L.lod[1].nt, L.lod[2].nt);

    /* the bake: every texel inside the sphere's UV rectangle is a unit normal, encoded; and it matches the surface */
    enum { N = 128 };
    static unsigned char rgb[N*N*3];
    CHECK(mesh_bake_normal_map(&m, N, 2, rgb));
    /* texel for (u,v): pole-free equator row r=RING/2 -> th=pi/2, s=0 -> ph=0: surface normal is (+1,0,0) */
    {
        int px = 2, py = N / 2;                                    /* u ~ 0.02, v ~ 0.5 */
        float nx = rgb[(py*N+px)*3]/255.0f*2-1, ny = rgb[(py*N+px)*3+1]/255.0f*2-1, nz = rgb[(py*N+px)*3+2]/255.0f*2-1;
        CHECK(nx > 0.9f && fabsf(ny) < 0.1f && fabsf(nz) < 0.35f);
        /* the quarter-turn: u = 0.25 -> ph = pi/2 -> normal (0,0,+1) */
        px = N / 4; nx = rgb[(py*N+px)*3]/255.0f*2-1; nz = rgb[(py*N+px)*3+2]/255.0f*2-1;
        CHECK(nz > 0.9f && fabsf(nx) < 0.2f);
        /* top row near v=1 -> th~0 -> normal (0,+1,0): image row 0 */
        px = N / 3; py = 1; ny = rgb[(py*N+px)*3+1]/255.0f*2-1;
        CHECK(ny > 0.9f);
    }
    int nonflat = 0; for (int i = 0; i < N*N; i++) if (rgb[i*3] != 128 || rgb[i*3+1] != 128 || rgb[i*3+2] != 128) nonflat++;
    CHECK(nonflat > N*N*9/10);                                     /* the sphere's unwrap fills the sheet; dilation covers the seam */
    CHECK(mesh_write_tga("/tmp/mesh_asset_test_n.tga", N, rgb));
    { FILE *f = fopen("/tmp/mesh_asset_test_n.tga", "rb"); fseek(f, 0, SEEK_END); CHECK(ftell(f) == 18 + N*N*3); fclose(f); }

    /* .smsh round trip */
    CHECK(mesh_lods_save("/tmp/mesh_asset_test.smsh", &L));
    MeshLods R;
    CHECK(mesh_lods_load("/tmp/mesh_asset_test.smsh", &R) && R.n == L.n);
    for (int i = 0; i < R.n; i++) CHECK(R.lod[i].nv == L.lod[i].nv && R.lod[i].nt == L.lod[i].nt && !memcmp(R.lod[i].idx, L.lod[i].idx, (size_t)L.lod[i].nt*12));
    /* a truncated / corrupt file is refused, never half-loaded */
    { FILE *f = fopen("/tmp/mesh_asset_test.smsh", "r+b"); fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); CHECK(truncate("/tmp/mesh_asset_test.smsh", n / 2) == 0); }
    MeshLods bad; CHECK(!mesh_lods_load("/tmp/mesh_asset_test.smsh", &bad) && bad.n == 0);

    /* LOD pick: by distance in model sizes, bias shifts, clamped */
    CHECK(mesh_lod_pick(2, 1, 3, 0) == 0 && mesh_lod_pick(6, 1, 3, 0) == 1 && mesh_lod_pick(50, 1, 3, 0) == 2);
    CHECK(mesh_lod_pick(2, 1, 3, 1) == 1 && mesh_lod_pick(50, 1, 3, 5) == 2 && mesh_lod_pick(2, 1, 3, -2) == 0 && mesh_lod_pick(50, 1, 2, 0) == 1);

    /* no UVs: loads and decimates, but cannot be baked; no normals: recomputed, unit */
    MeshData nu; gn = make_sphere_gltf(gl, sizeof gl, 0, 0);
    CHECK(mesh_gltf_load_buffer((const unsigned char *)gl, gn, &nu) && nu.uv == NULL);
    CHECK(!mesh_bake_normal_map(&nu, N, 2, rgb));
    int unit2 = 1; for (int i = 0; i < nu.nv; i++) { float l = sqrtf(nu.nrm[i*3]*nu.nrm[i*3] + nu.nrm[i*3+1]*nu.nrm[i*3+1] + nu.nrm[i*3+2]*nu.nrm[i*3+2]); if (fabsf(l - 1) > 1e-3f) unit2 = 0; }
    CHECK(unit2);

    /* hostile glTF: external uri, out-of-range index, garbage -- all refused */
    MeshData h;
    const char *ext = "{\"buffers\":[{\"byteLength\":4,\"uri\":\"/etc/passwd\"}],\"meshes\":[]}";
    CHECK(!mesh_gltf_load_buffer((const unsigned char *)ext, strlen(ext), &h));
    CHECK(!mesh_gltf_load_buffer((const unsigned char *)"not json at all", 15, &h));
    CHECK(!mesh_gltf_load_buffer((const unsigned char *)"glTF\x02\0\0\0\xff\xff\xff\x7f", 12, &h));
    gn = make_sphere_gltf(gl, sizeof gl, 1, 1);
    char *p = strstr(gl, "\"count\":2013"); /* shrink the index accessor's claim past the buffer instead: count too large */
    (void)p;
    char *q = strstr(gl, "\"componentType\":5125,\"count\":"); if (q) { q += strlen("\"componentType\":5125,\"count\":"); memcpy(q, "99999999", 8); }
    CHECK(!mesh_gltf_load_buffer((const unsigned char *)gl, strlen(gl), &h));

    mesh_free(&m); mesh_free(&nu); mesh_lods_free(&L); mesh_lods_free(&R);
    if (g_fail) { printf("test-mesh-asset: %d FAILED\n", g_fail); return 1; }
    printf("test-mesh-asset: ok\n");
    return 0;
}
