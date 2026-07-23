package system

import "testing"

func testRacingConfig() RacingModeConfig {
	return RacingModeConfig{
		Checkpoints: []Vec3{
			{X: 10, Y: 0, Z: 0},
			{X: 20, Y: 0, Z: 0},
			{X: 30, Y: 0, Z: 0},
		},
		CheckpointRadius: 3,
		LapsToWin:        3,
	}
}

func TestDetectCheckpointInRadius(t *testing.T) {
	cfg := testRacingConfig()
	progress := RacerProgress{Vehicle: VehicleState{Position: Vec3{X: 9, Y: 0, Z: 0}}}

	idx, hit := DetectCheckpoint(progress, cfg)
	if !hit || idx != 1 {
		t.Fatalf("expected checkpoint 0 to be hit and advance to 1, got idx=%d hit=%v", idx, hit)
	}
}

func TestDetectCheckpointOutOfRadius(t *testing.T) {
	cfg := testRacingConfig()
	progress := RacerProgress{Vehicle: VehicleState{Position: Vec3{X: 0, Y: 0, Z: 0}}}

	idx, hit := DetectCheckpoint(progress, cfg)
	if hit || idx != 0 {
		t.Fatalf("expected no checkpoint hit far from checkpoint 0, got idx=%d hit=%v", idx, hit)
	}
}

func TestDetectCheckpointRespectsOrder(t *testing.T) {
	cfg := testRacingConfig()
	// Standing right on checkpoint 2 (index 2) while NextCheckpoint is still 0
	// must not register a hit — checkpoints must be crossed in order.
	progress := RacerProgress{NextCheckpoint: 0, Vehicle: VehicleState{Position: Vec3{X: 30, Y: 0, Z: 0}}}

	idx, hit := DetectCheckpoint(progress, cfg)
	if hit || idx != 0 {
		t.Fatalf("expected out-of-order checkpoint to not register, got idx=%d hit=%v", idx, hit)
	}
}

func TestDetectLapComplete(t *testing.T) {
	cfg := testRacingConfig()

	notDone := RacerProgress{NextCheckpoint: 2}
	if DetectLapComplete(notDone, cfg) {
		t.Fatalf("expected lap not complete with checkpoints remaining")
	}

	done := RacerProgress{NextCheckpoint: 3}
	if !DetectLapComplete(done, cfg) {
		t.Fatalf("expected lap complete once all checkpoints hit")
	}
}

func TestWithLapCompleteResetsCheckpointAndIncrementsLap(t *testing.T) {
	p := RacerProgress{Lap: 1, NextCheckpoint: 3}
	p = p.WithLapComplete()

	if p.Lap != 2 || p.NextCheckpoint != 0 {
		t.Fatalf("expected lap incremented and checkpoint reset, got lap=%d checkpoint=%d", p.Lap, p.NextCheckpoint)
	}
}

func TestWithCheckpointGrantsUltimateCharge(t *testing.T) {
	p := RacerProgress{UltimateCharge: 90}
	p = p.WithCheckpoint(1)

	if p.UltimateCharge != UltimateChargeMax {
		t.Fatalf("expected ultimate charge to clamp at %d, got %d", UltimateChargeMax, p.UltimateCharge)
	}
}

func TestWithUltimateChargeClamps(t *testing.T) {
	p := RacerProgress{}
	p = p.WithUltimateCharge(-10)
	if p.UltimateCharge != 0 {
		t.Fatalf("expected ultimate charge to clamp at 0, got %d", p.UltimateCharge)
	}

	p = p.WithUltimateCharge(1000)
	if p.UltimateCharge != UltimateChargeMax {
		t.Fatalf("expected ultimate charge to clamp at %d, got %d", UltimateChargeMax, p.UltimateCharge)
	}
}

func TestWithUseItemBoost(t *testing.T) {
	p := RacerProgress{ItemSlot: RaceItemBoost}
	p = p.WithUseItem()

	if p.ItemSlot != RaceItemNone {
		t.Fatalf("expected item slot cleared after use")
	}
	if p.BoostTicksRemaining != BoostDurationTicks {
		t.Fatalf("expected boost window started, got %d ticks", p.BoostTicksRemaining)
	}
}

func TestWithUseItemShield(t *testing.T) {
	p := RacerProgress{ItemSlot: RaceItemShield}
	p = p.WithUseItem()

	if !p.ShieldActive {
		t.Fatalf("expected shield active after use")
	}
	if p.ItemSlot != RaceItemNone {
		t.Fatalf("expected item slot cleared after use")
	}
}

func TestWithUseUltimateRequiresFullCharge(t *testing.T) {
	p := RacerProgress{UltimateCharge: 99}
	p = p.WithUseUltimate()
	if p.OverdriveActive {
		t.Fatalf("expected ultimate to be a no-op below full charge")
	}

	p = RacerProgress{UltimateCharge: UltimateChargeMax}
	p = p.WithUseUltimate()
	if !p.OverdriveActive || p.UltimateCharge != 0 || p.BoostTicksRemaining != OverdriveDurationTicks {
		t.Fatalf("expected overdrive activated and charge consumed, got active=%v charge=%d ticks=%d",
			p.OverdriveActive, p.UltimateCharge, p.BoostTicksRemaining)
	}
}

