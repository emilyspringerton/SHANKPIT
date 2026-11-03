# Salvage & Component Destruction — NORTHSTAR

Founder real-time (2026-11-03, via a pasted design conversation): a garage that accepts "vehicles of
questionable origin" and hooks them to a mission; cars converted down to components "a bit variable
like wow disenchanting"; then destructible vehicles that shear components off mid-crash; all of it
PARENA-powered, in a game that is itself a sandbox for mapmakers and modmakers.

This document is the answer, and it starts with an audit rather than a design, because the paste that
prompted it described several systems as already existing that do not, and one that does as unwired
that is wired. **Everything in §1 was checked in this tree**, not inferred from the paste.

---

## 1. What actually exists (checked 2026-11-03)

| Claim in the paste | Reality |
|---|---|
| A car has one hull pool | **True.** `BuggyState.health` (`packages/common/protocol.h`); the rocket car is a `BuggyState` with `rocket = 1`, not a separate type. Helicopter/drone/plane are their own states. |
| Destruction ladder on the hull | **Real, and better than described.** `packages/simulation/vehicle_burn.h` + `PARENA/stdlib/shankpit/vehicle_burn_rules.prn` → `vehicle_burn_rules.c`: hull 300, smoke ≤150, fire ≤75, 300 burning ticks (5 s) to detonation, crash damage `mass × Δv²` staged to stay inside i32 (`vehicle_burn_impact_damage`). The fire is a function of the hull, not a latch — a repaired car goes out. |
| Structural load cascade | **Real.** `packages/simulation/structure.h`: joints at load (`struct_joint_strength`), support flood, load cascade over **`STRUCT_MAX_NODES 2048`** pieces and 3 rounds. The paste's "≤ 512 nodes" is wrong; there is no 512 node anything. |
| Cell-level destruction | **Real.** `packages/world/brick_fracture.h` (`bf_detect_detached`), `brick_world_structure_tick`, `PACKET_BRICK_STATE` (13). |
| Rigid shards on XPBD | **Real.** `packages/simulation/brick_bodies.{h,c}` on GOLDEN BAND `grb`, plus `packages/common/obb.h`. Shards are **per-client cosmetic and not synced** — that is the existing, deliberate contract. |
| Ragdoll is a standalone utility, not in gameplay | **False / outdated.** `packages/simulation/ragdoll_pool.{h,c}` is the live pool and `apps/lobby/src/main.c:9582` calls `ragdoll_pool_spawn` on death. Do not "wire the ragdoll in" — it is in. |
| Widgets + EduVM scripting | **Real.** `LevelEduWidget` (`packages/world/level_boxes.h`), `edu_bindings.{h,c}`, `widget_circuit.h`, `widget_runtime.h`, `PARENA/stdlib/shankpit/widget_rules.prn`; the agent-facing contract is `docs/WIDGET_AFFORDANCES.md`. Power is one integer 0..15 with **no back-EMF/overload**. |
| Live editing / long-poll sync | **Real.** `packages/world/edit_map.h`, `packages/world/edit_sync.h`, `tools/nock_editor.html`, IDUNA's NOCK handlers. |
| Materials + shaders | **Real, but narrow.** `PARENA/stdlib/shankpit/material_rules.prn` (5 material kinds, 8 Quake light styles, tints) and `brick_rules.prn` (weapon × material). The renderer is fixed-function GL 1.x plus **one hand-loaded GL 2.0 path** (`packages/render/gl_shader.{h,c}`) — see `docs/RENDER_NORTHSTAR.md`. |
| `salvage_rules.prn`, `vehicle_destruction.h`, `VehicleSubComponent`, `PACKET_VEHICLE_SHATTER`, `PACKET_COLLAB_BUILD`, `PACKET_LOOT_DROP` | **Do not exist.** No per-component hull of any kind exists today. Highest packet id in `protocol.h` is `PACKET_KIT_CALL` (19). |
| Ragdoll "17-capsule" | Real: 17 segments, `rigid_ragdoll.{h,c}` — but the pool, not the raw builder, is what gameplay uses. |
| Missions/quests | A `packages/simulation/mission.h` + `docs/STORY_SYSTEM_NORTHSTAR.md` story layer exist; nothing there is vehicle-aware. |

