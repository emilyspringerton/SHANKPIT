/* aim_test.c -- the inverse aim math, checked against the forward formula it must agree with.
 * make test-aim. If these two ever disagree, a cast that aims at its target shoots somewhere else. */
#include "physics.h"
#include "third_person.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("aim: FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void roundtrip(float dx, float dy, float dz) {
    float yaw = 0.0f, pitch = 0.0f, fx = 0.0f, fy = 0.0f, fz = 0.0f, len;
    tp_yaw_pitch_to_point(0.0f, 0.0f, 0.0f, dx, dy, dz, &yaw, &pitch);
    tp_dir_from_yaw_pitch(yaw, pitch, &fx, &fy, &fz);
    len = sqrtf(dx * dx + dy * dy + dz * dz);
    CHECK(fabsf(fx - dx / len) < 1e-3f && fabsf(fy - dy / len) < 1e-3f && fabsf(fz - dz / len) < 1e-3f,
          "aim at (%.2f,%.2f,%.2f) gave yaw %.2f pitch %.2f -> (%.3f,%.3f,%.3f)",
          dx, dy, dz, yaw, pitch, fx, fy, fz);
}

int main(void) {
    float yaw = 0.0f, pitch = 0.0f;
    roundtrip(1.0f, 0.0f, 0.0f);
    roundtrip(0.0f, 0.0f, -1.0f);
    roundtrip(0.0f, 1.0f, 0.0f);
    roundtrip(0.0f, -1.0f, 0.0f);
    roundtrip(0.3f, 0.2f, -4.0f);
    roundtrip(-2.0f, 0.5f, 1.0f);
    roundtrip(0.0f, 0.0f, 12.5f);
    /* +X is yaw -90 in this convention (forward = -sin yaw). Pin it rather than trust the round trip,
       which would still pass if both sides flipped together. */
    tp_yaw_pitch_to_point(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, &yaw, &pitch);
    CHECK(fabsf(yaw + 90.0f) < 0.01f && fabsf(pitch) < 0.01f, "+X is yaw %.2f, not -90", yaw);
    tp_yaw_pitch_to_point(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, &yaw, &pitch);
    CHECK(fabsf(yaw) < 0.01f, "-Z is yaw %.2f, not 0", yaw);
    tp_yaw_pitch_to_point(5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, &yaw, &pitch);
    CHECK(yaw == 0.0f && pitch == 0.0f, "a degenerate aim did not fall back to 0/0");
    if (fails) { printf("aim: %d FAILED\n", fails); return 1; }
    printf("aim: all checks passed\n");
    return 0;
}
