#ifndef SHANKPIT_MESH_ASSET_H
#define SHANKPIT_MESH_ASSET_H
/* mesh_asset.h -- a glTF mesh turned into what a level box can be skinned with: an LOD chain plus a baked normal map.
 *
 *   glTF/GLB  --mesh_gltf_load-->  MeshData (positions, normals, uv0, indices; normalised to a unit box)
 *   MeshData  --mesh_lod_build-->  MeshLods (LOD0 = the original, LOD1/2 = vertex-clustering decimations)
 *   MeshData  --mesh_bake_normal_map--> an RGB object-space normal map in the mesh's own UV layout
 *   MeshLods  --mesh_lods_save/load--> the runtime asset (.smsh): no glTF parsing in the game
 *
 * Why a bake at all: the LODs keep the ORIGINAL uv layout (a cluster keeps its representative vertex's uv), so ONE normal map baked from
 * the full-detail mesh's normals shades every LOD like the high-poly: the silhouette gets coarse with distance, the lighting does not.
 * The collision box stays the authority (level_boxes.h LevelBox.mesh); this is only the skin, scaled to the box's extents.
 *
 * Named limits (v0): glTF 2.0 only (.glb, or .gltf with embedded data: base64 buffers); float POSITION / NORMAL / TEXCOORD_0; u8/u16/u32
 * indices; triangle lists; every mesh primitive is merged and NODE TRANSFORMS ARE IGNORED (apply transforms on export); no skins, no morph targets,
 * no embedded material/textures. A mesh with no TEXCOORD_0 loads and decimates but cannot be baked. Caps: MESH_MAX_VERTS / MESH_MAX_TRIS.
 * Headless: no GL, no SDL. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MESH_MAX_VERTS 200000
#define MESH_MAX_TRIS 400000
#define MESH_MAX_LODS 3

typedef struct {
    int nv, nt;            /* vertices, triangles */
    float *pos;            /* nv*3, normalised so the AABB centre is 0 and the longest axis spans [-0.5, 0.5] */
    float *nrm;            /* nv*3, unit */
    float *uv;             /* nv*2, or NULL */
    uint32_t *idx;         /* nt*3 */
    float ext[3];          /* the ORIGINAL AABB extents, before normalisation (so a host can pick a sensible default box) */
} MeshData;

typedef struct { MeshData lod[MESH_MAX_LODS]; int n; } MeshLods;

static inline void mesh_free(MeshData *m) { free(m->pos); free(m->nrm); free(m->uv); free(m->idx); memset(m, 0, sizeof *m); }
static inline void mesh_lods_free(MeshLods *l) { for (int i = 0; i < l->n; i++) mesh_free(&l->lod[i]); l->n = 0; }

/* ---------------------------------------------------------------- a small JSON reader (just enough for glTF) */
typedef struct { const char *s; size_t n; } MjTok;     /* a value as a slice of the source text */

static inline const char *mj_ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; return p; }
/* end of the value starting at p (string, number, literal, object or array); NULL on malformed input */
static inline const char *mj_end(const char *p, const char *e) {
    p = mj_ws(p, e);
    if (p >= e) return NULL;
    if (*p == '"') { for (p++; p < e; p++) { if (*p == '\\') p++; else if (*p == '"') return p + 1; } return NULL; }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        for (; p < e; p++) {
            if (*p == '"') { const char *q = mj_end(p, e); if (!q) return NULL; p = q - 1; }
            else if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
        }
        return NULL;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p;
}
/* member `key` of the object [p,e): returns its value text, or {NULL,0} */
static inline MjTok mj_get(MjTok obj, const char *key) {
    MjTok none = {NULL, 0};
    const char *p = mj_ws(obj.s, obj.s + obj.n), *e = obj.s + obj.n;
    if (p >= e || *p != '{') return none;
    p++;
    size_t kl = strlen(key);
    while (p < e) {
        p = mj_ws(p, e);
        if (p >= e || *p == '}') break;
        if (*p == ',') { p++; continue; }
        if (*p != '"') return none;
        const char *ke = mj_end(p, e); if (!ke) return none;
        int match = (size_t)(ke - p - 2) == kl && !strncmp(p + 1, key, kl);
        p = mj_ws(ke, e); if (p >= e || *p != ':') return none;
        p = mj_ws(p + 1, e);
        const char *ve = mj_end(p, e); if (!ve) return none;
        if (match) { MjTok t = {p, (size_t)(ve - p)}; return t; }
        p = ve;
    }
    return none;
}
/* element i of the array value */
static inline MjTok mj_at(MjTok arr, int i) {
    MjTok none = {NULL, 0};
    const char *p = mj_ws(arr.s, arr.s + arr.n), *e = arr.s + arr.n;
    if (p >= e || *p != '[') return none;
    p++;
    for (int k = 0;; ) {
        p = mj_ws(p, e);
        if (p >= e || *p == ']') return none;
        if (*p == ',') { p++; continue; }
        const char *ve = mj_end(p, e); if (!ve) return none;
        if (k == i) { MjTok t = {p, (size_t)(ve - p)}; return t; }
        k++; p = ve;
    }
}
static inline int mj_len(MjTok arr) { int n = 0; while (mj_at(arr, n).s) n++; return n; }
static inline int mj_int(MjTok t, int dflt) { return t.s ? (int)strtol(t.s, NULL, 10) : dflt; }
static inline long mj_long(MjTok t, long dflt) { return t.s ? strtol(t.s, NULL, 10) : dflt; }

