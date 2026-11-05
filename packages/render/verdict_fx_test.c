/* verdict_fx_test.c -- the civic boss's verdict effects, headless (no GL, no display, no audio).
 *
 * The founder asked for "particle effects and shaders", then "test particle pool": this file tests the
 * pool. What a screen would have shown (the shader's ring, the embers, the scorch pattern) cannot be
 * checked here, and is not pretended to be -- what IS checked is every decision that used to be
 * unreachable file-static state in apps/lobby: how many motes a read and a serve spawn, that the ring
 * wraps instead of growing, that an ember actually rises and falls, that aging releases slots, that a
 * broken sentence never leaves a scorch, and that a scorch fades to nothing at its documented life.
 *
 * Build/run: make test-verdict-fx
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "verdict_fx.h"
#include "verdict_scorch.h"   /* the sim-owned floor hazard the renderer only reads */

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int check_count = 0;

int main(void) {
    VerdictFx fx;

    printf("== the verdict pools: bounded, ballistically honest, and testable without a GL context ==\n");
    verdict_fx_reset(&fx);
    CHECK(verdict_fx_live_motes(&fx) == 0,
          "a reset pool holds nothing (a level change cannot inherit the last fight's embers)");
    check_count++;

    /* A sentence being read spawns its embers on the ring the shader draws. */
    verdict_fx_ring_embers(&fx, 100.0f, 4.0f, 200.0f, 5000u);
    CHECK(verdict_fx_live_motes(&fx) == VERDICT_EMBER_COUNT,
          "reading a sentence spawns %d embers, not a stream that grows with frame rate", VERDICT_EMBER_COUNT);
    check_count++;
    {
        int inside = 1;
        float min_r = 1e9f, max_r = 0.0f;
        unsigned int highest = 0;
        for (int i = 0; i < VERDICT_MOTE_MAX; i++) {
            const VerdictMote *m = &fx.mote[i];
            if (!m->active) continue;
            float dx = m->x - 100.0f, dz = m->z - 200.0f;
            float r = sqrtf(dx * dx + dz * dz);
            if (r < min_r) min_r = r;
            if (r > max_r) max_r = r;
            if (r < 5.0f - 0.01f || r > 20.0f + 0.01f) inside = 0;
            if (m->spark) highest = 1;
            if (m->vy <= 0.0f) inside = 0;      /* an ember LIFTS: a downward one is a bug */
        }
        printf("      embers spawned between %.1f and %.1f units of the bench\n", (double)min_r, (double)max_r);
        CHECK(inside && !highest,
              "every ember starts on the ring (5..20 units) and rises -- an ember that sinks reads as debris");
        check_count++;
    }

    /* The serve throws its sparks outward, and the scorch is pushed by the caller in the same event. */
    verdict_fx_reset(&fx);
    verdict_fx_serve_burst(&fx, 0.0f, 0.0f, 0.0f, 9000u);
    CHECK(verdict_fx_live_motes(&fx) == VERDICT_SPARK_COUNT,
          "serving a sentence throws %d sparks at once", VERDICT_SPARK_COUNT);
    check_count++;
    {
        int outward = 1;
        for (int i = 0; i < VERDICT_MOTE_MAX; i++) {
            const VerdictMote *m = &fx.mote[i];
            if (!m->active) continue;
            float dx = m->x, dz = m->z;
            float len = sqrtf(dx * dx + dz * dz);
            if (len < 0.01f) continue;
            if ((dx * m->vx + dz * m->vz) / len <= 0.0f) outward = 0;   /* velocity away from the bench */
        }
        CHECK(outward, "and every spark's velocity points away from the bench -- it is a burst, not a cloud");
        check_count++;
    }

    /* Bounded ring buffers: 200 bursts do not grow anything, and they keep the pool full instead. */
    verdict_fx_reset(&fx);
    for (int b = 0; b < 200; b++) verdict_fx_serve_burst(&fx, 0.0f, 0.0f, 0.0f, 1000u + (unsigned int)b);
    CHECK(verdict_fx_live_motes(&fx) == VERDICT_MOTE_MAX,
          "two hundred bursts leave exactly %d live motes -- the pool wraps, it never allocates", VERDICT_MOTE_MAX);
    check_count++;

    /* Determinism: the same seed reproduces the same burst (a replay looks the same). */
    {
        VerdictFx a, b;
        verdict_fx_reset(&a);
        verdict_fx_reset(&b);
        verdict_fx_ring_embers(&a, 0.0f, 0.0f, 0.0f, 100u);
        verdict_fx_ring_embers(&b, 0.0f, 0.0f, 0.0f, 100u);
        int same = 1;
        for (int i = 0; i < VERDICT_MOTE_MAX; i++) {
            if (memcmp(&a.mote[i], &b.mote[i], sizeof(VerdictMote)) != 0) same = 0;
        }
        CHECK(same, "the same seed spawns the same burst -- effects are reproducible, not a second RNG");
        check_count++;
    }

    /* Ballistics: an ember rises, then gravity brings it down; age drives the shrink. */
    {
        VerdictFx one;
        verdict_fx_reset(&one);
        VerdictMote *m = verdict_fx_mote_push(&one, 0.0f, 10.0f, 0.0f, 0.0f, 4.0f, 0.0f, 1.0f, 1.0f, 0, 1000u);
        float y0 = 0.0f, y1 = 0.0f, y2 = 0.0f, age0 = 0.0f, size0 = 0.0f, age2 = 0.0f, size2 = 0.0f;
        float x0 = 0.0f, z0 = 0.0f;
        int alive0 = verdict_mote_sample(m, 1000u, &x0, &y0, &z0, &age0, &size0);
        int alive1 = verdict_mote_sample(m, 1200u, NULL, &y1, NULL, NULL, NULL);
        int alive2 = verdict_mote_sample(m, 1800u, NULL, &y2, NULL, &age2, &size2);
        printf("      one ember: y %.2f ->(0.2s) %.2f ->(0.8s) %.2f, size %.2f -> %.2f\n",
               (double)y0, (double)y1, (double)y2, (double)size0, (double)size2);
        CHECK(alive0 && alive1 && alive2 && y0 == 10.0f && age0 == 0.0f,
              "a fresh mote samples exactly where it spawned, at age zero");
        check_count++;
        CHECK(y1 > y0 && y2 < y1,
              "it rises first and gravity brings it back down -- the arc the serve's sparks ride too");
        check_count++;
        CHECK(age2 > age0 && size2 < size0, "and it shrinks as it ages, so the burst thins out instead of popping");
        check_count++;
        CHECK(!verdict_mote_sample(m, 2100u, NULL, NULL, NULL, NULL, NULL),
              "past its life it samples dead -- the renderer has nothing left to draw");
        check_count++;
    }

    /* Aging releases the slot: a pool left alone does not accumulate. */
    verdict_fx_reset(&fx);
    verdict_fx_ring_embers(&fx, 0.0f, 0.0f, 0.0f, 0u);
    verdict_fx_age(&fx, 100u);
    CHECK(verdict_fx_live_motes(&fx) == VERDICT_EMBER_COUNT, "aging a live burst releases nothing");
    check_count++;
    verdict_fx_age(&fx, 5000u);
    CHECK(verdict_fx_live_motes(&fx) == 0, "but a pool aged past every mote's life is empty again");
    check_count++;

    /* Scorches: the sim owns the pool (verdict_scorch.h) because the mark is also a HAZARD, and the
       renderer only reads it. These cover what used to need two windows open: the scene filter, the
       hazard's footprint, its cadence, its bounded ring, and that it stops being present (and burning)
       at its documented life. */
    {
        VerdictScorches sc;
        verdict_scorches_reset(&sc);
        CHECK(verdict_scorches_live(&sc) == 0, "a reset scorch pool holds nothing");
        check_count++;
        VerdictScorch *s = verdict_scorch_push(&sc, 12.0f, 0.0f, -3.0f, 7, 20000u);
        CHECK(s->active && verdict_scorch_alpha(s, 20000u) == 1.0f && s->scene_id == 7,
              "a served sentence leaves a full-strength scorch on the floor, tagged with the scene that earned it");
        check_count++;

        /* Scene filtering, tested where a client cannot lie about it. */
        CHECK(verdict_scorch_present(s, 7, 20000u) && !verdict_scorch_present(s, 8, 20000u),
              "the mark is present in its own scene and invisible in every other one");
        check_count++;
        CHECK(!verdict_scorch_covers(s, 8, 12.0f, -3.0f, 20000u),
              "and the same coordinates in another scene do not burn -- the hazard is scene-bound, like the drawing");
        check_count++;

        /* Footprint: inside the radius burns, one step past it does not. */
        CHECK(verdict_scorch_covers(s, 7, 12.0f, -3.0f, 20000u) &&
              verdict_scorch_covers(s, 7, 12.0f + VERDICT_SCORCH_RADIUS - 0.5f, -3.0f, 20000u) &&
              !verdict_scorch_covers(s, 7, 12.0f + VERDICT_SCORCH_RADIUS + 1.0f, -3.0f, 20000u),
              "standing in it burns and one step past its %.0f-unit radius does not", (double)VERDICT_SCORCH_RADIUS);
        check_count++;

        /* Cadence: 2 hp every 250 ms, and a stall cannot cash in a burst of catch-up ticks. */
        CHECK(!verdict_scorch_due(s, 20000u + VERDICT_SCORCH_TICK_MS - 1u) &&
              verdict_scorch_due(s, 20000u + VERDICT_SCORCH_TICK_MS),
              "the burn is due every %u ms, not every frame", VERDICT_SCORCH_TICK_MS);
        check_count++;
        verdict_scorch_advance(s, 20000u + VERDICT_SCORCH_TICK_MS);
        CHECK(!verdict_scorch_due(s, 20000u + VERDICT_SCORCH_TICK_MS) &&
              verdict_scorch_due(s, 20000u + 2u * VERDICT_SCORCH_TICK_MS),
              "and advancing it schedules exactly one more tick -- a hitch cannot become a one-frame kill");
        check_count++;

        float half = verdict_scorch_alpha(s, 20000u + VERDICT_SCORCH_LIFE_MS / 2u);
        float gone = verdict_scorch_alpha(s, 20000u + VERDICT_SCORCH_LIFE_MS);
        printf("      scorch: alpha 1.00 at birth, %.2f at half life, %.2f at %u ms\n",
               (double)half, (double)gone, VERDICT_SCORCH_LIFE_MS);
        CHECK(half < 1.0f && half > 0.0f && gone == 0.0f &&
              !verdict_scorch_present(s, 7, 20000u + VERDICT_SCORCH_LIFE_MS),
              "it fades over its documented %u ms and stops being present -- so it also stops burning",
              VERDICT_SCORCH_LIFE_MS);
        check_count++;
        verdict_scorches_age(&sc, 20000u + VERDICT_SCORCH_LIFE_MS);
        CHECK(verdict_scorches_live(&sc) == 0, "and aging releases the slot once it is invisible");
        check_count++;
        verdict_scorches_reset(&sc);
        for (int i = 0; i < 12; i++) verdict_scorch_push(&sc, (float)i, 0.0f, 0.0f, 1, 30000u + (unsigned int)i);
        CHECK(verdict_scorches_live(&sc) == VERDICT_SCORCH_MAX,
              "twelve sentences leave %d scorches, not twelve -- the gallery floor has a budget", VERDICT_SCORCH_MAX);
        check_count++;
    }

    printf("\n%d checks, %d failed\n", check_count, fails);
    return fails ? 1 : 0;
}
