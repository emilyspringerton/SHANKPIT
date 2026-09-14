// snapshot.go — real PacketSnapshot decode for emily-bot.
//
// S459-44, real, found-live protocol bug fix. The original decode (main.go, pre-fix) assumed a
// legacy 18-byte flat entity (id, scene_id, x, y, z, yaw) starting at buffer offset 2 -- i.e. it
// treated buf[1] as the entity count and skipped straight past the rest of the real NetHeader.
// The real, live wire format (packages/common/protocol.h + apps/server/src/main.c's own
// server_broadcast, confirmed both by reading the C source and by compiling a real
// sizeof/offsetof probe against this exact build): a 12-byte NetHeader (type@0 client_id@1
// sequence@2(u16) timestamp@4(u32) entity_count@8 scene_id@9, padded to 12), followed by ONE
// redundant count byte at offset 12 (server_broadcast's own count_offset), followed by
// entity_count NetPlayer entries of netPlayerSize bytes each starting at offset 13.
//
// This means every peer position (and the bot's own self-correction via the myID branch) had
// been reading pure garbage since MAX_CLIENTS-entity snapshots became possible: buf[off] at
// off=2 is the low byte of NetHeader.sequence, not an entity id, and the 18-byte stride bore no
// relation to the real 64-byte NetPlayer stride, so parsing desynced completely after the first
// (mis-parsed) "entity." Aim/targeting logic built on `nearest.x/y/z` (main.go) has effectively
// been aiming at noise. Real health/weapon/team/kills/deaths per peer were never even attempted
// -- this decode adds them, since a real, hand-engineered observation vector (observation.go)
// needs them.
package main

import (
	"encoding/binary"
	"math"
)

const (
	netHeaderSize = 12 // type(1) client_id(1) sequence(2) timestamp(4) entity_count(1) scene_id(1) pad(2)
	netPlayerSize = 64 // real, compiled sizeof(NetPlayer) on this build -- see this file's own doc comment

	// NetPlayer field byte offsets, matching packages/common/protocol.h's real field order and
	// this platform's real struct alignment (verified via a compiled offsetof() probe, not
	// assumed) -- natural C alignment, no #pragma pack anywhere in that header.
	offID            = 0
	offSceneID       = 1
	offIsBot         = 2
	offTeamID        = 3
	offLastSeq       = 4
	offX             = 8
	offY             = 12
	offZ             = 16
	offYaw           = 20
	offPitch         = 24
	offCurrentWeapon = 28
	offState         = 29
	offHealth        = 30
	offShield        = 31
	offIsShooting    = 32
	offCrouching     = 33
	offRewardFeedback = 36
	offAmmo          = 40
	offInVehicle     = 41
	offCarriedFlagTeamID = 42
	offHitFeedback   = 43
	offStormCharges  = 44
	offKills         = 46
	offDeaths        = 48
	offDeathElapsedMs = 50
	offDeathDurationMs = 52
	offDeathDirX     = 56
	offDeathDirZ     = 60
)

func f32(buf []byte, off int) float32 {
	return math.Float32frombits(binary.LittleEndian.Uint32(buf[off:]))
}

func i8(buf []byte, off int) int8 {
	return int8(buf[off])
}

func u16(buf []byte, off int) uint16 {
	return binary.LittleEndian.Uint16(buf[off:])
}

// decodedPlayer is every real field decodePacketSnapshot pulls out of one NetPlayer entity.
type decodedPlayer struct {
	id            uint8
	sceneID       uint8
	isBot         bool
	teamID        int8
	x, y, z       float32
	yaw, pitch    float32
	currentWeapon uint8
	state         uint8
	health        uint8
	shield        uint8
	isShooting    bool
	crouching     bool
	// rewardFeedback -- S459-45, real find: packages/common/physics.h already computes a real,
	// dense, server-authoritative reward signal per player (+150.0 on a confirmed kill,
	// phys_enter_death_state; +0.5*damage per point of damage actually dealt,
	// katana_apply_damage's own real "attacker->accumulated_reward += damage * 0.5" -- note the
	// name is generic, not katana-specific: every weapon's damage call funnels through the same
	// katana_apply_damage/phys_try_melee_strike path). The server accumulates it every tick and
	// zeroes it the instant it's read into a NetPlayer.reward_feedback (server_broadcast) or a
	// respawn is granted -- so this field IS a real "reward earned since the last snapshot you
	// saw," already computed authoritatively server-side. reward.go's own compute_reward design
	// leans on this directly instead of re-deriving a damage-dealt proxy from health deltas
	// (which would be far noisier -- health also changes from healing/shield regen).
	rewardFeedback float32
	hitFeedback   uint8
	ammo          uint8
	inVehicle     uint8
	kills         uint16
	deaths        uint16
}

// decodePacketSnapshot parses a real PacketSnapshot buffer per the layout documented above.
// Returns the decoded entities and true, or nil/false if the buffer is too short to trust
// (never partially trusts a truncated snapshot -- a short read is treated as "no data this
// tick," not as an invitation to parse whatever bytes happen to be present).
func decodePacketSnapshot(buf []byte, n int) ([]decodedPlayer, bool) {
	if n < netHeaderSize+1 {
		return nil, false
	}
	count := int(buf[8]) // NetHeader.entity_count -- the authoritative count; buf[12] carries the same value redundantly
	off := netHeaderSize + 1
	out := make([]decodedPlayer, 0, count)
	for i := 0; i < count; i++ {
		if off+netPlayerSize > n {
			break // real, honest truncation -- stop, don't misparse past the buffer's real end
		}
		e := decodedPlayer{
			id:            buf[off+offID],
			sceneID:       buf[off+offSceneID],
			isBot:         buf[off+offIsBot] != 0,
			teamID:        i8(buf, off+offTeamID),
			x:             f32(buf, off+offX),
			y:             f32(buf, off+offY),
			z:             f32(buf, off+offZ),
			yaw:           f32(buf, off+offYaw),
			pitch:         f32(buf, off+offPitch),
			currentWeapon: buf[off+offCurrentWeapon],
			state:         buf[off+offState],
			health:        buf[off+offHealth],
			shield:        buf[off+offShield],
			isShooting:    buf[off+offIsShooting] != 0,
			crouching:     buf[off+offCrouching] != 0,
			rewardFeedback: f32(buf, off+offRewardFeedback),
			hitFeedback:   buf[off+offHitFeedback],
			ammo:          buf[off+offAmmo],
			inVehicle:     buf[off+offInVehicle],
			kills:         u16(buf, off+offKills),
			deaths:        u16(buf, off+offDeaths),
		}
		out = append(out, e)
		off += netPlayerSize
	}
	return out, true
}
