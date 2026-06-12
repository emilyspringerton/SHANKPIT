# WorldBackend Interface Spec — Milestone 3

*Written: 2026-06-12 | Status: defining the seam*

---

## Purpose

This spec defines the `WorldBackend` Go interface — the stable seam between the SHANKPIT game
server and the persistent world layer (DragonsNShit / Dragonfly). The interface enables swapping
the backend implementation without touching game-server code.

Before this spec, portal resolution was a static hardcoded function call:
```go
dest, ok := system.ResolvePortalDestination(sceneID, portalID, slot)
```

After Milestone 3: the same call routes through `WorldBackend.ResolvePortalDestination()`. The
initial implementation (`StaticBackend`) preserves current behavior exactly. Future Dragonfly
integration plugs in by implementing the interface.

---

## The Interface

```go
// WorldBackend is the seam between the SHANKPIT game server and the persistent world.
// Implementations:
//   StaticBackend   — hardcoded portal routing, no persistence (current)
//   DragonflyBackend — live Dragonfly persistent world (Milestone 4+)
type WorldBackend interface {
    // ResolvePortalDestination maps (currentScene, portalID, playerSlot) → destination.
    // Returns ok=false if no route exists for this portal (reject travel).
    ResolvePortalDestination(currentScene, portalID, playerSlot int) (PortalDestination, bool)

    // OnPlayerEnterScene is called when a player enters a scene (connect or portal travel).
    // The backend may update persistent world state asynchronously.
    OnPlayerEnterScene(playerSlot, sceneID int)

    // OnPlayerLeaveScene is called when a player leaves a scene (disconnect or portal travel).
    OnPlayerLeaveScene(playerSlot, sceneID int)

    // SceneVoxelPayload returns a voxel chunk payload for the given scene for inclusion
    // in the server snapshot extension (THE_BRIDGE_SPEC.md §3). Returns nil in FPS-only mode.
    SceneVoxelPayload(sceneID int) []byte
}
```

---

## StaticBackend (Milestone 3)

`StaticBackend` is the initial implementation. It delegates portal resolution to the existing
hardcoded routing table in `portal.go` and is a no-op for all world-state callbacks.

```go
type StaticBackend struct{}

func (s *StaticBackend) ResolvePortalDestination(scene, portal, slot int) (PortalDestination, bool) {
    return ResolvePortalDestination(scene, portal, slot) // existing function
}

func (s *StaticBackend) OnPlayerEnterScene(slot, sceneID int) {} // no-op
func (s *StaticBackend) OnPlayerLeaveScene(slot, sceneID int) {} // no-op
func (s *StaticBackend) SceneVoxelPayload(sceneID int) []byte   { return nil }
```

---

## DragonflyBackend (Milestone 4 — not yet implemented)

`DragonflyBackend` will implement the same interface. Key behavioral differences:

| Method | DragonflyBackend behavior |
|--------|--------------------------|
| `ResolvePortalDestination` | May query Dragonfly for dynamic portal routing (e.g., overworld redirect based on zone state) |
| `OnPlayerEnterScene` | Sends `world_backend_player_enter(scene, slot)` to the Dragonfly process |
| `OnPlayerLeaveScene` | Sends `world_backend_player_leave(scene, slot)`, triggers scene persistence flush |
| `SceneVoxelPayload` | Returns compressed chunk data from Dragonfly's chunk store for the scene |

`DragonflyBackend` will live in a separate package (e.g., `server/backend/dragonfly/`) so the
`system` package has no dependency on the Dragonfly fork.

---

## Wire-up in main.go (Milestone 3)

The server creates a `WorldBackend` at startup and uses it for all portal operations:

```go
// main.go
var backend system.WorldBackend = &system.StaticBackend{}

// portal travel:
dest, ok := backend.ResolvePortalDestination(info.sceneID, portalID, int(info.id))
if ok {
    backend.OnPlayerLeaveScene(int(info.id), info.sceneID)
    info.sceneID = dest.Scene
    info.pos = system.Vec3{X: dest.X, Y: dest.Y, Z: dest.Z}
    backend.OnPlayerEnterScene(int(info.id), dest.Scene)
    sendSceneChange(...)
}
```

---

## Invariants

1. **StaticBackend must produce identical behavior to the pre-Milestone-3 direct call.** All
   existing tests must continue to pass without modification.

2. **The interface is the contract.** Game-server code must never call `system.ResolvePortalDestination`
   directly after Milestone 3 — only through `WorldBackend`. This is enforced by the naming: the
   raw function remains package-internal to `StaticBackend` after renaming (see Implementation Notes).

3. **SceneVoxelPayload returning nil is always safe.** The snapshot builder skips the voxel
   extension when `SceneVoxelPayload` returns nil. No client should crash on an absent extension.

4. **World-state callbacks are best-effort.** A backend is never allowed to block portal travel
   on a callback. Slow backends must buffer asynchronously.

---

## Implementation Notes

- `WorldBackend` interface and `StaticBackend` live in `server/system/backend.go`.
- `ResolvePortalDestination` in `portal.go` is NOT renamed or removed — `StaticBackend` calls it.
  The package-level function remains as the authoritative static routing table.
- `main.go` stores `backend` as a `system.WorldBackend` (interface type), not a concrete type.
  This ensures future backends require no main.go type changes.

---

## Testing

- Existing `TestPortalTriggered` and `TestResolvePortalDestination` tests continue to test the
  raw static routing table directly and must pass unchanged.
- New test: `TestStaticBackendMatchesDirectCall` verifies that `StaticBackend.ResolvePortalDestination`
  returns the same result as calling `system.ResolvePortalDestination` directly for all
  (scene, portal) pairs in the routing table.
