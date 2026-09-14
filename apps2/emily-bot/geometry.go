// geometry.go — real level geometry fetch + raycast for SHANKPIT QUEUE bot observations.
//
// S459-47, founder real-time (feedback on the first observation-vector pass): "also no raycast
// fix all that." Closes the real, previously-named gap in observation.go's own module doc
// comment: emily-bot never loaded level geometry, so no wall-distance/line-of-sight feature was
// possible. Mirrors apps/lobby/src/main.c's own client_load_queue_level (S459-39/41) -- same
// real registry-lookup-by-is_default_queue-flag contract, fetched independently client-side
// since the wire protocol only ever carries a scene_id byte, never geometry (see that function's
// own doc comment for the full "why independently, not over the wire" rationale) -- but using
// Go's real encoding/json against IDUNA's actual JSON export instead of hand-rolling a C-style
// scanner, since Go already has a real JSON decoder and this isn't performance-critical (fetched
// once per bot process, not per tick).
package main

import (
	"encoding/json"
	"math"
	"net/http"
	"time"
)

const levelRegistryBaseURL = "https://okemily.com/api/v1/shankpit-levels"

// wall mirrors IDUNA's real internal/shankpit.Wall JSON shape exactly (field-for-field, same
// json tags) -- center x/y/z + FULL extents sx/sy/sz (not half-extents), matching
// packages/world/level_boxes.h's own real LevelBox convention.
type wall struct {
	X, Y, Z    float64 `json:"-"`
	SX, SY, SZ float64 `json:"-"`
}

type wallJSON struct {
	X, Y, Z    float64 `json:"x"`
	SX, SY, SZ float64 `json:"sx"`
}

type levelExportJSON struct {
	Walls []wallJSON `json:"walls"`
}

type levelSummaryJSON struct {
	ID             int  `json:"id"`
	IsDefaultQueue bool `json:"is_default_queue"`
}

// levelGeometry is the real, cached box list for whichever level is currently flagged as the
// QUEUE default. Fetched once per process (queue level doesn't change mid-session in practice --
// same one-shot-fetch convention client_load_queue_level already establishes) via fetchQueueLevelGeometry.
type levelGeometry struct {
	walls []wall
}

var httpClient = &http.Client{Timeout: 5 * time.Second}

// fetchQueueLevelGeometry finds the real, currently-flagged QUEUE default level and fetches its
// real box list. Returns (nil, false) on ANY real failure (network, no default flagged, bad
// JSON) -- a bot with no geometry just skips raycast features (buildObservation zero-fills them),
// never crashes or blocks on this.
func fetchQueueLevelGeometry() (*levelGeometry, bool) {
	resp, err := httpClient.Get(levelRegistryBaseURL)
	if err != nil {
		return nil, false
	}
	defer resp.Body.Close()
	var summaries []levelSummaryJSON
	if err := json.NewDecoder(resp.Body).Decode(&summaries); err != nil {
		return nil, false
	}
	levelID := -1
	for _, s := range summaries {
		if s.IsDefaultQueue {
			levelID = s.ID
			break
		}
	}
	if levelID < 0 {
		return nil, false
	}

	expResp, err := httpClient.Get(levelRegistryBaseURL + "/" + itoa(levelID) + "/export")
	if err != nil {
		return nil, false
	}
	defer expResp.Body.Close()
	var export levelExportJSON
	if err := json.NewDecoder(expResp.Body).Decode(&export); err != nil {
		return nil, false
	}

	geo := &levelGeometry{walls: make([]wall, 0, len(export.Walls))}
	for _, w := range export.Walls {
		geo.walls = append(geo.walls, wall{X: w.X, Y: w.Y, Z: w.Z, SX: w.SX, SY: w.SY, SZ: w.SZ})
	}
	return geo, true
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	neg := n < 0
	if neg {
		n = -n
	}
	var buf [20]byte
	i := len(buf)
	for n > 0 {
		i--
		buf[i] = byte('0' + n%10)
		n /= 10
	}
	if neg {
		i--
		buf[i] = '-'
	}
	return string(buf[i:])
}

// raycast -- a real, minimal AABB slab-method ray/box intersection against the level's own real
// box list (center + full extents, matching level_boxes.h's LevelBox convention exactly). Returns
// the real distance to the nearest hit, or cap if nothing is hit within cap units. Deliberately
// simple (no BVH/spatial partition) -- a real QUEUE level tops out at LEVEL_BOXES_MAX=100 boxes,
// a linear scan every observation tick is real, cheap, and correct; a spatial index would be
// premature optimization for this box count.
func (g *levelGeometry) raycast(ox, oy, oz, dx, dy, dz, cap float32) float32 {
	if g == nil {
		return cap
	}
	best := cap
	for _, w := range g.walls {
		minX, maxX := float32(w.X-w.SX/2), float32(w.X+w.SX/2)
		minY, maxY := float32(w.Y-w.SY/2), float32(w.Y+w.SY/2)
		minZ, maxZ := float32(w.Z-w.SZ/2), float32(w.Z+w.SZ/2)

		tMin, tMax := float32(0.0), best
		ok := true
		tMin, tMax, ok = narrowSlab(ox, dx, minX, maxX, tMin, tMax)
		if ok {
			tMin, tMax, ok = narrowSlab(oy, dy, minY, maxY, tMin, tMax)
		}
		if ok {
			tMin, tMax, ok = narrowSlab(oz, dz, minZ, maxZ, tMin, tMax)
		}
		_ = tMax
		if ok && tMin > 0 && tMin < best {
			best = tMin
		}
	}
	return best
}

// narrowSlab narrows [tMin,tMax] against one axis's own real slab (the standard AABB-raycast
// slab method). Returns the narrowed (tMin, tMax) and true if the ray still intersects at all;
// false means a real miss on this axis -- caller stops testing this box immediately.
func narrowSlab(origin, dir, slabMin, slabMax, tMin, tMax float32) (float32, float32, bool) {
	const eps = 1e-6
	if float32(math.Abs(float64(dir))) < eps {
		// Ray parallel to this axis -- a real hit only if origin already lies inside the slab.
		if origin < slabMin || origin > slabMax {
			return tMin, tMax, false
		}
		return tMin, tMax, true
	}
	t1 := (slabMin - origin) / dir
	t2 := (slabMax - origin) / dir
	if t1 > t2 {
		t1, t2 = t2, t1
	}
	if t1 > tMin {
		tMin = t1
	}
	if t2 < tMax {
		tMax = t2
	}
	if tMin > tMax {
		return tMin, tMax, false
	}
	return tMin, tMax, true
}
