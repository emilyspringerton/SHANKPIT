# SHANKPIT ARCHIPELAGO — NORTHSTAR (scoping pass, SP-ARCH-ENG-004)

**Status:** V0 water physics built (`PARENA/stdlib/shankpit/ocean_rules.prn`, `packages/simulation/ocean.h`,
`make test-ocean`). Everything else in the pasted blueprint is scoped here, not built.

Founder paste, 2026-10-30: a Destiny-inspired archipelago battle royale on the SHANKPIT engine — client-side
"squatter" SQL ledger, unified UDP streams, heightfield ocean with vector buoyancy, fractal destruction, island
streaming.

Per the standing rule (docs first, then implement), this document is the honest audit: **what already exists in this
repo, what the blueprint gets wrong about it, what the V0 is, and what is deferred by name.** Three of the five
sections turned out to describe things this monorepo has already built under different names; one section is
genuinely new and large; one is a client-architecture decision that does not belong in the engine at all.

---

## 1. Audit: what already exists (checked, not assumed)

| Blueprint claim | Reality in this monorepo |
|---|---|
| §5 "stress propagation lines, load paths from anchors to foundations" | **Built last week, cell-and-piece level.** `packages/simulation/structure.h` + `PARENA/stdlib/shankpit/structure_rules.prn`: intersection joints (anchor/peg), a support flood to ground, and a top-down load cascade with compression limits and per-piece strength that degrades as material is shot away. Violently demolishing a pillar drops what it carried. So §5's *simulation* is not the gap. |
| §5 "kinetic energy propagation → node detachment" | **Half built.** `bf_damage_sphere`/`bf_hit_surface` already convert impact into cell damage, and `bf_detach_parent` drops a whole piece as one or more rigid chunks (`grb` XPBD bodies). Missing: impact *routed through the structure graph* (a hull collision should stress the joint, not just the touched cell). |
| §2 "append-only ledger, batched async writes" | **Exists, one repo over.** `IDUNA/event-stream` is a real append-only event log with sequence IDs, consumed asynchronously; `IDUNA/internal/shankpit` already stores levels/registries. A *client-side* SQLite mirror is a client decision, not an engine one — see §5 below. |
| §3 "unreliable stream for transforms, reliable for events" | **Half built.** `PACKET_SNAPSHOT` is fire-and-forget over UDP; brick damage is resent 3× plus a rotating refresh so a lost datagram converges (`brick_world_net_collect`). That is "reliable-ish for events" without a real RUDP layer, which is genuinely absent. |
| §3 "spatial sectors, delta serialization" | **Not built.** Snapshot is one whole-world packet. Fine at a duel's scale, wrong for an archipelago. |
| §4 heightfield water, Gerstner spectrum, hull probes, buoyancy, planing, storm | **Nothing existed.** Not a line of water code anywhere in SHANKPIT before this pass (`grep` for buoyancy/wave/water_level: zero hits). This is the real V0. |

Two things in the blueprint are **not** true of this engine and are corrected here rather than adopted:

- **"Multi-threaded simulation loop" as the primary physics target.** SHANKPIT's simulation is a single deterministic
  loop per tick. Moving rigid bodies to async worker threads breaks the property every netcode decision here depends
  on (same inputs → same outputs). Debris bodies are client-cosmetic and already off the authoritative path; that is
  the only place a thread is safe. The structure cascade is bounded (3 rounds, ≤2048 nodes) and runs on load and on
  breach, not per tick.
- **"Procedurally pre-fractured into hierarchical structural nodes"** as a load-time bake. NOCK levels are authored
  boxes; the existing fracture already bisects cells on demand and — the part that matters — `structure.h` infers the
  *joints* at load from real geometry (flush faces weld, overlaps anchor into concrete). That is the same intent
  without inventing a second, invisible authoring format.

---

## 2. The V0 built in this pass: water you can feel, with numbers in PARENA

`PARENA/stdlib/shankpit/ocean_rules.prn` → `packages/simulation/ocean.c`
(`scripts/gen_ocean_rules.sh`), and `packages/simulation/ocean.h` as the host:

- **Sea state, not a switch.** `ocean-amplitude-mm(sea)` and `ocean-wind-mps(sea)` give five real states from flat
  calm to tempest; every other number derives from them, so "a storm rolled in" is one integer changing.
- **A layered wave field**, three cosine swells at different wavelengths, directions and phase speeds, evaluated at
  `(x, z, t)`. Not Gerstner: no horizontal particle displacement, so it will not make a breaking crest or a barrel.
  It is a heightfield you can probe, which is what buoyancy actually needs — saying "Gerstner" here would be a lie
  about a formula.
- **Vector buoyancy per hull probe.** A vessel carries N probes; each asks the field for a height, gets a depth, and
  contributes lift proportional to submerged depth and its own area, plus drag that grows with speed. Planing lift
  rises with speed and saturates. The host integrates vertical velocity and pitch, so a boat in a storm pitches and
  slams instead of sliding along a flat plane — that behaviour is asserted (`make test-ocean`).
- **Normals by finite difference** in the host, not the rules: it is sampling, not a decision.

What the test pins (all headless, no GL):

- calm water is flat to the millimetre and storms raise RMS height monotonically;
- identical inputs give identical output (the property the netcode needs);
- **a hull dropped into the sea settles at a draft where lift ≈ weight instead of sinking or launching**;
- planing lift rises with speed and caps, drag rises with speed;
- a storm measurably increases hull slam (peak vertical acceleration) over calm;
- a sloped surface produces a tilted normal, so a boat resting on a swell leans.

---

## 3. What the archipelago needs that this slice does NOT do

Named, ordered by what unblocks the most:

1. **A water surface in the renderer.** The rules and the host are numbers; nothing draws water yet. The lobby is
   already unbuildable in the sandbox (no SDL2), so this is unverifiable here too.
2. **A boat entity.** No vessel exists: no model, no seat, no input, no `PACKET` for it. The physics function takes
   probes and returns forces; wiring that to a player-controlled thing is a separate slice with netcode.
3. **Island streaming.** The level loader loads one level; "unload distant islands, keep cheap ocean state" needs a
   level graph and a streaming budget that do not exist.
4. **Spatial sectors + delta snapshots.** Real work in `protocol.h`, and it changes the wire format, so it wants its
   own design doc and a migration story for existing clients.
5. **A reliable UDP channel.** Today's resend-and-refresh is not RUDP; it converges, it does not guarantee ordering.
6. **Impact routed through the structure graph.** The most valuable remaining *simulation* gap: a boat hitting a pier
   should stress the joint it hit, not just the cells in the blast radius.
7. **Client-side SQL ledger.** Explicitly not an engine feature. If it is wanted, it belongs in the client app with
   IDUNA's event-stream as the server half, and the honest first question is what happens to a local ledger when the
   server disagrees — that is a conflict-resolution design, not a SQLite call.
8. **Absolute-alpha sprite compositing for fragments.** Rendering detail; the debris path already produces the chunk
   geometry it would composite.

## 4. Scope discipline

The blueprint is five specs in one paste: two client-architecture decisions, one netcode migration, one simulation
system, one streaming system. Each of those is a slice on its own. This pass takes only the one that had literally
zero code behind it — water — and makes it real, small, and tested, so the next slice starts from something that
runs instead of from a diagram.
