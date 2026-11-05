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

// TestBuildObservation_ShieldRegenSignal -- the shield regen rule's two observable halves: the
// recovery landing on this snapshot, and the vulnerable window (alive with no shield). Both come
// from fields the wire already carries, so no clock is needed here.
func TestBuildObservation_ShieldRegenSignal(t *testing.T) {
	s := &botState{
		myID: 1, myHealth: 100, myShield: 0, myState: 0, myShieldGain: 0.03,
		sceneID: 9, peers: map[uint8]*peer{},
	}
	obs := buildObservation(s)
	if obs.SelfShieldGainFrac != 0.03 {
		t.Fatalf("expected the snapshot's shield gain (0.03), got %v", obs.SelfShieldGainFrac)
	}
	if obs.SelfShieldBroken != 1 {
		t.Fatalf("an alive body at 0 shield is inside its vulnerable window, got %v", obs.SelfShieldBroken)
	}
	s.myShield, s.myShieldGain = 55, 0
	obs = buildObservation(s)
	if obs.SelfShieldBroken != 0 || obs.SelfShieldGainFrac != 0 {
		t.Fatalf("a body with shield and no gain should read neither, got broken=%v gain=%v",
			obs.SelfShieldBroken, obs.SelfShieldGainFrac)
	}
	// The wire's clock drives the "how long am I exposed" feature: 1000 ms since the hit is 3/4 of
	// the window left, and a clock past the delay reads as 0 (the ticks have started).
	s.myShieldRegenSinceMs = 1000
	if got := buildObservation(s).SelfShieldRegenLeftFrac; got != 0.75 {
		t.Fatalf("1000 ms into the window should read 0.75 left, got %v", got)
	}
	s.myShieldRegenSinceMs = 5000
	if got := buildObservation(s).SelfShieldRegenLeftFrac; got != 0 {
		t.Fatalf("a started regeneration should read 0 left, got %v", got)
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

// TestComputeReward_ShieldRecoveryWhileDisengaged -- the reward for the regen only pays while the
// bot is NOT closing on an enemy: the refill takes nineteen seconds, so breaking contact is the
// behaviour worth buying, and turtling in someone's face must not be.
func TestComputeReward_ShieldRecoveryWhileDisengaged(t *testing.T) {
	prev := PlayerRewardSnapshot{Health: 80, Shield: 20, NearestEnemyDist: 30, ShieldRegenSinceMs: 5000}
	cur := PlayerRewardSnapshot{Health: 80, Shield: 21, NearestEnemyDist: 40, ShieldRegenSinceMs: 5000}
	if got := computeReward(prev, cur, TeamRewardContext{}, 0); got <= 0.003 {
		t.Fatalf("a point of shield recovered while opening distance should pay more than the alive tick, got %v", got)
	}

	// Same recovery, but walking INTO the enemy: only the alive tick remains.
	cur.NearestEnemyDist = 10
	if got := computeReward(prev, cur, TeamRewardContext{}, 0); got >= 0.002 {
		t.Fatalf("recovering while closing on an enemy must not pay, got %v", got)
	}

	// No visible enemy at all is a disengage too.
	prev.NearestEnemyDist, cur.NearestEnemyDist = 0, 0
	if got := computeReward(prev, cur, TeamRewardContext{}, 0); got <= 0.003 {
		t.Fatalf("a point of shield recovered out of contact should pay, got %v", got)
	}

	// Inside the four-second window the shield is NOT regenerating, whatever the delta says.
	prev.ShieldRegenSinceMs, cur.ShieldRegenSinceMs = 900, 1100
	if got := computeReward(prev, cur, TeamRewardContext{}, 0); got >= 0.002 {
		t.Fatalf("a shield gain inside the vulnerable window must not pay regen, got %v", got)
	}

	// A pack refills the bar outright: a 25-point jump is not a tick.
	prev.ShieldRegenSinceMs, cur.ShieldRegenSinceMs = 5000, 5000
	prev.Shield, cur.Shield = 40, 65
	if got := computeReward(prev, cur, TeamRewardContext{}, 0); got >= 0.002 {
		t.Fatalf("a pack-sized jump must not be paid as regeneration, got %v", got)
	}
}

func TestComputeReward_KillCreditAndDeathPenalty(t *testing.T) {
	prev := PlayerRewardSnapshot{Health: 100, Deaths: 0}
	cur := PlayerRewardSnapshot{Health: 100, Deaths: 0, RewardFeedback: 150.0} // a confirmed kill
	r := computeReward(prev, cur, TeamRewardContext{}, 0)
	if r <= 0 {
		t.Fatalf("expected positive reward for a kill, got %v", r)
	}

	prevD := PlayerRewardSnapshot{Health: 20, Deaths: 0}
	curD := PlayerRewardSnapshot{Health: 0, Deaths: 1}
	rd := computeReward(prevD, curD, TeamRewardContext{}, 0)
	if rd >= 0 {
		t.Fatalf("expected negative reward for a death, got %v", rd)
	}
}

func TestComputeReward_SurvivalStreak(t *testing.T) {
	prev := PlayerRewardSnapshot{Health: 100, Deaths: 0}
	cur := PlayerRewardSnapshot{Health: 100, Deaths: 0}

	early := computeReward(prev, cur, TeamRewardContext{}, 2)
	later := computeReward(prev, cur, TeamRewardContext{}, 8)
	if later <= early {
		t.Fatalf("expected a longer streak to score higher: early=%v later=%v", early, later)
	}

	fullHealth := computeReward(prev, PlayerRewardSnapshot{Health: 100, Deaths: 0}, TeamRewardContext{}, 5)
	lowHealth := computeReward(prev, PlayerRewardSnapshot{Health: 1, Deaths: 0}, TeamRewardContext{}, 5)
	if lowHealth <= fullHealth {
		t.Fatalf("expected a low-health streak to score higher: fullHealth=%v lowHealth=%v", fullHealth, lowHealth)
	}

	// Real, found-live bug this port ships already-fixed: must stop paying entirely past the
	// cap, not keep paying fib(cap) forever.
	atCap := computeReward(prev, cur, TeamRewardContext{}, survivalStreakFibCap)
	pastCap := computeReward(prev, cur, TeamRewardContext{}, survivalStreakFibCap+1)
	baseline := computeReward(prev, cur, TeamRewardContext{}, 0)
	if atCap <= baseline {
		t.Fatalf("expected tier 5 to contribute something at the cap, got atCap=%v baseline=%v", atCap, baseline)
	}
	if pastCap != baseline {
		t.Fatalf("expected tier 5 to contribute NOTHING past the cap, got pastCap=%v baseline=%v", pastCap, baseline)
	}

	// Never pays on the death tick itself, even with a stale positive survivalTicks.
	prevAlive := PlayerRewardSnapshot{Health: 20, Deaths: 0}
	died := PlayerRewardSnapshot{Health: 0, Deaths: 1}
	withStreak := computeReward(prevAlive, died, TeamRewardContext{}, 10)
	withoutStreak := computeReward(prevAlive, died, TeamRewardContext{}, 0)
	if withStreak != withoutStreak {
		t.Fatalf("expected tier 5 to refuse to pay on the death tick: withStreak=%v withoutStreak=%v", withStreak, withoutStreak)
	}
}

func TestFibonacci(t *testing.T) {
	want := []int{1, 1, 2, 3, 5, 8, 13}
	for i, w := range want {
		if got := fibonacci(i + 1); got != w {
			t.Fatalf("fibonacci(%d) = %d, want %d", i+1, got, w)
		}
	}
	if fibonacci(0) != 0 || fibonacci(-5) != 0 {
		t.Fatalf("expected fibonacci(n<=0) == 0")
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

	rEngaged := computeReward(prev, engaged, TeamRewardContext{}, 0)
	rDisengaged := computeReward(prev, disengaged, TeamRewardContext{}, 0)
	if rDisengaged <= rEngaged {
		t.Fatalf("expected disengaging while low HP to score higher than engaging: engaged=%v disengaged=%v", rEngaged, rDisengaged)
	}
}
