package system

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// DragonflyBackend implements WorldBackend by fetching chunk data from the
// GoblinFoxDragon world API. When the GFD server is unavailable, every method
// falls back to StaticBackend behaviour (nil voxel payload → procedural generator).
//
// Wire it in apps2/server-go/main.go:
//
//	var backend system.WorldBackend = &system.DragonflyBackend{
//	    APIURL: "http://localhost:7070",
//	}
type DragonflyBackend struct {
	APIURL  string        // e.g. "http://localhost:7070"
	Timeout time.Duration // default 100ms

	static StaticBackend
	client *http.Client
}

func (d *DragonflyBackend) httpClient() *http.Client {
	if d.client == nil {
		t := d.Timeout
		if t == 0 {
			t = 100 * time.Millisecond
		}
		d.client = &http.Client{Timeout: t}
	}
	return d.client
}

func (d *DragonflyBackend) ResolvePortalDestination(scene, portal, slot int) (PortalDestination, bool) {
	return d.static.ResolvePortalDestination(scene, portal, slot)
}

func (d *DragonflyBackend) OnPlayerEnterScene(slot, scene int) {
	d.static.OnPlayerEnterScene(slot, scene)
}

func (d *DragonflyBackend) OnPlayerLeaveScene(slot, scene int) {
	d.static.OnPlayerLeaveScene(slot, scene)
}

// SceneVoxelPayload fetches chunk (chunkX, chunkZ) in sceneID from the GFD world API.
// Returns nil on any error so the caller falls back to the procedural generator.
func (d *DragonflyBackend) SceneVoxelPayload(sceneID, chunkX, chunkZ int) []VoxelBlock {
	if d.APIURL == "" {
		return nil
	}
	url := fmt.Sprintf("%s/chunks?scene=%d&cx=%d&cz=%d", d.APIURL, sceneID, chunkX, chunkZ)
	resp, err := d.httpClient().Get(url)
	if err != nil {
		return nil
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil
	}
	var blocks []VoxelBlock
	if err := json.NewDecoder(resp.Body).Decode(&blocks); err != nil {
		return nil
	}
	return blocks
}
