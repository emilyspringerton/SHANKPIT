# GEMINI.md — Guidance for Gemini / Antigravity in SHANKPIT

## What This Is

SHANKPIT is a server-authoritative fast-paced FPS with a persistent world backend.
- The FPS game client and server run on C / SDL2 / OpenGL.
- The persistent world backend (DragonsNShit) runs on Dragonfly (Minecraft Bedrock Protocol fork, Go).
- Both share entity state, season lineage, and world events.

See `docs2/NORTHSTAR.md` for current system milestones and design directions.

## Build and Test

### Bazel (C Client & Server)
The repo uses Bazel 9.2.0 (managed via `bazelisk`):

```bash
# Build all Bazel targets (lobby client, server, serverctl, packages)
bazel build //...

# Build specific binaries
bazel build //apps/lobby:shank_lobby
bazel build //apps/server:shank_server
bazel build //apps/server:serverctl
```

Required system dependencies (Linux):
- `libsdl2-dev`
- `libgl1-mesa-dev`
- `libglu1-mesa-dev`
- `libncurses-dev`

### Go Backend (Dragonfly)
```bash
# Build Go matchmaker / scene server
go build ./apps2/server-go/

# Run Go tests
go test ./...
```

## Key Architectural Concepts

- **Season Lineage**: World history carries forward across resets; ancient civilizations leave ruins.
- **Zone Evolution**: Dynamic zones change state based on player activity and time.
- **Portal System**: `server/system/portal.go` routes between scenes and world zones.
- **Bridge Protocol**: `docs2/specs/THE_BRIDGE_SPEC.md` provides voxel data via `PACKET_VOXEL_DATA`.

## Operating Protocols (The Emily Way)

1. **Founder Direction**: Route founder real-time instructions through `emily observe -s info "Founder real-time: <summary>"` first, then curate into `EMILY/BACKLOG.md`.
2. **Apples**: File a completion Apple upon finishing a meaningful task:
   ```bash
   emily apples post -t completion -repo SHANKPIT "<description>"
   ```
3. **Level Registry as Living Docs**: When verifying any level-editor feature (doors, widgets, materials, characters), register real levels in the IDUNA NOCK registry (`shankpit_levels`/`shankpit_widgets`), not scratch files.
4. **README Reality (SAGA Reconciliation)**: If a change substantially updates capabilities, update `README.md` in the same unit of work.
5. **CHANGELOG**: Add a dated entry to `CHANGELOG.md`.
6. **Commit & Push Protocol**:
   - Commit and push immediately when work is verified — do not wait to be asked.
   - Commit messages must end with a blank line followed by the active session tag:
     ```
     session: <tag>
     ```
     (retrieve via `emily session current`).
   - Push to `master`: `git push origin master`.