/* ---------------------------------------------------------------- base64 */
static inline int mesh_b64(const char *in, size_t n, unsigned char **out, size_t *outn) {
    static signed char T[256]; static int init = 0;
    if (!init) { memset(T, -1, sizeof T); const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; i++) T[(unsigned char)a[i]] = (signed char)i; init = 1; }
    unsigned char *o = (unsigned char *)malloc(n / 4 * 3 + 3); size_t k = 0; unsigned v = 0; int bits = 0;
    if (!o) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '=') break;
        if (T[c] < 0) { if (c == '\n' || c == '\r' || c == ' ') continue; free(o); return 0; }
        v = (v << 6) | (unsigned)T[c]; bits += 6;
        if (bits >= 8) { bits -= 8; o[k++] = (unsigned char)(v >> bits); v &= (1u << bits) - 1; }
    }
    *out = o; *outn = k; return 1;
}

/* ---------------------------------------------------------------- glTF */
typedef struct { unsigned char *d; size_t n; } MeshBuf;

static inline int mesh_read_accessor(MjTok root, MeshBuf *bufs, int nbufs, int acc, int comps, float *outf, uint32_t *outi, int count_expected) {
    MjTok a = mj_at(mj_get(root, "accessors"), acc);
    if (!a.s) return 0;
    int count = mj_int(mj_get(a, "count"), 0), ctype = mj_int(mj_get(a, "componentType"), 0);
    int bv = mj_int(mj_get(a, "bufferView"), -1);
    long aoff = mj_long(mj_get(a, "byteOffset"), 0);
    if (count <= 0 || (count_expected >= 0 && count != count_expected) || bv < 0) return 0;
    MjTok v = mj_at(mj_get(root, "bufferViews"), bv);
    if (!v.s) return 0;
    int b = mj_int(mj_get(v, "buffer"), 0);
    long voff = mj_long(mj_get(v, "byteOffset"), 0), vlen = mj_long(mj_get(v, "byteLength"), 0), stride = mj_long(mj_get(v, "byteStride"), 0);
    if (b < 0 || b >= nbufs) return 0;
    int csz = ctype == 5126 || ctype == 5125 ? 4 : ctype == 5123 ? 2 : ctype == 5121 ? 1 : 0;
    if (!csz) return 0;
    if (!stride) stride = (long)csz * comps;
    long start = voff + aoff, last = start + (long)(count - 1) * stride + (long)csz * comps;
    if (start < 0 || last > (long)bufs[b].n || (vlen && voff + vlen < last)) return 0;
    for (int i = 0; i < count; i++) {
        const unsigned char *p = bufs[b].d + start + (long)i * stride;
        for (int c = 0; c < comps; c++) {
            const unsigned char *q = p + c * csz;
            if (outf) { if (ctype != 5126) return 0; float f; memcpy(&f, q, 4); outf[i * comps + c] = f; }
            else { uint32_t u = ctype == 5125 ? (uint32_t)(q[0] | q[1] << 8 | q[2] << 16 | (uint32_t)q[3] << 24) : ctype == 5123 ? (uint32_t)(q[0] | q[1] << 8) : q[0]; outi[i * comps + c] = u; }
        }
    }
    return count;
}

