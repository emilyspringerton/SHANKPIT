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
| 0 | version (1) |
| 1 | record count (≤38) |
| 2–3 | tick |
| 4+ | records of 6 B: entity u16, kind u8, value u16, extra u8 |

38 records × 6 B + 4 B header = 232 bytes, inside the ceiling. Kinds: `MOVE` (x, y), `DAMAGE` (dealt,
hp left), `XP` (gained), `ITEM` (id, quantity).

## The tick

`lora_world_tick()` resolves one tick from an intent list, in list order (that order is part of the
contract). Moves apply on the grid; melee requires **Chebyshev adjacency** (the old games' "you must be
next to it"); the swing is a hit if `lora_roll(attacker, target, tick) < hit_permille`, where the roll is
an FNV-style hash — **no `rand()`, no clock**, so a replay grades identically and two peers can check
each other. A tick that would not fit in one frame returns **-1 and changes nothing**, so the caller can
split it rather than ship half a tick nobody can reconstruct.

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

1. **Loss** is the real problem, not size: does a client resend its intent until it sees its own entity
   in a delta, or does every delta carry a tick the client can re-request? (A replay window is the
   honest answer; it needs a second frame type.)
2. **Multiple frames per tick** when more than 19 actions resolve — the -1 return is the seam for it.
3. **Persistence** of the slow world (the camp host can sleep between ticks) — IDUNA already holds
   accounts; the world file is the missing piece.
