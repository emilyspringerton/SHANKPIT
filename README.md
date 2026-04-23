# SHANKPIT Game Architecture Document

## What SHANKPIT is right now

At a high level, SHANKPIT currently includes:

- a lobby/client app with local and networked play flows
- a UDP server-authoritative game server
- multiple local and networked game modes
- linked scenes connected by portals
- terrain-backed worlds in addition to box/block geometry
- team modes including team deathmatch and capture the flag
- bot support for local team modes and online cold-start scenarios
- a growing retro rendering stack with procedural textures, sky, fog, and stylized lighting
- persistent skin selection
- test coverage for protocol and gameplay regressions. :contentReference[oaicite:2]{index=2} :contentReference[oaicite:3]{index=3} :contentReference[oaicite:4]{index=4}

---

## Current game modes

The current lobby/game flow exposes these core modes:

- **JOIN** — connect to the online deathmatch flow
- **TDMO** — online team deathmatch with bots supporting cold start
- **SOLO** — local single-player deathmatch flow
- **TRAIN** — local training-oriented flow
- **TDMB** — local team deathmatch with bots
- **CTFB** — local capture the flag with bots. :contentReference[oaicite:5]{index=5}

Internally, the client also recognizes additional local mode IDs such as `battle`, `tdm`, `training`, `recorder`, and `garage`, but the player-facing lobby is currently centered on the six modes above. :contentReference[oaicite:6]{index=6}

---

## Current worlds / scenes

SHANKPIT is now a multi-scene game rather than a single arena. The active scene list in the current construct includes:

- **Garage Osaka**
- **Stadium**
- **Voxworld**
- **Dust Compound**
- **Oil Tanker**
- **Poo Poo Island**. :contentReference[oaicite:7]{index=7} :contentReference[oaicite:8]{index=8} :contentReference[oaicite:9]{index=9}

These scenes are connected by explicit portal routing. The garage currently serves as a major hub and can route players into multiple destination worlds, while some worlds also provide return routes back to the garage or to adjacent connected scenes. :contentReference[oaicite:10]{index=10} :contentReference[oaicite:11]{index=11}

---

## Current visual direction

SHANKPIT is pursuing a deliberately stylized visual identity rather than realism-first rendering.

Key traits in the current renderer include:

- **legacy OpenGL immediate-mode rendering**
- **retro sky and retro lighting systems**
- **procedural texture hooks**
- **distance fog / haze tuning per scene**
- **terrain shading from sampled normals**
- **“neon brutalist” block rendering**
- stylized accent elements like hot pink trails and cyan grid motifs
- scene-specific readability tuning for larger outdoor worlds and tighter indoor spaces. :contentReference[oaicite:12]{index=12} :contentReference[oaicite:13]{index=13}

The codebase is intentionally keeping a cheap, centralized “fake baked” atmosphere model so that look-and-feel can evolve quickly without re-architecting the entire game. :contentReference[oaicite:14]{index=14}

---

## Current networking model

SHANKPIT uses a **server-authoritative UDP model**.

The current protocol includes at least these packet types:

- `PACKET_CONNECT`
- `PACKET_USERCMD`
- `PACKET_SNAPSHOT`
- `PACKET_WELCOME`
- `PACKET_DISCONNECT`. :contentReference[oaicite:15]{index=15}

The client currently includes:

- client-side command history
- interpolation state for remote players
- reconciliation correction values
- spawn synchronization logic
- snapshot timing and time offset tracking
- structured network diagnostics and timeout logging. :contentReference[oaicite:16]{index=16}

The lobby client defaults to:

- host: `s.farthq.com`
- port: `6969`. :contentReference[oaicite:17]{index=17}

This is still an actively evolving stack. The project favors clear packet semantics, aggressive diagnostics, and playable iteration over excessive abstraction.

---

## Team play and objective systems

SHANKPIT now has real team-mode support in both rules and world presentation.

That includes:

- team base markers
- team IDs on net players
- bot identity fields on net players
- team deathmatch variants
- online team deathmatch mode (`TDMO`)
- capture-the-flag state and rendering
- CTF pedestal / base anchors
- dropped / carried / home flag rendering logic. :contentReference[oaicite:18]{index=18} :contentReference[oaicite:19]{index=19}

