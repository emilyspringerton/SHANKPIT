package system

// WorldBackend is the seam between the SHANKPIT game server and the persistent world.
// Portal resolution, scene lifecycle events, and voxel payloads route through this interface.
// Swap StaticBackend for DragonflyBackend (Milestone 4) without touching game-server code.
type WorldBackend interface {
	// ResolvePortalDestination maps (currentScene, portalID, playerSlot) → destination.
	// Returns ok=false if no route exists; portal travel is rejected.
	ResolvePortalDestination(currentScene, portalID, playerSlot int) (PortalDestination, bool)

	// OnPlayerEnterScene notifies the backend when a player enters a scene.
	OnPlayerEnterScene(playerSlot, sceneID int)

	// OnPlayerLeaveScene notifies the backend when a player leaves a scene.
	OnPlayerLeaveScene(playerSlot, sceneID int)

	// SceneVoxelPayload returns a voxel chunk payload for snapshot extension (THE_BRIDGE_SPEC).
	// Returns nil in FPS-only mode; nil is always safe for the snapshot builder.
	SceneVoxelPayload(sceneID int) []byte
}

// StaticBackend delegates to the hardcoded portal routing table in portal.go.
// No persistence, no world-state callbacks. Behavior is identical to pre-Milestone-3.
type StaticBackend struct{}

func (s *StaticBackend) ResolvePortalDestination(scene, portal, slot int) (PortalDestination, bool) {
	return ResolvePortalDestination(scene, portal, slot)
}

func (s *StaticBackend) OnPlayerEnterScene(_, _ int) {}
func (s *StaticBackend) OnPlayerLeaveScene(_, _ int) {}
func (s *StaticBackend) SceneVoxelPayload(_ int) []byte { return nil }