/* Parses glTF JSON text (+ the binary chunk for a .glb: glb_bin/glb_n, else NULL/0) into *out. Returns 1 on success. */
static inline int mesh_gltf_parse(const char *json, size_t jn, const unsigned char *glb_bin, size_t glb_n, MeshData *out) {
    memset(out, 0, sizeof *out);
    MjTok root = {json, jn};
    MjTok bufs_j = mj_get(root, "buffers");
    int nb = mj_len(bufs_j);
    if (nb < 1 || nb > 8) return 0;
    MeshBuf bufs[8]; memset(bufs, 0, sizeof bufs);
    int ok = 0;
    for (int i = 0; i < nb; i++) {
        MjTok b = mj_at(bufs_j, i), uri = mj_get(b, "uri");
        if (!uri.s) {                                            /* the GLB-embedded buffer */
            if (i != 0 || !glb_bin) goto done;
            bufs[i].d = (unsigned char *)malloc(glb_n ? glb_n : 1); if (!bufs[i].d) goto done;
            memcpy(bufs[i].d, glb_bin, glb_n); bufs[i].n = glb_n;
        } else {
            const char *p = uri.s + 1, *e = uri.s + uri.n - 1, *comma = (const char *)memchr(p, ',', (size_t)(e - p));
            if (strncmp(p, "data:", 5) || !comma || !memchr(p, ';', (size_t)(comma - p))) goto done;    /* external files: refused, not fetched */
            if (!mesh_b64(comma + 1, (size_t)(e - comma - 1), &bufs[i].d, &bufs[i].n)) goto done;
        }
    }
    MjTok meshes = mj_get(root, "meshes");
    float *pos = NULL, *nrm = NULL, *uv = NULL; uint32_t *idx = NULL; int nv = 0, nt = 0, have_uv = 1;
    for (int mi = 0; mi < mj_len(meshes); mi++) {
        MjTok prims = mj_get(mj_at(meshes, mi), "primitives");
        for (int pi = 0; pi < mj_len(prims); pi++) {
            MjTok pr = mj_at(prims, pi), at = mj_get(pr, "attributes");
            if (mj_int(mj_get(pr, "mode"), 4) != 4) continue;                                  /* triangles only */
            int ap = mj_int(mj_get(at, "POSITION"), -1), an = mj_int(mj_get(at, "NORMAL"), -1), au = mj_int(mj_get(at, "TEXCOORD_0"), -1), ai = mj_int(mj_get(pr, "indices"), -1);
            if (ap < 0) continue;
            MjTok pa = mj_at(mj_get(root, "accessors"), ap);
            int cnt = mj_int(mj_get(pa, "count"), 0);
            if (cnt <= 0 || nv + cnt > MESH_MAX_VERTS) { free(pos); free(nrm); free(uv); free(idx); goto done; }
            pos = (float *)realloc(pos, (size_t)(nv + cnt) * 3 * sizeof(float));
            nrm = (float *)realloc(nrm, (size_t)(nv + cnt) * 3 * sizeof(float));
            uv = (float *)realloc(uv, (size_t)(nv + cnt) * 2 * sizeof(float));
            if (!pos || !nrm || !uv) { goto done; }
            if (mesh_read_accessor(root, bufs, nb, ap, 3, pos + nv * 3, NULL, cnt) != cnt) { free(pos); free(nrm); free(uv); free(idx); goto done; }
            if (an < 0 || mesh_read_accessor(root, bufs, nb, an, 3, nrm + nv * 3, NULL, cnt) != cnt) memset(nrm + nv * 3, 0, (size_t)cnt * 3 * sizeof(float));   /* recomputed below */
            if (au < 0 || mesh_read_accessor(root, bufs, nb, au, 2, uv + nv * 2, NULL, cnt) != cnt) { have_uv = 0; memset(uv + nv * 2, 0, (size_t)cnt * 2 * sizeof(float)); }
            int ni = 0;
            uint32_t *tmp = NULL;
            if (ai >= 0) {
                MjTok ia = mj_at(mj_get(root, "accessors"), ai);
                ni = mj_int(mj_get(ia, "count"), 0);
                if (ni <= 0 || ni % 3 || nt + ni / 3 > MESH_MAX_TRIS) { free(pos); free(nrm); free(uv); free(idx); goto done; }
                tmp = (uint32_t *)malloc((size_t)ni * sizeof(uint32_t));
                if (!tmp || mesh_read_accessor(root, bufs, nb, ai, 1, NULL, tmp, ni) != ni) { free(tmp); free(pos); free(nrm); free(uv); free(idx); goto done; }
            } else {                                                                            /* non-indexed: 0,1,2,... */
                ni = cnt - cnt % 3; if (ni <= 0) continue;
                tmp = (uint32_t *)malloc((size_t)ni * sizeof(uint32_t)); if (!tmp) { goto done; }
                for (int k = 0; k < ni; k++) tmp[k] = (uint32_t)k;
            }
            for (int k = 0; k < ni; k++) if (tmp[k] >= (uint32_t)cnt) { free(tmp); free(pos); free(nrm); free(uv); free(idx); goto done; }
            idx = (uint32_t *)realloc(idx, (size_t)(nt * 3 + ni) * sizeof(uint32_t));
            if (!idx) { free(tmp); goto done; }
            for (int k = 0; k < ni; k++) idx[nt * 3 + k] = tmp[k] + (uint32_t)nv;
            free(tmp);
            nv += cnt; nt += ni / 3;
        }
    }
    if (nv < 3 || nt < 1) { free(pos); free(nrm); free(uv); free(idx); goto done; }
    /* normals the file did not supply: area-weighted from faces */
    {
        int need = 0; for (int i = 0; i < nv; i++) if (nrm[i*3]==0 && nrm[i*3+1]==0 && nrm[i*3+2]==0) { need = 1; break; }
        if (need) {
            float *acc = (float *)calloc((size_t)nv * 3, sizeof(float));
            if (!acc) { free(pos); free(nrm); free(uv); free(idx); goto done; }
            for (int t = 0; t < nt; t++) {
                uint32_t a = idx[t*3], b = idx[t*3+1], c = idx[t*3+2];
                float u[3], v[3], n[3];
                for (int k = 0; k < 3; k++) { u[k] = pos[b*3+k] - pos[a*3+k]; v[k] = pos[c*3+k] - pos[a*3+k]; }
                n[0] = u[1]*v[2] - u[2]*v[1]; n[1] = u[2]*v[0] - u[0]*v[2]; n[2] = u[0]*v[1] - u[1]*v[0];
                uint32_t vs[3] = {a, b, c};
                for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) acc[vs[j]*3+k] += n[k];
            }
            for (int i = 0; i < nv; i++) if (nrm[i*3]==0 && nrm[i*3+1]==0 && nrm[i*3+2]==0) memcpy(nrm + i*3, acc + i*3, 3 * sizeof(float));
            free(acc);
        }
        for (int i = 0; i < nv; i++) {
            float l = sqrtf(nrm[i*3]*nrm[i*3] + nrm[i*3+1]*nrm[i*3+1] + nrm[i*3+2]*nrm[i*3+2]);
            if (l < 1e-12f) { nrm[i*3] = 0; nrm[i*3+1] = 1; nrm[i*3+2] = 0; } else { nrm[i*3] /= l; nrm[i*3+1] /= l; nrm[i*3+2] /= l; }
        }
    }
    /* normalise: AABB centre -> 0, longest axis -> 1 */
    {
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < nv; i++) for (int k = 0; k < 3; k++) { float v = pos[i*3+k]; if (!(v == v) || fabsf(v) > 1e9f) { free(pos); free(nrm); free(uv); free(idx); goto done; } if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
        float longest = 0; for (int k = 0; k < 3; k++) { out->ext[k] = hi[k] - lo[k]; if (out->ext[k] > longest) longest = out->ext[k]; }
        if (longest < 1e-9f) { free(pos); free(nrm); free(uv); free(idx); goto done; }
        for (int i = 0; i < nv; i++) for (int k = 0; k < 3; k++) pos[i*3+k] = (pos[i*3+k] - (lo[k] + hi[k]) * 0.5f) / longest;
    }
    out->nv = nv; out->nt = nt; out->pos = pos; out->nrm = nrm; out->idx = idx;
    if (have_uv) out->uv = uv; else free(uv);
    ok = 1;
