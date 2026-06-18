package common_test

import (
	"testing"

	"dragonsnshit/packages2/common"
)

func TestDragonflyNameToBlockID(t *testing.T) {
	cases := []struct {
		name string
		want uint16
	}{
		{"minecraft:stone", common.BlockStone},
		{"minecraft:cobblestone", common.BlockStone},
		{"minecraft:grass_block", common.BlockGrass},
		{"minecraft:grass", common.BlockGrass},
		{"minecraft:dirt", common.BlockDirt},
		{"minecraft:oak_log", common.BlockLog},
		{"minecraft:log", common.BlockLog},
		{"minecraft:oak_leaves", common.BlockLeaf},
		{"minecraft:leaves2", common.BlockLeaf},
		{"minecraft:air", 0},
		{"minecraft:sand", 0},
		{"", 0},
	}
	for _, c := range cases {
		got := common.DragonflyNameToBlockID(c.name)
		if got != c.want {
			t.Errorf("DragonflyNameToBlockID(%q) = %d, want %d", c.name, got, c.want)
		}
	}
}

// Verify all canonical SHANKPIT block IDs are reachable via the map.
func TestAllCanonicalIDsReachable(t *testing.T) {
	want := map[uint16]bool{
		common.BlockStone: false,
		common.BlockGrass: false,
		common.BlockDirt:  false,
		common.BlockLog:   false,
		common.BlockLeaf:  false,
	}
	for name := range map[string]struct{}{
		"minecraft:stone": {}, "minecraft:grass": {}, "minecraft:dirt": {},
		"minecraft:log": {}, "minecraft:leaves": {},
	} {
		id := common.DragonflyNameToBlockID(name)
		if id == 0 {
			t.Errorf("%q mapped to air (0)", name)
		}
		want[id] = true
	}
	for id, found := range want {
		if !found {
			t.Errorf("block ID %d not reachable from any mapped name", id)
		}
	}
}
