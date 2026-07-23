package main

import (
	"encoding/binary"
	"math"
	"net"
	"sync"
	"time"

	"dragonsnshit/packages2/common"
	"dragonsnshit/server/system"
)

// Bedrock Racers server-side wiring — see docs2/specs/BEDROCK_RACERS_SPEC.md.
// Reuses server/system's dormant vehicle_dynamics.go/vehicle_physics.go chassis
// and the racing_mode.go scaffold, wiring both into the live tick loop for the
// first time. Scoped to one vehicle, one track, 3 items + 1 ultimate.

const (
	racingMinPlayers = 2 // start a race when this many players are queued
)

// defaultRacingConfig is the single hand-authored track for this pass: an
// 8-checkpoint loop matching the checkpoint_triggers[] array authored in
// packages/common/physics.h (kept in sync by hand, same convention used
// everywhere else in this cross-language codebase).
func defaultRacingConfig() system.RacingModeConfig {
	// Scale matches the existing world (Garage ~170 units across, Stadium
	// ~1880) — a 150-unit-radius loop, not the SHANKPIT-scale-agnostic 40
	// originally sketched. Must stay in sync with checkpoint_triggers[] in
	// packages/common/physics.h (hand-kept, same convention used repo-wide).
	return system.RacingModeConfig{
		Checkpoints: []system.Vec3{
			{X: 150, Y: 0, Z: 0},
			{X: 106, Y: 0, Z: 106},
			{X: 0, Y: 0, Z: 150},
			{X: -106, Y: 0, Z: 106},
			{X: -150, Y: 0, Z: 0},
			{X: -106, Y: 0, Z: -106},
			{X: 0, Y: 0, Z: -150},
			{X: 106, Y: 0, Z: -106},
		},
		CheckpointRadius: 20.0,
		LapsToWin:        3,
		VehicleCfg: system.VehicleConfig{
			Mass:              750,
			MaxEngineForce:    9000,
			MaxBrakeForce:     12000,
			DragCoefficient:   0.35,
			RollingResistance: 12,
			Wheelbase:         2.7,
			SurfaceGrip:       1.0,
			Steering: system.SteeringModel{
				LowSpeedLimit:   0.6,
				HighSpeedLimit:  0.15,
				TransitionSpeed: 25,
			},
		},
		Tire:   system.TireGripCurve{PeakSlip: 0.15, PeakGrip: 1.4, SlideGrip: 0.9},
		Aero:   system.AeroModel{BaseDownforce: 200, DownforcePerMS2: 5, MaxDownforce: 2500},
		Brakes: system.BrakeModel{MaxBrakeForce: 12000, ABSResponse: 0.6},
	}
}

// racingCfg is constructed once — RacingModeConfig is read-only after
// creation, so sharing it across goroutines needs no locking.
var racingCfg = defaultRacingConfig()

// racingStartPosition is the shared start line — every racer entering the
// track begins here, facing checkpoint 0.
func racingStartPosition() (system.Vec3, float64) {
	return system.Vec3{X: 150, Y: 0, Z: 0}, math.Pi / 2
}

// racePickup is a fixed on-track item spawn point. Kept deliberately simple
// for this pass: always available (no respawn timer), grants the next item
// in a fixed rotation so pickups feel varied without needing RNG plumbing.
type racePickup struct {
	Position system.Vec3
	Radius   float64
}

var racePickups = []racePickup{
	{Position: system.Vec3{X: 127.5, Y: 0, Z: 52.5}, Radius: 15},
	{Position: system.Vec3{X: -52.5, Y: 0, Z: 127.5}, Radius: 15},
	{Position: system.Vec3{X: -127.5, Y: 0, Z: -52.5}, Radius: 15},
	{Position: system.Vec3{X: 52.5, Y: 0, Z: -127.5}, Radius: 15},
}

var pickupRotation = []uint8{system.RaceItemBoost, system.RaceItemShield, system.RaceItemTrap}

// racingWorld holds shared, non-per-client racing state: dropped traps and a
// pickup-rotation cursor. Guarded by its own mutex since it's touched from
// the UserCmd handler for every racing-scene client.
type racingWorld struct {
	mu           sync.Mutex
	traps        []system.TrapInstance
	pickupCursor int
}

var raceWorld = &racingWorld{}

// nextPickupItem advances the fixed item rotation so consecutive pickups
// (across all racers/points) don't all hand out the same item.
func (rw *racingWorld) nextPickupItem() uint8 {
	rw.mu.Lock()
	defer rw.mu.Unlock()
	item := pickupRotation[rw.pickupCursor%len(pickupRotation)]
	rw.pickupCursor++
	return item
}

func (rw *racingWorld) dropTrap(pos system.Vec3, ownerID uint8) {
	rw.mu.Lock()
	defer rw.mu.Unlock()
	rw.traps = append(rw.traps, system.TrapInstance{Position: pos, OwnerID: ownerID})
}

