package main

import "testing"

// TestObservationSize -- S459-45. Asserts the real, hand-engineered feature count lands inside
// the founder's own explicit 50-100 range and that Values() actually emits that many floats (a
// mismatch here would mean the struct and the flattener drifted apart silently).
func TestObservationSize(t *testing.T) {
	if ObservationSize < 50 || ObservationSize > 100 {
		t.Fatalf("ObservationSize=%d outside the requested 50-100 feature range", ObservationSize)
	}
	var obs Observation
	got := len(obs.Values())
	if got != ObservationSize {
		t.Fatalf("Values() emitted %d floats, want ObservationSize=%d -- struct and flattener drifted apart", got, ObservationSize)
	}
}

func TestBuildObservation_NoPeers(t *testing.T) {
	s := &botState{
		myID: 1, myHealth: 100, sceneID: 9,
		peers: map[uint8]*peer{},
	}
	obs := buildObservation(s)
	if obs.SelfHealthFrac != 1.0 {
		t.Fatalf("expected full health frac, got %v", obs.SelfHealthFrac)
	}
	for i, opp := range obs.Opponents {
		if opp.Present != 0 {
			t.Fatalf("opponent slot %d should be zero-filled with no peers, got Present=%v", i, opp.Present)
		}
	}
	if obs.VisibleEnemyCountNorm != 0 {
		t.Fatalf("expected 0 visible enemies, got %v", obs.VisibleEnemyCountNorm)
	}
}

func TestBuildObservation_NearestOpponentOrdering(t *testing.T) {
	s := &botState{
		myID: 1, myHealth: 100, sceneID: 9, myX: 0, myY: 0, myZ: 0,
		peers: map[uint8]*peer{
			2: {id: 2, sceneID: 9, x: 50, y: 0, z: 0, health: 80, state: 0},
			3: {id: 3, sceneID: 9, x: 5, y: 0, z: 0, health: 60, state: 0},
			4: {id: 4, sceneID: 8, x: 1, y: 0, z: 0, health: 10, state: 0}, // different scene -- must be excluded
		},
	}
	obs := buildObservation(s)
	if obs.Opponents[0].Present != 1 || obs.Opponents[0].DistanceNorm >= obs.Opponents[1].DistanceNorm {
		t.Fatalf("expected nearest-first ordering, got %+v", obs.Opponents[:2])
	}
	// The id=3 peer (distance 5) should be nearer than id=2 (distance 50).
	if obs.Opponents[0].HealthFrac != 0.6 {
		t.Fatalf("expected nearest opponent (id=3, health=60) first, got HealthFrac=%v", obs.Opponents[0].HealthFrac)
	}
	if obs.VisibleEnemyCountNorm == 0 {
		t.Fatalf("expected non-zero visible enemy count (2 same-scene peers)")
	}
}

func TestComputeReward_KillCreditAndDeathPenalty(t *testing.T) {
	prev := PlayerRewardSnapshot{Health: 100, Deaths: 0}
	cur := PlayerRewardSnapshot{Health: 100, Deaths: 0, RewardFeedback: 150.0} // a confirmed kill
	r := computeReward(prev, cur, TeamRewardContext{})
	if r <= 0 {
		t.Fatalf("expected positive reward for a kill, got %v", r)
	}

	prevD := PlayerRewardSnapshot{Health: 20, Deaths: 0}
	curD := PlayerRewardSnapshot{Health: 0, Deaths: 1}
	rd := computeReward(prevD, curD, TeamRewardContext{})
	if rd >= 0 {
		t.Fatalf("expected negative reward for a death, got %v", rd)
	}
}

