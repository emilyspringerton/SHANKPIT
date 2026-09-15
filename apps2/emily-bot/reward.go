// reward.go — real reward design for SHANKPIT QUEUE bot training.
//
// S459-46, founder real-time: "design the reward system for the bots for now these will just be
// ffa bots we will do new training for teams so i dunno if you want to plan team rewards into
// the rewards system now? we want the training to be FFA to start lets leave the team complexity
// out but please plan for it architecturally if it makes sense - so same thing as BP bots."
//
// Same real, multi-tier dense-reward shape as BRAWLPIT's own compute_reward
// (scripts/rl_env_packet.py) -- outcome, positional/danger shaping, survival, activity -- adapted
// to SHANKPIT's real wire signals instead of re-deriving BRAWLPIT's stock/damage-percent model.
// The single biggest real find this design leans on: packages/common/physics.h ALREADY computes
// a real, dense, server-authoritative reward signal per player (PlayerState.accumulated_reward:
// +150.0 on a confirmed kill, +0.5 per point of damage actually dealt) that reaches the client as
// NetPlayer.reward_feedback and was simply never being read before this session (see snapshot.go
// S459-45). Tier 1 below is that signal, not a re-derived heuristic -- the server already knows
// exactly how much damage was dealt each tick; re-deriving it client-side from noisy health
// deltas (which also move from healing/shield regen, unrelated to combat) would be strictly
// worse.
//
// FFA-only for now (compute_reward has no team-aware term at all, matching the founder's own
// explicit "leave the team complexity out"), but architected so a team term can be added without
// reshaping the function signature: TeamRewardContext is a real, already-defined, currently-
// unused parameter type. See the "Team reward, architected not built" section below for exactly
// what a team term would need and why it isn't guessed at here.
package main

import "math"

const (
	// Tier 1: outcome. RewardKillKnown/RewardDamageKnown are NOT independent constants here --
	// they ARE physics.h's own +150.0/+0.5 (see this file's own module doc comment), already
	// summed into RewardFeedback by the time this reward function ever sees it. This tier is
	// just "trust the server's own number," scaled down to keep it in the same rough numeric
	// range as the other tiers below (a bare +150 kill reward would dwarf every shaping term by
	// two orders of magnitude, the same numerical-stability concern BRAWLPIT's own reward design
	// names explicitly for its own survival tier).
	rewardFeedbackScale = 1.0 / 50.0

	// RewardDeath -- a real, direct penalty the instant self.deaths increases. Not already
	// covered by RewardFeedback (accumulated_reward is the ATTACKER's own reward on a kill; the
	// victim's own accumulated_reward is untouched by physics.h's kill-credit code path -- dying
	// needs its own explicit penalty term or a bot would have zero learned incentive to avoid
	// it).
	rewardDeath = -1.0

	// Tier 2: positional/danger shaping. A real, direct incentive to disengage at low health
	// (mirrors BRAWLPIT's own commander-posture-conditioned edge-danger shaping, adapted to
	// SHANKPIT's own real health-based retreat threshold already used by bot_think's commander
	// posture logic, packages/simulation/local_game.h -- COMMANDER retreat kicks in under 30%
	// health, matched here for consistency rather than picking an unrelated new threshold).
	lowHealthThresholdFrac = 0.30
	rewardLowHealthEngagedPerTick = -0.02 // being in a fight (an opponent visible+close) while under 30% HP -- real, direct pressure to disengage rather than trade unfavorably
	rewardLowHealthDisengagedPerTick = 0.01 // successfully creating distance while low -- small, real credit for a good retreat decision

	// Tier 3: survival (numerical-stability nudge only, same rationale and same two-orders-of-
	// magnitude-below-a-real-reward-tick sizing as BRAWLPIT's own REWARD_ALIVE_PER_TICK).
	rewardAlivePerTick = 0.001

	// Tier 4: engagement/activity. A real, small incentive to close distance on a visible,
	// beatable target (health advantage) rather than a policy learning "hide forever" as a
	// degenerate local optimum -- the same real failure mode BRAWLPIT's own inactivity-penalty
	// design (S442) was built to fix, adapted here as a positive pull instead of a standing
	// penalty since SHANKPIT/QUEUE has continuous respawns (no single terminal "did nothing all
	// match" case the way a timed 1v1 stock match has).
	rewardApproachAdvantagedTargetPerTick = 0.01

	// Tier 5: real, growing survival-streak shaping -- S459-51, founder real-time: "give us the
	// survival streak bonus," pointing at BRAWLPIT's own real design (originally: "add a reward
	// that ticks up over time so fib like 1 1 2 3 5 reward for not die also it should go
	// exponentially ish for the higher damage you are it should reward you even more when you oof
	// it resets"). A direct, faithful port of BRAWLPIT/scripts/rl_env_packet.py's own tier 5,
	// mirrored here for the same reason every other tier already is -- keeping the Go bot-pool AI
	// and the Python training env's reward semantics faithful ports of each other, not two
	// independently-drifting designs. Ported already carrying BRAWLPIT's own real, found-and-
	// fixed bug fix: the per-tick value must stop being PAID once the streak exceeds the cap, not
	// just have its Fibonacci index clamped there (the original bug paid fib(cap) forever after
	// the cap -- a real reward-hacking incentive toward passive stalling).
	//
	// Real, deliberate adaptation from BRAWLPIT's own damage-percent scaling (Smash-style damage
	// climbs 0->300%+): SHANKPIT is health-based (100->0), so the "how close to death" scale here
	// is (100-health)/100, not health itself -- same real intent, surviving one more tick near
	// death is worth more than surviving one more tick at full health.
	rewardSurvivalStreakUnit = 0.001
	survivalStreakFibCap = 14 // matches BRAWLPIT's own real, bug-fixed cap exactly
	survivalStreakHealthExpBase = 2.0
)

