package system

// Bedrock Racers — see docs2/specs/BEDROCK_RACERS_SPEC.md. Structured like
// stadium_mode.go: static *Config, mutable *State/*Progress, pure Detect*
// functions, and value-receiver reducers rather than pointer mutation.

// Item slots — mirrors packages/common/protocol.h RACE_ITEM_* / packages2/common.RaceItem*.
const (
	RaceItemNone uint8 = iota
	RaceItemBoost
	RaceItemShield
	RaceItemTrap
)

const (
	UltimateChargePerCheckpoint = 15
	UltimateChargeMax           = 100

	BoostDurationTicks       = 60  // ~2s @ 30Hz
	OverdriveDurationTicks   = 150 // ~5s @ 30Hz
	OverdriveEngineForceMult = 1.5
	ThrottleCapDurationTicks = 180 // ~6s @ 30Hz
	ThrottleCapMax           = 0.4
	TrapRadius               = 2.0
)

type RacingModeConfig struct {
	Checkpoints      []Vec3
	CheckpointRadius float64
	LapsToWin        int
	VehicleCfg       VehicleConfig
	Tire             TireGripCurve
	Aero             AeroModel
	Brakes           BrakeModel
}

// RacerProgress is one racer's mutable runtime state within a race.
type RacerProgress struct {
	ClientID       uint8
	Vehicle        VehicleState
	NextCheckpoint int
	Lap            int
	ItemSlot       uint8
	UltimateCharge int

	ShieldActive        bool
	OverdriveActive     bool
	ThrottleCapTicks    int
	BoostTicksRemaining int
}

type RacingState struct {
	Racers map[uint8]RacerProgress
}

// TrapInstance is a hazard placed on the track by a racer using the Trap item.
// It affects other racers only — DetectTrapHit excludes the owner.
type TrapInstance struct {
	Position Vec3
	OwnerID  uint8
}

// DetectCheckpoint reports the next-checkpoint index a racer should advance to,
// given their current position. Checkpoints must be hit in order.
func DetectCheckpoint(progress RacerProgress, cfg RacingModeConfig) (int, bool) {
	if len(cfg.Checkpoints) == 0 || progress.NextCheckpoint >= len(cfg.Checkpoints) {
		return progress.NextCheckpoint, false
	}
	target := cfg.Checkpoints[progress.NextCheckpoint]
	if progress.Vehicle.Position.Sub(target).Len() <= cfg.CheckpointRadius {
		return progress.NextCheckpoint + 1, true
	}
	return progress.NextCheckpoint, false
}

// DetectLapComplete reports whether a racer has hit every checkpoint in order.
func DetectLapComplete(progress RacerProgress, cfg RacingModeConfig) bool {
	return len(cfg.Checkpoints) > 0 && progress.NextCheckpoint >= len(cfg.Checkpoints)
}

// DetectTrapHit reports whether a racer's vehicle has driven into a trap it
// doesn't own.
func DetectTrapHit(progress RacerProgress, trap TrapInstance) bool {
	if progress.ClientID == trap.OwnerID {
		return false
	}
	return progress.Vehicle.Position.Sub(trap.Position).Len() <= TrapRadius
}

func (p RacerProgress) WithCheckpoint(idx int) RacerProgress {
	p.NextCheckpoint = idx
	p.UltimateCharge += UltimateChargePerCheckpoint
	if p.UltimateCharge > UltimateChargeMax {
		p.UltimateCharge = UltimateChargeMax
	}
	return p
}

func (p RacerProgress) WithLapComplete() RacerProgress {
	p.Lap++
	p.NextCheckpoint = 0
	return p
}

func (p RacerProgress) WithItemPickup(item uint8) RacerProgress {
	p.ItemSlot = item
	return p
}

func (p RacerProgress) WithUltimateCharge(delta int) RacerProgress {
	p.UltimateCharge += delta
	if p.UltimateCharge < 0 {
		p.UltimateCharge = 0
	}
	if p.UltimateCharge > UltimateChargeMax {
		p.UltimateCharge = UltimateChargeMax
	}
	return p
}

// WithUseItem consumes the held item, applying its effect to the racer's own
// state. Trap's world placement (a TrapInstance at the racer's position) is
// the caller's responsibility, since it must be tracked in shared RacingState,
// not on the individual RacerProgress.
func (p RacerProgress) WithUseItem() RacerProgress {
	switch p.ItemSlot {
	case RaceItemBoost:
		p.BoostTicksRemaining = BoostDurationTicks
	case RaceItemShield:
		p.ShieldActive = true
	}
	p.ItemSlot = RaceItemNone
	return p
}

// WithUseUltimate spends a full ultimate charge for an Overdrive window
// (stronger/longer Boost). No-op below full charge.
func (p RacerProgress) WithUseUltimate() RacerProgress {
	if p.UltimateCharge < UltimateChargeMax {
		return p
	}
	p.OverdriveActive = true
	p.BoostTicksRemaining = OverdriveDurationTicks
	p.UltimateCharge = 0
	return p
}

// WithTrapHit applies a Trap hit: Shield absorbs one hit, otherwise the racer
// is throttle-capped for a few seconds.
func (p RacerProgress) WithTrapHit() RacerProgress {
	if p.ShieldActive {
		p.ShieldActive = false
		return p
	}
	p.ThrottleCapTicks = ThrottleCapDurationTicks
	return p
}

// TickEffects decrements active buff/debuff timers by one server tick.
func (p RacerProgress) TickEffects() RacerProgress {
	if p.ThrottleCapTicks > 0 {
		p.ThrottleCapTicks--
	}
	if p.BoostTicksRemaining > 0 {
		p.BoostTicksRemaining--
		if p.BoostTicksRemaining == 0 {
			p.OverdriveActive = false
		}
	}
	return p
}

// ApplyBoost forces full throttle — used while BoostTicksRemaining > 0,
// whether from the Boost item or an Overdrive ultimate window.
func ApplyBoost(input VehicleInput) VehicleInput {
	input.Throttle = 1.0
	return input
}

// ApplyThrottleCap caps throttle while a Trap debuff is active.
func ApplyThrottleCap(input VehicleInput, progress RacerProgress) VehicleInput {
	if progress.ThrottleCapTicks > 0 && input.Throttle > ThrottleCapMax {
		input.Throttle = ThrottleCapMax
	}
	return input
}

// EffectiveInput composes a racer's active item effects onto raw player input,
// for use as the VehicleInput passed to StepVehicle.
func (p RacerProgress) EffectiveInput(input VehicleInput) VehicleInput {
	if p.BoostTicksRemaining > 0 {
		input = ApplyBoost(input)
	}
	return ApplyThrottleCap(input, p)
}

// ApplyUltimate scales engine force up for the Overdrive window — the part of
// Overdrive that makes it "stronger" than a plain Boost, not just longer.
func ApplyUltimate(cfg VehicleConfig) VehicleConfig {
	cfg.MaxEngineForce *= OverdriveEngineForceMult
	return cfg
}

// EffectiveVehicleConfig composes a racer's active item effects onto the base
// vehicle config, for use as the VehicleConfig passed to StepVehicle.
func (p RacerProgress) EffectiveVehicleConfig(cfg VehicleConfig) VehicleConfig {
	if p.OverdriveActive {
		cfg = ApplyUltimate(cfg)
	}
	return cfg
}
