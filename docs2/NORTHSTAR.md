# SHANKPIT — Northstar

*Last updated: 2026-06-06*

---

## What This Document Is

This is the northstar for SHANKPIT: where it is going, why, and what "done" means at each layer. It is meant to be kept alive alongside the codebase — updated as the system evolves, used by agents and engineers to orient work, and used to drive recursive documentation of the larger system.

This is not a roadmap with dates. It is a statement of direction that makes priorities obvious even when the shape of the work isn't yet fully visible.

---

## The Three-Sentence Version

SHANKPIT is a fast, stylized, server-authoritative FPS being built into the front end of a persistent world engine (DragonsNShit / Dragonfly). The Minecraft Bedrock Protocol (Dragonfly fork) gives us a programmable, destructible, buildable voxel world as a backend — BedWars, zone evolution, season lineage, all of it flows from that. Documentation-powered recursive self-improvement is the process: the system cannot improve faster than it can understand itself.

---

## Layer 1: The FPS Core (current)

SHANKPIT today is a working UDP multiplayer shooter with:

- server-authoritative physics and combat
- multi-scene worlds connected by portals
- bots with neural net support
- retro stylized rendering (immediate-mode OpenGL, neon brutalist aesthetic)
- team modes (TDM, CTF), capture mechanics, flag carry/melee
- a Construct system that snapshots the codebase into a reviewable artifact
- skateboard-cli and test runners for regression coverage

The client (`apps/lobby`) is intentionally kept cheap and client-agnostic: it sends UDP UserCmds and renders what the server says. The feel — turbo movement, snappy combat, crouching-landing speed boost — is locked in and must survive all further architecture work.

**Nothing about the client's feel should be sacrificed to enable the persistent world.**

---

## Layer 2: Scene Correctness + Portal Travel (immediate priority)

The infrastructure for multi-scene existence is mostly wired but not delivering value yet. This is the most important near-term completion.

What exists:
- `scene_id` on NetPlayer and server snapshot headers
- Portal geometry, `scene_portal_triggered()`, `scene_spawn_point()`
- `BTN_USE` in UserCmd already wired to client input
- `pending_scene`, `transition_timer` fields on ServerState
- `phys_set_scene()` for scene-swapping

What is missing:
- Authoritative server-side portal travel (validate radius, flip player scene, safe respawn)
- Per-player scene isolation — physics must stop being a global active-scene swap
- Client filtering of players/projectiles/vehicles by `scene_id`
- A travel state machine that uses `pending_scene` / `transition_timer`
- Cross-scene attack prevention (server-side check)

Until these land, every scene and portal in the game is set dressing. The moment they land, every scene becomes genuinely traversable space and the world becomes real.

**This is milestone 1 of the DragonsNShit bridge.**

---

## Layer 3: DragonsNShit — The Dragonfly Backend

The long game is wiring the Shankpit client against a Dragonfly-fork backend that speaks Minecraft Bedrock Protocol. This is the persistent world layer.

What this enables:
- Programmable, destructible, buildable voxel worlds
- Games like BedWars, siege modes, objective-based competitive play — built on top of a world that players can physically shape
- Zone geometry that evolves across seasons (see Layer 4)
- Authoritative item/inventory/economy persistence
- Entity systems (NPCs, monsters, creatures) living in the same world
- Dragonfly's chunk streaming + entity authority feeding a simplified voxel payload to the Shankpit C client (see `THE_BRIDGE_SPEC.md`)

The wire contract is the thing that enables incremental adoption:

```
Shankpit client → UserCmd (UDP) → Dragonfly/DNS backend
                ← Snapshot (UDP) ← (with voxel payload extension)
```

The client never needs to know whether it is talking to the old Go game server or the Dragonfly backend. Weapon logic, hitscan, movement all stay on the Shankpit server path. Dragonfly handles persistence, world mutation, chunk authority.

The seam in the code is `portal_resolve_destination()` / `world_backend_*()` — a resolver the Shankpit server calls. Initially stubbed, later routes to the Dragonfly fork. **This interface must be defined before any integration work starts** so the direction is structurally inevitable.

**Current state:** The Dragonfly fork lives in the repo but is not in the Construct. From the Construct's perspective it does not exist. Fixing this is milestone 4 in `SHANKPIT_DRAGONSNSHIT_SYSTEMS_SPEC.md` and a prerequisite for the Construct to serve as an accurate system snapshot.

---

## Layer 4: Season Lineage — Worlds With Memory

This is the design concept that has no spec yet and deserves one.

