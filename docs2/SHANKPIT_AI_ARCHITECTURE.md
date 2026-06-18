# SHANKPIT AI Architecture

*How Emily Prime plays SHANKPIT and how bots learn.*

---

## Overview

SHANKPIT has two distinct AI systems that coexist in the same game session:

1. **C bot clients** (`apps/bot_client/`) — autonomous in-process bots running inside the physics simulation. Evolved via genetic algorithm. These run on every session.

2. **emily-bot** (`apps2/emily-bot/`) — Emily Prime as a live network player. Connects over UDP just like a human client. Swappable between a heuristic policy, a GPT-2 policy, or future LLM integrations.

---

## System 1: C Evolution Bots

### What they are

C bots are `PlayerState` entities with a `BotGenome` struct in `packages/common/physics.h`. They live inside the physics loop and run `bot_think()` every tick.

```c
typedef struct {
    int   version;      // v2 = current
    float w_aggro;      // push toward target when in range
    float w_strafe;     // left/right oscillation amplitude
    float w_jump;       // jump frequency
    float w_slide;      // slide frequency
    float w_turret;     // aim yaw correction speed
    float w_repel;      // evasive strafe on incoming damage
    float w_retreat;    // reverse thrust when health < 30%
} BotGenome;
```

### How bot_think works

`bot_think()` in `packages/simulation/local_game.h` runs once per physics tick for each bot:

1. **Find nearest enemy** — scan all other `PlayerState` entities for minimum XZ distance.
2. **Aim** — rotate `out_yaw` toward target using `atan2`. Speed gated by `w_turret`.
3. **Move** — choose thrust based on range:
   - `> 15u` → advance (scaled by `w_aggro`)
   - `< 5u` → retreat slightly
   - `< 30% health` → full reverse (`-w_aggro * w_retreat`)
4. **Strafe** — sinusoidal side motion controlled by `w_strafe × 600° × dt`.
5. **Evasion** — if `hit_feedback > 0` (bot was shot this tick), add `w_repel × 300° × dt` yaw dodge.
6. **Shoot** — fire when yaw delta to target is within 12°.

Per-tick reward accumulation (training mode only):
- `+0.05` per tick alive (survival incentive)
- `+0.1` per tick when an enemy is within 25u (engagement incentive)
- `+1.0` on confirmed kill (via `katana_apply_damage` path)

### Evolution loop

Evolution fires on every bot respawn in `phys_respawn()` (`packages/common/physics.h`):

```
respawn triggered
    → find winner = get_best_bot()   # highest accumulated_reward
    → if winner != self:
        evolve_bot(self, winner)     # two-parent crossover + mutation
    → self.accumulated_reward = 0   # CRITICAL: reset after selection
```

`evolve_bot()` two-parent crossover:
- 50% chance each gene comes from winner vs uniform random init
- Each gene independently perturbed by `rand_f() * 0.1`
- Version field set to 2

`get_best_bot()` scans all live bots, returns pointer to highest `accumulated_reward`. This is now per-life (reset on respawn), so it reflects recent performance rather than time-alive.

**Key bugs fixed (2026-06-18):**
- `accumulated_reward` was never reset → oldest bot always won selection → fixed in `phys_respawn`
- `hit_feedback` was only set on attacker → `w_repel` couldn't evolve evasion → fixed: defender also gets `hit_feedback = 15` on incoming damage

### Genome persistence (bot_client)

The `apps/bot_client` can load/save genomes from disk. Version guard introduced in S38-05:

```c
fread(&brain, sizeof(BotGenome), 1, f);
if (brain.version < 2)
    brain.w_retreat = 0.5f;   // upgrade path for old 28-byte saves
```

---

## System 2: emily-bot (Emily Prime as player)

### Architecture

`apps2/emily-bot/main.go` is a headless Go UDP client. It runs exactly like a human client from the server's perspective — same `PacketConnect` handshake, same `PacketUserCmd` at 20 Hz.

```
emily-bot
├── receiveLoop()       — parse incoming UDP packets
│   ├── PacketWelcome   → record myID
│   ├── PacketSnapshot  → update own position + peer positions
│   └── PacketImpact    → kill detection → emily observe
│
└── tick loop (20 Hz)
    └── think()
        ├── dead-reckoning position update
        ├── peer map pruning (2s TTL)
        ├── nearest enemy selection
        └── policy dispatch:
            ├── GPT-2 policy (4 Hz, when --gpt2-url set)
            └── heuristic fallback
```

