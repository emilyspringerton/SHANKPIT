# BedWars — SHANKPIT Game Mode Spec

*Written: 2026-06-18 | Status: northstar*

---

## What This Is

BedWars is the anchor game mode for the DragonsNShit persistent world layer. It is the first
mode that requires destructible blocks, persistent team state, and island economies — all of
which come from the Dragonfly backend. BedWars is intentionally chosen as the milestone because
it exercises every critical system at once: voxel world, beds (destructible objectives), generators
(economy), shops (inventory), and team elimination (game loop).

BedWars is Milestone 7 in the NORTHSTAR. It ships as the first post-Steam-EA major update.

---

## Core Loop

```
GAME START
  → 4 teams, each on an island in SCENE_VOXWORLD
  → Each island has a Bed (indestructible while team has 1+ players alive)
  → Generators spawn iron/gold/diamond on timers
  → Players buy gear from Shop NPC

PLAY
  → Attack enemy beds (mine blocks, breach defenses, destroy Bed block)
  → Once a bed is destroyed: that team's players no longer respawn
  → Eliminate all bedless players to knock out the team

WIN
  → Last team standing wins

SEASON END (optional)
  → Island snapshots written as lineage records (see SEASON_LINEAGE_SCHEMA_SPEC.md)
```

---

## Server Systems Required

### 1. DragonflyBackend (Prerequisite — Milestone 4)

The Go SHANKPIT server must swap `StaticBackend` → `DragonflyBackend` via the
`WorldBackend` interface (`server/system/backend.go`). All BedWars systems flow through
the Dragonfly world as authoritative state.

- `SceneVoxelPayload(sceneID)` returns real Dragonfly chunk data for the island scene
- Block mutations (bed destruction, block placement, mining) are persisted in Dragonfly
- `OnPlayerEnterScene` / `OnPlayerLeaveScene` drive Dragonfly entity lifecycle

### 2. Bed Entity

- Block ID: to be assigned (suggest 26 — unused in current block table)
- One per team island, placed at world origin of each island
- Tracked in server state: `BedState{TeamID, Destroyed, X, Y, Z}`
- When a player mines the Bed block: `BedDestroyed(teamID)` fires
- Effect: that team's `respawn_time` becomes infinite (no more respawns)

### 3. Resource Generator

- Static entities at fixed island positions (center, mid-bridge)
- Types: `GEN_IRON` (2s tick), `GEN_GOLD` (8s tick), `GEN_DIAMOND` (30s tick)
- Each tick: spawn an item entity at generator position
- Players walk over items to collect them (proximity check in server tick)
- Inventory: extend `PlayerState` with resource counts per type

### 4. Shop NPC

- Static entity per island
- `BTN_USE` near Shop NPC opens a shop request (new packet or existing `BTN_USE` handler)
- Shop inventory:
  | Item | Cost | Effect |
  |------|------|--------|
  | Iron Sword | 10 iron | upgrade WPN_KNIFE damage |
  | AR | 8 gold | WPN_AR ammo refill |
  | Wool block (64) | 4 iron | building material |
  | TNT | 4 gold | explosive block |
  | Ender Pearl | 4 gold | teleport projectile |

### 5. Respawn System

- On death: if team bed intact → respawn at island spawn after 5s
- If team bed destroyed → no respawn; spectate until eliminated
- Elimination: last player on bedless team dies → team eliminated → broadcast

### 6. Island Layout (SCENE_VOXWORLD)

Four islands at cardinal positions (±80 blocks on X/Z axis):
- Blue Island: (-80, groundY, 0)
- Red Island: (+80, groundY, 0)
- Green Island: (0, groundY, -80)
- Yellow Island: (0, groundY, +80)

Center island (0, groundY, 0): diamond generator, contested.
Mid-bridges at ±40 on each axis: gold generators.

Each island is a 16×16 block platform (one Dragonfly chunk), 3 blocks thick above the void.
Bed placed at island center (chunk_x * 16 + 8, groundY + 3, chunk_z * 16 + 8).

### 7. Block Mining

- `BTN_ATTACK` on a voxel block: server ray-traces from player position/yaw/pitch
- Hit test: for each block in the target chunk, check ray-AABB intersection
- On hit: decrement block health (defaults by type: Wool=1, Stone=8, Bed=30)
- On destroy: remove from Dragonfly chunk, broadcast updated chunk via `PacketVoxelData`
- Special case: Bed block at 0 health → `BedDestroyed(teamID)`

### 8. Block Placement

- Item: Wool block (inventory slot)
- `BTN_USE` on an empty voxel position adjacent to an existing block: place block
- Write to Dragonfly chunk, broadcast updated chunk

---

## Client Changes Required

### New block IDs to render

| Block ID | Name | Color |
|----------|------|-------|
| 35 | Wool (Blue) | (0.2, 0.3, 0.9) |
| 35+team_color_offset | Wool (Red/Green/Yellow) | team colors |
| 26 | Bed (active) | (0.8, 0.6, 0.1) |
| 26+1 | Bed (destroyed) | (0.3, 0.1, 0.1) dark red |
| 46 | TNT | (0.9, 0.15, 0.1) |

All are extension of the existing `draw_voxel_chunks` block color table
added in the ground-blocks commit (stone/grass/dirt already there).

### HUD extensions

- Team bed status bar (4 team icons; dimmed = bed destroyed)
- Resource inventory counter (iron/gold/diamond)
- Respawn countdown when waiting

---

## Wire Protocol Extensions

### PacketBedEvent (new type: 8)

```
[0]  = 8 (type)
[1]  = team_id
[2]  = 0=destroyed 1=health_update
[3]  = health (0=gone, 255=full)
```

### PacketResourcePickup (new type: 9)

```
[0]  = 9 (type)
[1]  = resource_type (0=iron 1=gold 2=diamond)
[2:4] = count (uint16)
```

---

## What "Done" Looks Like

**BedWars MVP (Milestone 7):**
- Four islands in SCENE_VOXWORLD loaded from Dragonfly chunks
- Beds placeable in world, destroyable by mining
- Iron generator ticking at center island
- No respawn after bed destroyed
- Win condition: last team standing
- Client renders island, bed, wool blocks
- 4-player LAN session functional

**BedWars Full:**
- All three generator types
- Shop NPC interaction
- Wool block placement + TNT
- Cross-island bridges buildable
- Season end → lineage snapshot written

---

## Prerequisite Chain

```
[DONE] Milestone 5: EA FPS core (portal travel, scene isolation)
[S39?] Milestone 4: DragonflyBackend interface wired
[S39?] Milestone 4: GoblinFoxDragon chunk data → SceneVoxelPayload
[Milestone 7] BedWars game mode
```

The DragonflyBackend is the critical prerequisite. Without it, BedWars runs on
`StaticBackend` with hardcoded chunks and no block mutation persistence.

---

## Related Specs

- `docs2/NORTHSTAR.md` — Milestone 7 owns BedWars
- `docs2/specs/THE_BRIDGE_SPEC.md` — voxel wire protocol (PacketVoxelData)
- `docs2/specs/WORLD_BACKEND_INTERFACE_SPEC.md` — WorldBackend interface
- `docs2/specs/SEASON_LINEAGE_SCHEMA_SPEC.md` — season-end snapshot schema
- `docs2/specs/SHANKPIT_DRAGONSNSHIT_SYSTEMS_SPEC.md` — full systems map
