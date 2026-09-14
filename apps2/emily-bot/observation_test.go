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
