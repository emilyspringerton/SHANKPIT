/* Headless checks for packages/protocol/lora_wire.h: the bytes, the bounds, and the determinism.
   No radio, no Meshtastic -- that layer does not exist yet and this test does not pretend it does. */
#include <stdio.h>
#include <string.h>

#include "lora_wire.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void) {
    /* ---- 1. every action survives a 12-byte round trip ---- */
    {   uint8_t buf[LORA_INTENT_BYTES];
        LoraIntent in, out;
        int a;
        for (a = 0; a < LORA_ACT_COUNT; a++) {
            memset(&in, 0, sizeof in);
            in.seq = (uint16_t)(0x1234 + a);
            in.entity = 0xBEEF;
            in.action = (uint8_t)a;
            in.arg = 7;
            in.x = 63;
            in.y = 0;
            in.target = 0xFFFF;
            CHECK(lora_intent_encode(&in, buf) == LORA_INTENT_BYTES, "action %d did not encode", a);
            CHECK(lora_intent_decode(buf, LORA_INTENT_BYTES, &out) == 1, "action %d did not decode", a);
            CHECK(out.seq == in.seq && out.entity == in.entity && out.action == in.action &&
                  out.arg == in.arg && out.x == in.x && out.y == in.y && out.target == in.target,
                  "action %d did not round-trip", a);
        }
        CHECK(lora_intent_encode(NULL, buf) == 0, "a null intent encoded");
        CHECK(lora_intent_decode(buf, LORA_INTENT_BYTES - 1, &out) == 0, "an 11-byte frame decoded");
    }

    /* ---- 2. untrusted input is REJECTED, not clamped ---- */
    {   uint8_t buf[LORA_INTENT_BYTES];
        LoraIntent in, out;
        memset(&in, 0, sizeof in);
        in.action = LORA_ACT_MOVE;
        in.x = LORA_GRID_MAX;                 /* one past the grid */
        CHECK(lora_intent_encode(&in, buf) == 0, "an off-grid tile encoded");
        in.x = 0;
        in.action = LORA_ACT_COUNT;           /* unknown action */
        CHECK(lora_intent_encode(&in, buf) == 0, "an unknown action encoded");
        /* ...and a hostile frame straight off the air is refused by the decoder */
        memset(buf, 0, sizeof buf);
        buf[4] = 9;
        CHECK(lora_intent_decode(buf, sizeof buf, &out) == 0, "decoder accepted action 9");
        buf[4] = LORA_ACT_MOVE;
        buf[6] = 64;
        CHECK(lora_intent_decode(buf, sizeof buf, &out) == 0, "decoder accepted x=64");
    }

    /* ---- 3. a delta frame: header, records, and the 237-byte ceiling ---- */
    {   LoraDelta d;
        LoraDeltaView v;
        LoraDeltaRecord r;
        int len, i;
        lora_delta_begin(&d, 0x0102);
        CHECK(lora_delta_push(&d, 5, LORA_DELTA_MOVE, 9, 3) == 1, "first push failed");
        CHECK(lora_delta_push(&d, 5, LORA_DELTA_DAMAGE, 4, 200) == 1, "second push failed");
        CHECK(lora_delta_push(&d, 5, 0, 1, 1) == 0, "kind 0 was accepted");
        CHECK(lora_delta_push(&d, 5, LORA_DELTA_COUNT, 1, 1) == 0, "kind COUNT was accepted");
        len = lora_delta_finish(&d);
        CHECK(len == LORA_DELTA_HEADER_BYTES + 2 * LORA_DELTA_RECORD_BYTES, "len %d", len);
        CHECK(d.buf[1] == 2, "count byte %d", d.buf[1]);
        CHECK(lora_delta_read(d.buf, len, &v) == 1, "a valid frame did not read");
        CHECK(v.count == 2 && v.tick == 0x0102, "count %d tick %d", v.count, v.tick);
        CHECK(lora_delta_get(&v, 0, &r) == 1 && r.kind == LORA_DELTA_MOVE && r.value == 9 && r.extra == 3,
              "record 0 wrong");
        CHECK(lora_delta_get(&v, 1, &r) == 1 && r.kind == LORA_DELTA_DAMAGE && r.value == 4, "record 1 wrong");
        CHECK(lora_delta_get(&v, 2, &r) == 0, "an out-of-range record read");
        CHECK(lora_delta_get(&v, -1, &r) == 0, "a negative record read");
        /* truncated and hostile frames */
        CHECK(lora_delta_read(d.buf, LORA_DELTA_HEADER_BYTES + LORA_DELTA_RECORD_BYTES, &v) == 0,
              "a frame with one record missing was accepted");
        CHECK(lora_delta_read(d.buf, 3, &v) == 0, "a 3-byte frame was accepted");
        {   uint8_t bad[LORA_MAX_PAYLOAD];
            memcpy(bad, d.buf, (size_t)len);
            bad[0] = 2;
            CHECK(lora_delta_read(bad, len, &v) == 0, "version 2 was accepted");
            bad[0] = LORA_DELTA_VERSION;
            bad[1] = (uint8_t)(LORA_MAX_DELTA_RECORDS + 1);
            CHECK(lora_delta_read(bad, len, &v) == 0, "an over-capacity count was accepted");
        }
        /* the frame fills to exactly the ceiling and no further */
        lora_delta_begin(&d, 0);
        for (i = 0; i < LORA_MAX_DELTA_RECORDS; i++)
            CHECK(lora_delta_push(&d, (uint16_t)i, LORA_DELTA_ITEM, (uint16_t)i, 1) == 1, "push %d failed", i);
        CHECK(lora_delta_push(&d, 0, LORA_DELTA_ITEM, 0, 0) == 0, "record %d was accepted past the cap", LORA_MAX_DELTA_RECORDS);
        len = lora_delta_finish(&d);
        CHECK(len <= LORA_MAX_PAYLOAD, "a full frame is %d bytes, over the %d-byte payload", len, LORA_MAX_PAYLOAD);
        CHECK(len == LORA_MAX_PAYLOAD - (LORA_MAX_PAYLOAD - LORA_DELTA_HEADER_BYTES) % LORA_DELTA_RECORD_BYTES, "len %d", len);
    }

    /* ---- 4. the tick: moves, adjacency, and deterministic dice ---- */
    {   LoraWorld w, w2;
        LoraIntent in[4];
        LoraDelta d, d2;
        LoraDeltaView v;
        LoraDeltaRecord r;
        int n, hits = 0, misses = 0, tick;
        memset(&w, 0, sizeof w);
        CHECK(lora_world_add(&w, 1, 5, 5, 30) == 1, "actor 1 not added");
        CHECK(lora_world_add(&w, 2, 6, 5, 30) == 1, "actor 2 not added");
        CHECK(lora_world_add(&w, 3, 64, 0, 30) == 0, "an off-grid actor was added");

        memset(in, 0, sizeof in);
        in[0].entity = 1;
        in[0].action = LORA_ACT_MOVE;
        in[0].x = 6; in[0].y = 6;
        n = lora_world_tick(&w, in, 1, 0, &d);
        CHECK(n == 1, "move tick wrote %d records", n);
        CHECK(lora_world_find(&w, 1)->x == 6 && lora_world_find(&w, 1)->y == 6, "the actor did not move");
        CHECK(lora_delta_read(d.buf, d.len, &v) == 1 && v.count == 1, "move delta did not read");
        CHECK(lora_delta_get(&v, 0, &r) == 1 && r.kind == LORA_DELTA_MOVE && r.value == 6 && r.extra == 6,
              "move delta wrong (%d %d %d)", r.kind, r.value, r.extra);

        /* moving nowhere is not an event */
        n = lora_world_tick(&w, in, 0, 0, &d);
        CHECK(n == 0, "an empty intent list wrote %d records", n);

        /* attack from adjacent: either damage+xp or nothing, never anything else */
        memset(in, 0, sizeof in);
        in[0].entity = 1; in[0].action = LORA_ACT_ATTACK; in[0].target = 2;
        for (tick = 0; tick < 64; tick++) {
            LoraActor *t = lora_world_find(&w, 2);
            t->hp = 30;
            n = lora_world_tick(&w, in, 1, 0, &d);
            CHECK(n == 0 || n == 2, "an attack wrote %d records", n);
            if (n == 2) {
                hits++;
                CHECK(lora_delta_read(d.buf, d.len, &v) == 1, "hit delta did not read");
                CHECK(lora_delta_get(&v, 0, &r) == 1 && r.kind == LORA_DELTA_DAMAGE &&
                      r.value == (uint16_t)lora_damage(0), "damage record wrong");
                CHECK(lora_delta_get(&v, 1, &r) == 1 && r.kind == LORA_DELTA_XP &&
                      r.value == (uint16_t)lora_xp_hit(0), "xp record wrong");
            } else {
                misses++;
            }
        }
        CHECK(hits > 0 && misses > 0, "the dice never varied (hits %d, misses %d)", hits, misses);

        /* out of reach: two tiles away is not melee */
        {   LoraWorld far;
            memset(&far, 0, sizeof far);
            lora_world_add(&far, 1, 0, 0, 30);
            lora_world_add(&far, 2, 2, 0, 30);
            CHECK(lora_world_tick(&far, in, 1, 0, &d) == 0, "a two-tile attack resolved");
        }

        /* determinism: identical world + identical intents -> identical bytes */
        w2 = w;
        w2.tick = w.tick;
        CHECK(lora_world_tick(&w, in, 1, 0, &d) >= 0, "tick failed on the original");
        CHECK(lora_world_tick(&w2, in, 1, 0, &d2) >= 0, "tick failed on the copy");
        CHECK(d.len == d2.len && memcmp(d.buf, d2.buf, (size_t)d.len) == 0, "two identical ticks produced different frames");
    }

    /* ---- 5. all-or-nothing: a tick too big for one frame changes nothing ---- */
    {   LoraWorld w, before;
        LoraIntent many[20];
        LoraDelta d;
        int i;
        memset(&w, 0, sizeof w);
        lora_world_add(&w, 1, 3, 3, 30);
        before = w;
        memset(many, 0, sizeof many);
        for (i = 0; i < 20; i++) {
            many[i].entity = 1;
            many[i].action = LORA_ACT_MOVE;
            many[i].x = (uint16_t)(i % LORA_GRID_MAX);
            many[i].y = 1;
        }
        CHECK(lora_world_tick(&w, many, 20, 0, &d) == -1, "a 20-intent tick was accepted into one frame");
        CHECK(memcmp(&w, &before, sizeof w) == 0, "a refused tick still moved the world");
    }

    /* ---- 6. the PARENA table is the one the header was written against ---- */
    CHECK(lora_tick_ms(0) == 5000, "tick_ms(0) = %d", lora_tick_ms(0));
    CHECK(lora_tick_ms(1) == 12000, "tick_ms(1) = %d", lora_tick_ms(1));
    CHECK(lora_tick_ms(2) == 3000, "tick_ms(2) = %d", lora_tick_ms(2));
    CHECK(lora_tick_ms(99) == 5000, "an unknown preset did not fall back (%d)", lora_tick_ms(99));
    CHECK(lora_grid_w(0) == LORA_GRID_MAX && lora_grid_h(1) == LORA_GRID_MAX, "grid is not %d", LORA_GRID_MAX);
    CHECK(lora_hit_permille(0) == 600 && lora_hit_permille(2) == 550, "hit permille moved");
    CHECK(lora_damage(0) == 3 && lora_damage(1) == 4, "damage moved");
    CHECK(lora_xp_hit(0) == 10 && lora_xp_hit(1) == 12 && lora_xp_move(0) == 1, "xp moved");

    if (fails) { printf("lora: %d FAILED\n", fails); return 1; }
    printf("lora: all checks passed\n");
    return 0;
}