A dedicated regression test exists for **CTFB carry melee**, confirming that flag carry attack behavior reuses the intended melee envelope and cooldown logic. :contentReference[oaicite:20]{index=20}

---

## Terrain and world systems

The project now includes an actual terrain package rather than relying only on block-map geometry.

Current terrain support includes:

- heightfield allocation and cleanup
- height sampling
- world-space containment checks
- normal sampling
- terrain rendering in the client
- terrain debug overlays
- scene-specific route / spawn / objective anchors over terrain-backed worlds. :contentReference[oaicite:21]{index=21} :contentReference[oaicite:22]{index=22}

This matters because SHANKPIT is no longer just a box-map shooter. It is now supporting larger outdoor spaces, racetrack-style terrain, canyon spaces, island spaces, and other scene-specific layouts. :contentReference[oaicite:23]{index=23}

---

## Bots

There are currently at least two bot-side code paths in the construct:

- a more direct / older bot sender
- a dedicated `apps/bot_client` implementation with a simple “brain” file, target acquisition, reward accumulation, and autosave behavior. :contentReference[oaicite:24]{index=24}

The current bot client can:

- connect to the UDP server
- load or initialize a brain file
- track visible players by scene
- target enemies
- shoot / move / strafe
- randomly jump / crouch
- save updated brain data after hitting a reward threshold. :contentReference[oaicite:25]{index=25}

Bot support is also part of the product strategy for team modes so the game remains playable before a large human player population exists. :contentReference[oaicite:26]{index=26}

---

## Skins

The current skin list is:

- BAT
- MAYRICE
- CYBORG
- PIRATE
- NINJA
- PIMP
- VIKING
- BILL
- GENIE
- WANDERER
- PINK
- GEISHA
- ALPINE. :contentReference[oaicite:27]{index=27}

Skin selection is persisted through:

- `shankpit_skin.cfg`. :contentReference[oaicite:28]{index=28}

The lobby also includes a dedicated **SKINS** entry. :contentReference[oaicite:29]{index=29}

---

## Repo shape

From the current construct, the repo includes work across these kinds of areas:

- `apps/lobby`
- `apps/server`
- `apps/bot_client`
- `apps/tests`
- `packages/common`
- `packages/simulation`
- `packages/render`
- `packages/world`
- `services/game-server`. :contentReference[oaicite:30]{index=30} :contentReference[oaicite:31]{index=31}

This reflects the current direction of the project:

- small native apps
- reusable low-level packages
- simulation and rendering split into packages
- services separated from the core client/server gameplay binaries.
## Controls (Player + Vehicles)

### Core movement & combat
- `W / S`: move forward/back.
- `A / D`: strafe left/right on foot.
- `Mouse`: look (yaw/pitch).
- `Left Mouse`: fire weapon.
- `Right Mouse`: ADS/zoom (sniper narrows FOV).
- `Space`: jump.
- `Left Ctrl`: crouch.
- `R`: reload (on foot).
- `1..6`: weapon select.
- `F`: use/interact (portal use, enter/exit vehicles).
- `E`: ability 1.

### Helicopter controls
- `F`: enter/exit helicopter when in range.
- `W / S`: helicopter forward/back thrust.
- `A / D`: helicopter yaw (turn left/right).
- `Space`: ascend.
- `Left Ctrl`: descend.
- `E`: strafe left.
- `Q`: strafe right.

### Ground vehicle controls
- `F`: toggle enter/exit ground vehicle pads where available (ex: garage pads).
- `W / A / S / D`: drive/steer with the standard movement axes.

### Debug / client toggles
- `F6`: terrain wireframe debug.
- `F7`: terrain normals debug.
- `F8`: vehicle style toggle.
- `F9`: vehicle underglow toggle.
- `F10`: vehicle worklights toggle.
- `F11`: scene points debug.
- `F12`: VS0 art direction toggle.
- `Esc`: return to lobby (or quit focused session state).

## System Architecture

### High-Level Components

