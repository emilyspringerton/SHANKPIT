package system

import "math"

// Scene IDs (mirrors packages/common/protocol.h SCENE_* constants).
const (
	SceneGarageOsaka  = 0
	SceneStadium      = 1
	SceneVoxworld     = 2
	SceneDustCompound = 3
	SceneCity         = 4
	SceneOilTanker    = 5
	ScenePooPooisland = 6
	SceneStoryCave    = 7
)

// Portal IDs (mirrors physics.h PORTAL_ID_* constants).
const (
	PortalIDGarageExit           = 0
	PortalIDStadiumToVoxworld    = 1
	PortalIDVoxworldToStadium    = 2
	PortalIDGarageToVoxworld     = 3
	PortalIDGarageToDust         = 4
	PortalIDDustToGarage         = 5
	PortalIDGarageToTanker       = 6
	PortalIDTankerToGarage       = 7
	PortalIDGarageToPooPooisland = 8
	PortalIDPooPooislandToGarage = 9
)

// Portal describes a portal entry-point in a scene.
type Portal struct {
	ID     int
	X, Z   float64
	Radius float64
}

// PortalDestination is where a portal sends the player.
type PortalDestination struct {
	Scene   int
	X, Y, Z float64
}

// ScenePortals returns all portals active in sceneID.
// Positions match the C physics.h GARAGE_*, STADIUM_*, etc. constants exactly.
func ScenePortals(sceneID int) []Portal {
	switch sceneID {
	case SceneGarageOsaka:
		return []Portal{
			{ID: PortalIDGarageExit, X: 0, Z: 56, Radius: 6},
			{ID: PortalIDGarageToVoxworld, X: 48, Z: 0, Radius: 6.5},
			{ID: PortalIDGarageToDust, X: -48, Z: -12, Radius: 6},
			{ID: PortalIDGarageToTanker, X: -48, Z: 0, Radius: 6},
			{ID: PortalIDGarageToPooPooisland, X: 52, Z: -24, Radius: 6.5},
		}
	case SceneStadium:
		return []Portal{
			{ID: PortalIDGarageExit, X: 0, Z: 0, Radius: 16},
			{ID: PortalIDStadiumToVoxworld, X: 406, Z: 0, Radius: 14},
		}
	case SceneVoxworld:
		return []Portal{
			{ID: PortalIDVoxworldToStadium, X: -360, Z: 0, Radius: 16},
		}
	case SceneDustCompound:
		return []Portal{
			{ID: PortalIDDustToGarage, X: -470, Z: -210, Radius: 14},
		}
	case SceneOilTanker:
		return []Portal{
			{ID: PortalIDTankerToGarage, X: 292, Z: 0, Radius: 13},
		}
	case ScenePooPooisland:
		return []Portal{
			{ID: PortalIDPooPooislandToGarage, X: 0, Z: 0, Radius: 16},
		}
	}
	return nil
}

// PortalTriggered returns the portal ID if (x,z) is inside any portal in sceneID.
func PortalTriggered(sceneID int, x, z float64) (portalID int, triggered bool) {
	for _, p := range ScenePortals(sceneID) {
		dx := x - p.X
		dz := z - p.Z
		if math.Sqrt(dx*dx+dz*dz) <= p.Radius {
			return p.ID, true
		}
	}
	return 0, false
}

// ResolvePortalDestination maps (currentScene, portalID) to a destination.
// slot is the player slot (used for scene_spawn_point parity with C; ignored here).
func ResolvePortalDestination(currentScene, portalID, slot int) (PortalDestination, bool) {
	switch {
	case currentScene == SceneGarageOsaka && portalID == PortalIDGarageExit:
		return PortalDestination{Scene: SceneStadium, X: 0, Y: 8, Z: 0}, true
	case currentScene == SceneStadium && portalID == PortalIDGarageExit:
		return PortalDestination{Scene: SceneGarageOsaka, X: 0, Y: 6, Z: 46}, true
	case currentScene == SceneGarageOsaka && portalID == PortalIDGarageToVoxworld:
		return PortalDestination{Scene: SceneVoxworld, X: -420, Y: 8, Z: 180}, true
	case currentScene == SceneGarageOsaka && portalID == PortalIDGarageToDust:
		return PortalDestination{Scene: SceneDustCompound, X: -430, Y: 9, Z: -210}, true
	case currentScene == SceneGarageOsaka && portalID == PortalIDGarageToTanker:
		return PortalDestination{Scene: SceneOilTanker, X: -265, Y: 6, Z: 0}, true
	case currentScene == SceneGarageOsaka && portalID == PortalIDGarageToPooPooisland:
		return PortalDestination{Scene: ScenePooPooisland, X: 0, Y: 10, Z: 0}, true
	case currentScene == SceneStadium && portalID == PortalIDStadiumToVoxworld:
		return PortalDestination{Scene: SceneVoxworld, X: 386, Y: 8, Z: 0}, true
	case currentScene == SceneVoxworld && portalID == PortalIDVoxworldToStadium:
		return PortalDestination{Scene: SceneStadium, X: 386, Y: 2, Z: 0}, true
	case currentScene == SceneDustCompound && portalID == PortalIDDustToGarage:
		return PortalDestination{Scene: SceneGarageOsaka, X: -38, Y: 6, Z: -12}, true
	case currentScene == SceneOilTanker && portalID == PortalIDTankerToGarage:
		return PortalDestination{Scene: SceneGarageOsaka, X: -38, Y: 6, Z: 0}, true
	case currentScene == ScenePooPooisland && portalID == PortalIDPooPooislandToGarage:
		return PortalDestination{Scene: SceneGarageOsaka, X: 42, Y: 6, Z: -24}, true
	}
	return PortalDestination{}, false
}