### Position tracking

Two-level own-position tracking:

1. **Server anchor** — when `PacketSnapshot` contains the bot's own `id == myID`, overwrite `myX/myY/myZ` directly. Server-authoritative.

2. **Dead-reckoning** — between snapshots, integrate last tick's `(fwd, str)` inputs:
   ```go
   yawRad := myYaw * π/180
   myX += (drFwd*sin(yawRad) + drStr*cos(yawRad)) * 8u/s * 0.05s
   myZ += (drFwd*(-cos(yawRad)) + drStr*sin(yawRad)) * 8u/s * 0.05s
   ```

### Heuristic policy

The default policy when no GPT-2 server is set:

| Condition | Action |
|---|---|
| No peers visible | Patrol: spin +5°/tick, slow forward |
| Target dist > 20u | Advance (`fwd = 1.0`) |
| Target dist < 8u | Back off (`fwd = -0.5`) |
| Always when target exists | Strafe ±0.7 (2s period toggle) |
| Yaw delta to target < 15° | Fire (`BtnAttack`) |

Weapon selection via `weaponForRange(dist)`:
- `dist < 30u` → Magnum (idx 1)
- `30u–50u` → AR (idx 2)
- `> 50u` → Sniper (idx 4)

### GPT-2 policy

When `--gpt2-url http://localhost:8088` is set, runs at 4 Hz (every 5 ticks at 20 Hz):

```
serializeState() → state token string
    e.g. "pos:14,8 hp:100 enemies:1 nearest:dir=NE dist=12 vis=1"

POST /generate { prompt: stateStr, max_tokens: 24, temperature: 0.7 }
    200ms timeout → fallback to heuristic on timeout/error

parseActionTokens() → UserCmd
    e.g. "fwd:0.80 str:-0.30 yaw_delta:5.0 shoot:1 jump:0 crouch:0 weapon:ar"
```

State format (from `gpt2-alpine-c/scripts/game_state.py`):
- Own position in XZ
- Own health
- Up to 4 nearest enemies: direction (N/NE/E/SE/S/SW/W/NW), distance bucket, visibility bit

The GPT-2 model is fine-tuned on Emily operational docs + game replay data (`--game-replays`).

### Replay logging

When `--replay-dir` is set, every tick writes one NDJSON line:

```json
{"tick": 42, "state": "pos:14,8 hp:100 ...", "action": "fwd:1.00 str:0.70 ... weapon:ar"}
```

Files named `YYYYMMDD-HHmmssZ.ndjson`. Aggregated by `gpt2-alpine-c/scripts/build_game_corpus.py` → training JSONL for the next fine-tune round.

### Emily Prime reporting

Non-blocking `emily observe` calls at key events:
- Session start
- `PacketWelcome` (connected, received `myID`)
- `PacketImpact` with `hit_entity == 1` (kill confirmed)

Rate-limited: minimum 15s between observations. Disabled with `--no-report`.

---

## System 3: Dragonfly World Backend (S40–S41)

The voxel world layer has been abstracted behind `WorldBackend` so SHANKPIT can source terrain from either the built-in procedural generator or GoblinFoxDragon's Dragonfly world:

```
server/system/WorldBackend interface
    SceneVoxelPayload(sceneID, chunkX, chunkZ int) []VoxelBlock

StaticBackend           → returns nil → server uses procedural scanChunkForVoxelBlocks()
DragonflyBackend        → GET http://gfd:7070/chunks?scene=N&cx=X&cz=Z → []VoxelBlock
```

Activate with `./server-go --dragonfly-url http://localhost:7070`. Without the flag, `StaticBackend` is used and terrain is identical to the old hardcoded generator.

Block ID mapping (`packages2/common/block_map.go`, `packages/common/block_map.h`) translates Dragonfly `minecraft:stone` / `minecraft:grass_block` etc. to SHANKPIT block IDs (stone=1, grass=2, dirt=3, log=17, leaf=18).

---

## Data Flow Diagram

