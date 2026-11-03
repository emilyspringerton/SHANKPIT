# LoRa RPG — a RuneScape-Classic-shaped game over a 237-byte frame

Founder (2026-11-03): *"how do we evolve the engine for a low bandwidth runescape classic style rpg
that can run over meshtastic"*. This is the first slice of the answer that is actual code: the **wire**
and the **tick**. Not the radio, not the client.

## The constraint, stated once

Meshtastic carries roughly **237 bytes** of payload per frame, with airtime measured in **seconds** and
loss that is normal rather than exceptional. The arena shooter's model (60 Hz position replication over
UDP) does not degrade into that; it stops existing. So:

- clients send **intents** (12 bytes, once per action, not per frame),
- the host resolves them on a fixed grid **once per tick**,
- every client receives a **delta** of what changed, in one frame.

No state streaming, no assets over the radio, no wall-clock anywhere.

## The wire

`packages/protocol/lora_wire.h` — hand-packed little-endian bytes, never C bitfields (`unsigned x : 4`
is implementation-defined, so gcc and msvc would disagree on the air).

| Intent (12 B) | field |
| --- | --- |
| 0–1 | sequence (duplicate/out-of-order filtering) |
| 2–3 | entity |
| 4 | action (idle, move, attack, interact, say) |
| 5 | arg |
| 6–7 | target x |
| 8–9 | target y |
| 10–11 | target entity (0 = none) |

The design paste's 100-bit table included a "checksum/salt" — pointless, because LoRa/Meshtastic
already carries a CRC and retransmission; a second checksum buys a wasted byte. Coordinates are 16-bit
but bounded to a 64×64 world, and an out-of-grid tile is **refused by the decoder**, not clamped.

| Delta (≤237 B) | field |
| --- | --- |
| 0 | version (2) |
| 1 | record count in THIS frame (≤38) |
| 2–3 | tick |
| 4 | chunk index (0-based) |
| 5 | chunk count — a tick may span frames |
| 6+ | records of 6 B: entity u16, kind u8, value u16, extra u8 |

38 records × 6 B + 6 B header = 234 bytes, inside the ceiling. Kinds: `MOVE` (x, y), `DAMAGE` (dealt,
hp left), `XP` (gained), `ITEM` (id, quantity), **`ACK`** (value = the intent's sequence number).

The ACK is the answer to "did my click land?": every **accepted** intent — including a swing that
missed, or a move to the tile you are already on — comes back with its sequence number, so a client
resends only what was never acknowledged and needs no timer of its own. A receiver that is missing a
chunk stays incomplete; a duplicate, a foreign tick or a contradictory chunk count is a hard reject,
because half a tick is a client that silently diverges.

**Snapshot** records (kind `STATE`) pack x and y into one uint16 and hp into `extra`, so a full state
frame is ONE record per actor: 64 actors is 2 frames, reassembled by the same chunk code as a tick, and it
does not advance the tick because it resolves nothing. That is the answer for a client that lost
everything, not just one chunk.

**Requests** are their own 8-byte frame (first byte **3**, a delta's is **2**, so a receiver tells them
apart before parsing), carrying kind, the asking entity, the asker's sequence number and a tick. Two
kinds: `SNAPSHOT` — "give me the world", expensive and **budgeted per client** (`lora-snapshot-interval-ms`:
30 s at LONG_FAST, 60 s at LONG_SLOW, 15 s at SHORT_TURBO), answered `0` for "wait"; and `TICK` —
"resend tick N", answered straight out of an 8-tick history ring as one frame, and deliberately **not**
charged against the snapshot budget. The per-client table holds **8** clients and never evicts: a ninth
distinct asker is refused rather than resetting somebody else's clock, because eviction would let a
rotating client spend everyone's budget. The total snapshot rate is therefore bounded by 8 per interval
— one every 3.75 s worst case at LONG_FAST — which is stated here rather than assumed harmless.

## The tick

`lora_world_tick()` resolves one tick from an intent list, in list order (that order is part of the
contract). Moves apply on the grid; melee requires **Chebyshev adjacency** (the old games' "you must be
next to it"); the swing is a hit if `lora_roll(attacker, target, tick) < hit_permille`, where the roll is
an FNV-style hash — **no `rand()`, no clock**, so a replay grades identically and two peers can check
each other. Resolving happens into a **copy** of the world that is committed only once the frames exist, so a tick
that cannot be sent changes nothing. `lora_world_tick()` is the one-frame path and returns **-1** when
the tick needs more; `lora_world_tick_frames()` is the general path and splits into chunks. With 64
actors and three records per intent, a full tick is 192 records = 6 frames of 38.

## The numbers are PARENA's

`PARENA/stdlib/shankpit/lora_rules.prn` → `packages/protocol/lora_rules.c`
(`scripts/gen_lora_rules.sh`, `make regen-/check-lora-rules`). Total I32 functions over the Meshtastic
modem preset:

| preset | tick | hit ‰ | damage | xp/hit |
| --- | --- | --- | --- | --- |
| 0 LONG_FAST (default) | 5000 ms | 600 | 3 | 10 |
| 1 LONG_SLOW | 12000 ms | 600 | 4 | 12 |
| 2 SHORT_TURBO | 3000 ms | 550 | 3 | 10 |
| unknown | 5000 ms | 600 | 3 | 10 |

These are design choices, not measurements — no radio has been in the loop. The grid is 64×64 on every
preset: the radio changes the **pace**, never the world.

## What exists, and what does not

Built and tested headless: intent round-trip for every action, refusal of short/unknown/off-grid input,
the delta header and record layout, the 38-record ceiling and the exact 232-byte filled frame, truncated
and hostile frames refused, move/no-op/miss/hit resolution, adjacency, determinism (same world + same
intents → identical bytes), all-or-nothing on an oversized tick, and parity between the generated PARENA
table and the header's expectations. Run: `make test-lora-wire` (plain + ASan/UBSan) and
`make check-lora-rules`.

**Not built, named:** the Meshtastic transport (serial/BLE, portnum registration, retransmission and the
replay window a lossy link needs), client-side A* pathing and interpolation, asset bundling for a client
that is not the arena shooter, any UI, and any radio hardware in the loop. The paste's `meshtastic`
Python daemon and `ForwardToWotanMatchmaker` do not exist in this repo and were not invented here.

## Open questions for the next slice

1. **Loss**: policy, requests and catch-up exist -- `lora_client_send/ack/due` with a retry budget,
   `SNAPSHOT` budgeted **per client** in an 8-slot table, and `TICK` resends from an 8-tick ring. What is
   NOT built: a **global airtime cap** on top of the per-client budget (the worst case is 8 snapshots per
   interval), a ranged "everything since tick N" request (the client walks the ring a tick at a time),
   and nothing persists between host restarts.
2. **Multiple frames per tick** — done (v2 header, chunk reassembly); the remaining question is what a
   receiver does when chunk 2 of 3 never arrives: re-request by tick, or wait for the next full state.
3. **Persistence** of the slow world (the camp host can sleep between ticks) — IDUNA already holds
   accounts; the world file is the missing piece.
