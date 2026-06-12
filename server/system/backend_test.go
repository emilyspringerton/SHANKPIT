package system

import "testing"

// TestStaticBackendMatchesDirectCall verifies StaticBackend is a transparent wrapper:
// every (scene, portal) pair returns the same result as calling ResolvePortalDestination directly.
func TestStaticBackendMatchesDirectCall(t *testing.T) {
	backend := &StaticBackend{}

	routes := []struct {
		scene, portalID int
	}{
		{SceneGarageOsaka, PortalIDGarageExit},
		{SceneStadium, PortalIDGarageExit},
		{SceneGarageOsaka, PortalIDGarageToVoxworld},
		{SceneGarageOsaka, PortalIDGarageToDust},
		{SceneGarageOsaka, PortalIDGarageToTanker},
		{SceneGarageOsaka, PortalIDGarageToPooPooisland},
		{SceneStadium, PortalIDStadiumToVoxworld},
		{SceneVoxworld, PortalIDVoxworldToStadium},
		{SceneDustCompound, PortalIDDustToGarage},
		{SceneOilTanker, PortalIDTankerToGarage},
		{ScenePooPooisland, PortalIDPooPooislandToGarage},
		{SceneStoryCave, PortalIDGarageExit}, // unknown — should be false
		{SceneGarageOsaka, 99},               // invalid portal ID — should be false
	}

	for _, r := range routes {
		wantDest, wantOK := ResolvePortalDestination(r.scene, r.portalID, 0)
		gotDest, gotOK := backend.ResolvePortalDestination(r.scene, r.portalID, 0)
		if gotOK != wantOK {
			t.Errorf("scene=%d portal=%d: ok mismatch: want %v got %v", r.scene, r.portalID, wantOK, gotOK)
			continue
		}
		if gotOK && gotDest != wantDest {
			t.Errorf("scene=%d portal=%d: dest mismatch: want %+v got %+v", r.scene, r.portalID, wantDest, gotDest)
		}
	}
}

// TestStaticBackendVoxelPayloadNil verifies FPS-only mode returns nil payload.
func TestStaticBackendVoxelPayloadNil(t *testing.T) {
	backend := &StaticBackend{}
	for _, sceneID := range []int{SceneGarageOsaka, SceneStadium, SceneVoxworld} {
		if p := backend.SceneVoxelPayload(sceneID); p != nil {
			t.Errorf("scene=%d: expected nil voxel payload, got %d bytes", sceneID, len(p))
		}
	}
}

// TestStaticBackendImplementsInterface is a compile-time assertion.
var _ WorldBackend = (*StaticBackend)(nil)