```
┌─────────────────────────────────────────────────────────────┐
│                        SHANKPIT                              │
├─────────────────────────────────────────────────────────────┤
│  apps/lobby/          │ Main game client + rendering        │
│  packages/common/     │ Shared protocol & physics           │
│  packages/simulation/ │ Local game simulation               │
│  apps/training/       │ Headless sim for ML training        │
└─────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Main Application (`apps/lobby/src/main.c`)

**Purpose**: Game client with rendering, input handling, and state management

**Key Features**:
- Three application states: `STATE_LOBBY`, `STATE_GAME_LOCAL`, `STATE_GAME_NET`
- SDL2 window management and OpenGL rendering
- Network initialization (UDP sockets)
- Input processing (keyboard, mouse)

**Rendering Pipeline**:
- 3D scene rendering with perspective projection
- HUD overlay with orthographic projection
- Dynamic FOV adjustment (zoom for sniper)
- Crosshair and hit markers

**Visual Elements**:
```
Scene Hierarchy:
├── Grid (cyan lines for spatial reference)
├── Map Geometry (gray boxes with magenta outlines)
├── Players (red boxes with colored heads)
│   ├── Body (3.8 unit height)
│   ├── Head (0.8 unit cube, color-coded by weapon)
│   └── Weapon (3rd person view)
└── First-Person Weapon (with animations)
```

### 2. Protocol Layer (`packages/common/protocol.h`)

**Purpose**: Data structures and constants shared across all components

**Key Definitions**:

```c
// System Limits
#define MAX_CLIENTS 70
#define MAX_WEAPONS 5
#define MAX_PROJECTILES 1024

// Weapon IDs
WPN_KNIFE (0), WPN_MAGNUM (1), WPN_AR (2), 
WPN_SHOTGUN (3), WPN_SNIPER (4)

// Timing Constants
#define RELOAD_TIME 60 frames (~1 second)
#define SHIELD_REGEN_DELAY 180 frames (~3 seconds)
```

**Weapon Statistics**:

| Weapon   | Damage | ROF | Count | Spread | Ammo |
|----------|--------|-----|-------|--------|------|
| Knife    | 200    | 20  | 1     | 0.0    | ∞    |
| Magnum   | 45     | 25  | 1     | 0.0    | 6    |
| AR       | 20     | 6   | 1     | 0.04   | 30   |
| Shotgun  | 64     | 50  | 8     | 0.15   | 8    |
| Sniper   | 101    | 70  | 1     | 0.0    | 5    |

**Data Structures**:

```c
PlayerState {
    // Identity & Status
    id, active, is_bot
    
    // Transform
    x, y, z, vx, vy, vz, yaw, pitch, on_ground
    
    // Input State
    in_fwd, in_strafe, in_jump, in_shoot, in_reload, crouching
    
    // Combat
    current_weapon, ammo[5], reload_timer, attack_cooldown
    health, shield, shield_regen_timer
    
    // Feedback
    kills, hit_feedback, recoil_anim
    
    // AI/ML
    accumulated_reward
}

ServerState {
    players[MAX_CLIENTS]
    projectiles[MAX_PROJECTILES]
    server_tick
}
```

### 3. Physics Engine (`packages/common/physics.h`)

**Purpose**: Movement, collision detection, and combat mechanics

**Movement System**:

```
Movement Parameters (Tuned for "Turbo" feel):
├── GRAVITY: 0.018 units/frame²
├── JUMP_FORCE: 0.61 units/frame
├── MAX_SPEED: 0.75 units/frame
├── FRICTION: 0.42 (ground only)
├── ACCEL: 1.618 (golden ratio!)
└── MAX_AIR_SPEED: 0.2 units/frame
```

**Movement Pipeline**:
1. **Friction Application** (ground only)
   - Reduces velocity based on current speed
   - Uses `STOP_SPEED` threshold for standing still

2. **Acceleration**
   - Calculates wish direction from input
   - Applies acceleration toward wish speed
   - Respects max speed limits

3. **Gravity**
   - Constant downward force when airborne

4. **Collision Resolution**
   - AABB collision with map geometry
   - Separates players from walls/floors
   - Sets `on_ground` flag for landing

**Map Structure**:

The map consists of 70+ box primitives (`Box {x, y, z, w, h, d}`):

```
Map Zones:
├── Floor (900×300 base)
├── Central Ziggurat (3-tier pyramid)
├── East/West Spires (tall pillars with platforms)
├── Spine (long walls dividing the map)
├── Ruins (scattered box obstacles)
├── Parkour Platforms (floating jumps)
├── Cover Positions
├── Alpha Base (complex multi-level structure)
├── Omega Base (mirrored complex)
└── Boundary Walls (prevent out-of-bounds)
```

**Combat System**:

**Hit Detection**:
```
Ray-Capsule Intersection Algorithm:
1. Cast ray from shooter position + eye height
2. Check sphere intersection with target HEAD
   - Position: target.y + 4.5
   - Radius: 0.8 units
