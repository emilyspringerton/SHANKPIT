#ifndef SHANKPIT_SPRAY_REGISTRY_H
#define SHANKPIT_SPRAY_REGISTRY_H

// spray_registry.h -- native client access to SHANKPIT's real sprays registry (S459-23, founder
// real-time: "remap that key to spray please and ensure there is a spray interface in the client
// HAVE IT REPLACE TDMO in the shankpit menu"). Deliberately reuses level_boxes.h's own real
// curl-via-popen fetch (level_boxes_fetch_url) and hand-rolled JSON scanner helpers
// (level_boxes_find_key/level_boxes_parse_number/level_boxes_parse_string/level_boxes_parse_bool)
// instead of duplicating them -- same real "smallest real thing" precedent that header itself
// documents, applied one layer up.
//
// S459-33 (2026-09-14, founder real-time: "finish the png thingy for the sprays do it in
// parena"): spray_registry_decode_image below now turns a spray's own real uploaded PNG artwork
// into a real GL texture, via a real PARENA-compiled PNG decoder (png_decode_gen.c, built from
// PARENA/stdlib/image/png.prn + compress/inflate.prn -- see that inflate.prn's own header for a
// real, found-and-fixed VS0 compiler codegen bug this needed first). Real, honest, still-narrower
// scope, matching image/png.prn's own header: only 8-bit-depth, non-interlaced RGB/RGBA PNGs
// decode -- IDUNA's own real /admin/nock spray-upload path always produces exactly this shape
// (standard PNG export), so this covers every real spray in the live registry, but a
// paletted/16-bit/interlaced PNG would gracefully report a decode failure instead of corrupt
// pixels. `spray_registry_decode_image` returns 0 (network/decode failure) or 1 (real texture
// ready) -- callers keep the existing hash-color placeholder as their own real fallback on 0,
// never crash or show garbage.

#include "level_boxes.h"
#include "png_decode.h"
/* Real, relative cross-package include -- matching apps/lobby/src/main.c's own established
   convention for reaching packages/render headers under Bazel's own strict per-package include
   resolution (a bare `#include "proc_tex.h"` only works under the Makefile's blanket -I flags,
   not under `bazel build`, confirmed live). */
#include "../render/proc_tex.h"

#define SPRAY_REGISTRY_MAX_ENTRIES 64
#define SPRAY_REGISTRY_NAME_LEN LEVEL_BOXES_MAX_NAME
#define SPRAY_REGISTRY_BASE_URL "https://okemily.com/api/v1/shankpit-sprays"

typedef struct {
    int id;
    char name[SPRAY_REGISTRY_NAME_LEN];
    int is_default;
} SprayRegistryEntry;

// spray_registry_parse_list parses IDUNA's real GET /api/v1/shankpit-sprays response (a JSON
// array of {"id":N,"name":"...","is_default":bool,...} objects -- see
// internal/shankpit.SpraySummary's own real shape) -- same real "flat array of flat objects"
// scanner convention level_boxes_parse_registry_list already established.
static inline int spray_registry_parse_list(const char *json, SprayRegistryEntry *out, int max) {
    int count = 0;
    const char *cursor = json;
    while (*cursor && count < max) {
        const char *obj_start = strchr(cursor, '{');
        if (!obj_start) break;
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        const char *id_val = level_boxes_find_key(obj_start, obj_end, "id");
        const char *name_val = level_boxes_find_key(obj_start, obj_end, "name");
        if (id_val && name_val) {
            float id_f;
            if (level_boxes_parse_number(id_val, &id_f)) {
                out[count].id = (int)id_f;
                level_boxes_parse_string(name_val, out[count].name, sizeof(out[count].name));
                out[count].is_default = 0;
                const char *def_val = level_boxes_find_key(obj_start, obj_end, "is_default");
                if (def_val) level_boxes_parse_bool(def_val, &out[count].is_default);
                count++;
            }
        }
        cursor = obj_end + 1;
    }
    return count;
}

// spray_registry_fetch_list fetches and parses the real, live sprays list. Returns the real entry
// count (0 if the registry is empty or unreachable -- a real network failure degrades to "no
// sprays shown," not a crash, matching level_boxes.h's own established convention).
static inline int spray_registry_fetch_list(SprayRegistryEntry *out, int max) {
    char *buf = NULL;
    long n = level_boxes_fetch_url(SPRAY_REGISTRY_BASE_URL, &buf);
    if (n <= 0) return 0;
    int count = spray_registry_parse_list(buf, out, max);
    free(buf);
    return count;
}

// spray_registry_image_url -- the real, public, unauthenticated GET .../{id}/image endpoint
// (internal/http/handlers/shankpit_sprays.go's own ShankpitSpraysPublicHandler.image) that serves
// a spray's raw, real PNG bytes.
static inline void spray_registry_image_url(int id, char *out, size_t out_len) {
    snprintf(out, out_len, "%s/%d/image", SPRAY_REGISTRY_BASE_URL, id);
}

// spray_registry_decode_image -- fetches a spray's real uploaded PNG (level_boxes_fetch_url, same
// real curl-via-popen fetch every other registry call in this file already uses -- binary-safe,
// fread-based, not text/line-oriented) and decodes it into a real, ready-to-render GL texture via
// png_decode (PARENA-compiled). Returns 0 on any real failure (network, non-PNG, or a PNG outside
// this decoder's own honest v0 scope) -- `out` is left untouched on failure so a caller can keep
// whatever texture (or the hash-color placeholder) it already had. The PARENA-side Arena is a
// real, local, one-shot allocation -- torn down (`arena_free_all`) once the decoded pixels are
// copied into `out`'s own malloc'd `ProcTexture` buffer, not kept alive past this call.
static inline int spray_registry_decode_image(int id, ProcTexture *out) {
    char url[256];
    spray_registry_image_url(id, url, sizeof(url));
    char *buf = NULL;
    long n = level_boxes_fetch_url(url, &buf);
    if (n <= 0) return 0;

    Arena a;
    arena_init(&a);
    Bytes data = bytes_alloc_impl(&a, (int)n);
    for (long i = 0; i < n; i++) bytes_set_impl(data, (int)i, (unsigned char)buf[i]);
    free(buf);

    PngImage img = png_decode(data, &a);
    if (!img.ok || img.width <= 0 || img.height <= 0) {
        arena_free_all(&a);
        return 0;
    }

    if (!proc_tex_create(out, img.width, img.height)) {
        arena_free_all(&a);
        return 0;
    }
    for (int i = 0; i < img.width * img.height * 4; i++) {
        out->pixels[i] = (unsigned char)bytes_get_impl(img.pixels, i);
    }
    proc_tex_upload(out);
    arena_free_all(&a);
    return 1;
}

#endif
