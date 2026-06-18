package common

// Block ID constants matching VOXEL_BLOCK_* in packages/common/protocol.h.
const (
	BlockStone uint16 = 1
	BlockGrass uint16 = 2
	BlockDirt  uint16 = 3
	BlockLog   uint16 = 17
	BlockLeaf  uint16 = 18
)

// blockNameMap maps Dragonfly/Bedrock block name prefixes to SHANKPIT block IDs.
// Dragonfly represents blocks as "minecraft:<name>" strings.
// Variant suffixes (e.g. "_oak", "_birch", "_dark_oak") are stripped before lookup.
var blockNameMap = map[string]uint16{
	"minecraft:stone":         BlockStone,
	"minecraft:cobblestone":   BlockStone,
	"minecraft:stone_bricks":  BlockStone,
	"minecraft:mossy_cobblestone": BlockStone,
	"minecraft:grass":         BlockGrass,
	"minecraft:grass_block":   BlockGrass,
	"minecraft:dirt":          BlockDirt,
	"minecraft:podzol":        BlockDirt,
	"minecraft:coarse_dirt":   BlockDirt,
	"minecraft:oak_log":       BlockLog,
	"minecraft:birch_log":     BlockLog,
	"minecraft:spruce_log":    BlockLog,
	"minecraft:jungle_log":    BlockLog,
	"minecraft:acacia_log":    BlockLog,
	"minecraft:dark_oak_log":  BlockLog,
	"minecraft:log":           BlockLog,
	"minecraft:oak_leaves":    BlockLeaf,
	"minecraft:birch_leaves":  BlockLeaf,
	"minecraft:spruce_leaves": BlockLeaf,
	"minecraft:jungle_leaves": BlockLeaf,
	"minecraft:acacia_leaves": BlockLeaf,
	"minecraft:dark_oak_leaves": BlockLeaf,
	"minecraft:leaves":        BlockLeaf,
	"minecraft:leaves2":       BlockLeaf,
}

// DragonflynameToBlockID converts a Dragonfly block name to a SHANKPIT block ID.
// Returns 0 (air) for any name that has no mapping, so callers can skip unmapped blocks.
func DragonflyNameToBlockID(name string) uint16 {
	if id, ok := blockNameMap[name]; ok {
		return id
	}
	return 0
}
