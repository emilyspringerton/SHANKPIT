# Shared procedural zones (SHANKPIT ARCHIPELAGO, phase 1)

The founder's shape, in their own words: *"like diablo where we can travel to a map and do it as a
dungeon it can be procedurally generated ... the first person to enter it it generates and then everyone
else enters the same one ... a grace period of 15 minutes ... dungeons are not instanced they are
generated and then they exist until everyone leaves + 15 minutes."*

This is that, built and tested, plus the GFD map import. It is **not** the whole GFD merge — see
`docs/GFD_UNIFICATION.md` for what is still a phase.

## The three pieces

| File | Owns |
|---|---|
| `PARENA/stdlib/shankpit/zone_rules.prn` → `packages/simulation/zone_rules.c` | every number: 900-second grace, 8 live zones, rooms/sizes/walls/doors/spawners/materials/difficulty, and the expiry predicate |
| `packages/simulation/zone_registry.h` | the shared-zone table and its clock — enter / leave / tick, no I/O, no allocation |
| `packages/simulation/proc_dungeon.h` | the layout: one seed → one `CustomLevelData`, saved as real level JSON |

`packages/simulation/zone_rules.h` carries numerically identical C defaults so tests and any binary can
compile without the generated file; `make test-proc-dungeon` asserts the two copies agree (over 100
samples), the same contract `widget_rules.h` and `slide_rules.h` already keep.

## Determinism is the feature

The generator uses only integer arithmetic from the rules plus an LCG seed, and every coordinate is an
add or a multiply of small integers — no `rand()`, no float trig, no time. So:

- the same seed produces **byte-identical level JSON** (the test saves two and `strcmp`s them),
- a co-op client given the seed rebuilds the same dungeon,
- a farmed-out dungeon that expires comes back **different**, because expiry clears the slot and the next
  entrant gets a new seed.

## Lifecycle, exactly

```
ENTER  first entrant  -> creates the slot, gets the seed, host generates and saves the zone
       later entrants -> join the SAME slot and seed (one layout, not one per player)
       re-entry while the countdown runs -> cancels the countdown, keeps the layout
LEAVE  drops occupancy; at zero the zone goes IDLE and starts counting. It is not deleted.
TICK   expires a zone only when empty for >= 900 s; the host callback deletes that zone's file and the
       slot is cleared, so the next entrant generates from a new seed.
```

An occupied zone never expires, however old it is — that is an invariant in `zone_registry_tick`, not a
special case.

## Shape of a generated dungeon

A hub plus 2–4 cardinal rooms joined by corridors, all axis-aligned, enclosed, with a ground plane. The
hub carries a gate that is authored as a **real circuit**, not a special door type:

```
proc_gate_lever (6 LEVER, reg 40) -> proc_gate_relay (7 DELAY, 45 ticks) -> proc_gate_motor (1 MACHINE, box_index = the gate)
```

So the gate opens 45 ticks after the lever, through the same `widget_runtime.h` path a lab door uses, and
the lever is the familiar `[F] USE` prompt.

## Reaching it

- In game (local), **F7** descends: it creates/joins `dungeon_N` and loads it through the normal
  custom-level loader (so lighting, destructible brick, the widget circuit and zombies population all
  apply).
- `./bin/shank_lobby --dungeon [zone]` starts straight into a generated zone.
- `./bin/shank_server --dungeon [zone]` **hosts** one: the server owns the zone and the clock, broadcasts
  `PACKET_ZONE_STATE` (id, seed, players, state, idle_ms) at 1 Hz, and a client that receives it rebuilds
  the identical layout locally and reloads only when the id or the seed changes. No geometry crosses the
  wire — the packet carries the identity and both sides run the same deterministic generator.

On the server, occupancy is the number of connected players: the first one in generates the zone, the
last one out starts the fifteen-minute countdown, and a later arrival after expiry regenerates from a new
seed (which is what makes the client reload).

## GFD maps

`scripts/import_gfd_levels.py` reads a GoblinFoxDragon checkout and writes `var/maps/gfd_town.json`:

- **Town** is real 3D content: 17 named buildings in `packages/world/town_map.c` plus the generated
  expansion blocks, at `TOWN_WORLD_SCALE 3.0`. Each becomes a solid box, with a perimeter wall, materials
  and spawners.
- **Meadow** is *not* a level: `data/crystal_seed_meadow.json` is a 44×44 goblin **simulation** seed
  (tick, goblins, kinds, grid coordinates) with no wall, floor or building in it. The script says so
  rather than inventing geometry.

## Not built yet (named, not implied)

- **No single-command co-op host.** `--dungeon` works with the server started separately: there is no
  `--coop-host` that starts the server in-process and advertises it on the LAN yet. That is phase 3 in
  `docs/GFD_UNIFICATION.md`.
- **No explicit "enter zone" request.** On the server, occupancy is presence, so the zone is generated when
  the first client connects rather than when they walk through a door. A portal/`/enter` trigger is a
  follow-up; the registry already supports the lifecycle it would call.
- **No menu tile.** F7 and `--dungeon` are the reachable paths; the archipelago browser is UI work.
- **No GFD state server.** Job/stat/inventory progression is a separate phase (see the unification doc);
  the dungeon carries a `difficulty` tier for it to consume later.
- **No instanced dungeons.** By design: that is the founder's explicit choice, and the phase-1 behaviour
  above is what "not instanced" means in practice.