// checkTrapHits returns whether progress hit any trap it doesn't own, and
// removes that trap (single-use, matching the "drop a hazard" fantasy).
func (rw *racingWorld) checkTrapHits(progress system.RacerProgress) bool {
	rw.mu.Lock()
	defer rw.mu.Unlock()
	for i, trap := range rw.traps {
		if system.DetectTrapHit(progress, trap) {
			rw.traps = append(rw.traps[:i], rw.traps[i+1:]...)
			return true
		}
	}
	return false
}

// applyRacingTick advances one racer's vehicle simulation by dt, given raw
// player input, and updates checkpoint/lap/item/ultimate state. Called once
// per UserCmd for any client in raceTrackScene. Returns the updated clientInfo
// (position/yaw synced back into info.pos/info.yaw for the existing snapshot
// path) — this is the first place in the Go server that continuously
// simulates player-driven movement from UserCmd input.
func applyRacingTick(info clientInfo, cmd common.UserCmd, cfg system.RacingModeConfig, dt float64) clientInfo {
	progress := info.racing
	progress.ClientID = info.id
	progress.Vehicle.Position = info.pos
	progress.Vehicle.Yaw = float64(info.yaw)

	throttle, brake := 0.0, 0.0
	if cmd.Fwd > 0 {
		throttle = float64(cmd.Fwd)
	} else if cmd.Fwd < 0 {
		brake = float64(-cmd.Fwd)
	}
	input := system.VehicleInput{Throttle: throttle, Brake: brake, Steer: float64(cmd.Str)}
	input = progress.EffectiveInput(input)
	vehicleCfg := progress.EffectiveVehicleConfig(cfg.VehicleCfg)

	newState, _ := system.StepVehicle(progress.Vehicle, input, vehicleCfg, cfg.Tire, cfg.Aero, cfg.Brakes, dt)
	progress.Vehicle = newState
	progress = progress.TickEffects()

	if idx, hit := system.DetectCheckpoint(progress, cfg); hit {
		progress = progress.WithCheckpoint(idx)
		if system.DetectLapComplete(progress, cfg) {
			progress = progress.WithLapComplete()
		}
	}

	if raceWorld.checkTrapHits(progress) {
		progress = progress.WithTrapHit()
	}

	if progress.ItemSlot == system.RaceItemNone {
		for _, p := range racePickups {
			if progress.Vehicle.Position.Sub(p.Position).Len() <= p.Radius {
				progress = progress.WithItemPickup(raceWorld.nextPickupItem())
				break
			}
		}
	}

	if cmd.Buttons&common.BtnAbility1 != 0 {
		if progress.ItemSlot == system.RaceItemTrap {
			raceWorld.dropTrap(progress.Vehicle.Position, progress.ClientID)
		}
		progress = progress.WithUseItem()
	}
	if cmd.Buttons&common.BtnUltimate != 0 {
		progress = progress.WithUseUltimate()
	}

	info.racing = progress
	info.pos = progress.Vehicle.Position
	info.yaw = float32(progress.Vehicle.Yaw)
	return info
}

// broadcastRacingState runs parallel to broadcastSnapshots on the same
// cadence, sending PacketRacingState only to clients in raceTrackScene.
// Deliberately a separate packet from PacketSnapshot's already-mismatched
// 18-byte format — see BEDROCK_RACERS_SPEC.md's Known Gaps section.
func broadcastRacingState(conn *net.UDPConn, mu *sync.Mutex, clients map[string]clientInfo) {
	const entitySize = 8 // client_id(1) + lap(1) + checkpoint(1) + item(1) + ultimate(1) + speed(4)
	ticker := time.NewTicker(33 * time.Millisecond)
	defer ticker.Stop()
	for range ticker.C {
		mu.Lock()
		var racers []clientInfo
		for _, info := range clients {
			if info.remote != nil && info.sceneID == common.SceneRaceTrack {
				racers = append(racers, info)
			}
		}
		mu.Unlock()

		if len(racers) == 0 {
			continue
		}

		payload := make([]byte, 2+len(racers)*entitySize)
		payload[0] = common.PacketRacingState
		payload[1] = uint8(len(racers))
		offset := 2
		for _, info := range racers {
			payload[offset] = info.id
			payload[offset+1] = uint8(info.racing.Lap)
			payload[offset+2] = uint8(info.racing.NextCheckpoint)
			payload[offset+3] = info.racing.ItemSlot
			payload[offset+4] = uint8(info.racing.UltimateCharge)
			speed := math.Hypot(info.racing.Vehicle.Velocity.X, info.racing.Vehicle.Velocity.Z)
			binary.LittleEndian.PutUint32(payload[offset+5:], math.Float32bits(float32(speed)))
			offset += entitySize
		}

		for _, info := range racers {
			_, _ = conn.WriteToUDP(payload, info.remote)
		}
	}
}

// --- matchmaker: racing queue, mirrors the existing ctfQueue pattern exactly ---

func (m *matchmaker) enqueueRacing(key string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	for _, k := range m.racingQueue {
		if k == key {
			return
		}
	}
	m.racingQueue = append(m.racingQueue, key)
}

func (m *matchmaker) pollForRacingMatch() []string {
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.racingQueue) < racingMinPlayers {
		return nil
	}
	matched := append([]string{}, m.racingQueue...)
	m.racingQueue = m.racingQueue[:0]
	return matched
}
