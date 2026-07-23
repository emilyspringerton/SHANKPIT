package common

const (
	MaxClients     = 70
	MaxWeapons     = 5
	MaxProjectiles = 1024
	LagHistory     = 64
)

const (
	PacketConnect     = 0
	PacketUserCmd     = 1
	PacketSnapshot    = 2
	PacketWelcome     = 3
	PacketVoxelData   = 4
	PacketImpact      = 5
	PacketSceneChange = 6
	// 7 = PacketDisconnect (v1 only). 8, 9 reserved by BEDWARS_SPEC.md (not yet implemented).
	PacketRacingState = 10
)

const (
	StateAlive     = 0
	StateDead      = 1
	StateSpectator = 2
)

const (
	GameModeDeathmatch uint8 = 0
	GameModeTDM        uint8 = 1
	GameModeSurvival   uint8 = 2
	GameModeCTF        uint8 = 3
	GameModeRacing     uint8 = 4
)

const (
	SceneRaceTrack = 8
)

const (
	RaceItemNone   uint8 = 0
	RaceItemBoost  uint8 = 1
	RaceItemShield uint8 = 2
	RaceItemTrap   uint8 = 3
)

const (
	WpnKnife   = 0
	WpnMagnum  = 1
	WpnAR      = 2
	WpnShotgun = 3
	WpnSniper  = 4
)

const (
	BtnJump     = 1
	BtnAttack   = 2
	BtnCrouch   = 4
	BtnReload   = 8
	BtnUse      = 16
	BtnAbility1 = 32  // Bedrock Racers — consume held item
	BtnUltimate = 128 // Bedrock Racers — spend ultimate charge (racing scene only)
)

type NetHeader struct {
	Type        uint8
	ClientID    uint8
	Sequence    uint16
	Timestamp   uint32
	EntityCount uint8
}

type UserCmd struct {
	Sequence  uint32
	Timestamp uint32
	Msec      uint16
	Fwd       float32
	Str       float32
	Yaw       float32
	Pitch     float32
	Buttons   uint32
	WeaponIdx int32
}

type RacingTelemetry struct {
	ClientID       uint8
	Lap            uint8
	CheckpointIdx  uint8
	ItemSlot       uint8
	UltimateCharge uint8 // 0-100
	Speed          float32
}

type VoxelBlock struct {
	X       uint8
	Y       uint8
	Z       uint8
	BlockID uint16
}

type NetVoxelPacket struct {
	Type       uint8
	ChunkX     int32
	ChunkZ     int32
	BlockCount uint16
	Blocks     []VoxelBlock
}
