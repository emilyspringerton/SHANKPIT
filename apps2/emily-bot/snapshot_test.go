package main

import (
	"encoding/binary"
	"math"
	"testing"
)

// TestDecodePacketSnapshot_RealWireLayout -- S459-44. Builds a synthetic buffer byte-for-byte
// matching the real, compiled NetHeader+NetPlayer layout (see snapshot.go's own doc comment for
// how these offsets were derived: a compiled offsetof()/sizeof() probe against this exact build,
// not assumed from the header alone) and asserts the decoder recovers the real values, not the
// pre-fix decoder's garbage (which read header/count bytes as if they were entity position
// floats).
func TestDecodePacketSnapshot_RealWireLayout(t *testing.T) {
	buf := make([]byte, netHeaderSize+1+netPlayerSize*2)

	// NetHeader
	buf[0] = 2 // PACKET_SNAPSHOT
	buf[1] = 0 // client_id (server always sends 0 here)
	binary.LittleEndian.PutUint16(buf[2:], 42)          // sequence
	binary.LittleEndian.PutUint32(buf[4:], 123456)      // timestamp
	buf[8] = 2                                          // entity_count
	buf[9] = 9                                           // scene_id (SCENE_CUSTOM_LEVEL)
	buf[netHeaderSize] = 2                              // redundant count byte

	putEntity := func(off int, id uint8, x, y, z, yaw float32, health uint8, weapon uint8) {
		buf[off+offID] = id
		buf[off+offSceneID] = 9
		buf[off+offIsBot] = 1
		buf[off+offTeamID] = 0xFF // -1 as int8 (no team, FFA)
		binary.LittleEndian.PutUint32(buf[off+offX:], math.Float32bits(x))
		binary.LittleEndian.PutUint32(buf[off+offY:], math.Float32bits(y))
		binary.LittleEndian.PutUint32(buf[off+offZ:], math.Float32bits(z))
		binary.LittleEndian.PutUint32(buf[off+offYaw:], math.Float32bits(yaw))
		buf[off+offCurrentWeapon] = weapon
		buf[off+offHealth] = health
	}

	e0 := netHeaderSize + 1
	e1 := e0 + netPlayerSize
	putEntity(e0, 1, 10.5, 0.0, -3.25, 90.0, 100, 2)
	putEntity(e1, 2, -7.0, 1.5, 20.0, 180.0, 42, 4)

	entities, ok := decodePacketSnapshot(buf, len(buf))
	if !ok {
		t.Fatalf("decodePacketSnapshot returned ok=false for a well-formed buffer")
	}
	if len(entities) != 2 {
		t.Fatalf("expected 2 entities, got %d", len(entities))
	}

	if entities[0].id != 1 || entities[0].x != 10.5 || entities[0].y != 0.0 || entities[0].z != -3.25 ||
		entities[0].yaw != 90.0 || entities[0].health != 100 || entities[0].currentWeapon != 2 {
		t.Fatalf("entity 0 decoded wrong: %+v", entities[0])
	}
	if entities[1].id != 2 || entities[1].x != -7.0 || entities[1].y != 1.5 || entities[1].z != 20.0 ||
		entities[1].yaw != 180.0 || entities[1].health != 42 || entities[1].currentWeapon != 4 {
		t.Fatalf("entity 1 decoded wrong: %+v", entities[1])
	}
	if entities[0].teamID != -1 {
		t.Fatalf("expected teamID -1 (FFA, no team), got %d", entities[0].teamID)
	}
}

// TestDecodePacketSnapshot_TruncatedBuffer -- a short/truncated read must never be partially
// trusted (see decodePacketSnapshot's own doc comment).
func TestDecodePacketSnapshot_TruncatedBuffer(t *testing.T) {
	buf := make([]byte, netHeaderSize) // shorter than netHeaderSize+1
	if _, ok := decodePacketSnapshot(buf, len(buf)); ok {
		t.Fatalf("expected ok=false for a buffer shorter than the header+count prefix")
	}

	// A header claiming 2 entities but only carrying data for 1 must stop at 1, not read
	// past the real buffer end.
	buf2 := make([]byte, netHeaderSize+1+netPlayerSize) // room for exactly 1 entity
	buf2[8] = 2                                          // claims 2 entities
	entities, ok := decodePacketSnapshot(buf2, len(buf2))
	if !ok {
		t.Fatalf("expected ok=true even when entity_count overclaims -- should truncate, not fail")
	}
	if len(entities) != 1 {
		t.Fatalf("expected exactly 1 entity (buffer only has room for 1), got %d", len(entities))
	}
}
