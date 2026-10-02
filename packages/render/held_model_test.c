/* held_model_test.c -- headless checks for held_model.h (#447). make test-held-model */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "held_model.h"

static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

int main(void) {
    /* widget JSON as IDUNA serves it: handle along X, heavy head at the -X end (a hammer lying on its side) */
    const char *json =
        "{\"id\":4,\"name\":\"MODEL_HAMMER\",\"walls\":["
        "{\"id\":1,\"x\":0,\"y\":0,\"z\":0,\"sx\":2.0,\"sy\":0.1,\"sz\":0.1,\"r\":0.5,\"g\":0.3,\"b\":0.1,\"friction\":0.3},"
        "{\"id\":2,\"x\":-1.0,\"y\":0,\"z\":0,\"sx\":0.5,\"sy\":0.6,\"sz\":0.4,\"r\":0.7,\"g\":0.7,\"b\":0.7,\"friction\":0.3}"
        "],\"doors\":[]}";
    HeldBox in[HELD_MODEL_MAX];
    int n = held_model_parse_json(json, in, HELD_MODEL_MAX);
    assert(n == 2);
    assert(near(in[0].w, 2.0f) && near(in[1].x, -1.0f) && near(in[1].r, 0.7f));

    HeldModel m;
    assert(held_model_normalize(in, n, 1.6f, -0.45f, &m));
    assert(m.count == 2);
    /* handle (box 0): long axis -> Z, length 2.0 * (1.6/2.25) ; the head (box 1) must be at +Z of the handle */
    assert(m.box[1].z > m.box[0].z);                       /* heavy end forward even though it was at -X */
    float zmin = 1e9f, zmax = -1e9f;
    for (int i = 0; i < m.count; i++) { zmin = fminf(zmin, m.box[i].z - m.box[i].d * 0.5f); zmax = fmaxf(zmax, m.box[i].z + m.box[i].d * 0.5f); }
    assert(near(zmin, -0.45f));                            /* grip end at grip_z */
    assert(near(zmax - zmin, 1.6f));                       /* scaled to target_len */
    assert(near(m.box[0].x, 0.0f) && near(m.box[0].y, 0.0f)); /* centred on the other two axes */

    /* the same hammer standing up (Blender is Y-up): handle along Y, head at the TOP */
    const char *up =
        "{\"walls\":[{\"x\":0,\"y\":0,\"z\":0,\"sx\":0.1,\"sy\":2.0,\"sz\":0.1},"
        "{\"x\":0,\"y\":1.0,\"z\":0,\"sx\":0.5,\"sy\":0.4,\"sz\":0.6}]}";
    n = held_model_parse_json(up, in, HELD_MODEL_MAX);
    assert(n == 2);
    assert(held_model_normalize(in, n, 1.6f, -0.45f, &m));
    assert(m.box[1].z > m.box[0].z);                       /* head still forward */

    /* a model whose handle is already along +Z with the head forward is left in place (just scaled) */
    const char *fwd =
        "{\"walls\":[{\"x\":0,\"y\":0,\"z\":0,\"sx\":0.1,\"sy\":0.1,\"sz\":2.0},"
        "{\"x\":0,\"y\":0,\"z\":1.0,\"sx\":0.6,\"sy\":0.4,\"sz\":0.5}]}";
    n = held_model_parse_json(fwd, in, HELD_MODEL_MAX);
    assert(held_model_normalize(in, n, 1.6f, -0.45f, &m));
    assert(m.box[1].z > m.box[0].z);

    /* junk in, nothing out -- and never a crash */
    assert(held_model_parse_json("{}", in, HELD_MODEL_MAX) == 0);
    assert(held_model_parse_json("{\"walls\":[]}", in, HELD_MODEL_MAX) == 0);
    assert(held_model_parse_json("{\"walls\":[{\"x\":1}]}", in, HELD_MODEL_MAX) == 0);   /* missing extents */
    assert(!held_model_normalize(in, 0, 1.6f, 0.0f, &m));
    HeldBox flat = { 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0, 0 };
    assert(!held_model_normalize(&flat, 1, 1.6f, 0.0f, &m));

    /* more boxes than the cap are dropped, not overflowed */
    char big[8192]; int off = snprintf(big, sizeof(big), "{\"walls\":[");
    for (int i = 0; i < HELD_MODEL_MAX + 10; i++)
        off += snprintf(big + off, sizeof(big) - (size_t)off, "%s{\"x\":%d,\"y\":0,\"z\":0,\"sx\":1,\"sy\":1,\"sz\":1}", i ? "," : "", i);
    snprintf(big + off, sizeof(big) - (size_t)off, "]}");
    assert(held_model_parse_json(big, in, HELD_MODEL_MAX) == HELD_MODEL_MAX);

    printf("held_model_test OK\n");
    return 0;
}
