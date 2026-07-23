# Bedrock Racers — SHANKPIT Game Mode Spec

*Written: 2026-07-22 | Status: northstar*

---

## What This Is

Bedrock Racers is a new competitive mode: F1-tier realistic vehicle handling (tire grip curves,
speed-scaled downforce, slip-angle drift, ABS lockup risk) with a LoL-style item/ultimate layer
riding on top of it. It reuses two dormant physics modules that already exist in
`server/system/` — `vehicle_dynamics.go`/`vehicle_physics.go` (the chassis) — which were fully
unit-tested but never wired into a live game mode.

This spec revises the handling philosophy laid out in `WEAKNIGHT_VS0_ACCEPTANCE_CRITERIA.md`.
That doc treats "feels like Mario Kart" as an explicit fail signal and calls for pure F1-realism.
Bedrock Racers keeps the F1-realistic chassis as load-bearing — accessible at low speed, twitchy
and unforgiving at high speed — but adds pickup items and one ultimate ability as a scoring/combat
layer on top of that physics, rather than simplifying the handling itself. The driving feel does
not change; what a driver can *do* with a well-driven lap does.

Unlike BedWars (Milestone 7), Bedrock Racers does **not** depend on the Dragonfly backend —
it runs entirely on `StaticBackend` with hardcoded track geometry, the same way Stadium and
Garage do today. It has no prerequisite chain blocking it.

**Out of scope for this vertical slice** (deferred to the full WEAKNIGHT VS0 vision or a later
pass): a second vehicle class, destructible track terrain, build macros, boids/NPC traffic, trade
routes, power-grid cascades, self-healing infrastructure, evolving factions, and a real navigable
menu system (this pass extends the existing keybind-legend menu, it does not replace it).

---

## Core Loop

```
QUEUE
  → Player presses R at the main menu → requests GameModeRacing
  → Matchmaker enqueues until racingMinPlayers (2) reached
  → All queued players moved to SCENE_RACE_TRACK, vehicle state initialized at the start line

RACE
  → Drive: W/S/A/D → throttle/brake/steer, fed through StepVehicle() each server tick
  → Cross checkpoints in order (8 checkpoints); completing all in order advances the lap
  → 3 laps to finish
  → Pick up items on track (Boost / Shield / Trap); hold one at a time
  → Q consumes the held item; ultimate charge fills passively (e.g. per checkpoint); F spends it
    at 100 charge (Overdrive)

FINISH
  → First player to complete lap 3 through the final checkpoint wins
  → Scene resets for the next queued race
```

---

## Server Systems Required

### 1. `server/system/racing_mode.go` (new)

Mirrors the existing `stadium_mode.go` scaffold pattern exactly:
- `RacingModeConfig{Checkpoints []Vec3, CheckpointRadius float64, LapsToWin int, VehicleCfg VehicleConfig, Tire TireGripCurve, Aero AeroModel, Brakes BrakeModel}` — static parameters, no behavior.
- `RacerProgress{ClientID uint8, Vehicle VehicleState, NextCheckpoint int, Lap int, ItemSlot uint8, UltimateCharge int}` — mutable per-racer runtime state.
- `RacingState{Racers map[uint8]RacerProgress}` — aggregate.
- Pure detection: `DetectCheckpoint(progress, cfg) (int, bool)`, `DetectLapComplete(progress, cfg) bool`.
- Value-receiver reducers: `WithCheckpoint`, `WithLapComplete`, `WithItemPickup`, `WithUltimateCharge` (clamped 0-100), `WithUltimateConsumed`.
- Item/ultimate effects as pure functions over `VehicleInput`/`VehicleConfig`: `ApplyBoost`, `ApplyShield`, `ApplyTrap`, `ApplyUltimate`.

### 2. Vehicle Physics Wiring (existing code, currently dormant)

`server/system/vehicle_dynamics.go` + `vehicle_physics.go` — `StepVehicle()` is called once per
server tick per racer, driven by `Fwd`/`Str` from the existing `UserCmd` (no new input axes
needed). This is the first live caller of this physics module.

### 3. Matchmaker + Scene Routing

`apps2/server-go/main.go`'s `matchmaker` gets a `racingQueue []string` field mirroring the
existing `ctfQueue` pattern. `PacketConnect` handling routes `GameModeRacing` requests through it;
once `racingMinPlayers` (2) are queued, all move to `SCENE_RACE_TRACK` scene ID 8.

### 4. Item/Ultimate Roster (v0 working defaults)

| Item | Effect |
|------|--------|
| Boost | Short throttle/impulse burst |
| Shield | Nullifies the next Trap hit |
| Trap | Dropped behind the racer; caps the next vehicle that hits it to reduced throttle for a few seconds |

| Ultimate | Effect |
|----------|--------|
| Overdrive | Longer/stronger Boost, plus brief immunity to Trap. Charges passively (e.g. one charge tick per checkpoint crossed); usable once at 100. |

Names/numeric tuning (durations, charge rate, throttle caps) are placeholders for this pass —
easy to retune without protocol changes since they're pure functions over existing fields.

### 5. Wire Protocol

New packet type `PACKET_RACING_STATE = 10` (chosen to avoid colliding with `PacketBedEvent=8` /
`PacketResourcePickup=9`, already claimed — but not yet implemented — by `BEDWARS_SPEC.md`).
Carries a `RacingTelemetry` struct per racer (client_id, lap, checkpoint_idx, item_slot,
ultimate_charge, speed), sent only to clients in `SCENE_RACE_TRACK`. Deliberately does **not**
extend the existing `PacketSnapshot` payload — see Known Gaps.

New scene `SCENE_RACE_TRACK = 8` (next free scene ID after `SCENE_STORY_CAVE=7`).

