# Changelog

## Recent changes

### 2026-06-12 (continued 4)
- feat(s19-04): EA build packaging — Makefile targets: go-server (GOWORK=off), ea (Linux dist), ea-windows (mingw cross-compile); docs/EA_BUILD.md install/run guide; 4-player LAN/internet session instructions

### 2026-06-12 (continued 3)
- docs: Steam launch plan added to NORTHSTAR — M5 EA build (FPS core), M6 Steam Direct, M7 BedWars post-launch; S19-01/02 verified implemented
- Construct Phase 508: expanded to include Go server (apps2/server-go/main.go), portal system (server/system/portal.go), WorldBackend interface (backend.go), and WorldBackend spec --no-apple
- Season lineage snapshot schema spec at docs2/specs/SEASON_LINEAGE_SCHEMA_SPEC.md: player records, zone records, events, lineage block, v0.1 --no-apple
- Document GoblinFoxDragon repo relationship in NORTHSTAR.md: FPS fork (SHANKPIT) vs persistent world fork (GoblinFoxDragon), intended merge at WorldBackend seam --no-apple
- Milestone 3: WorldBackend Go interface + StaticBackend; portal travel wired through backend in apps2/server-go/main.go; spec at docs2/specs/WORLD_BACKEND_INTERFACE_SPEC.md; 3 new tests --no-apple
- True lobby + CTF matchmaker. Server: connecting clients placed in DM warm-up (SCENE_DUST_COMPOUND=3) immediately; mode byte from PacketConnect (buf[12]) parsed and stored in clientInfo.requestedMode. matchmaker struct tracks ctfQueue; when ≥2 CTF players queued → match fires, all moved to SCENE_STADIUM=1 via PacketSceneChange with gameMode byte. sendWelcome extended 12→13 bytes (payload[9]=sceneID, payload[12]=gameMode). sendSceneChange extended 14→15 bytes (payload[14]=gameMode). GameMode constants added to packages2/common. Client: LOBBY_JOIN now sends MODE_CTF (label "FIND CTF"); PacketSceneChange handler in net_tick applies new scene + game_mode + player spawn position; animated "SEARCHING FOR CTF..." HUD banner shown while in DM warm-up; live CTF score header (BLUE N — N RED) shown once matched into MODE_CTF.

### 2026-06-12 (continued 2)
- Implemented voxel chunk rendering in C client (THE_BRIDGE_SPEC.md). Resolved `PACKET_DISCONNECT=4` / `PacketVoxelData=4` type ID conflict by renaming DISCONNECT to `PACKET_VOXEL_DATA=4` in `packages/common/protocol.h` (DISCONNECT was never handled by the client). Added `PACKET_IMPACT=5`, `PACKET_SCENE_CHANGE=6`, `VoxelChunkCache` typedef, and voxel constants to protocol.h. Added 32-slot `g_voxel_chunks` cache, `net_apply_voxel_packet()` parser (parses chunk_x/z + block array from 16-byte header + 6-byte blocks), `PACKET_VOXEL_DATA` handler in `net_tick()`, and `draw_voxel_chunks()` renderer (retro brutalist style: GL_QUADS body + GL_LINE_LOOP neon wireframe; logs=brown/amber, leaves=forest green). Render call inserted after `draw_map()` in draw_scene. Go server builds clean, packet type IDs now match across C and Go.

### 2026-06-12 (continued)
- Fixed buggy jitter: added velocity-based extrapolation for all buggies between server snapshots. Added `snap_x/y/z`, `snap_vx/vy/vz`, `snap_yaw`, `snap_yaw_rate_dps`, `snap_recv_ms` to `BuggyState` (local only, not in `NetBuggy`). On each snapshot receive, these are set as an anchor. `buggy_advance_remote_positions()` is called every game tick and extrapolates position + yaw from the anchor, capped at 150 ms. Prevents the hard-position-snap that caused visible stutter at ~32 ms snapshot intervals.
- Fixed voxworld FPS drop: `draw_voxworld_grass_overlay` radius reduced from 900 to 600 units (~55% fewer candidate positions). Added early distance-squared reject before any terrain query to avoid sqrtf on grid cells outside the circle. Smoothstep fade adjusted to match new radius.
- Go server snapshot rate increased from 20 Hz (50 ms) to ~30 Hz (33 ms) to match the C server's `SERVER_SNAPSHOT_INTERVAL_TICKS=2` at 60 Hz. Added comment clarifying the 250 ms `SetReadDeadline` is a read poll timeout, not the tick rate.

