#ifndef SHANKPIT_WORLD_PARENA_RUNTIME_H
#define SHANKPIT_WORLD_PARENA_RUNTIME_H

// parena_runtime.h -- a real, minimal, hand-vendored subset of PARENA/runtime/parena_runtime.h
// (Arena/Vec/Bytes only -- no SDL2, no POSIX networking/tty/hardware headers that real
// parena_runtime.h unconditionally pulls in). Same real "vendor only what's needed" precedent
// packages/simulation/parena_runtime.h already set for the scalar-only cutscene_effect_mod.c --
// applied here a second time now that a real PARENA module (image/png.prn, via compress/
// inflate.prn) needs the heavier Arena/Vec/Bytes trio. Kept byte-for-byte identical to the real
// runtime's own struct layouts and function bodies (copy, not reinvention) so the checked-in
// generated C (png_decode_gen.c) links against it exactly as `parena build` expects. See
// EMILY/BACKLOG.md S459-33 for the real story (a genuine VS0 codegen bug found and fixed in
// compress/inflate.prn before this could work at all).

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct ParenaArenaBlock {
    struct ParenaArenaBlock *next;
    size_t used;
    size_t capacity;
    unsigned char data[];
} ParenaArenaBlock;

typedef struct {
    ParenaArenaBlock *head;
} Arena;

void arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *src, size_t len);
void arena_free_all(Arena *a);

typedef struct {
    int tag;
    void *value;
} Result;

typedef struct {
    int tag;
    void *value;
} Option;

static inline Result result_ok(void *v) { Result r; r.tag = 1; r.value = v; return r; }
static inline Result result_err(void *v) { Result r; r.tag = 0; r.value = v; return r; }
static inline Option option_some(void *v) { Option o; o.tag = 1; o.value = v; return o; }
static inline Option option_none(void) { Option o; o.tag = 0; o.value = NULL; return o; }

static inline Result result_unwrap_check(Result r) {
    if (!r.tag) { fprintf(stderr, "parena: unwrap called on an Err result\n"); abort(); }
    return r;
}
static inline Option option_unwrap_check(Option o) {
    if (!o.tag) { fprintf(stderr, "parena: unwrap called on a None option\n"); abort(); }
    return o;
}

#define OK (result_ok(NULL))
#define ERR (result_err(NULL))

typedef struct {
    Arena *arena;
    void **items;
    size_t count;
    size_t capacity;
} Vec;

static inline Vec vec_new(Arena *dest) {
    Vec v;
    v.arena = dest;
    v.items = NULL;
    v.count = 0;
    v.capacity = 0;
    return v;
}

static inline void vec_push_(Vec *v, void *item) {
    if (v->count == v->capacity) {
        size_t new_cap = v->capacity == 0 ? 4 : v->capacity * 2;
        void **new_items = (void **)arena_alloc(v->arena, new_cap * sizeof(void *));
        for (size_t i = 0; i < v->count; i++) new_items[i] = v->items[i];
        v->items = new_items;
        v->capacity = new_cap;
    }
    v->items[v->count++] = item;
}

static inline void *vec_get(Vec *v, int idx) {
    if (idx < 0 || (size_t)idx >= v->count) return NULL;
    return v->items[idx];
}

static inline int vec_len(Vec *v) {
    return (int)v->count;
}

static inline void vec_set_at_(Vec *v, int idx, void *value) {
    if (idx < 0 || (size_t)idx >= v->count) return;
    v->items[idx] = value;
}

static inline void *vec_box_i32(Vec *v, int value) {
    int *cell = (int *)arena_alloc(v->arena, sizeof(int));
    *cell = value;
    return cell;
}
static inline void *vec_box_f64(Vec *v, double value) {
    double *cell = (double *)arena_alloc(v->arena, sizeof(double));
    *cell = value;
    return cell;
}

typedef struct {
    unsigned char *data;
    int len;
} Bytes;

static inline Bytes bytes_alloc_impl(Arena *dest, int len) {
    Bytes b;
    b.len = len > 0 ? len : 0;
    b.data = (unsigned char *)arena_alloc(dest, (size_t)(b.len > 0 ? b.len : 1));
    return b;
}

static inline int bytes_len_impl(Bytes b) {
    return b.len;
}

static inline int bytes_get_impl(Bytes b, int idx) {
    if (idx < 0 || idx >= b.len) return -1;
    return (int)b.data[idx];
}

static inline void bytes_set_impl(Bytes b, int idx, int value) {
    if (idx < 0 || idx >= b.len) return;
    b.data[idx] = (unsigned char)value;
}

static inline Bytes bytes_from_string_impl(Arena *dest, const char *s) {
    size_t len = strlen(s);
    Bytes b = bytes_alloc_impl(dest, (int)len);
    if (len > 0) memcpy(b.data, s, len);
    return b;
}

static inline char *bytes_to_string_lossy_impl(Bytes b, Arena *dest) {
    int n = 0;
    while (n < b.len && b.data[n] != 0) n++;
    char *out = (char *)arena_alloc(dest, (size_t)n + 1);
    if (n > 0) memcpy(out, b.data, (size_t)n);
    out[n] = '\0';
    return out;
}

#endif