func TestWithTrapHitAbsorbedByShield(t *testing.T) {
	p := RacerProgress{ShieldActive: true}
	p = p.WithTrapHit()

	if p.ShieldActive {
		t.Fatalf("expected shield to be consumed by trap hit")
	}
	if p.ThrottleCapTicks != 0 {
		t.Fatalf("expected no throttle cap when shield absorbs the hit")
	}
}

func TestWithTrapHitAppliesThrottleCapWithoutShield(t *testing.T) {
	p := RacerProgress{}
	p = p.WithTrapHit()

	if p.ThrottleCapTicks != ThrottleCapDurationTicks {
		t.Fatalf("expected throttle cap applied, got %d ticks", p.ThrottleCapTicks)
	}
}

func TestDetectTrapHitIgnoresOwner(t *testing.T) {
	trap := TrapInstance{Position: Vec3{X: 5, Y: 0, Z: 0}, OwnerID: 1}
	owner := RacerProgress{ClientID: 1, Vehicle: VehicleState{Position: Vec3{X: 5, Y: 0, Z: 0}}}

	if DetectTrapHit(owner, trap) {
		t.Fatalf("expected trap to not hit its own owner")
	}

	rival := RacerProgress{ClientID: 2, Vehicle: VehicleState{Position: Vec3{X: 5, Y: 0, Z: 0}}}
	if !DetectTrapHit(rival, trap) {
		t.Fatalf("expected trap to hit a rival within radius")
	}
}

func TestTickEffectsDecrementsAndClearsOverdrive(t *testing.T) {
	p := RacerProgress{ThrottleCapTicks: 2, BoostTicksRemaining: 1, OverdriveActive: true}
	p = p.TickEffects()

	if p.ThrottleCapTicks != 1 {
		t.Fatalf("expected throttle cap to decrement, got %d", p.ThrottleCapTicks)
	}
	if p.BoostTicksRemaining != 0 || p.OverdriveActive {
		t.Fatalf("expected boost/overdrive to end when ticks reach 0, got remaining=%d overdrive=%v",
			p.BoostTicksRemaining, p.OverdriveActive)
	}
}

func TestApplyBoostForcesFullThrottle(t *testing.T) {
	input := VehicleInput{Throttle: 0.2}
	input = ApplyBoost(input)

	if input.Throttle != 1.0 {
		t.Fatalf("expected boosted throttle to be 1.0, got %v", input.Throttle)
	}
}

func TestApplyThrottleCapLimitsHighThrottle(t *testing.T) {
	progress := RacerProgress{ThrottleCapTicks: 10}
	input := VehicleInput{Throttle: 1.0}
	input = ApplyThrottleCap(input, progress)

	if input.Throttle != ThrottleCapMax {
		t.Fatalf("expected throttle capped at %v, got %v", ThrottleCapMax, input.Throttle)
	}
}

func TestApplyThrottleCapNoOpWithoutDebuff(t *testing.T) {
	progress := RacerProgress{}
	input := VehicleInput{Throttle: 1.0}
	input = ApplyThrottleCap(input, progress)

	if input.Throttle != 1.0 {
		t.Fatalf("expected throttle unaffected without a trap debuff, got %v", input.Throttle)
	}
}

func TestEffectiveInputComposesBoostAndCap(t *testing.T) {
	// Boost should win even if throttle-capped is also active, since a driver
	// using an item on top of a trap debuff should feel it — boost applies
	// full throttle, and the cap check afterward is a no-op since 1.0 > cap
	// is capped back down. This documents WHICH effect should take priority:
	// the trap debuff wins over a stacked boost, since it's applied after.
	progress := RacerProgress{BoostTicksRemaining: 5, ThrottleCapTicks: 5}
	input := progress.EffectiveInput(VehicleInput{Throttle: 0.1})

	if input.Throttle != ThrottleCapMax {
		t.Fatalf("expected throttle cap to win over boost when both active, got %v", input.Throttle)
	}
}

func TestEffectiveVehicleConfigAppliesOverdrive(t *testing.T) {
	base := VehicleConfig{MaxEngineForce: 1000}

	notActive := RacerProgress{}
	cfg := notActive.EffectiveVehicleConfig(base)
	if cfg.MaxEngineForce != 1000 {
		t.Fatalf("expected base config unchanged without overdrive")
	}

	active := RacerProgress{OverdriveActive: true}
	cfg = active.EffectiveVehicleConfig(base)
	if cfg.MaxEngineForce != 1000*OverdriveEngineForceMult {
		t.Fatalf("expected overdrive to scale engine force, got %v", cfg.MaxEngineForce)
	}
}