### 2026-06-12
- Verified Dragonfly Go backend (apps2/server-go) builds clean and serves UDP on :6969. PacketConnect → PacketWelcome handshake confirmed. VoxelData streaming, portal travel (BTN_USE → system.PortalTriggered → ResolvePortalDestination → sendSceneChange), and 20Hz per-scene snapshot broadcast all operational. Milestones 1 and 2 (portal travel + per-player scene isolation + cross-scene attack guard) confirmed complete per docs2/NORTHSTAR.md.
- Architecture memo filed to Emily Prime (EMILY/signals/tasks/) requesting: WorldBackend Go interface spec, GoblinFoxDragon repo relationship clarification, season lineage snapshot schema, and Construct expansion plan. Emily to doc-first before any Milestone 3 implementation.

### 2026-06-09
- Cross-scene attack guard: `gameWorld` replaces the `world{}` stub in `apps2/server-go`. `RayTrace` iterates same-scene clients only (skips clients with a different `sceneID`), using ray-sphere intersection (hitbox radius 0.4, sphere centered at chest height). Shooter is excluded. Per-shot `shankPlayer` now uses real client position and `sceneID` from `clientInfo`.
- Added `Vec3.Sub`, `Vec3.Len`, `Vec3.Dot` to `server/system/ballistics.go` for ray-sphere math.
- Tests: `TestCrossSceneAttackGuard`, `TestCrossSceneNoHit`, `TestShooterDoesNotHitSelf`.

### 2026-06-06 (continued 5)
- Fixed death camera blend not resetting between sessions: `death_cam_blend` is now cleared on spawn transition sync (network) and on lobby→game transition, preventing a tilted death-camera perspective at the start of a new game after dying in the previous one.
- Fixed scene transition not clearing buggies: `client_apply_scene_id` now zeroes buggy slots (was already doing helis and projectiles).

### 2026-06-06 (continued 4)
- Fixed `net_cmd_seq` type mismatch: changed from `int` to `unsigned int` to match `cmd.sequence` (unsigned) and prevent sign-extension on assignment.
- Fixed `CLIENT_RECON_HISTORY` in `docs2/CLIENT_PREDICTION_SPEC.md`: was documented as 64, actual value is 256.

### 2026-06-06 (continued 3)
- Fixed helicopter and buggy snapshot count mismatch: same two-pass fix applied — `heli_count` and `buggy_count` bytes are now written after serialization. Buffer enlarged to exact worst-case: `MAX_CLIENTS * sizeof(NetPlayer) + MAX_HELICOPTERS * sizeof(NetHelicopter) + MAX_BUGGIES * sizeof(NetBuggy) + headers`.

### 2026-06-06 (continued 2)
- Fixed snapshot serialization count mismatch: `server_broadcast` now writes the count byte *after* serializing players so the declared count always matches the actual bytes written. The old pre-counted value could exceed the actual serialized entities when the buffer was full, causing the client to reject the entire snapshot. Buffer resized to `MAX_CLIENTS * sizeof(NetPlayer) + headroom` so the guard is no longer needed.
- Fixed remote player interpolation cross-scene blending: interp buffer is now flushed when a snapshot shows a player's `scene_id` changed (portal travel), preventing brief teleport-smearing between two different map locations.

### 2026-06-06 (continued)
- Fixed DNS stall on JOIN: `net_connect()` now tries `inet_pton` first (no DNS call for IP addresses) and caches the resolved address so repeated JOIN attempts don't re-resolve.
- Fixed orphaned-projectile teamfire bug: lingering projectiles from disconnected players (zeroed slot, team_id=0) could incorrectly block damage to team-0 players; owner pointer is now nulled when the owner slot is inactive.
- Fixed bot targeting across scenes: bot AI now excludes players in a different scene_id from the target candidate list, preventing bots from rotating/shooting at invisible targets.
- Fixed projectile lower-bounds escape: added `y < -100` floor check in `update_projectiles` (was only checking the upper bound `y > 2000`).
- Removed dead code: `ctf_schedule_respawn` (its logic was already inlined in `apply_projectile_damage`).
- Cleaned up server compile warnings: `(void)` casts for log-macro-gated variables (`avg_ents`, `newest_seq`, `dt_ms`); removed unused `mode_before`.
- Updated window title from stale "BUILD 181 - CTF RELOADED" to "SHANKPIT".