done:
    for (int i = 0; i < nb; i++) free(bufs[i].d);
    return ok;
}

/* .glb container: 12-byte header, then JSON chunk (0x4E4F534A) and BIN chunk (0x004E4942). */
static inline int mesh_gltf_load_buffer(const unsigned char *d, size_t n, MeshData *out) {
    if (n >= 20 && !memcmp(d, "glTF", 4)) {
        uint32_t ver, total; memcpy(&ver, d + 4, 4); memcpy(&total, d + 8, 4);
        if (ver != 2 || total > n) return 0;
        size_t o = 12; const unsigned char *json = NULL, *bin = NULL; size_t jn = 0, bn = 0;
        while (o + 8 <= total) {
            uint32_t len, type; memcpy(&len, d + o, 4); memcpy(&type, d + o + 4, 4);
            if (o + 8 + len > total) return 0;
            if (type == 0x4E4F534Au && !json) { json = d + o + 8; jn = len; }
            else if (type == 0x004E4942u && !bin) { bin = d + o + 8; bn = len; }
            o += 8 + ((len + 3) & ~3u);
        }
        return json ? mesh_gltf_parse((const char *)json, jn, bin, bn, out) : 0;
    }
    return mesh_gltf_parse((const char *)d, n, NULL, 0, out);
}