```
                  SHANKPIT UDP Server (:6969)
                         │
             ┌───────────┼───────────────┐
             │           │               │
       C bot clients   emily-bot     Human clients
       (physics loop)  (Go, headless)  (C + SDL2)
             │           │
             │      ┌────┴─────────────┐
             │      │ think() 20 Hz    │
             │      │                  │
             │  heuristic        GPT-2 policy
             │  policy           (4 Hz, POST /generate)
             │                         │
             │                   gpt2-alpine-c
             │                   serve.py :8088
             │                   (fine-tuned on Emily
             │                    docs + game replays)
             │
        Evolution loop                  Replay logger
        (on respawn)                    NDJSON → JSONL
             │                          corpus builder
        BotGenome                            │
        crossover                     prime_directive
        + mutation                    _dataset.py
                                      (next fine-tune)
```

---

## Running the AI

### Heuristic mode (always available)

```bash
cd /home/fatbaby/SHANKPIT
GOWORK=off go run ./apps2/server-go &          # start game server
GOWORK=off go run ./apps2/emily-bot            # connect emily-bot heuristic
```

### GPT-2 mode (requires fine-tuned model)

```bash
cd /home/fatbaby/gpt2-alpine-c
python3 scripts/serve.py                       # start inference server :8088

cd /home/fatbaby/SHANKPIT
GOWORK=off go run ./apps2/emily-bot \
  --gpt2-url http://localhost:8088 \
  --replay-dir var/replays
```

### Evolution bots (C server)

```bash
cd /home/fatbaby/SHANKPIT
make server MODE=evolution                     # C server in evolution mode
make bot_client                               # build bot_client binary
./bin/bot_client                              # bot joins and evolves
```

### With Dragonfly world (S40–S41)

```bash
cd /home/fatbaby/GoblinFoxDragon
GOWORK=off go run ./apps2/server-go &         # GFD world API on :7070 (once implemented)

cd /home/fatbaby/SHANKPIT
GOWORK=off go run ./apps2/server-go \
  --dragonfly-url http://localhost:7070 &     # SHANKPIT with Dragonfly terrain
GOWORK=off go run ./apps2/emily-bot           # emily-bot connects
```

---

## Key Files

| File | Purpose |
|---|---|
| `packages/common/physics.h` | `BotGenome`, `evolve_bot()`, `phys_respawn()`, `bot_think()` |
| `packages/simulation/local_game.h` | `bot_think()` reward accumulation, health-aware retreat |
| `apps/bot_client/src/main.c` | C bot process: genome load/save, version guard |
| `apps2/emily-bot/main.go` | Emily Prime network bot: heuristic + GPT-2 policy, dead-reckoning |
| `server/system/backend.go` | `WorldBackend` interface + `VoxelBlock` type |
| `server/system/dragonfly_backend.go` | HTTP-based `DragonflyBackend` |
| `server/worldapi/worldapi.go` | GFD `/chunks` HTTP endpoint (ChunkGenerator interface) |
| `server/worldapi/dragonfly_gen.go` | `DragonflyChunkGenerator`: WorldStore hook + procedural fallback |
| `packages2/common/block_map.go` | Dragonfly name → SHANKPIT block ID mapping (Go) |
| `packages/common/block_map.h` | Dragonfly name → SHANKPIT block ID mapping (C) |
| `gpt2-alpine-c/scripts/game_state.py` | State serializer + action decoder for GPT-2 |
| `gpt2-alpine-c/scripts/build_game_corpus.py` | Replay NDJSON → training JSONL |

---

## Current Status

| System | Status |
|---|---|
| C evolution bots (heuristic) | Production-ready. `w_retreat` + `hit_feedback` bugs fixed S38-06/07 |
| emily-bot heuristic | Production-ready. Dead-reckoning, weapon rotation, Emily observe wired |
| emily-bot GPT-2 policy | Infrastructure complete. Model not yet fine-tuned on game data (S26-04 pending Colab) |
| Replay corpus pipeline | Complete. `--replay-dir` → NDJSON → `build_game_corpus.py` → JSONL |
| Dragonfly backend | Interface wired (S40). `--dragonfly-url` flag active (S41-01). `DragonflyChunkGenerator` ready (S41-02). Pending: wire GFD ChunkGenerator to real Dragonfly world data. |