### 2026-06-06
- Added ESC pause menu: RESUME / QUIT TO LOBBY items with keyboard navigation (UP/DOWN/W/S, ENTER); ESC no longer immediately exits to lobby.
- Server disconnect detection: client times out after 5 s of no snapshots, shows red overlay ("SERVER DISCONNECTED") for 2.5 s, then auto-returns to lobby; tick loop breaks cleanly on state change.
- Fixed remote player interpolation: `INTERP_DELAY_MS` corrected to 24 ms (was 220 ms), server snapshot interval reduced to 32 ms (was 48 ms). Old values caused extrapolation (t < 0) instead of smooth interpolation.
- Fixed reconcile correction runaway: correction vector capped at 1.2 units per frame (`RECONCILE_CORR_MAX`).
- Fixed terrain downslope jitter: step-down snap of 0.55 units in `resolve_collision()` keeps players glued to downslopes.
- Fixed frame accumulator tick-spike on game entry: accumulator now resets to 0 when transitioning from lobby to game, preventing dozens of physics ticks in the first game frame.
- Fixed pause input leak: weapon hotkeys (1-6) and sniper RMB zoom no longer fire while paused.
- Fixed `net_local_pid` stale value: explicitly reset to -1 in `reset_client_render_state_for_net()`.
- Hit detection printf gated behind `PHYS_COMBAT_LOG=0` macro (was printing on every bullet impact).
- Reduced client/server log noise: `NET_VERBOSE_LOG`, `NET_LOG_SNAPSHOT`, `NET_LOG_USERCMD`, `NET_LOG_HANDSHAKE` default to 0; only `NET_LOG_TIMEOUT` stays on.
- Server: timeout sweep now computes `now_seconds()` once per sweep; idle status log throttled when no clients connected.
- Removed stale `.bak` files.
- Added `docs2/NORTHSTAR.md`: system northstar covering FPS core, scene correctness, DragonsNShit backend, season lineage, and documentation-powered RSI.
- Added `docs2/CLIENT_PREDICTION_SPEC.md`: documents the two-snapshot interpolator math, prediction + reconcile algorithm, and all tuning constants.

### 2026-06-04
- Added `server/system/portal.go`: Go portal system with scene constants, `ScenePortals`, `PortalTriggered`, and `ResolvePortalDestination` — mirrors C physics.h portal geometry exactly. Covers all 11 portal routes across Garage Osaka, Stadium, Voxworld, Dust Compound, Oil Tanker, and Poo Poo Island.
- Added `server/system/portal_test.go`: 10 tests covering center/edge/boundary triggering, per-scene routing, end-to-end travel, and negative cases. All green in CI via `go test ./...`.
- Wired `BTN_USE` portal travel into `apps2/server-go/main.go`: tracks per-client `sceneID`, `pos`, and `portalCooldownUntil`; resolves destination on USE press in portal radius; logs `[PORTAL]` travel events. Added two `main_test.go` tests for travel state and cooldown behavior.

### 2026-05-29
- Fixed helicopter strafe-right being completely non-functional: `client_create_cmd` now accepts a `bike` param and sets `BTN_VEHICLE_2`; Q is correctly bound to strafe-right in helicopter mode (was incorrectly wired to the unused `BTN_RELOAD`); `local_update` now sets `p0->in_bike` so offline play is also fixed.