static inline int mesh_gltf_load(const char *path, MeshData *out) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64L * 1024 * 1024) { fclose(f); return 0; }
    unsigned char *b = (unsigned char *)malloc((size_t)n); if (!b) { fclose(f); return 0; }
    size_t got = fread(b, 1, (size_t)n, f); fclose(f);
    int ok = got == (size_t)n && mesh_gltf_load_buffer(b, (size_t)n, out);
    free(b); return ok;
}

/* ---------------------------------------------------------------- LOD by vertex clustering */
/* Snap vertices to a res^3 grid over [-0.5,0.5]^3; each occupied cell becomes one vertex (mean position, mean normal renormalised, the uv of
 * its first member so the ORIGINAL unwrap -- and so the baked normal map -- still applies). Triangles that collapse are dropped. */
static inline int mesh_decimate(const MeshData *src, int res, MeshData *out) {
    memset(out, 0, sizeof *out);
    long cells = (long)res * res * res;
    int *cell_of = (int *)malloc((size_t)src->nv * sizeof(int));
    int *map = (int *)malloc((size_t)cells * sizeof(int));
    if (!cell_of || !map) { free(cell_of); free(map); return 0; }
    for (long i = 0; i < cells; i++) map[i] = -1;
    int nvo = 0;
    float *pos = (float *)calloc((size_t)src->nv * 3, sizeof(float)), *nrm = (float *)calloc((size_t)src->nv * 3, sizeof(float));
    float *uv = src->uv ? (float *)calloc((size_t)src->nv * 2, sizeof(float)) : NULL;
    int *cnt = (int *)calloc((size_t)src->nv, sizeof(int));
    uint32_t *remap = (uint32_t *)malloc((size_t)src->nv * sizeof(uint32_t));
    uint32_t *idx = (uint32_t *)malloc((size_t)src->nt * 3 * sizeof(uint32_t));
    if (!pos || !nrm || !cnt || !remap || !idx || (src->uv && !uv)) { free(cell_of); free(map); free(pos); free(nrm); free(uv); free(cnt); free(remap); free(idx); return 0; }
    for (int i = 0; i < src->nv; i++) {
        int c[3];
        for (int k = 0; k < 3; k++) { int g = (int)floorf((src->pos[i*3+k] + 0.5f) * (float)res); c[k] = g < 0 ? 0 : g >= res ? res - 1 : g; }
        long cell = ((long)c[2] * res + c[1]) * res + c[0];
        if (map[cell] < 0) { map[cell] = nvo; if (uv) { uv[nvo*2] = src->uv[i*2]; uv[nvo*2+1] = src->uv[i*2+1]; } nvo++; }
        int o = map[cell]; remap[i] = (uint32_t)o; cnt[o]++;
        for (int k = 0; k < 3; k++) { pos[o*3+k] += src->pos[i*3+k]; nrm[o*3+k] += src->nrm[i*3+k]; }
    }
    for (int o = 0; o < nvo; o++) {
        for (int k = 0; k < 3; k++) pos[o*3+k] /= (float)cnt[o];
        float l = sqrtf(nrm[o*3]*nrm[o*3] + nrm[o*3+1]*nrm[o*3+1] + nrm[o*3+2]*nrm[o*3+2]);
        if (l < 1e-9f) { nrm[o*3] = 0; nrm[o*3+1] = 1; nrm[o*3+2] = 0; } else for (int k = 0; k < 3; k++) nrm[o*3+k] /= l;
    }
    int nto = 0;
    for (int t = 0; t < src->nt; t++) {
        uint32_t a = remap[src->idx[t*3]], b = remap[src->idx[t*3+1]], c = remap[src->idx[t*3+2]];
        if (a == b || b == c || a == c) continue;
        idx[nto*3] = a; idx[nto*3+1] = b; idx[nto*3+2] = c; nto++;
    }
    free(cell_of); free(map); free(cnt); free(remap);
    if (nto < 1) { free(pos); free(nrm); free(uv); free(idx); return 0; }
    out->nv = nvo; out->nt = nto; out->pos = pos; out->nrm = nrm; out->uv = uv; out->idx = idx;
    memcpy(out->ext, src->ext, sizeof out->ext);
    return 1;
}