**So the real gap is narrow and specific:** a car is one `int health`. There is no notion of *which*
part of it broke, nothing to shear off, and no way for a vehicle to carry provenance ("questionable
origin"). That is the whole first slice.

---

## 2. Slice 1 — components as data, not meshes

A vehicle gets a fixed, small component table in PARENA, not per-polygon geometry:

```
PARENA/stdlib/shankpit/vehicle_salvage_rules.prn
  -> packages/simulation/vehicle_salvage_rules.c   (scripts/gen_vehicle_salvage_rules.sh, mk/salvage.mk)
```

Five components for every ground vehicle, index 0..4, in the same spirit `dslash` brick kinds already
use: `ENGINE`, `CHASSIS`, `WHEELS`, `LOGIC` (the overclockable board), `TANK` (rocket car only — 0..) .
Each has, as **palena integers only**:

- `hp_permille_of_hull(kind, component)` — how much of the 300-point hull that part represents.
- `component_material(kind, component)` — maps onto `BF_KIND_*` so shards use the material behaviour
  that already exists rather than a parallel table.
- `component_offset_centi(kind, component, axis)` — local offset, integer centi-units.
- `component_effect_permille(kind, component)` — what losing it does (drive force, turn rate, top
  speed), expressed as permille multipliers the host multiplies into the **existing** `PhysBuggyRules`
  values. This is why `buggy_rules_host.h` is the right seam: a missing engine is a smaller
  `buggy_drive_force_permille`, not a second physics path.

No floats, no `sin`, no time-dependent state — the same discipline `material_rules.prn` states and
`vehicle_burn_rules.prn` already follows. A headless test can therefore hand-derive every number.

## 3. Slice 2 — fracture, then ejection

`packages/simulation/vehicle_salvage.h` (pure, no GL/SDL/net, like `vehicle_burn.h`):

1. `vehicle_salvage_apply(SalvageVehicle *v, int impact_damage, int damage_type)` splits the incoming
   damage across live components in authoring order using the permille table. **The hull total is
   conserved** — `sum(component_hp) == health` always, which is the one invariant the test must assert
   after every permutation, or the car and its parts disagree.
2. A component that reaches 0 is marked `detached` and its world position is computed
   `vehicle position + rotated local offset` — the paste's formula is right.
3. The **host** (server tick) spawns a shard via the existing `brick_bodies`/`grb` path and a loot
   entry via the existing ground-item pattern; the rules layer never touches entities.
4. Losing `ENGINE`/`WHEELS` re-multiplies the vehicle's handling rules, so a limping car is slower for
   a reason that is visible on the ground behind it.

Deliberately **not** modelled: bending of the chassis, a real damage localisation solve against the
actual mesh, and component-vs-component collisions. Same honesty `structure.h` already writes down.

## 4. Slice 3 — the variable roll ("disenchanting")

Rolls must be **deterministic from identity, never from wall-clock**, or two clients watching the same
wreck disagree and an MMO ledger forks. Use the hash already in `vehicle_burn.h`
(`vehicle_burn_hash`) over `(entity id, tick, component index)`.

Yield table in the same `.prn`: `salvage_tier_permille(component, roll) -> 0..3`
(`SCRAP_STEEL` / `REBAR_ALLOY` / `COMPOSITE_MESH` / `OVERCLOCKED_LOGIC`), with **quantity scaled by the
component's remaining hp** — a part destroyed by a rocket yields less than one you carefully unbolted.
That is the actual WoW-DE shape: rarity is a table, quantity is a function of condition.

**Do not invent an inventory.** `packages/common/kit.h` is explicit that items are *inputs this engine
already acts on*, not a parallel item universe, and `food_pickup.h` shows the ground-item precedent
(auto-collect on walk-over, client-local until a server-authoritative story exists). A first cut should
map salvage yield onto **existing** consumables/ammo/kit items. A real salvage economy is a separate,
larger decision that has not been made.

## 5. Slice 4 — the garage, wired to a mission

- **Trigger:** a `PLATE` widget in a garage bay (`docs/WIDGET_AFFORDANCES.md` §3) — the host writes it
  from the body actually standing on it, so a script cannot forge "a car is here".
- **Provenance:** keys off a new per-vehicle field (`origin_tag`, integer: `CIVILIAN` / `STOLEN` /
  `MILITARY`), set at spawn from the level's `vehicle_spawn` entry or by the theft/impound code. This
  field does not exist and is the real prerequisite for "questionable origin".
- **Arbitration:** a `garage_quest.prn` module reads the plate register + provenance + `mission.h`'s
  active objective and calls back into the mission layer. Dialogue/quest *state* stays in `mission.h`;
  PARENA decides only "does this car satisfy this objective", so modmakers can rewrite the condition
  without touching C.
- **Reload:** a modmaker edits the `.prn`, `make regen-vehicle-salvage-rules`, rebuild — one binary, no
  scripting hot-swap exists (see §7).

## 6. Word of warning on three "obvious next steps" from the paste

1. **`PACKET_COLLAB_BUILD` is the wrong packet to write.** Multi-user geometry edits already go through
   `edit_sync.h` + `edit_map.h` ops, and `PACKET_BRICK_STATE` already carries idempotent absolute
   damage. A second build-event path would be two ledgers for one world.
2. **Do not sync shard trajectories.** `brick_bodies` is deliberately per-client cosmetic; the wire
   carries *state plus a seed*, and clients re-simulate. A `PACKET_VEHICLE_SHATTER` full of velocities
   would be a bandwidth regression AND a divergence risk.
3. **In-game GLSL authoring is not a small slice.** There is one hand-loaded GL 2.0 program; there is no
   runtime shader compiler path in this build. Material *rules* are the PARENA seam — the honest
   affordance is authoring rules and EduVM scripts, not live GLSL.

## 7. Wire (Slice 5)

`PACKET_VEHICLE_STATE` at type **20** (19 is `PACKET_KIT_CALL`), server → client, following
`PACKET_BRICK_STATE`'s contract exactly: absolute and idempotent, `(vehicle id, live-component bitmask,
per-component hp quantized, seed)`, repeated 3×, no acks. A late joiner heals on the next packet; a
client that receives it for a car it does not have ignores it. Loot entries can ride the same packet as
`(component, tier, quantity)` — there is no need for a separate `PACKET_LOOT_DROP` until drops become
authoritative entities.

## 8. Phases

| Phase | Deliverable | Verification |
|---|---|---|
| S1 | `vehicle_salvage_rules.prn`, gen script, `mk/salvage.mk` (`regen-`/`check-`), component table + `vehicle_salvage.h` | `make regen-vehicle-salvage-rules`, `make check-vehicle-salvage-rules`, hand-derived integer cases |
| S2 | Conserved-damage split, effects folded into `PhysBuggyRules`, shard spawn via `brick_bodies` | `make test-vehicle-salvage` (ASan+UBSan, same pattern as `mk/brick.mk`) |
| S3 | Seeded yield table + ground-item mapping | test asserts the same seed yields the same table twice, and hull-conservation holds after every hit |
| S4 | `origin_tag`, garage `PLATE`, `garage_quest.prn` | `make test-edu-widgets` extension + a level JSON round-trip (`level_save_test` pattern) |
| S5 | `PACKET_VEHICLE_STATE` (20) + client mirror | e2e like `brick_e2e.py`: real server, real client, late joiner converges |

## 9. Named gaps this does not close

- No power *grid*: widget power is 0..15 with no watts, no dependency graph, no back-EMF. A "digital
  twin" power plant needs a value type and an overload model first.
- No semantic tags for agents (`is_cover`, `is_choke`) — nothing to build a strategic builder on.
- No in-game material/shader authoring, no nav-debug overlay.
- No visual wire-drawing editor for widgets; pins are edited in JSON.
- No salvage economy decision (see §4).

---

## 10. Status — S1 is built (2026-11-03)

Slice 1 exists and is green, with **no gameplay wiring yet** (deliberately: a car cannot be parted out
until the split is proven against hand-derived numbers).

**PARENA** — `stdlib/shankpit/vehicle_salvage_rules.prn`: `salvage-hull` (300, asserted equal to
`vehicle_burn_rules.prn`'s own `vehicle-hull`), `salvage-component-count`, `salvage-component-hp`
(buggy 90/120/60/30, rocket 80/100/50/30/40 — both exactly 300), `salvage-component-material`
(BF_KIND values), `salvage-component-offset-centi`, `salvage-handling-permille`, `salvage-tier`,
`salvage-quantity`.

**SHANKPIT** — `packages/simulation/vehicle_salvage.h` (state, hull-conserving split, handling stack,
seeded roll), `vehicle_salvage_rules.c` (generated, checked in), `vehicle_salvage_rules_host.h`,
`vehicle_salvage_test.c`, `scripts/gen_vehicle_salvage_rules.sh`, `mk/salvage.mk`.

What the test actually asserts: parts sum to the hull; a 120-point hit lands 36/48/24/12; a part comes
off once and never absorbs damage again; **400 random hits conserve the hull exactly** (`before -
damage`, floored at 0); the handling stack is the product of live parts (engine off = 300 drive, and
engine+wheels = 240); the logic board changes no handling; the tier table is hand-checked at every
boundary; and the same car/tick/part rolls identically while still spreading across ticks.

Verified: `make check-vehicle-salvage-rules` (regen is byte-identical), `make test-vehicle-salvage`
clean at `-Wall -Wextra -Werror` and again under ASan+UBSan.

### S2 — the parts own the car (2026-11-03, built)

**The split runs on the live car.** `vehicle_salvage_sync(v, hull, &fresh)` is the reconciliation the
authority calls once per tick: the hull is not this layer's private property — a shot, a repair and
`vehicle_burn.h`'s 0.25/tick drain all move it — so the real hull goes in and the parts are made to
agree in **both directions**. Down is the proportional split. Up refills only what is still attached,
so a repaired wreck is capped at the hull it can still hold and cannot grow a second engine. It returns
the hull the car can represent, and `buggy_tick_all` writes that back: conservation is an invariant on
every tick, not only while the number falls.

**A proportional split does not single out a part**, and the test now says so: a 240-point hit on a full
buggy wears all four parts to 18/24/12/6 and detaches none. A part comes off when the hit is more than
the car can absorb — which is what a wreck is.

**Effects folded into `PhysBuggyRules`.** `physics.h`'s `phys_set_buggy_salvage_permille` sits beside
`g_phys_buggy_rules`, not inside it (the rules are one mod for every car; a missing engine is per-car
state), and multiplies top/reverse speed, drive force, turn rate, steering authority and lateral grip.
Exact passthrough at 1000 keeps an intact car and every existing physics test bit-identical.

**Shards via `brick_bodies`.** `vehicle_salvage_shards.h` turns one detached part into one GOLDEN BAND
body with the component's `BF_KIND_*` material, a nominal mass and a seeded tumble. `local_game.h` owns
the hook, so the rules layer still never touches an entity: the lobby binds it to `g_brick_bodies`, the
dedicated server leaves it NULL (it discards broken-brick bodies on purpose). A car first seen already
damaged adopts its parts without throwing shards.

**Not done, named:** component state is **not on the wire**, so a networked client derives parts from
the replicated hull and can show a part off a tick before the server agrees (S5); `apps/lobby` cannot be
compiled on this box, so its three binding lines are reviewed, not compiled; no loot entity yet, no
`origin_tag`, no garage. Those are S3-S5.

---

Written 2026-11-03. Every file path in §1 was checked in this tree; every "does not exist" was
grepped, not assumed.
