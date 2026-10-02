/* held_model.h -- a hand-held weapon model that comes from a NOCK widget (card #447: "use the model in
 * 3p"). The hammer is modelled in Blender, imported through NOCK's Widgets glTF importer (one box per
 * mesh object) and saved as the widget MODEL_HAMMER; IDUNA serves it read-only at
 * /api/v1/shankpit-models/MODEL_HAMMER and the lobby draws those boxes in the hand, in first and third
 * person. Nothing about how it was modelled needs to be remembered: held_model_normalize() turns the
 * boxes into hand space whatever their orientation -- the LONGEST axis becomes the handle (+Z), the
 * HEAVIER end (more box volume) points forward, the whole thing is scaled to target_len and its grip end
 * sits at grip_z. Pure functions, headless-testable (held_model_test.c). */
#ifndef HELD_MODEL_H
#define HELD_MODEL_H

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "level_boxes.h"

#define HELD_MODEL_MAX 64
#define HELD_MODEL_BASE_URL "https://okemily.com/api/v1/shankpit-models/"

typedef struct { float x, y, z, w, h, d, r, g, b; } HeldBox;   /* centre + full extents + colour */
typedef struct { HeldBox box[HELD_MODEL_MAX]; int count; } HeldModel;

/* held_model_parse_json -- a widget's JSON ({"walls":[{x,y,z,sx,sy,sz,r,g,b,...}],...}) -> boxes. Returns the
 * count (0 if there is no usable walls array). Extra boxes past `max` are dropped, never overflowed. */
static inline int held_model_parse_json(const char *json, HeldBox *out, int max) {
    const char *end = json + strlen(json);
    const char *key = level_boxes_find_key(json, end, "walls");
    if (!key) return 0;
    const char *arr = level_boxes_skip_ws(key);
    if (*arr != '[') return 0;
    const char *arr_end = level_boxes_find_array_end(arr, end);
    if (!arr_end) return 0;
    int n = 0;
    const char *cur = arr + 1;
    while (cur < arr_end && n < max) {
        cur = level_boxes_skip_ws(cur);
        if (cur >= arr_end) break;
        if (*cur != '{') { cur++; continue; }
        const char *o0 = cur, *o1 = strchr(o0, '}');
        if (!o1 || o1 > arr_end) break;
        HeldBox b; memset(&b, 0, sizeof(b));
        const char *v; int ok = 1;
        if ((v = level_boxes_find_key(o0, o1, "x"))) ok &= level_boxes_parse_number(v, &b.x); else ok = 0;
        if ((v = level_boxes_find_key(o0, o1, "y"))) ok &= level_boxes_parse_number(v, &b.y); else ok = 0;
        if ((v = level_boxes_find_key(o0, o1, "z"))) ok &= level_boxes_parse_number(v, &b.z); else ok = 0;
        if ((v = level_boxes_find_key(o0, o1, "sx"))) ok &= level_boxes_parse_number(v, &b.w); else ok = 0;
        if ((v = level_boxes_find_key(o0, o1, "sy"))) ok &= level_boxes_parse_number(v, &b.h); else ok = 0;
        if ((v = level_boxes_find_key(o0, o1, "sz"))) ok &= level_boxes_parse_number(v, &b.d); else ok = 0;
        b.r = b.g = b.b = 0.6f;
        if ((v = level_boxes_find_key(o0, o1, "r"))) level_boxes_parse_number(v, &b.r);
        if ((v = level_boxes_find_key(o0, o1, "g"))) level_boxes_parse_number(v, &b.g);
        if ((v = level_boxes_find_key(o0, o1, "b"))) level_boxes_parse_number(v, &b.b);
        if (ok && b.w > 0 && b.h > 0 && b.d > 0) out[n++] = b;
        cur = o1 + 1;
    }
    return n;
}