/* LOD0 = a copy of src; LOD1 / LOD2 = clustering at progressively coarser grids, kept only if they actually shrink the triangle count. */
static inline int mesh_lod_build(const MeshData *src, MeshLods *out) {
    memset(out, 0, sizeof *out);
    MeshData *l0 = &out->lod[0];
    l0->nv = src->nv; l0->nt = src->nt; memcpy(l0->ext, src->ext, sizeof l0->ext);
    l0->pos = (float *)malloc((size_t)src->nv * 3 * sizeof(float)); l0->nrm = (float *)malloc((size_t)src->nv * 3 * sizeof(float));
    l0->idx = (uint32_t *)malloc((size_t)src->nt * 3 * sizeof(uint32_t)); l0->uv = src->uv ? (float *)malloc((size_t)src->nv * 2 * sizeof(float)) : NULL;
    if (!l0->pos || !l0->nrm || !l0->idx || (src->uv && !l0->uv)) { mesh_free(l0); return 0; }
    memcpy(l0->pos, src->pos, (size_t)src->nv * 3 * sizeof(float)); memcpy(l0->nrm, src->nrm, (size_t)src->nv * 3 * sizeof(float));
    memcpy(l0->idx, src->idx, (size_t)src->nt * 3 * sizeof(uint32_t)); if (src->uv) memcpy(l0->uv, src->uv, (size_t)src->nv * 2 * sizeof(float));
    out->n = 1;
    /* grid resolutions chosen from the vertex count: LOD1 ~ cbrt-ish of a quarter of the verts per axis, LOD2 half of that */
    int r1 = (int)ceilf(cbrtf((float)src->nv) * 1.5f); if (r1 < 8) r1 = 8; if (r1 > 64) r1 = 64;
    int r2 = r1 / 2; if (r2 < 3) r2 = 3;
    int res[2] = {r1, r2};
    for (int i = 0; i < 2; i++) {
        MeshData d;
        if (!mesh_decimate(src, res[i], &d)) break;
        if (d.nt >= out->lod[out->n - 1].nt) { mesh_free(&d); continue; }    /* no real reduction: skip this level */
        out->lod[out->n++] = d;
    }
    return 1;
}

/* Which LOD to draw: distance in units of the model's size (dist / size), 0 = nearest. bias shifts it (+1 coarser, -1 finer); clamped. */
static inline int mesh_lod_pick(float dist, float size, int nlods, int bias) {
    float r = size > 1e-6f ? dist / size : 1e9f;
    int l = r < 4.0f ? 0 : r < 12.0f ? 1 : 2;
    l += bias;
    if (l < 0) l = 0;
    if (l > nlods - 1) l = nlods - 1;
    return l < 0 ? 0 : l;
}