// TestSelfDualCooldownFeatures -- S459-47, founder real-time feedback: "make sure the features
// understand that there are 2 different cooldowns and you can activate sniper cooldown and let
// the cooldown reset and then dash on weapon 6 and if you never shot the sniper projectiles from
// storm they are still activated." Verifies storm charges (a persistent, spendable resource) and
// ability cooldown (the shared gate on activating any ability, sniper storm included) are decoded
// and surfaced as two real, independent features -- not conflated into one.
func TestSelfDualCooldownFeatures(t *testing.T) {
	s := &botState{
		myID: 1, myHealth: 100, sceneID: 9,
		myStormCharges: 5, myAbilityCooldown: 0, myReloadTimer: 0,
		peers: map[uint8]*peer{},
	}
	obs := buildObservation(s)
	if obs.SelfStormChargesFrac != 1.0 {
		t.Fatalf("expected full storm charges (5/5), got %v", obs.SelfStormChargesFrac)
	}
	if obs.SelfAbilityReady != 1.0 {
		t.Fatalf("expected ability ready (cooldown=0), got %v", obs.SelfAbilityReady)
	}

	// Real scenario from the founder's own report: sniper storm was just activated (grants 5
	// charges AND starts the shared cooldown), but none of the 5 shots have been fired yet --
	// storm charges must stay at 5 (unspent) even while the shared cooldown is still counting
	// down, and SelfAbilityReady must correctly read false until it actually reaches 0.
	s2 := &botState{
		myID: 1, myHealth: 100, sceneID: 9,
		myStormCharges: 5, myAbilityCooldown: 480, myReloadTimer: 0,
		peers: map[uint8]*peer{},
	}
	obs2 := buildObservation(s2)
	if obs2.SelfStormChargesFrac != 1.0 {
		t.Fatalf("expected storm charges to remain banked at 5/5 even mid-cooldown, got %v", obs2.SelfStormChargesFrac)
	}
	if obs2.SelfAbilityReady != 0.0 {
		t.Fatalf("expected ability NOT ready while cooldown is still counting down, got %v", obs2.SelfAbilityReady)
	}
	if obs2.SelfAbilityCooldownFrac != 1.0 {
		t.Fatalf("expected ability cooldown fraction at max (480/480), got %v", obs2.SelfAbilityCooldownFrac)
	}
}

func TestSelfOutboundShootCrouch(t *testing.T) {
	s := &botState{myID: 1, myHealth: 100, sceneID: 9, myIsShooting: true, myCrouching: false, peers: map[uint8]*peer{}}
	obs := buildObservation(s)
	if obs.SelfIsShooting != 1.0 || obs.SelfCrouching != 0.0 {
		t.Fatalf("expected real outbound shoot=1/crouch=0, got shoot=%v crouch=%v", obs.SelfIsShooting, obs.SelfCrouching)
	}
}

func TestOpponentIsReloading(t *testing.T) {
	s := &botState{
		myID: 1, myHealth: 100, sceneID: 9,
		peers: map[uint8]*peer{
			2: {id: 2, sceneID: 9, x: 5, health: 80, state: 0, reloadTimer: 30},
		},
	}
	obs := buildObservation(s)
	if obs.Opponents[0].IsReloading != 1.0 {
		t.Fatalf("expected nearest opponent to read as reloading (reloadTimer=30), got %v", obs.Opponents[0].IsReloading)
	}
}

func TestRaycast_SimpleWall(t *testing.T) {
	geo := &levelGeometry{walls: []wall{
		{X: 0, Y: 0, Z: -10, SX: 4, SY: 4, SZ: 1}, // a wall centered 10 units ahead (along -Z)
	}}
	// Ray from origin straight along -Z should hit the wall's near face at z=-9.5.
	d := geo.raycast(0, 0, 0, 0, 0, -1, 40)
	if d < 9.0 || d > 10.0 {
		t.Fatalf("expected raycast to hit the wall around distance 9.5, got %v", d)
	}
	// A ray pointed the opposite direction should miss and read as the cap.
	dMiss := geo.raycast(0, 0, 0, 0, 0, 1, 40)
	if dMiss != 40 {
		t.Fatalf("expected a ray pointed away from the only wall to read as the cap (40), got %v", dMiss)
	}
}

func TestComputeReward_LowHealthShaping(t *testing.T) {
	prev := PlayerRewardSnapshot{Health: 25, NearestEnemyDist: 5}
	engaged := PlayerRewardSnapshot{Health: 25, NearestEnemyDist: 4} // closing distance while low HP
	disengaged := PlayerRewardSnapshot{Health: 25, NearestEnemyDist: 10} // creating distance while low HP

	rEngaged := computeReward(prev, engaged, TeamRewardContext{})
	rDisengaged := computeReward(prev, disengaged, TeamRewardContext{})
	if rDisengaged <= rEngaged {
		t.Fatalf("expected disengaging while low HP to score higher than engaging: engaged=%v disengaged=%v", rEngaged, rDisengaged)
	}
}