/* held_model_normalize -- see the file comment. Returns 1 on success, 0 if there is nothing to normalise. */
static inline int held_model_normalize(const HeldBox *in, int n, float target_len, float grip_z, HeldModel *out) {
    if (n <= 0 || target_len <= 0.0f) return 0;
    if (n > HELD_MODEL_MAX) n = HELD_MODEL_MAX;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < n; i++) {
        float c[3] = { in[i].x, in[i].y, in[i].z }, s[3] = { in[i].w, in[i].h, in[i].d };
        for (int a = 0; a < 3; a++) {
            if (c[a] - s[a] * 0.5f < lo[a]) lo[a] = c[a] - s[a] * 0.5f;
            if (c[a] + s[a] * 0.5f > hi[a]) hi[a] = c[a] + s[a] * 0.5f;
        }
    }
    int la = 0;                                         /* longest axis = the handle */
    for (int a = 1; a < 3; a++) if (hi[a] - lo[a] > hi[la] - lo[la]) la = a;
    float len = hi[la] - lo[la];
    if (len < 1e-6f) return 0;
    float mid = (lo[la] + hi[la]) * 0.5f;
    double vol = 0, moment = 0;                         /* volume-weighted centre along the handle axis */
    for (int i = 0; i < n; i++) {
        float c[3] = { in[i].x, in[i].y, in[i].z };
        double v = (double)in[i].w * in[i].h * in[i].d;
        vol += v; moment += v * c[la];
    }
    int flip = (vol > 0 && moment / vol < mid) ? 1 : 0; /* heavy end must point to +Z */
    float s = target_len / len;
    int oa = (la + 1) % 3, ob = (la + 2) % 3;
    float cen_a = (lo[oa] + hi[oa]) * 0.5f, cen_b = (lo[ob] + hi[ob]) * 0.5f;
    out->count = n;
    for (int i = 0; i < n; i++) {
        float c[3] = { in[i].x, in[i].y, in[i].z }, e[3] = { in[i].w, in[i].h, in[i].d };
        float along = (c[la] - mid) * (flip ? -1.0f : 1.0f);
        HeldBox *o = &out->box[i];
        o->x = (c[oa] - cen_a) * s;
        o->y = (c[ob] - cen_b) * s;
        o->z = grip_z + target_len * 0.5f + along * s;
        o->w = e[oa] * s; o->h = e[ob] * s; o->d = e[la] * s;
        o->r = in[i].r; o->g = in[i].g; o->b = in[i].b;
    }
    return 1;
}

/* held_model_load -- fetch the named widget model from IDUNA (curl, short timeouts), cache the raw JSON next
 * to the executable as shankpit_model_<name>.json, and fall back to that cache when offline. Blocking: call it
 * from a worker thread. Returns the box count (0 = no model, the caller draws its built-in one). */
static inline int held_model_load(const char *name, HeldBox *out, int max) {
    char cache[160];
    snprintf(cache, sizeof(cache), "shankpit_model_%s.json", name);
    char url[256];
    snprintf(url, sizeof(url), "%s%s", HELD_MODEL_BASE_URL, name);
    char *buf = NULL;
    long n = level_boxes_fetch_url(url, &buf);
    if (n > 0) {
        int count = held_model_parse_json(buf, out, max);
        if (count > 0) {
            FILE *f = fopen(cache, "wb");
            if (f) { fwrite(buf, 1, (size_t)n, f); fclose(f); }
        }
        free(buf);
        if (count > 0) return count;
    } else if (buf) {
        free(buf);
    }
    FILE *f = fopen(cache, "rb");
    if (!f) return 0;
    char *data = (char *)malloc(LEVEL_BOXES_MAX_FETCH_BYTES + 1);
    if (!data) { fclose(f); return 0; }
    size_t got = fread(data, 1, LEVEL_BOXES_MAX_FETCH_BYTES, f);
    fclose(f);
    data[got] = '\0';
    int count = held_model_parse_json(data, out, max);
    free(data);
    return count;
}

#endif