/* ---------------------------------------------------------------- normal map bake */
/* Rasterise every triangle of the FULL-detail mesh in its own UV space, writing the interpolated, renormalised object-space normal as RGB
 * (n*0.5+0.5). Texels no triangle covers are filled by dilation (`pad` passes) so bilinear sampling at a seam does not pull in black.
 * rgb = size*size*3 bytes, caller-owned. Returns 1, or 0 if the mesh has no UVs. UV origin is bottom-left (glTF is top-left: v is flipped on write). */
static inline int mesh_bake_normal_map(const MeshData *m, int size, int pad, unsigned char *rgb) {
    if (!m->uv || size < 4) return 0;
    unsigned char *set = (unsigned char *)calloc((size_t)size * size, 1);
    if (!set) return 0;
    memset(rgb, 128, (size_t)size * size * 3);
    for (int t = 0; t < m->nt; t++) {
        uint32_t id[3] = {m->idx[t*3], m->idx[t*3+1], m->idx[t*3+2]};
        float x[3], y[3];
        for (int k = 0; k < 3; k++) { x[k] = m->uv[id[k]*2] * (float)size; y[k] = (1.0f - m->uv[id[k]*2+1]) * (float)size; }
        float den = (y[1]-y[2])*(x[0]-x[2]) + (x[2]-x[1])*(y[0]-y[2]);
        if (fabsf(den) < 1e-12f) continue;
        int x0 = (int)floorf(fminf(x[0], fminf(x[1], x[2]))), x1 = (int)ceilf(fmaxf(x[0], fmaxf(x[1], x[2])));
        int y0 = (int)floorf(fminf(y[0], fminf(y[1], y[2]))), y1 = (int)ceilf(fmaxf(y[0], fmaxf(y[1], y[2])));
        if (x0 < 0) { x0 = 0; }
        if (y0 < 0) { y0 = 0; }
        if (x1 > size - 1) { x1 = size - 1; }
        if (y1 > size - 1) { y1 = size - 1; }
        for (int py = y0; py <= y1; py++) for (int px = x0; px <= x1; px++) {
            float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
            float w0 = ((y[1]-y[2])*(fx-x[2]) + (x[2]-x[1])*(fy-y[2])) / den;
            float w1 = ((y[2]-y[0])*(fx-x[2]) + (x[0]-x[2])*(fy-y[2])) / den;
            float w2 = 1.0f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
            float n[3];
            for (int k = 0; k < 3; k++) n[k] = w0 * m->nrm[id[0]*3+k] + w1 * m->nrm[id[1]*3+k] + w2 * m->nrm[id[2]*3+k];
            float l = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]); if (l < 1e-9f) continue;
            unsigned char *o = rgb + ((size_t)py * size + px) * 3;
            for (int k = 0; k < 3; k++) { float v = (n[k] / l) * 0.5f + 0.5f; o[k] = (unsigned char)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f); }
            set[(size_t)py * size + px] = 1;
        }
    }
    for (int pass = 0; pass < pad; pass++) {                      /* dilate: an unset texel takes a set 4-neighbour */
        unsigned char *add = (unsigned char *)calloc((size_t)size * size, 1);
        if (!add) break;
        for (int py = 0; py < size; py++) for (int px = 0; px < size; px++) {
            if (set[(size_t)py * size + px]) continue;
            static const int d[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
            for (int k = 0; k < 4; k++) {
                int qx = px + d[k][0], qy = py + d[k][1];
                if (qx < 0 || qy < 0 || qx >= size || qy >= size || !set[(size_t)qy * size + qx]) continue;
                memcpy(rgb + ((size_t)py * size + px) * 3, rgb + ((size_t)qy * size + qx) * 3, 3);
                add[(size_t)py * size + px] = 1; break;
            }
        }
        for (size_t i = 0; i < (size_t)size * size; i++) if (add[i]) set[i] = 1;
        free(add);
    }
    int covered = 0; for (size_t i = 0; i < (size_t)size * size; i++) covered += set[i];
    free(set);
    return covered > 0;
}