Mode routing: the client sends the raw byte value of Go's `GameModeRacing` constant (4) as the
requested-mode byte on connect — this is **not** added to the v1 `GameMode` C enum (which already
uses 0-4 and 98-107 for unrelated local/bot-mode semantics); the wire contract for
"requestedMode sent to a Go server" is anchored in `packages2/common/protocol.go`'s small enum,
not the larger legacy v1 enum, to avoid a numeric collision with `MODE_ODDBALL=4`.

`BTN_ABILITY_1` (bit 32, already defined in `packages/common/protocol.h`, currently unused by any
live code) is reused for item-use. A new bit is added for ultimate-use.

### 6. Track Geometry

`packages/common/physics.h` gets a new `map_geo_racetrack[]` static box array (hand-authored,
same literal style as `map_geo_stadium`) plus a parallel `checkpoint_triggers[]` array (8 entries,
order matching `RacingModeConfig.Checkpoints`). No new map file format is introduced — there
isn't one anywhere in this repo today, and building one is out of scope for this pass.

---

## Client Changes Required

### Menu

One new line in the existing keybind-legend block: `R: RACE (BEDROCK RACERS)`. One new
`SDLK_r` branch mirroring the existing mode-entry branches.

### HUD

`draw_hud()` gains, only when `scene_id == SCENE_RACE_TRACK`:
- Lap counter (`LAP X/3`) and checkpoint index text
- Item-slot indicator (reuse `draw_circle`, colored by held item)
- Ultimate-charge bar (reuse the existing two-`glRectf` health/shield bar pattern)

### Input

Reuses existing `W/S/A/D` → `Fwd`/`Str` as throttle/steer (no new movement keys). New: `Q` = use
held item, `F` = use ultimate (only fires once charge is 100).

---

## What "Done" Looks Like

**Bedrock Racers v0 (this pass):**
- One F1 vehicle, driven with real grip/drift/downforce/lockup feel (not floaty)
- One hand-authored track, 8 checkpoints, 3 laps
- 3 items + 1 ultimate, pickup → use → effect loop working end-to-end
- Matchmaker queues 2+ players into the race scene over the network
- HUD shows live lap/checkpoint/item/ultimate state
- `go test ./...` green, including previously-dormant vehicle/racing tests

**Deferred to a later pass:** second vehicle, destructible track, real navigable menu, emergent
systems from the full WEAKNIGHT VS0 vision.

---

## Known Gaps (pre-existing, discovered during this work, not fixed here)

1. **`PacketSnapshot`/`NetPlayer` size mismatch.** `broadcastSnapshots()` in `apps2/server-go/main.go`
   sends a fixed 18-byte-per-peer payload; the C client's `net_process_snapshot()` decodes a much
   larger `NetPlayer` struct. This predates Bedrock Racers and is unrelated to it — racing
   telemetry ships as its own packet type specifically to avoid building on this. Worth its own
   fix as separate work.
2. **CTF-over-network never actually enqueues.** `net_connect()` in `apps2/lobby/src/main.c`
   sends only `sizeof(NetHeader)` bytes on `PACKET_CONNECT` — no mode byte — so
   `requestedMode` is never read server-side (`main.go` only reads it `if n > netHeaderSize`).
   CTF today only works via the local-bot path (`C` key → `local_init_match`), never over a real
   network connection. Fixing `net_connect()` to send the mode byte (required for Racing
   matchmaking to work at all) incidentally fixes this for CTF too, as a side effect of this work.
3. **`apps2/lobby/src/main.c` does not compile, independent of this work.** It has no Makefile
   target anywhere in the repo, and a direct compile attempt (after installing the missing
   `libglu1-mesa-dev` system dependency) surfaces ~49 pre-existing errors having nothing to do
   with Bedrock Racers: it `#include`s v1's `packages/common/protocol.h` but calls v2-only types
   (`NetVoxelPacket`, `VoxelBlock`, `NetImpactPacket`) that only exist in `packages2/common/
   protocol.h`; `SHANKPIT_NET_FIXED_DT` is referenced but never defined; numerous `RONIN_*`
   render constants used in `draw_ronin_shell`/`draw_storm_mask` are undeclared; and
   `local_update()` is called with fewer arguments than its `local_game.h` signature requires.
   `render_voxel.c`'s own header (`render_voxel.h`) also depends on `NetVoxelPacket`, and
   `render_voxel.c` includes a `shim_gles.h` that doesn't exist anywhere in the repo. This is
   consistent with `apps2/lobby`, `server/system/vehicle_dynamics.go`, and `server/system/
   stadium_mode.go` all landing in the same single "yolo" commit as speculative, never-build-
   verified scaffolding. **Verified:** every compile error traces to pre-existing code untouched
   by this work — none of the Bedrock Racers additions (`net_connect_mode`, `net_process_racing_
   state`, the `R` keybind, `draw_hud`'s racing block, `client_create_cmd`'s new parameters)
   introduce a new error. Making `apps2/lobby` buildable at all is real, separate, pre-existing
   work — needed before Bedrock Racers can be end-to-end play-tested on the real client.

---

## Prerequisite Chain

```
None — runs on StaticBackend like Stadium/Garage. No Dragonfly dependency.
```

---

## Related Specs

- `docs2/NORTHSTAR.md` — Milestone 8 owns Bedrock Racers
- `docs2/specs/WEAKNIGHT_VS0_ACCEPTANCE_CRITERIA.md` — the larger F1/emergent-systems vision this
  slice draws its handling philosophy from (and narrows down from)
- `docs2/specs/BEDWARS_SPEC.md` — sibling new-mode spec; source of the `PacketBedEvent`/
  `PacketResourcePickup` type numbers this spec avoided colliding with