The core idea: when a season ends and a world is rebooted, **the world does not start from zero**. Player data, zone history, and significant events from prior seasons are available as lineage chains. A "Classic" server spin-up can reconstruct its history. A new season can carry forward echoes of the old one — at minimum as queryable data, optionally as visible world state (a burned-out ruin where a base used to be, a monument to a dominant team, a scar in the terrain from a major battle).

This is not a feature for today. But it shapes architecture decisions now:

- **Player identity must be durable across world reboots.** Ephemeral client slots are fine for sessions; account/character identity needs to survive. Dragonfly backend owns this.
- **Zone state must be snapshotted at season end,** not just discarded. The snapshot format needs to be defined so that future servers can read it.
- **Lineage chains are queryable, not just visible.** A "classic" server that loads a prior-season snapshot should have a proper lineage record: who played, what they built, what the zone looked like at world-end. This is the data model, not the renderer.
- **Bleed-over is opt-in per server/season configuration.** Some servers want clean starts. Some want echoes. The system should support both without special-casing.

The early IDUNA spec (IAM layer) already handles durable identity. When Dragonfly backend lands, season lineage becomes a persistence design question on top of Dragonfly's chunk + entity store.

**This is a northstar concept, not an immediate build target.** But it should inform every persistence design decision from now on.

---

## Layer 5: Documentation-Powered Recursive Self-Improvement

The system cannot improve faster than it can understand itself. This is the process layer, not a product feature.

**What this means in practice:**

- **The Construct is the truth artifact.** Any subsystem not in the Construct effectively does not exist for agents, reviewers, or automation. The Construct must expand to include DragonsNShit bridge surfaces, scene/portal code, and all voxel integration work.
- **Specs precede integration.** The bridge seam (`world_backend_*`), the voxel packet format, the season snapshot schema — these must be written down before the code lands, so that agents working on one side of the seam know what the other side expects.
- **CHANGELOG.md is maintained continuously.** Every meaningful change gets an entry. This is non-negotiable.
- **Agents can act on the Construct.** The Construct format exists precisely so that Claude Code and other agents can read the full system state without navigating the entire repo. Expanding it is as important as writing the code.
- **Docs live next to the code they describe.** Specs in `docs2/specs/` are the right place. They should be updated when the code they describe changes, not abandoned.

The northstar for documentation is: **a new agent (or new engineer) dropped into this repo should be able to reconstruct a complete mental model of the system from `NORTHSTAR.md` + the Construct + the specs in `docs2/specs/`.** We are not there yet. Every documentation session should move the needle toward that goal.

---

## Known Gaps (things that are partially built but not delivering value)

These are tracked in `SHANKPIT_DRAGONSNSHIT_SYSTEMS_SPEC.md` section 4. Summary:

| Gap | What exists | What's missing |
|-----|-------------|----------------|
| Scene correctness | `scene_id` on wire, portal geometry, per-player sceneID, 20Hz scene-filtered snapshot, cross-scene attack guard (Go server) | Per-player physics isolation (C server) |
| Portal travel | Trigger + constants + Go server ack (PacketSceneChange) | Travel state machine in C server, client-side scene transition |
| USE verb | `BTN_USE` wired | Not a first-class gameplay action yet |
| Transition state | `pending_scene`, `transition_timer` | State machine consuming them (C server) |
| Dragonfly backend | Plan + code in repo | Construct inclusion, bridge seam interface |
| Season lineage | Concept | Snapshot schema, identity durability spec |
| Construct coverage | Core FPS files | Dragon bridge, world code, scene code |

**Layer 2 progress (2026-06-09):** Go server now sends PacketSceneChange (type=6) immediately after portal travel, giving the traveling client their new scene ID and spawn position. broadcastSnapshots() goroutine runs at 20Hz and sends each client a PacketSnapshot containing only peers in the same scene (18 bytes/entity: clientID + sceneID + pos + yaw). yaw is now tracked per-client from UserCmds. Mutex protects the clients map. SHANKPIT commit beb975b.

**Layer 2 progress (2026-06-09, continued):** Cross-scene attack guard implemented in Go server. gameWorld replaces the world{} stub — RayTrace iterates same-scene clients only, using ray-sphere intersection (hitbox radius 0.4, sphere centered at chest height). Shooter is excluded from hits. Vec3.Sub/Len/Dot added to ballistics.go. Per-shot shankPlayer uses real client position and sceneID. Tests: TestCrossSceneAttackGuard, TestCrossSceneNoHit, TestShooterDoesNotHitSelf. go test ./apps2/server-go/ ./server/system/ passes.

---

## Steam Launch Plan (Revenue Track 1)