3. If miss, check sphere intersection with BODY
   - Position: target.y + 2.0
   - Radius: 2.7 units
4. Return: 0=Miss, 1=Body, 2=Headshot
```

**Damage Calculation**:
```python
if hit_type == HEADSHOT and target.shield <= 0:
    damage = base_damage × 3  # Triple damage
    
# Shield absorbs first
if target.shield > 0:
    shield_damage = min(damage, target.shield)
    target.shield -= shield_damage
    damage -= shield_damage

# Remaining damage to health
target.health -= damage
```

**Shield Mechanics**:
- **Capacity**: 100 points
- **Regen Delay**: 3 seconds after last hit
- **Regen Rate**: 1 point per frame (60/sec) after delay
- **Damage Absorption**: Shields take all damage before health

**Weapon-Specific Logic**:

- **Knife**: Melee range check (100 units²) before hit detection
- **Shotgun**: Fires 8 pellets with 0.15 spread
- **AR**: Continuous fire with 0.04 spread
- **Sniper**: No spread, high damage, low ROF

### 4. Local Game Simulation (`packages/simulation/local_game.h`)

**Purpose**: Single-player/bot match logic (referenced but not shown in files)

**Expected Functionality**:
- `local_init_match(int bot_count)`: Initialize match with bots
- `local_update(...)`: Step simulation forward
- `local_state`: Global `ServerState` instance
- `USE_NEURAL_NET`: Flag to enable ML-powered bots

**Game Modes** (based on lobby inputs):
- **D Key**: Demo mode (1 bot)
- **B Key**: Bot battle (31 bots, no AI)
- **S Key**: Smart mode (31 neural network bots)
- **N Key**: Network multiplayer

### 5. Training Interface (`apps/training/headless.c`)

**Purpose**: Headless simulation for reinforcement learning

**Python C API**:
```c
void sim_init(int bots)
void sim_step(float fwd, float strafe, float yaw, 
              float pitch, int shoot, int jump)
ServerState* sim_get_state()
```

**Training Loop**:
```
1. Initialize environment with bots
2. For each timestep:
   a. Get game state
   b. Neural network computes action
   c. Step simulation
   d. Calculate reward from state change
   e. Update network weights
```

## Rendering System

### Camera System

**First-Person View**:
```c
Eye Height: 
- Standing: 4.0 units
- Crouching: 2.5 units

Camera Transform:
1. Translate to -player.position
2. Rotate by -yaw (Y-axis)
3. Rotate by -pitch (X-axis)
4. Offset by eye height
```

**FOV System**:
- Default: 75°
- Sniper ADS: 20° (interpolated smoothly)
- Affects sensitivity: zoom mode uses 0.05, normal uses 0.15

### Weapon Rendering

**First-Person Weapon**:
```
Animations:
├── Recoil Kick: Backward + upward motion
├── Reload Dip: Sine wave during reload
├── Movement Bob: Speed-based sine oscillation
└── Position: Offset right for normal, centered for zoom
```

**Third-Person View**:
- Body: 1.2×3.8×1.2 box
- Head: 0.8×0.8×0.8 cube (positioned at y+2.0)
- Weapon: Attached to body, rotates with pitch

**Color Coding**:
- Players match their equipped weapon color
- Dead players: dark red (0.2, 0, 0)

### HUD System

**Elements**:
1. **Crosshair**:
   - Normal: Small cross (green)
   - Sniper ADS: Full-screen crosshair (green)

2. **Hit Marker**:
   - Body hit: Cyan circle (10 frames)
   - Headshot: Magenta circle (20 frames)

3. **Health Bar** (bottom-left):
   - Dark red background (50×20px)
   - Bright red fill (width = health × 2)

4. **Shield Bar** (above health):
   - Dark blue background
   - Light blue fill (width = shield × 2)

5. **Ammo Display** (bottom-right):
   - Yellow vertical bars
   - Each bar represents 1 ammo
   - Knife shows 99 bars

## Network Architecture

**Protocol**: UDP (unreliable, connectionless)

**Server Address**:
- Primary: `s.farthq.com:5314`
- Fallback: `127.0.0.1:6969`

**Socket Configuration**:
- Non-blocking mode (`O_NONBLOCK` / `FIONBIO`)
- Platform-specific initialization (Winsock on Windows)

**Expected Packet Types** (not implemented in shown code):
- Client → Server: Input state
- Server → Client: World state snapshot

## Input System

**Keyboard Bindings**:
```
Movement:
  W/A/S/D - Forward/Left/Back/Right
  Space - Jump
  LCtrl - Crouch