### 2026-04-26
- Fixed buggy camera yaw decoupling from body steering and added steering catch-up behavior for tighter vehicle control feel (`cf9a35d`, merged in #273).
- Replaced STORY mode with a Voxworld breach titan boss prototype to refocus the mode direction (`7a772f7`, merged in #271).

### 2026-04-25
- Repaired merge-conflict damage in `lobby/main.c` skin rendering to restore clean builds (`dcc4885`, merged in #270).
- Added the **EMIREE** playable skin with palette-zone and glow-accent rendering updates (`bb3871e`, merged in #263).
- Improved articulated run posing for the bat skin and weapon-arm mounting for cleaner third-person motion (`e8d5e7f`, merged in #265).
- Removed in-game scene label and portal-list overlay clutter for a cleaner gameplay HUD (`c6af459`, merged in #262).
- Updated the Ability 1 HUD into a square tile format and tuned cooldown/icon label styling (`8c18e67`, `a484796`, merged in #261).
- Removed gameplay HUD debug readouts from the in-game UI (`db9e4b6`, merged in #260).

### 2026-04-22
- Tuned live weapon balance by adjusting magnum capacity and sniper fire rate (`8e8b86b`, merged in #259).

### 2026-04-21
- Added a STORY mode vertical-slice flow with intro and cave encounter content (`3b522ad`, merged in #253).

### 2026-04-20
- Refactored first-person magnum viewmodel massing for improved weapon framing (`5132f3e`, merged in #243).

### 2026-04-18
- Added a deterministic night starfield layer to the retro sky system for richer low-light atmosphere while preserving visual reproducibility (`e0e441b`, merged in #235).
- Refactored the buggy flow into a persistent terrain-aligned vehicle entity to improve movement coherence and world integration (`27af898`, merged in #234).
- Added an articulated character animation pass with procedurally posed bat-skin limbs for more expressive third-person running motion (`54ca0f8`, merged in #227).
- Added voxel grass tufts and swath shading over Voxworld terrain to increase scene depth and ground-detail readability (`e12eed8`, `4556a03`, merged in #225).
- Retuned buggy acceleration behavior and corrected a vehicle friction regression for more realistic driving feel (`62ef86e`, `e1adde6`, merged in #224).
- Added a dedicated CTFB flag-carry first-person viewmodel and reused the knife melee envelope while carrying to improve objective readability in combat (`282d0f2`, merged in #217).
- Refined death-animation pacing with a short pre-fall hold and a subtle post-impact dead-state twist to make eliminations read more naturally (`9a51c17`, `54892f1`, merged in #220, #223).
- Upgraded the stadium rally environment with a large tiered coliseum perimeter to strengthen scene scale and spectator-arena framing (`744b602`, merged in #218).
- Introduced a first-pass graphics transition initiative with stabilized world-light evaluation, world-light rig integration for terrain, and broader weapon visual upgrades for a more cohesive rendering pipeline (`6b48b81`, `4ee8159`, `656ea2e`, `906a2b2`, merged in #194, #197, #201, #203).
- Added a simple death-animation feature, then tuned gun-hit death-state impulse and direction so knockback reads more clearly during combat (`59f609a`, `988f9d5`, `323fd49`, merged in #205, #210).
- Expanded the stadium into a terrain rally-loop layout to improve traversal space and scene variety (`b3c5bca`, `315c9d7`, merged in #208).
- Increased retro atmospheric fog/haze through scene-aware lighting presets for stronger mood consistency (`ab8d7c4`, `b6eb274`, merged in #207).
- Iterated knife presentation and stab behavior across multiple fixes, ending on a simplified straight-thrust attack with sharpened geometry and corrected motion polarity (`4f0be59`, `a86f7d2`, `a912b5c`, `d6bf2f2`, `d3d30a6`, merged in #206, #213).
- Fixed CTF base pedestal visuals and adjusted flag home anchoring for clearer capture-point presentation (`da1aa2a`, `00b7cf3`, merged in #212).

### 2026-04-17
- Added a new skin batch to the lobby customization lineup (`dde86dd`, merged in #191).
- Implemented CTFB respawn-timer flow and fixed flag-drop behavior for more reliable objective-state handling (`6768fc3`, merged in #188).
- Added a new sky rendering system with material abstraction support to expand scene rendering flexibility (`a432844`, merged in #186).
- Added verbose multiplayer join-path diagnostics to improve troubleshooting for connection issues (`13df908`, merged in #190).
- Updated the changelog with then-current merged work to keep release notes in sync (`d57168b`, merged in #185).

### 2026-04-16
- Added authoritative **CTFB** mode with shared flag state replication and bot objective handling for online capture-the-flag battles (`a7785a5`, merged in #182).
- Tuned first-person weapon viewmodel art direction and applied an additional global scale reduction pass for cleaner weapon framing (`41069c5`, `cfbcc8a`, merged in #176).
- Added and refined a new selectable **pink** skin into a samurai space-marine inspired presentation (`387a37f`, `549e301`, merged in #173).
- Added a standalone **Poo Poo Island** scene with a dedicated garage portal to broaden map rotation variety (`5f044fe`, merged in #178).
- Scaled down weapon model sizing and proportions by roughly ten percent for better visual balance (`f30cfb7`, merged in #180).
- Added random **TDMB** map selection with scene-specific team spawn support to improve match variety (`9e29d9f`, merged in #172).

### 2026-04-15
- Renamed the Battle Bots menu label to **TRAIN** for clearer mode intent (`bb37503`, merged in #170).
- Added **TDMO** online team deathmatch mode with server-side bot fill and team snapshot handling to expand online match options (`760798a`, merged in #169).
- Added bottom-right HUD ammo bars in the lobby/game HUD pass for clearer at-a-glance ammo tracking (`dba280d`, merged in #168).
- Polished the lobby menu into a two-column, launch-ready layout for improved readability and navigation (`5e38d39`, merged in #167).
- Converted TAB scoring for TDM/TDMB into a truly team-based scoreboard presentation (`ed071a4`, merged in #164).
- Added hard-coded **TDMB** mode flow and server-authoritative team rules while removing the earlier evolution-mode path (`deb1d91`, merged in #163).
- Fixed skin chooser behavior by pinning the BACK row for correct scrolling and corrected a malformed lobby click block that caused build failures (`60cb38b`, `ca83a38`, merged in #161).
- Polished TAB scoreboard readability with improved row layout and slightly increased row spacing for easier scanning during matches (`76f1945`, `33e783a`, merged in #159).

### 2026-04-14
- Expanded lobby customization with two new selectable character skins, **Genie** and **Wanderer** (`64cff5a`, merged in #158).
- Renamed the previous POC skin option to **BILL** for clearer in-game skin naming (`c0d2098`, merged in #156).

### 2026-04-13
- Added **pirate** and **ninja** character skins to the lobby skin lineup (`06300d0`, merged in #154).

### 2026-04-12
- Added the **CYBORG** player skin and integrated it into the skin selector (`ab38202`, merged in #135).
- Added a deterministic Voxworld bush foliage pass to improve scene dressing consistency (`127bdde`, merged in #136).

### 2026-04-12
- Added Voxworld helicopter gameplay support, including scene-aware network replication and rooftop spawn pads with stair access for better traversal and vehicle flow (`d26454f`, `a9f5a30`, merged in #132).
- Added a VS0 skin selector with Bat/Mayrice rendering options to expand in-game visual customization (`072e4ce`, #130).
- Added a new oil tanker scene with a garage portal, deathmatch rotation updates, and a tab scoreboard to improve map variety and match usability (`396b1cd`, #128).

### 2026-04-11
- Added a VS0 art-direction toggle and calmer world render pass for alternate visual presentation (`2daba9a`, #126).
- Added a dust-compound infantry map with a dedicated garage portal (`ba1915d`, #124).
- Reworked Voxworld into a large canyon vehicle battleground, then expanded it with a first-pass heightfield terrain system and direct garage portal (`789fc58`, `112fab7`, `0728f8e`, #121, #119).
- Fixed CI terrain-module compilation linkage while integrating terrain changes (`c650c74`).

### 2026-04-06
- Added a first-pass authoritative helicopter vehicle implementation (`3dcae0a`, #117).

### 2026-03-31 to 2026-03-30
- Added a katana weapon with blade-dash ability and fixed katana-model draw-box forward declaration issues (`0136375`, `110acb2`, #114).

### 2026-03-23
- Fixed Windows lobby build cross-link failures for `proc_tex` (`a47756b`, #112).

### 2026-03-01
- Refreshed buggy fixed-pipeline vehicle styling with procedural visual accents (`3e5ee0e`, #111).

### 2026-02-21 to 2026-02-20
- Added CI workflow support for Dragonfly Construct builds (`2c1d0f9`, #110).
- Improved multiplayer correctness and feel across multiple iterations: spawn synchronization, jitter diagnostics, parity harness hardening, unified simulation/movement paths, camera-relative movement, and stale-player cleanup (`2fddae0`, `4ff7fca`, `471cec3`, `4dd03e4`, `1376350`, `75b3429`, plus merged PRs #109, #107, #106, #105, #104, #103).
- Added procedural title-screen background texture work (`bf852d2`, #108).
- Added a definitive client-server netcode contract spec and follow-up yaw/input convention fixes (`852caf5`, `9d1af28`, #101, #102, #100).