**Strategy:** Ship Early Access with FPS core only. Dragonfly/BedWars lands as major post-launch update.
No need to wait for full persistent world — the FPS core is already differentiated.

### Milestone 5: Steam EA Build (FPS core only)
- Client-side portal travel ✓ (PACKET_SCENE_CHANGE received + client_apply_scene_id + travel overlay)
- Per-player physics isolation ✓ (C server calls phys_set_scene(p->scene_id) per player)
- Cross-scene attack prevention ✓ (Go server, Milestone 2)
- Package: headless Go server binary + C client build for Linux/Windows
- Target: 4-player LAN/internet session without local setup beyond running two binaries
- Price point: $9.99 USD EA

### Milestone 6: Steam Direct + Launch
- Steam Direct account ($100, human action required)
- Page assets: 3 screenshots, 30-second capsule trailer, short description
- Launch: EA store page live, builds uploaded via Steamworks SDK
- BLOCKED until: Milestone 5 ✓ + Steam account created (human action)

### Milestone 7: BedWars + Dragonfly (post-EA major update)
- GoblinFoxDragon DragonflyBackend implements WorldBackend interface
- Shankpit client connects to Dragonfly world, mines/places/destroys blocks
- BedWars game mode: beds, island progression, team elimination
- Season 1 lineage: snapshot written at season-end, loadable for next season

---

## What "Done" Looks Like

**FPS Core done (Milestone 5 EA):** Scenes fully traversable ✓, portal travel server-authoritative ✓,
per-player physics isolated ✓, cross-scene attacks impossible ✓. Packaged into standalone EA build.

**Dragonfly bridge done (Milestone 7):** Minimal `world_backend_*` interface defined and stubbed.
Construct includes all bridge surfaces. A Dragonfly-backed world boots and accepts a Shankpit client.

**BedWars / destructible modes done (Milestone 7):** A Shankpit client connects to a Dragonfly world,
can mine/place/destroy blocks within game rules, voxel state authoritative and persistent.

**Season lineage done:** A season-end snapshot is written. A subsequent server spin-up can read the
snapshot and present lineage data. Zone history is queryable.

**Documentation done:** The Construct includes all active subsystems. A new agent dropped into the repo
can reconstruct the full system from docs alone. Every spec in `docs2/specs/` is current.

---

## GoblinFoxDragon Repo Relationship

**GoblinFoxDragon** (`github.com/emilyspringerton/GoblinFoxDragon`) and **SHANKPIT**
(`github.com/emilyspringerton/SHANKPIT`) share `module dragonsnshit` in `go.mod` because
they are development forks of the same codebase, not competing codebases.

| Repo | Focus | Key additions |
|------|-------|---------------|
| SHANKPIT | FPS game server — combat, portal travel, physics, matchmaker | `server/system/portal.go`, `server/system/backend.go`, `apps2/server-go/` |
| GoblinFoxDragon | Persistent world layer — entity management, EduScript, world scripting | EduScript entity system, Architect Orb reality manipulation, sandboxed scripting |

**GoblinFoxDragon is the DragonsNShit backend fork.** It is where the Dragonfly-derived
persistent world systems are built. SHANKPIT is where the FPS mechanics live.

**Intended merge direction:** When Milestone 4 (Construct expansion) lands and `DragonflyBackend`
is implemented, the bridge surface (`server/system/backend.go`) will pull GoblinFoxDragon's
entity/persistence systems in as a `DragonflyBackend` implementation. The two forks converge
at the `WorldBackend` interface seam.

**Why the same module name:** Both repos were originally one codebase. The module name
`dragonsnshit` is shared intentionally — it signals they are two halves of the same system.
When the merge happens, the combined module stays `dragonsnshit`.

**For agent context:** When working on FPS mechanics, hitscan, portal travel, physics — work in
SHANKPIT. When working on entity persistence, scripting, zone evolution, Dragonfly integration —
work in GoblinFoxDragon. Don't cross-modify: agents reading only SHANKPIT or only GoblinFoxDragon
should understand they hold half the picture.

---

## Related Specs

- `docs2/specs/SHANKPIT_DRAGONSNSHIT_SYSTEMS_SPEC.md` — full systems map for the FPS→persistent world bridge
- `docs2/specs/THE_BRIDGE_SPEC.md` — wire protocol for Shankpit ↔ Bedrock voxel data
- `docs2/specs/WORLD_BACKEND_INTERFACE_SPEC.md` — WorldBackend Go interface (Milestone 3)
- `docs2/NETCODE_CONTRACT_SPEC.md` — UDP protocol contract
- `docs/M_OVERLAY_PROTOCOL.md` — overlay protocol
- `CHANGELOG.md` — living change log