Combat:
  LMB - Shoot
  RMB - Aim/Zoom
  R - Reload
  1-5 - Weapon select

Menu:
  Escape - Return to lobby
  D/B/S/N - Start game modes (lobby only)
```

**Mouse**:
- Relative motion mode (captured in-game)
- Sensitivity scaling based on FOV
- Yaw/Pitch clamping (pitch: -89° to +89°)

## Performance Characteristics

**Frame Rate**: 60 FPS target (16ms delay)

**Simulation Rate**: Same as frame rate (no separate tick)

**Memory Footprint** (approximate):
- PlayerState: ~200 bytes × 70 = 14KB
- Projectiles: ~40 bytes × 1024 = 40KB
- Map geometry: ~24 bytes × 70 = 1.7KB
- Total state: ~56KB

**Rendering Complexity**:
- Draw calls: ~200 (map) + 70 (players) + HUD
- No texture loading (pure geometry)
- Immediate mode OpenGL (glBegin/glEnd)

## AI/ML Integration

**Neural Network Interface**:
- **Observation Space**: Full `ServerState` (56KB)
- **Action Space**: 6D continuous (fwd, strafe, yaw, pitch, shoot, jump)
- **Reward Signal**: `accumulated_reward` field in `PlayerState`

**Training Pipeline**:
```
Python (PyTorch/TF) ←→ C Extension ←→ Headless Sim
                    ↑               ↑
                    ctypes/cffi     game logic
```

## Future Extensions

**Potential Improvements**:
1. Projectile system (currently hitscan-only)
2. Network multiplayer implementation
3. Server-side state authority
4. Lag compensation techniques
5. Texture mapping and materials
6. Sound effects and music
7. Particle systems for effects
8. Spectator mode
9. Killcam/replay system
10. Leaderboard and stats tracking

## Dependencies

**External Libraries**:
- SDL2: Window management, input, timing
- OpenGL: 3D rendering (legacy immediate mode)
- GLU: Utility functions (gluPerspective, gluOrtho2D)
- Platform-specific: Winsock2 (Windows) / BSD sockets (Unix)

**Build Requirements**:
- C compiler (C99+)
- SDL2 development libraries
- OpenGL headers

## Code Style Notes

**Conventions**:
- Snake_case for functions and variables
- UPPER_CASE for constants and macros
- Minimal comments (code is documentation)
- Single-file compilation units
- Global state (e.g., `local_state`)

**Performance Optimizations**:
- Stack allocation for temporaries
- No dynamic memory allocation in hot paths
- Inline hit detection in combat loop
- Minimal function call overhead

## Grass shader path (hybrid + fallback)

A grass-only GLSL render path now exists for Voxworld and can be toggled at runtime without touching the rest of the legacy renderer.

- Procedural placement remains the source of truth.
- `F5` toggles shader grass on/off live (`ON` uses shader path, `OFF` forces legacy path).
- `[` / `]` decrease/increase `grass_render_distance` during runtime.
- Grass settings persist to `shankpit_grass.cfg`:
  - `grass_shader_enabled=0|1`
  - `grass_render_distance=<float>`
  - `grass_density_scale=<float>`

If shader compile/link or shader texture setup fails, SHANKPIT logs the failure and immediately falls back to the legacy grass renderer.
