package system

import (
	"math"
	"testing"
)

func TestScenePortalsNonEmpty(t *testing.T) {
	scenes := []int{SceneGarageOsaka, SceneStadium, SceneVoxworld, SceneDustCompound, SceneOilTanker, ScenePooPooisland}
	for _, s := range scenes {
		if len(ScenePortals(s)) == 0 {
			t.Errorf("scene %d has no portals", s)
		}
	}
}

func TestScenePortalsNoPortalsForCave(t *testing.T) {
	if len(ScenePortals(SceneStoryCave)) != 0 {
		t.Error("story cave should have no portals")
	}
}

func TestPortalTriggeredCenter(t *testing.T) {
	// Garage exit portal is at (0,56) radius 6
	id, ok := PortalTriggered(SceneGarageOsaka, 0, 56)
	if !ok {
		t.Fatal("expected portal triggered at garage exit center")
	}
	if id != PortalIDGarageExit {
		t.Fatalf("expected PortalIDGarageExit, got %d", id)
	}
}

func TestPortalTriggeredEdge(t *testing.T) {
	// Just inside the vox portal radius (6.5) in garage
	id, ok := PortalTriggered(SceneGarageOsaka, 48+6.4, 0)
	if !ok {
		t.Fatal("expected trigger at edge of vox portal")
	}
	if id != PortalIDGarageToVoxworld {
		t.Fatalf("expected PortalIDGarageToVoxworld, got %d", id)
	}
}

func TestPortalTriggeredOutside(t *testing.T) {
	_, ok := PortalTriggered(SceneGarageOsaka, 0, 100)
	if ok {
		t.Fatal("expected no trigger outside all portals")
	}
}

func TestPortalTriggeredRadiusBoundary(t *testing.T) {
	// Exactly at radius (6.0) for garage exit — should trigger (<=)
	id, ok := PortalTriggered(SceneGarageOsaka, 0, 56+6.0)
	if !ok {
		t.Fatal("expected trigger exactly at radius boundary")
	}
	if id != PortalIDGarageExit {
		t.Fatalf("expected PortalIDGarageExit at boundary, got %d", id)
	}

	// Just outside radius — should not trigger
	_, ok = PortalTriggered(SceneGarageOsaka, 0, 56+6.0+0.01)
	if ok {
		t.Fatal("expected no trigger just outside radius")
	}
}

func TestPortalTriggeredWrongScene(t *testing.T) {
	// Garage exit portal position, but queried in Stadium scene — different portal geometry
	_, ok := PortalTriggered(SceneVoxworld, 0, 56)
	if ok {
		t.Fatal("garage portal coords should not trigger in voxworld")
	}
}

func TestPortalTriggeredAllGaragePortals(t *testing.T) {
	garagePortals := []struct {
		x, z float64
		id   int
	}{
		{0, 56, PortalIDGarageExit},
		{48, 0, PortalIDGarageToVoxworld},
		{-48, -12, PortalIDGarageToDust},
		{-48, 0, PortalIDGarageToTanker},
		{52, -24, PortalIDGarageToPooPooisland},
	}
	for _, tc := range garagePortals {
		id, ok := PortalTriggered(SceneGarageOsaka, tc.x, tc.z)
		if !ok {
			t.Errorf("portal %d not triggered at (%.0f,%.0f)", tc.id, tc.x, tc.z)
			continue
		}
		if id != tc.id {
			t.Errorf("at (%.0f,%.0f): expected portal %d, got %d", tc.x, tc.z, tc.id, id)
		}
	}
}

func TestResolvePortalDestinationAllRoutes(t *testing.T) {
	routes := []struct {
		scene, portalID int
		destScene       int
	}{
		{SceneGarageOsaka, PortalIDGarageExit, SceneStadium},
		{SceneStadium, PortalIDGarageExit, SceneGarageOsaka},
		{SceneGarageOsaka, PortalIDGarageToVoxworld, SceneVoxworld},
		{SceneGarageOsaka, PortalIDGarageToDust, SceneDustCompound},
		{SceneGarageOsaka, PortalIDGarageToTanker, SceneOilTanker},
		{SceneGarageOsaka, PortalIDGarageToPooPooisland, ScenePooPooisland},
		{SceneStadium, PortalIDStadiumToVoxworld, SceneVoxworld},
		{SceneVoxworld, PortalIDVoxworldToStadium, SceneStadium},
		{SceneDustCompound, PortalIDDustToGarage, SceneGarageOsaka},
		{SceneOilTanker, PortalIDTankerToGarage, SceneGarageOsaka},
		{ScenePooPooisland, PortalIDPooPooislandToGarage, SceneGarageOsaka},
	}
	for _, r := range routes {
		dest, ok := ResolvePortalDestination(r.scene, r.portalID, 0)
		if !ok {
			t.Errorf("scene=%d portal=%d: no destination resolved", r.scene, r.portalID)
			continue
		}
		if dest.Scene != r.destScene {
			t.Errorf("scene=%d portal=%d: expected dest scene %d, got %d", r.scene, r.portalID, r.destScene, dest.Scene)
		}
		if dest.Y <= 0 {
			t.Errorf("scene=%d portal=%d: destination Y should be above ground, got %f", r.scene, r.portalID, dest.Y)
		}
	}
}

func TestResolvePortalDestinationUnknown(t *testing.T) {
	_, ok := ResolvePortalDestination(SceneStoryCave, PortalIDGarageExit, 0)
	if ok {
		t.Fatal("story cave has no portal destinations")
	}
}

func TestResolvePortalDestinationInvalidID(t *testing.T) {
	_, ok := ResolvePortalDestination(SceneGarageOsaka, 99, 0)
	if ok {
		t.Fatal("invalid portal ID should not resolve")
	}
}

func TestPortalTriggeredThenResolve(t *testing.T) {
	// End-to-end: player in garage steps onto vox portal → arrives in voxworld
	id, ok := PortalTriggered(SceneGarageOsaka, 48, 0)
	if !ok || id != PortalIDGarageToVoxworld {
		t.Fatal("garage→vox portal not triggered")
	}
	dest, ok := ResolvePortalDestination(SceneGarageOsaka, id, 1)
	if !ok {
		t.Fatal("destination not resolved")
	}
	if dest.Scene != SceneVoxworld {
		t.Fatalf("expected voxworld, got %d", dest.Scene)
	}
	// spawn should be somewhere plausible in voxworld
	if math.Abs(dest.X) > 2000 || math.Abs(dest.Z) > 2000 {
		t.Fatalf("destination out of expected world bounds: %.1f,%.1f,%.1f", dest.X, dest.Y, dest.Z)
	}
}