// TeamRewardContext -- Team reward, architected not built (S459-46). A real, currently-unused
// parameter type rather than a guessed-at implementation: MODE_QUEUE has no teams today (every
// entity's team_id is -1, see snapshot.go's own decode), so there is no real signal to design
// against yet -- team_scores, ally identity, and a real "did my team win the round" terminal
// condition all come from MODE_TDMO's own already-existing server state
// (local_state.team_scores, server_team_mode_enabled()) once QUEUE (or a team variant of it)
// actually carries team_id != -1 over the wire, which it already CAN (the field exists, S459-44
// decodes it) -- just never populated in FFA. When team training starts, the real additions
// here would be: (1) a team-kill-assist credit (damage/kill by an ally counts as a smaller,
// real positive, matching BRAWLPIT's own "reward the team, not just the individual" precedent
// once one exists), (2) a team-score-differential shaping term (same shape as tier 2's health-
// based shaping, but keyed to local_state.team_scores[my_team] - team_scores[enemy_team]), and
// (3) a terminal team-win/loss outcome replacing the FFA round-frag-limit outcome below. Left as
// a real, named gap rather than stubbed with fabricated constants.
type TeamRewardContext struct {
	// Present but unused in FFA -- kept as a real, typed placeholder (not a bare bool flag) so
	// the eventual team fields have an obvious home without another signature change.
	Enabled bool
}

// PlayerRewardSnapshot is the real, minimal state compute_reward needs from one tick -- a subset
// of what buildObservation already reads out of botState, kept separate because reward
// computation and observation-building are two different real consumers of the same underlying
// decoded state (a training loop wants both every tick, but they're conceptually distinct: one
// describes "what do I see," the other "how did that last action go").
type PlayerRewardSnapshot struct {
	Health         float32
	RewardFeedback float32 // real, server-computed (see this file's own module doc comment)
	Deaths         uint16
	NearestEnemyDist float32 // 0 = no visible enemy
	NearestEnemyHealthFrac float32 // health of the nearest visible enemy, 0 if none visible
}

// fibonacci -- the real, standard sequence, 1-indexed (fib(1)=1, fib(2)=1, fib(3)=2, fib(4)=3,
// fib(5)=5, ...), a direct port of BRAWLPIT/scripts/rl_env_packet.py's own _fibonacci. n is
// always small in practice (bounded by survivalStreakFibCap), so no memoization needed.
func fibonacci(n int) int {
	if n <= 0 {
		return 0
	}
	a, b := 1, 1
	for i := 0; i < n-1; i++ {
		a, b = b, a+b
	}
	return a
}

// computeReward -- the real, per-tick dense reward. prev/cur are the same bot's own state one
// tick apart (the caller's own responsibility to snapshot before/after, same convention
// BRAWLPIT's own compute_reward(prev_own, prev_opp, cur_own, cur_opp, ...) already establishes).
// team is accepted but, per this file's own module doc comment, contributes nothing yet.
// survivalTicks is the real count of consecutive ticks this life has lasted (including this one),
// maintained by the caller and reset to 0 the tick a death happens -- 0 skips tier 5 entirely,
// matching this repo's own established optional-degrade convention for this exact parameter
// (BRAWLPIT's own compute_reward treats survival_ticks=None the same way).
//
// S459-52's real multikill bonus (double/triple/killtacular, packages/common/physics.h's
// MULTIKILL_BONUS_*) needs no separate tier here -- it's added directly to the server's own
// accumulated_reward (the same field the base +150 kill credit already uses), so it reaches the
// bot automatically through tier 1's own RewardFeedback term the instant a multikill lands.
func computeReward(prev, cur PlayerRewardSnapshot, team TeamRewardContext, survivalTicks int) float32 {
	var reward float32

	// Tier 1: outcome -- trust the server's own real reward_feedback signal directly.
	reward += cur.RewardFeedback * rewardFeedbackScale
	if cur.Deaths > prev.Deaths {
		reward += rewardDeath * float32(cur.Deaths-prev.Deaths)
	}

	// Tier 2: low-health shaping.
	if cur.Health/100.0 < lowHealthThresholdFrac && cur.NearestEnemyDist > 0 {
		if cur.NearestEnemyDist > prev.NearestEnemyDist {
			reward += rewardLowHealthDisengagedPerTick
		} else {
			reward += rewardLowHealthEngagedPerTick
		}
	}

	// Tier 3: survival.
	if cur.Health > 0 {
		reward += rewardAlivePerTick
	}

	// Tier 4: engagement -- closing distance on a visibly weaker (lower-health) opponent.
	if cur.NearestEnemyDist > 0 && cur.NearestEnemyDist < prev.NearestEnemyDist &&
		cur.NearestEnemyHealthFrac > 0 && cur.NearestEnemyHealthFrac < cur.Health/100.0 {
		reward += rewardApproachAdvantagedTargetPerTick
	}

	// Tier 5: survival streak (see this const block's own doc comment for the full rationale,
	// including the real bug this port ships already-fixed). Refuses to apply on the exact tick
	// a death happened, even with a stale/positive survivalTicks -- "it resets" is enforced here.
	if survivalTicks > 0 && survivalTicks <= survivalStreakFibCap && cur.Deaths == prev.Deaths {
		healthScale := math.Pow(survivalStreakHealthExpBase, float64(100.0-cur.Health)/100.0)
		reward += float32(rewardSurvivalStreakUnit * float64(fibonacci(survivalTicks)) * healthScale)
	}

	// Team term -- deliberately a no-op today (see TeamRewardContext's own doc comment).
	_ = team

	return reward
}
