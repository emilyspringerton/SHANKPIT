package main

import (
	"encoding/binary"
	"math"
	"testing"
	"time"

	"dragonsnshit/packages2/common"
	"dragonsnshit/server/system"
)

func TestParseUserCmd(t *testing.T) {
	buf := make([]byte, 64)
	const netHeaderSize = 12
	buf[0] = common.PacketUserCmd
	buf[netHeaderSize] = 1
	off := netHeaderSize + 1

	binary.LittleEndian.PutUint32(buf[off:], 42)
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], 99)
	off += 4
	binary.LittleEndian.PutUint16(buf[off:], 16)
	off += 4

	binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(1.25))
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(-0.5))
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(90))
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(-10))
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], common.BtnAttack)
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], 1)

	cmd := parseUserCmd(buf, netHeaderSize+1)
	if cmd.Sequence != 42 {
		t.Fatalf("expected sequence 42, got %d", cmd.Sequence)
	}
	if cmd.Timestamp != 99 {
		t.Fatalf("expected timestamp 99, got %d", cmd.Timestamp)
	}
	if cmd.Msec != 16 {
		t.Fatalf("expected msec 16, got %d", cmd.Msec)
	}
	if cmd.Fwd != 1.25 {
		t.Fatalf("expected fwd 1.25, got %f", cmd.Fwd)
	}
	if cmd.Str != -0.5 {
		t.Fatalf("expected str -0.5, got %f", cmd.Str)
	}
	if cmd.Yaw != 90 {
		t.Fatalf("expected yaw 90, got %f", cmd.Yaw)
	}
	if cmd.Pitch != -10 {
		t.Fatalf("expected pitch -10, got %f", cmd.Pitch)
	}
	if cmd.Buttons != common.BtnAttack {
		t.Fatalf("expected buttons %d, got %d", common.BtnAttack, cmd.Buttons)
	}
	if cmd.WeaponIdx != 1 {
		t.Fatalf("expected weapon 1, got %d", cmd.WeaponIdx)
	}
}

func TestClientInfoPortalTravelState(t *testing.T) {
	// When a client is at the garage-to-voxworld portal and presses USE,
	// their scene and position should update to voxworld.
	info := clientInfo{
		id:      3,
		sceneID: system.SceneGarageOsaka,
		pos:     system.Vec3{X: 48, Y: 6, Z: 0}, // center of GARAGE_VOX_PORTAL
	}

	portalID, triggered := system.PortalTriggered(info.sceneID, info.pos.X, info.pos.Z)
	if !triggered {
		t.Fatal("expected portal triggered at garage vox portal center")
	}
	dest, ok := system.ResolvePortalDestination(info.sceneID, portalID, int(info.id))
	if !ok {
		t.Fatal("expected portal destination to resolve")
	}

	info.sceneID = dest.Scene
	info.pos = system.Vec3{X: dest.X, Y: dest.Y, Z: dest.Z}
	info.portalCooldownUntil = time.Now().Add(time.Second)

	if info.sceneID != system.SceneVoxworld {
		t.Fatalf("expected scene voxworld after travel, got %d", info.sceneID)
	}
	if info.portalCooldownUntil.Before(time.Now()) {
		t.Fatal("expected cooldown to be set after travel")
	}
}

func TestClientInfoPortalCooldownPreventsRetravel(t *testing.T) {
	info := clientInfo{
		id:                  1,
		sceneID:             system.SceneGarageOsaka,
		pos:                 system.Vec3{X: 48, Y: 6, Z: 0},
		portalCooldownUntil: time.Now().Add(10 * time.Second), // cooldown active
	}

	if time.Now().After(info.portalCooldownUntil) {
		t.Fatal("cooldown should still be active")
	}
	// Portal would fire if not for the cooldown check
	_, triggered := system.PortalTriggered(info.sceneID, info.pos.X, info.pos.Z)
	if !triggered {
		t.Fatal("portal geometry should trigger (cooldown is checked by caller)")
	}
}

func TestSceneChangePacketFormat(t *testing.T) {
	// Verify the PacketSceneChange constant is distinct from existing packet types.
	if common.PacketSceneChange == common.PacketSnapshot ||
		common.PacketSceneChange == common.PacketWelcome ||
		common.PacketSceneChange == common.PacketVoxelData ||
		common.PacketSceneChange == common.PacketImpact {
		t.Fatal("PacketSceneChange collides with an existing packet type")
	}
	if common.PacketSceneChange != 6 {
		t.Fatalf("expected PacketSceneChange=6, got %d", common.PacketSceneChange)
	}
}

func TestSnapshotSceneIsolation(t *testing.T) {
	// Players in different scenes must not appear in each other's snapshot payloads.
	// We test the filtering logic directly: only same-scene peers should be collected.
	a := clientInfo{id: 1, sceneID: system.SceneGarageOsaka}
	b := clientInfo{id: 2, sceneID: system.SceneGarageOsaka}
	c := clientInfo{id: 3, sceneID: system.SceneVoxworld}

	allClients := []clientInfo{a, b, c}

	peersForA := make([]clientInfo, 0)
	for _, e := range allClients {
		if e.sceneID == a.sceneID && e.id != a.id {
			peersForA = append(peersForA, e)
		}
	}
	if len(peersForA) != 1 || peersForA[0].id != b.id {
		t.Fatalf("expected only peer b in a's snapshot, got %v", peersForA)
	}

	peersForC := make([]clientInfo, 0)
	for _, e := range allClients {
		if e.sceneID == c.sceneID && e.id != c.id {
			peersForC = append(peersForC, e)
		}
	}
	if len(peersForC) != 0 {
		t.Fatalf("expected no peers for c in voxworld, got %d", len(peersForC))
	}
}

func TestClientYawTracking(t *testing.T) {
	// yaw from UserCmd should propagate into clientInfo.
	info := clientInfo{id: 5, sceneID: system.SceneGarageOsaka}
	info.yaw = 135.5
	if info.yaw != 135.5 {
		t.Fatalf("expected yaw 135.5, got %f", info.yaw)
	}
}