/* uncompressed 24-bit TGA (top-left origin). Returns 1 on success. */
static inline int mesh_write_tga(const char *path, int size, const unsigned char *rgb) {
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    unsigned char h[18] = {0}; h[2] = 2; h[12] = (unsigned char)(size & 255); h[13] = (unsigned char)(size >> 8); h[14] = (unsigned char)(size & 255); h[15] = (unsigned char)(size >> 8); h[16] = 24; h[17] = 0x20;
    int ok = fwrite(h, 1, 18, f) == 18;
    for (size_t i = 0; ok && i < (size_t)size * size; i++) { unsigned char bgr[3] = {rgb[i*3+2], rgb[i*3+1], rgb[i*3]}; ok = fwrite(bgr, 1, 3, f) == 3; }
    return fclose(f) == 0 && ok;
}

/* ---------------------------------------------------------------- the runtime asset (.smsh) */
/* "SMSH" u32 version(1) u32 nlod, then per LOD: u32 nv, u32 nt, u32 has_uv, f32 pos[nv*3], f32 nrm[nv*3], f32 uv[nv*2]?, u32 idx[nt*3]. Little-endian host assumed (x86/ARM). */
static inline int mesh_lods_save(const char *path, const MeshLods *l) {
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    uint32_t h[3] = {0x48534D53u, 1u, (uint32_t)l->n}; int ok = fwrite(h, 4, 3, f) == 3;
    for (int i = 0; ok && i < l->n; i++) {
        const MeshData *m = &l->lod[i]; uint32_t g[3] = {(uint32_t)m->nv, (uint32_t)m->nt, m->uv ? 1u : 0u};
        ok = fwrite(g, 4, 3, f) == 3 && fwrite(m->pos, 4, (size_t)m->nv * 3, f) == (size_t)m->nv * 3 && fwrite(m->nrm, 4, (size_t)m->nv * 3, f) == (size_t)m->nv * 3
          && (!m->uv || fwrite(m->uv, 4, (size_t)m->nv * 2, f) == (size_t)m->nv * 2) && fwrite(m->idx, 4, (size_t)m->nt * 3, f) == (size_t)m->nt * 3
          && fwrite(m->ext, 4, 3, f) == 3;
    }
    return fclose(f) == 0 && ok;
}
static inline int mesh_lods_load(const char *path, MeshLods *l) {
    memset(l, 0, sizeof *l);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    uint32_t h[3]; int ok = fread(h, 4, 3, f) == 3 && h[0] == 0x48534D53u && h[1] == 1u && h[2] >= 1 && h[2] <= MESH_MAX_LODS;
    for (uint32_t i = 0; ok && i < h[2]; i++) {
        uint32_t g[3]; MeshData *m = &l->lod[i];
        ok = fread(g, 4, 3, f) == 3 && g[0] >= 3 && g[0] <= MESH_MAX_VERTS && g[1] >= 1 && g[1] <= MESH_MAX_TRIS;
        if (!ok) break;
        m->nv = (int)g[0]; m->nt = (int)g[1];
        m->pos = (float *)malloc((size_t)m->nv * 12); m->nrm = (float *)malloc((size_t)m->nv * 12); m->idx = (uint32_t *)malloc((size_t)m->nt * 12);
        m->uv = g[2] ? (float *)malloc((size_t)m->nv * 8) : NULL;
        ok = m->pos && m->nrm && m->idx && (!g[2] || m->uv)
          && fread(m->pos, 4, (size_t)m->nv * 3, f) == (size_t)m->nv * 3 && fread(m->nrm, 4, (size_t)m->nv * 3, f) == (size_t)m->nv * 3
          && (!m->uv || fread(m->uv, 4, (size_t)m->nv * 2, f) == (size_t)m->nv * 2) && fread(m->idx, 4, (size_t)m->nt * 3, f) == (size_t)m->nt * 3
          && fread(m->ext, 4, 3, f) == 3;
        for (int k = 0; ok && k < m->nt * 3; k++) if (m->idx[k] >= (uint32_t)m->nv) ok = 0;     /* a corrupt file must not index out of range */
        l->n = (int)i + 1;
    }
    fclose(f);
    if (!ok) mesh_lods_free(l);
    return ok;
}
#endif
