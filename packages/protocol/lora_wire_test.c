/* Headless checks for packages/protocol/lora_wire.h: the bytes, the bounds, the chunking, the ACKs and
   the determinism. No radio, no Meshtastic -- that layer does not exist yet and this test says so. */
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
        in.x = LORA_GRID_MAX;
        CHECK(lora_intent_encode(&in, buf) == 0, "an off-grid tile encoded");
        in.x = 0;
        in.action = LORA_ACT_COUNT;
        CHECK(lora_intent_encode(&in, buf) == 0, "an unknown action encoded");
        memset(buf, 0, sizeof buf);
        buf[4] = 9;
        CHECK(lora_intent_decode(buf, sizeof buf, &out) == 0, "decoder accepted action 9");
        buf[4] = LORA_ACT_MOVE;
        buf[6] = 64;
        CHECK(lora_intent_decode(buf, sizeof buf, &out) == 0, "decoder accepted x=64");
    }

    /* ---- 3. one delta frame: header, v2 chunk fields, and the 237-byte ceiling ---- */
    {   LoraDelta d;
        LoraDeltaView v;
        LoraDeltaRecord r;
        int len, i;
        lora_delta_begin(&d, 0x0102, 0, 1);
        CHECK(lora_delta_push(&d, 5, LORA_DELTA_MOVE, 9, 3) == 1, "first push failed");
        CHECK(lora_delta_push(&d, 5, LORA_DELTA_ACK, 42, 0) == 1, "ack push failed");
        CHECK(lora_delta_push(&d, 5, 0, 1, 1) == 0, "kind 0 was accepted");
        CHECK(lora_delta_push(&d, 5, LORA_DELTA_COUNT, 1, 1) == 0, "kind COUNT was accepted");
        len = lora_delta_finish(&d);
        CHECK(len == LORA_DELTA_HEADER_BYTES + 2 * LORA_DELTA_RECORD_BYTES, "len %d", len);
        CHECK(lora_delta_read(d.buf, len, &v) == 1, "a valid frame did not read");
        CHECK(v.count == 2 && v.tick == 0x0102 && v.chunk == 0 && v.chunks == 1,
              "header wrong: count %d tick %d chunk %d/%d", v.count, v.tick, v.chunk, v.chunks);
        CHECK(lora_delta_get(&v, 1, &r) == 1 && r.kind == LORA_DELTA_ACK && r.value == 42, "ack record wrong");
        CHECK(lora_delta_get(&v, 2, &r) == 0, "an out-of-range record read");
        CHECK(lora_delta_read(d.buf, LORA_DELTA_HEADER_BYTES + LORA_DELTA_RECORD_BYTES, &v) == 0,
              "a frame with one record missing was accepted");
        CHECK(lora_delta_read(d.buf, 3, &v) == 0, "a 3-byte frame was accepted");
        {   uint8_t bad[LORA_MAX_PAYLOAD];
            memcpy(bad, d.buf, (size_t)len);
            bad[0] = 1;                      /* v1 frame: this code is v2 only */
            CHECK(lora_delta_read(bad, len, &v) == 0, "a v1 frame was accepted as v2");
            bad[0] = LORA_DELTA_VERSION;
            bad[1] = (uint8_t)(LORA_MAX_DELTA_RECORDS + 1);
            CHECK(lora_delta_read(bad, len, &v) == 0, "an over-capacity count was accepted");
            bad[1] = 2;
            bad[4] = 3;                      /* chunk 3 of 1 */
            bad[5] = 1;
            CHECK(lora_delta_read(bad, len, &v) == 0, "chunk 3/1 was accepted");
        }
        lora_delta_begin(&d, 0, 0, 1);
        for (i = 0; i < LORA_MAX_DELTA_RECORDS; i++)
            CHECK(lora_delta_push(&d, (uint16_t)i, LORA_DELTA_ITEM, (uint16_t)i, 1) == 1, "push %d failed", i);
        CHECK(lora_delta_push(&d, 0, LORA_DELTA_ITEM, 0, 0) == 0, "record %d was accepted past the cap", LORA_MAX_DELTA_RECORDS);
        len = lora_delta_finish(&d);
        CHECK(len <= LORA_MAX_PAYLOAD, "a full frame is %d bytes, over %d", len, LORA_MAX_PAYLOAD);
    }

    /* ---- 4. a tick that spans frames: split, then reassemble ---- */
    {   LoraDeltaRecord rec[LORA_MAX_RECORDS];
        LoraDelta frames[LORA_MAX_FRAMES];
        LoraDeltaView v;
        LoraReassembly r;
        LoraDeltaRecord got;
        int i, n, f;
        for (i = 0; i < 80; i++) {
            rec[i].entity = (uint16_t)i;
            rec[i].kind = LORA_DELTA_ITEM;
            rec[i].value = (uint16_t)(i * 2);
            rec[i].extra = 1;
        }
        n = lora_frames_build(rec, 80, 7, frames, LORA_MAX_FRAMES);
        CHECK(n == 3, "80 records built %d frames, want 3", n);
        CHECK(frames[0].count == LORA_MAX_DELTA_RECORDS && frames[2].count == 80 - 2 * LORA_MAX_DELTA_RECORDS,
              "chunk counts %d/%d", frames[0].count, frames[2].count);
        for (f = 0; f < n; f++) {
            CHECK(frames[f].len <= LORA_MAX_PAYLOAD, "frame %d is %d bytes", f, frames[f].len);
            CHECK(frames[f].chunk == f && frames[f].chunks == 3, "frame %d says chunk %d/%d", f, frames[f].chunk, frames[f].chunks);
        }
        CHECK(lora_frames_build(rec, 80, 7, frames, 2) == -1, "three frames were not refused into two slots");
        /* nothing may be reassembled until the last chunk is in */
        lora_reassembly_begin(&r);
        CHECK(lora_reassembly_put(&r, frames[0].buf, frames[0].len) == 0, "one chunk completed a tick");
        CHECK(lora_reassembly_put(&r, frames[2].buf, frames[2].len) == 0, "two chunks completed a tick");
        CHECK(lora_reassembly_put(&r, frames[0].buf, frames[0].len) == -1, "a duplicate chunk was accepted");
        CHECK(lora_reassembly_put(&r, frames[1].buf, frames[1].len) == 1, "the last chunk did not complete the tick");
        CHECK(r.count == 80, "reassembled %d records, want 80", r.count);
        for (i = 0; i < 80; i++) {
            if (r.rec[i].entity != (uint16_t)i || r.rec[i].value != (uint16_t)(i * 2)) {
                CHECK(0, "reassembly reordered record %d", i);
                break;
            }
        }
        CHECK(lora_delta_read(frames[0].buf, frames[0].len, &v) == 1 && lora_delta_get(&v, 0, &got) == 1 &&
              got.entity == 0, "the first record of the frame is wrong");
        /* a foreign tick cannot join an in-progress reassembly */
        lora_reassembly_begin(&r);
        CHECK(lora_reassembly_put(&r, frames[0].buf, frames[0].len) == 0, "chunk 0 of tick 7 failed");
        CHECK(lora_reassembly_put(&r, frames[1].buf, frames[1].len) >= 0, "chunk 1 of tick 7 failed");
        {   LoraDelta other[LORA_MAX_FRAMES];
            CHECK(lora_frames_build(rec, 10, 8, other, LORA_MAX_FRAMES) == 1, "a small tick did not build");
            lora_reassembly_begin(&r);
            CHECK(lora_reassembly_put(&r, frames[0].buf, frames[0].len) == 0, "tick 7 chunk 0 failed");
            CHECK(lora_reassembly_put(&r, other[0].buf, other[0].len) == -1, "tick 8 joined tick 7's reassembly");
        }
    }

    /* ---- 5. the world: moves, ACKs, adjacency, deterministic dice ---- */
    {   LoraWorld w, w2;
        LoraIntent in[4];
        LoraDelta d, d2;
        LoraDeltaView v;
        LoraDeltaRecord r;
        int n, hits = 0, misses = 0, tick, i;
        memset(&w, 0, sizeof w);
        CHECK(lora_world_add(&w, 1, 5, 5, 30) == 1, "actor 1 not added");
        CHECK(lora_world_add(&w, 2, 6, 5, 30) == 1, "actor 2 not added");
        CHECK(lora_world_add(&w, 3, 64, 0, 30) == 0, "an off-grid actor was added");
        for (i = w.count; i < LORA_MAX_ACTORS; i++)
            CHECK(lora_world_add(&w, (uint16_t)(100 + i), (uint16_t)(i % LORA_GRID_MAX), 1, 10) == 1, "actor %d not added", i);
        CHECK(lora_world_add(&w, 999, 0, 0, 1) == 0, "actor %d was accepted past the cap", LORA_MAX_ACTORS);

        memset(in, 0, sizeof in);
        in[0].entity = 1;
        in[0].action = LORA_ACT_MOVE;
        in[0].seq = 77;
        in[0].x = 6; in[0].y = 6;
        n = lora_world_tick(&w, in, 1, 0, &d);
        CHECK(n == 2, "a move wrote %d records, want move+ack", n);
        CHECK(lora_world_find(&w, 1)->x == 6 && lora_world_find(&w, 1)->y == 6, "the actor did not move");
        CHECK(lora_delta_read(d.buf, d.len, &v) == 1 && v.count == 2, "move delta did not read");
        CHECK(lora_delta_get(&v, 0, &r) == 1 && r.kind == LORA_DELTA_MOVE && r.value == 6 && r.extra == 6, "move record wrong");
        CHECK(lora_delta_get(&v, 1, &r) == 1 && r.kind == LORA_DELTA_ACK && r.value == 77, "the ack did not echo seq 77");

        /* moving nowhere is still acknowledged, and is not a move */
        n = lora_world_tick(&w, in, 1, 0, &d);
        CHECK(n == 1, "a no-op move wrote %d records, want just the ack", n);
        CHECK(lora_delta_read(d.buf, d.len, &v) == 1 && lora_delta_get(&v, 0, &r) == 1 && r.kind == LORA_DELTA_ACK,
              "a no-op move was not acknowledged");

        /* an entity that is not here is not acknowledged */
        memset(in, 0, sizeof in);
        in[0].entity = 4000; in[0].action = LORA_ACT_IDLE; in[0].seq = 1;
        CHECK(lora_world_tick(&w, in, 1, 0, &d) == 0, "an absent entity produced a record");

        memset(in, 0, sizeof in);
        in[0].entity = 1; in[0].action = LORA_ACT_ATTACK; in[0].target = 2;
        for (tick = 0; tick < 64; tick++) {
            LoraActor *t = lora_world_find(&w, 2);
            t->hp = 30;
            n = lora_world_tick(&w, in, 1, 0, &d);
            CHECK(n == 1 || n == 3, "an attack wrote %d records", n);
            if (n == 3) {
                hits++;
                CHECK(lora_delta_read(d.buf, d.len, &v) == 1, "hit delta did not read");
                CHECK(lora_delta_get(&v, 0, &r) == 1 && r.kind == LORA_DELTA_DAMAGE && r.value == (uint16_t)lora_damage(0), "damage wrong");
                CHECK(lora_delta_get(&v, 1, &r) == 1 && r.kind == LORA_DELTA_XP && r.value == (uint16_t)lora_xp_hit(0), "xp wrong");
                CHECK(lora_delta_get(&v, 2, &r) == 1 && r.kind == LORA_DELTA_ACK, "the swing was not acknowledged");
            } else {
                misses++;
                CHECK(lora_delta_read(d.buf, d.len, &v) == 1 && lora_delta_get(&v, 0, &r) == 1 &&
                      r.kind == LORA_DELTA_ACK, "a miss was not acknowledged");
            }
        }
        CHECK(hits > 0 && misses > 0, "the dice never varied (hits %d, misses %d)", hits, misses);

        {   LoraWorld far;
            memset(&far, 0, sizeof far);
            lora_world_add(&far, 1, 0, 0, 30);
            lora_world_add(&far, 2, 2, 0, 30);
            memset(in, 0, sizeof in);
            in[0].entity = 1; in[0].action = LORA_ACT_ATTACK; in[0].target = 2;
            CHECK(lora_world_tick(&far, in, 1, 0, &d) == 1, "a two-tile attack did not resolve to just an ack");
            CHECK(lora_world_find(&far, 2)->hp == 30, "a two-tile attack dealt damage");
        }

        w2 = w;
        CHECK(lora_world_tick(&w, in, 1, 0, &d) >= 0, "tick failed on the original");
        CHECK(lora_world_tick(&w2, in, 1, 0, &d2) >= 0, "tick failed on the copy");
        CHECK(d.len == d2.len && memcmp(d.buf, d2.buf, (size_t)d.len) == 0, "two identical ticks produced different frames");
    }

    /* ---- 6. all-or-nothing: a tick too big for one frame changes nothing ---- */
    {   LoraWorld w, before;
        LoraIntent many[20];
        LoraDelta d, frames[LORA_MAX_FRAMES];
        int i, n;
        memset(&w, 0, sizeof w);
        lora_world_add(&w, 1, 3, 3, 30);
        memset(many, 0, sizeof many);
        for (i = 0; i < 20; i++) {
            many[i].entity = 1;
            many[i].action = LORA_ACT_MOVE;
            many[i].seq = (uint16_t)i;
            many[i].x = (uint16_t)(i + 1);
            many[i].y = 1;
        }
        before = w;
        CHECK(lora_world_tick(&w, many, 20, 0, &d) == -1, "a 40-record tick fit one frame");
        CHECK(memcmp(&w, &before, sizeof w) == 0, "a refused tick still moved the world");
        n = lora_world_tick_frames(&w, many, 20, 0, frames, LORA_MAX_FRAMES);
        CHECK(n == 2, "the same tick took %d frames, want 2", n);
        CHECK(w.tick == (uint16_t)(before.tick + 1), "the multi-frame tick did not advance the world once");
    }

    /* ---- 7. the PARENA table is the one the header was written against ---- */
    CHECK(lora_tick_ms(0) == 5000, "tick_ms(0) = %d", lora_tick_ms(0));
    CHECK(lora_tick_ms(1) == 12000, "tick_ms(1) = %d", lora_tick_ms(1));
    CHECK(lora_tick_ms(2) == 3000, "tick_ms(2) = %d", lora_tick_ms(2));
    CHECK(lora_tick_ms(99) == 5000, "an unknown preset did not fall back (%d)", lora_tick_ms(99));
    CHECK(lora_grid_w(0) == LORA_GRID_MAX && lora_grid_h(1) == LORA_GRID_MAX, "grid is not %d", LORA_GRID_MAX);
    CHECK(lora_hit_permille(0) == 600 && lora_hit_permille(2) == 550, "hit permille moved");
    CHECK(lora_damage(0) == 3 && lora_damage(1) == 4, "damage moved");
    CHECK(lora_xp_hit(0) == 10 && lora_xp_hit(1) == 12 && lora_xp_move(0) == 1, "xp moved");

    /* ---- 8. a snapshot: the whole world in frames, reassembled like any tick ---- */
    {   LoraWorld w, empty;
        LoraDelta frames[LORA_MAX_FRAMES];
        LoraReassembly r;
        int i, f, n;
        memset(&w, 0, sizeof w);
        for (i = 0; i < LORA_MAX_ACTORS; i++)
            CHECK(lora_world_add(&w, (uint16_t)(i + 1), (uint16_t)(i % LORA_GRID_MAX),
                                 (uint16_t)((i * 3) % LORA_GRID_MAX), (uint16_t)(10 + i)) == 1, "actor %d", i);
        n = lora_world_snapshot(&w, frames, LORA_MAX_FRAMES);
        CHECK(n == 2, "64 actors took %d snapshot frames, want 2", n);
        CHECK(frames[0].count == LORA_MAX_DELTA_RECORDS &&
              frames[1].count == LORA_MAX_ACTORS - LORA_MAX_DELTA_RECORDS,
              "snapshot counts %d/%d", frames[0].count, frames[1].count);
        lora_reassembly_begin(&r);
        for (f = 0; f < n; f++) CHECK(lora_reassembly_put(&r, frames[f].buf, frames[f].len) >= 0, "chunk %d", f);
        CHECK(r.count == LORA_MAX_ACTORS, "snapshot reassembled %d actors", r.count);
        for (i = 0; i < LORA_MAX_ACTORS; i++) {
            LoraActor *a = lora_world_find(&w, (uint16_t)(i + 1));
            if (r.rec[i].kind != LORA_DELTA_STATE || r.rec[i].entity != a->id ||
                LORA_STATE_X(r.rec[i].value) != a->x || LORA_STATE_Y(r.rec[i].value) != a->y ||
                r.rec[i].extra != (uint8_t)a->hp) {
                CHECK(0, "snapshot record %d does not match the world", i);
                break;
            }
        }
        CHECK(lora_world_snapshot(NULL, frames, LORA_MAX_FRAMES) == -1, "a null world snapshotted");
        memset(&empty, 0, sizeof empty);
        CHECK(lora_world_snapshot(&empty, frames, 1) == 1 && frames[0].count == 0,
              "an empty world did not produce one empty frame");
        CHECK(lora_world_snapshot(&empty, frames, 0) == -1, "a zero-slot snapshot was accepted");
    }

    /* ---- 9. the resend policy: wait, resend, then give up and say nothing more ---- */
    {   LoraClient c;
        LoraIntent in, out[LORA_CLIENT_PENDING];
        int i;
        uint32_t window = (uint32_t)lora_resend_ms(0);
        memset(&in, 0, sizeof in);
        in.entity = 7; in.seq = 100; in.action = LORA_ACT_MOVE; in.x = 1; in.y = 2;
        lora_client_begin(&c, 0);
        CHECK(lora_client_send(&c, &in, 1000) == 1, "the first intent was not queued");
        CHECK(lora_client_pending(&c) == 1, "pending %d", lora_client_pending(&c));
        CHECK(lora_client_due(&c, 1000 + window - 1, out, LORA_CLIENT_PENDING) == 0, "it resent before the window");
        CHECK(lora_client_due(&c, 1000 + window, out, LORA_CLIENT_PENDING) == 1 && out[0].seq == 100,
              "it did not resend after the window");
        CHECK(lora_client_ack(&c, 8, 100) == 0, "an ack for another entity cleared the intent");
        CHECK(lora_client_ack(&c, 7, 100) == 1, "the ack did not clear the intent");
        CHECK(lora_client_pending(&c) == 0, "an acked intent is still pending");
        CHECK(lora_client_due(&c, 100000, out, LORA_CLIENT_PENDING) == 0, "an acked intent was resent");

        lora_client_begin(&c, 0);
        CHECK(lora_client_send(&c, &in, 0) == 1, "queue failed");
        for (i = 1; i < lora_max_sends(0); i++)
            CHECK(lora_client_due(&c, (uint32_t)(i * 100000), out, LORA_CLIENT_PENDING) == 1,
                  "resend %d was not due", i);
        CHECK(lora_client_due(&c, 1000000, out, LORA_CLIENT_PENDING) == 0, "it resends past the budget");
        CHECK(lora_client_pending(&c) == 0, "an intent past the budget is still pending");

        lora_client_begin(&c, 0);
        for (i = 0; i < LORA_CLIENT_PENDING; i++) {
            in.seq = (uint16_t)i;
            CHECK(lora_client_send(&c, &in, 0) == 1, "queue slot %d refused", i);
        }
        CHECK(lora_client_send(&c, &in, 0) == 0, "a %dth outstanding intent was accepted", LORA_CLIENT_PENDING + 1);
        CHECK(lora_client_ack(&c, 7, 9999) == 0, "an unknown seq was acked");
        CHECK(lora_client_due(&c, 10, out, 0) == 0, "a zero-capacity due call emitted something");
    }

    /* ---- 10. a client can ask for the world, and the host will not be flooded ---- */
    {   LoraRequest rq, got, bad;
        uint8_t buf[LORA_REQUEST_BYTES];
        LoraWorld w;
        LoraDelta frames[LORA_MAX_FRAMES];
        LoraSnapshotPump pump;
        uint32_t gap = (uint32_t)lora_snapshot_interval_ms(0);
        int i;
        memset(&w, 0, sizeof w);
        for (i = 0; i < 40; i++)
            CHECK(lora_world_add(&w, (uint16_t)(i + 1), (uint16_t)(i % LORA_GRID_MAX),
                                 (uint16_t)(i % LORA_GRID_MAX), 20) == 1, "actor %d", i);
        rq.kind = LORA_REQ_SNAPSHOT; rq.entity = 9; rq.seq = 5;
        CHECK(lora_request_encode(&rq, buf) == LORA_REQUEST_BYTES, "a request did not encode");
        CHECK(buf[0] == LORA_REQUEST_VERSION, "first byte %d is not the request marker", buf[0]);
        CHECK(lora_request_decode(buf, LORA_REQUEST_BYTES, &got) == 1, "a request did not decode");
        CHECK(got.kind == LORA_REQ_SNAPSHOT && got.entity == 9 && got.seq == 5, "request round trip wrong");
        CHECK(lora_request_decode(buf, LORA_REQUEST_BYTES - 1, &got) == 0, "a 5-byte request decoded");
        buf[0] = (uint8_t)LORA_DELTA_VERSION;    /* a delta frame is not a request */
        CHECK(lora_request_decode(buf, LORA_REQUEST_BYTES, &got) == 0, "a delta frame decoded as a request");
        buf[0] = (uint8_t)LORA_REQUEST_VERSION; buf[1] = 7;
        CHECK(lora_request_decode(buf, LORA_REQUEST_BYTES, &got) == 0, "an unknown request kind decoded");

        lora_snapshot_pump_begin(&pump);
        CHECK(lora_host_request(&rq, &w, 0, 5000, &pump, frames, LORA_MAX_FRAMES) == 2,
              "the first request did not produce 2 frames for 40 actors");
        CHECK(lora_host_request(&rq, &w, 0, 5000 + gap - 1, &pump, frames, LORA_MAX_FRAMES) == 0,
              "a second request was served inside the budget");
        CHECK(lora_host_request(&rq, &w, 0, 5000 + gap, &pump, frames, LORA_MAX_FRAMES) == 2,
              "the budget never expired");
        bad = rq;
        bad.kind = LORA_REQ_COUNT;
        CHECK(lora_host_request(&bad, &w, 0, 900000, &pump, frames, LORA_MAX_FRAMES) == -1,
              "an unknown request kind was served");
        CHECK(lora_host_request(NULL, &w, 0, 0, &pump, frames, LORA_MAX_FRAMES) == -1, "a null request was served");
        CHECK(lora_host_request(&rq, NULL, 0, 0, &pump, frames, LORA_MAX_FRAMES) == -1, "a null world was served");
        CHECK(lora_host_request(&rq, &w, 0, 900000, &pump, frames, 0) == -1, "a zero-slot request was served");
        CHECK(lora_snapshot_interval_ms(0) == 30000 && lora_snapshot_interval_ms(1) == 60000 &&
              lora_snapshot_interval_ms(2) == 15000, "the snapshot budget is not the PARENA table's");
    }

    if (fails) { printf("lora: %d FAILED\n", fails); return 1; }
    printf("lora: all checks passed\n");
    return 0;
}
