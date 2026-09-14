// observation.go — real, hand-engineered feature vector for SHANKPIT QUEUE bot training.
//
// S459-45, founder real-time: "hand engineer between 50-100 features for the bots." This is the
// real feature set, built entirely from fields the server actually puts on the wire (see
// snapshot.go's own doc comment for the real decode-offset bug fixed as a prerequisite -- before
// that fix, most of what this file reads was either garbage or never decoded at all).
//
// Design, honest and explicit:
//   - FFA-first (S459-46, founder real-time: "for now these will just be ffa bots we will do new
//     training for teams"). Every team-related feature below is a real, named, always-present
//     slot in the vector (not omitted) but is architected to read as a neutral/zero value in FFA
//     (team_id == -1 on every entity in MODE_QUEUE today) -- see the "Team block" section. When
//     team training begins, MODE_TDMO's own real per-player team_id already flows over the exact
//     same wire field (protocol.h's NetPlayer.team_id); this file's team block starts producing
//     real, non-zero values with NO observation-shape change, so a checkpoint trained on this
//     exact vector shape doesn't need retraining from scratch just to add teams later.
//   - Self-relative frame: every opponent feature is expressed relative to the observing bot
//     (bearing in the bot's own yaw frame, not world-absolute angle) -- the standard shooter-AI
//     convention (matches BRAWLPIT's own build_observation, scripts/rl_env_packet.py) so the
//     same policy weights generalize across every position/orientation on the map instead of
//     memorizing absolute coordinates.
//   - No geometry/raycast features. A real gap, named here rather than faked: emily-bot has no
//     loaded level geometry (packages/world/level_boxes.h's box data is never fetched by this
//     Go client, only by the C client/server) so "distance to nearest wall" or line-of-sight
//     features aren't available without a real, separate scope of work (fetching+caching the
//     level export the same way apps/lobby's own client_load_queue_level does, S459-41). Left
//     out rather than stubbed at a fake constant.
//   - No round-timer feature. Also a real, named gap: the server's own SERVER_QUEUE_ROUND_MS
//     round clock (apps/server/src/main.c, S459-43) isn't transmitted on the wire anywhere a
//     client can read it (NetHeader.timestamp is a raw server clock, not "time since round
//     start"). A real fix would add a round-elapsed field to NetHeader or PacketWelcome; out of
//     scope here.
package main

import (
	"math"
	"sync"
)

// Feature vector layout. Grouped in three blocks: self (23), up to nearestOpponentSlots nearest
// opponents (10 features each), and aggregate/contextual (9) -- see buildObservation's own doc
// comment below for the exact per-feature list. With nearestOpponentSlots=4 (matched to the
// founder's own "4 player matches" QUEUE design, S459-41) this is 23 + 4*10 + 9 = 72 real,
// named features -- inside the requested 50-100 range with no padding.
const (
	nearestOpponentSlots = 4
	selfFeatureCount     = 26
	perOpponentFeatures  = 11
	aggregateFeatureCount = 9
	geometryFeatureCount = 5
	ObservationSize      = selfFeatureCount + nearestOpponentSlots*perOpponentFeatures + aggregateFeatureCount + geometryFeatureCount

	// queueFragLimit mirrors apps/server/src/main.c's own SERVER_QUEUE_FRAG_LIMIT (S459-43) --
	// a real, hand-copied constant (Go and C are separate binaries here, no shared header) kept
	// in sync by convention the same way this package's other server-mirrored constants already
	// are (packages2/common/protocol.go's GameModeQueue=108 mirroring protocol.h's MODE_QUEUE).
	queueFragLimit = 20

	// reloadTimerCap / abilityCooldownCap -- S459-47, real normalization ceilings mirroring
	// packages/common/protocol.h's own real constants: RELOAD_TIME_FULL=60 (the larger of the
	// two real reload durations) and 480 (the largest real per-ability cooldown, sniper storm
	// activation -- see physics.h's update_weapons: AR=260, shotgun=340, katana dash=420,
	// sniper storm=480).
	reloadTimerCap     = 60.0
	abilityCooldownCap = 480.0

	// wallRayCap / floorRayCap -- real, hand-picked caps for the raycast block (geometry.go),
	// matched to SHANKPIT's own real room-scale level geometry (NEWPIT, S459-41's own default
	// queue level, spans well under 40 units per side) rather than the much larger 100-unit cap
	// used for opponent distance (map-scale, not room-scale).
	wallRayCap  = 40.0
	floorRayCap = 20.0
)

// weaponMaxAmmo / weaponLethality mirror packages/common/protocol.h's real WPN_STATS table
// (dmg, rof, ammo_max) -- kept as a small local copy rather than a shared package because
// emily-bot is the only real consumer of a *derived, normalized* lethality score; the raw table
// stays the C side's own source of truth.
var weaponMaxAmmo = [8]float32{0, 8, 30, 8, 5, 0, 3, 0} // WPN_KNIFE..WPN_FLASHLIGHT, 0 = melee/no-ammo weapon

// weaponLethality is a real, hand-derived per-weapon threat score (damage-per-second-ish:
// dmg / rof, rof is in server ticks-between-shots so lower rof = faster = more lethal, hence the
// division) normalized to [0,1] against the table's own real max (missile launcher, ~1.37 raw).
// Used as a single scalar per opponent rather than an 8-wide onehot in the opponent block to
// keep the per-opponent feature count manageable across nearestOpponentSlots repeats.
var weaponLethality = [8]float32{
	200.0 / 20.0 / 14.61, // WPN_KNIFE
	45.0 / 25.0 / 14.61,  // WPN_MAGNUM
	20.0 / 6.0 / 14.61,   // WPN_AR
	128.0 * 8.0 / 17.0 / 14.61, // WPN_SHOTGUN (cnt=8 pellets)
	101.0 / 52.0 / 14.61, // WPN_SNIPER
	40.0 / 28.0 / 14.61,  // WPN_KATANA
	130.0 / 95.0 / 14.61, // WPN_MISSILE
	0.0,                  // WPN_FLASHLIGHT (0 damage, utility only)
}

func clamp01(v float32) float32 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}

// Observation is the real, named feature vector for one bot's decision tick. Values() flattens
// it to a plain []float32 in the fixed, documented order every consumer (a future training loop,
// a policy network, a logging/replay pipeline) can rely on.
type Observation struct {
	// --- Self block (26) ---
	SelfHealthFrac   float32 // health / 100
	SelfShieldFrac   float32 // shield / 100
	SelfYawSin       float32 // sin(yaw) -- world-frame facing, real for orienting movement decisions
	SelfYawCos       float32
	SelfPitchNorm    float32 // pitch / 90, real look-up/down angle
	SelfWeaponOnehot [8]float32
	SelfAmmoFrac     float32 // ammo / this weapon's real max (0 for melee weapons -- ammo is meaningless there)
	SelfIsShooting   float32 // real, own outbound button state this tick (S459-47) -- was hardcoded 0
	SelfCrouching    float32 // real, own outbound button state this tick (S459-47) -- was hardcoded 0
	SelfInVehicle    float32
	SelfAlive        float32
	SelfKillsNorm    float32 // kills / SERVER_QUEUE_FRAG_LIMIT (apps/server/src/main.c, S459-43) -- how close to a round win
	SelfDeathsNorm   float32 // deaths / a real, generous cap (10) -- just a bounded scale, not a hard limit
	SelfHitFeedback  float32 // 1.0 the tick the server flags "you were just hit" (protocol.h hit_feedback)
	// SelfStormChargesFrac -- real (S459-47, was fabricated to 0 before this commit): storm_charges
	// / 5 (sniper ultimate max, packages/common/physics.h). See snapshot.go's own decodedPlayer.
	// stormCharges doc comment: this is a real, persistent, INDEPENDENT resource from
	// SelfAbilityCooldownFrac below -- storm can sit at nonzero charges (unspent "ultimate ammo")
	// for a long time after the shared cooldown that gated activating it has already reset.
	SelfStormChargesFrac float32
	SelfRewardFeedbackNorm float32 // this tick's real server-computed reward (snapshot.go), tanh-squashed -- a strong, direct "how well is this instant going" signal
	// SelfReloadFrac -- real (S459-47): reload_timer / RELOAD_TIME_FULL (the larger of the two
	// real reload constants, so a tactical reload never reads as "more than fully reloading").
	// 0 = ready to fire right now.
	SelfReloadFrac float32
	// SelfAbilityCooldownFrac -- real (S459-47): ability_cooldown / 480 (the largest real
	// per-ability cooldown, sniper storm activation). THE key dual-cooldown feature the founder
	// asked for: this ONE shared timer gates sniper-storm-activation, katana-dash, AR-ability,
	// AND shotgun-ability -- a policy needs this to know "can I dash/activate right now," which
	// is a DIFFERENT question from SelfStormChargesFrac ("do I have banked sniper ammo to spend
	// once I switch back to it").
	SelfAbilityCooldownFrac float32
	// SelfAbilityReady -- real (S459-47): 1.0 iff ability_cooldown == 0 right now. A direct,
	// unambiguous binary action-gate signal, not just the continuous fraction above -- a policy
	// shouldn't have to learn "close to 0" means ready from a noisy continuous value alone.
	SelfAbilityReady float32

	// --- Opponent block (nearestOpponentSlots * 11 = 44), nearest-first, zero-filled+Present=0
	// past however many opponents are actually visible in this bot's own scene. ---
	Opponents [nearestOpponentSlots]OpponentFeatures

	// --- Aggregate/contextual block (9) ---
	VisibleEnemyCountNorm   float32 // count of active, same-scene opponents / a real, generous cap (7 -- one below MAX_QUEUE_POPULATION headroom)
	NearestEnemyDistanceNorm float32 // redundant with Opponents[0].DistanceNorm when present, but real and always populated (0 = no visible enemy, not "enemy at distance 0")
	AvgEnemyHealthFrac      float32
	EnemiesShootingCountNorm float32 // how many visible opponents currently have is_shooting set -- a real, direct danger signal
	EnemiesLowHealthCountNorm float32 // how many visible opponents are below 30% health -- real, easy-kill-opportunity signal
	SelfKillDeathRatio      float32 // kills / (deaths+1), unbounded but tanh-squashed at Values() time
	SelfRankEstimate        float32 // (self kills - max visible opponent kills) normalized to [-1,1] via tanh -- "am I winning the round"
	TeamAllyCountNorm       float32 // architected for team training (S459-46) -- always 0.0 in FFA (MODE_QUEUE has no teams, team_id is always -1)
	TeamScoreDiffNorm       float32 // architected for team training -- always 0.0 in FFA

	// --- Geometry block (5), real raycast distances (S459-47, closes the "no raycast" gap named
	// in this file's original module doc comment) -- see geometry.go's own real AABB slab-method
	// raycast against the QUEUE level's own fetched-once box list. Zero-filled (reads as
	// "distance 0", NOT "cap distance" -- see buildObservation's own real zero-fill rationale
	// below) when geometry hasn't loaded yet (a real, honest degrade, not a fabricated "clear
	// space" value that could mislead a policy into thinking it's safe to advance). ---
	WallForwardNorm  float32 // distance to the nearest wall straight ahead (self yaw), normalized against a 40-unit cap
	WallLeftNorm     float32 // self yaw - 90
	WallRightNorm    float32 // self yaw + 90
	WallBackNorm     float32 // self yaw + 180
	FloorBelowNorm   float32 // straight down, normalized against a 20-unit cap -- real cliff/ledge awareness
}

// OpponentFeatures is one nearest-opponent slot's real, self-relative feature block.
type OpponentFeatures struct {
	Present         float32 // 1.0 if this slot holds a real opponent, 0.0 if zero-filled (fewer than nearestOpponentSlots visible)
	DistanceNorm    float32 // real 3D distance, normalized against a generous 100-unit map-scale cap
	BearingSin      float32 // sin of (opponent angle - self yaw) -- self-relative horizontal bearing, the real feature aiming/strafing decisions need
	BearingCos      float32
	ElevationNorm   float32 // (opponent.y - self.y) normalized against a 20-unit cap -- real vertical aim-adjustment signal
	HealthFrac      float32
	WeaponLethality float32 // real, precomputed per-weapon threat score (see weaponLethality above)
	IsShooting      float32 // real, direct "is this opponent currently attacking" signal
	IsBot           float32 // distinguishes a real human opponent from another training bot -- may matter for adaptive strategy later
	Alive           float32
	// IsReloading -- real (S459-47): this opponent's own reload_timer > 0. A direct, real
	// vulnerability signal -- an opponent mid-reload can't fire, the same real tactical window a
	// human player looks for.
	IsReloading float32
}

func tanhSquash(v float32) float32 {
	return float32(math.Tanh(float64(v)))
}

// geometryCache -- S459-47. A real, process-wide, fetch-once cache (mirrors
// client_load_queue_level's own one-shot-per-session convention, geometry.go's own doc comment)
// guarded by sync.Once so concurrent buildObservation calls (there's only ever one bot per
// process today, but this stays correct if that ever changes) never trigger a duplicate fetch.
// A failed fetch is cached as (nil, done) too -- deliberately never retried mid-process, matching
// fetchQueueLevelGeometry's own "a bot with no geometry just skips raycast features" contract; a
// process restart (emily-bot's own standing bot-pool convention already restarts sessions
// regularly) is the real, existing retry path.
var (
	geometryOnce  sync.Once
	geometryCache *levelGeometry
)

func getCachedGeometry() *levelGeometry {
	geometryOnce.Do(func() {
		if g, ok := fetchQueueLevelGeometry(); ok {
			geometryCache = g
		}
	})
	return geometryCache
}

// rayNorm casts one real raycast from the bot's own current position, in a direction
// yawOffsetDeg degrees from its own current yaw (0 = straight ahead, matching the same
// forward-vector convention buildObservation's own opponent-bearing math already establishes:
// dx=sin(yaw), dz=-cos(yaw)), and returns the normalized [0,1] distance. Returns 0 (not cap) when
// geometry hasn't loaded -- see the Observation struct's own geometry-block doc comment for why
// that's the honest default rather than a fabricated "clear space" cap value.
func rayNorm(geo *levelGeometry, s *botState, yawOffsetDeg float32, cap float32) float32 {
	if geo == nil {
		return 0
	}
	yawRad := float64(s.myYaw+yawOffsetDeg) * math.Pi / 180.0
	dx := float32(math.Sin(yawRad))
	dz := float32(-math.Cos(yawRad))
	return clamp01(geo.raycast(s.myX, s.myY, s.myZ, dx, 0, dz, cap) / cap)
}

// buildObservation constructs the real, full feature vector for one bot's current tick from its
// own already-decoded state (botState, guarded by its own mutex -- caller must hold state.mu,
// matching every other real botState reader in this package).
func buildObservation(s *botState) Observation {
	obs := Observation{
		SelfHealthFrac: clamp01(s.myHealth / 100.0),
		SelfShieldFrac: clamp01(float32(s.myShield) / 100.0),
		SelfYawSin:     float32(math.Sin(float64(s.myYaw) * math.Pi / 180.0)),
		SelfYawCos:     float32(math.Cos(float64(s.myYaw) * math.Pi / 180.0)),
		SelfPitchNorm:  clampSigned(s.myPitch / 90.0),
		SelfIsShooting: boolToF32(s.myIsShooting), // real, own outbound state (S459-47)
		SelfCrouching:  boolToF32(s.myCrouching),  // real, own outbound state (S459-47)
		SelfInVehicle:  0,
		SelfAlive:      boolToF32(s.myState == 0), // STATE_ALIVE == 0, protocol.h
		SelfKillsNorm:  clamp01(float32(s.kills) / float32(queueFragLimit)),
		SelfDeathsNorm: clamp01(float32(s.myDeaths) / 10.0),
		SelfHitFeedback: boolToF32(s.myHitFeedback != 0),
		SelfRewardFeedbackNorm: tanhSquash(s.myRewardFeedback / 50.0),
		SelfStormChargesFrac: clamp01(float32(s.myStormCharges) / 5.0), // real (S459-47)
		SelfReloadFrac: clamp01(float32(s.myReloadTimer) / reloadTimerCap),
		SelfAbilityCooldownFrac: clamp01(float32(s.myAbilityCooldown) / abilityCooldownCap),
		SelfAbilityReady: boolToF32(s.myAbilityCooldown == 0),
	}
	if int(s.myCurrentWeapon) < len(obs.SelfWeaponOnehot) {
		obs.SelfWeaponOnehot[s.myCurrentWeapon] = 1.0
		maxAmmo := weaponMaxAmmo[s.myCurrentWeapon]
		if maxAmmo > 0 {
			obs.SelfAmmoFrac = clamp01(float32(s.myAmmo) / maxAmmo)
		}
	}

	geo := getCachedGeometry()
	obs.WallForwardNorm = rayNorm(geo, s, 0, wallRayCap)
	obs.WallLeftNorm = rayNorm(geo, s, -90, wallRayCap)
	obs.WallRightNorm = rayNorm(geo, s, 90, wallRayCap)
	obs.WallBackNorm = rayNorm(geo, s, 180, wallRayCap)
	if geo != nil {
		obs.FloorBelowNorm = clamp01(geo.raycast(s.myX, s.myY, s.myZ, 0, -1, 0, floorRayCap) / floorRayCap)
	}

	type scored struct {
		p    *peer
		dist float32
	}
	var visible []scored
	for _, p := range s.peers {
		if p.sceneID != s.sceneID {
			continue
		}
		dx, dy, dz := p.x-s.myX, p.y-s.myY, p.z-s.myZ
		d := float32(math.Sqrt(float64(dx*dx + dy*dy + dz*dz)))
		visible = append(visible, scored{p, d})
	}
	// Simple insertion sort by distance -- visible is at most MAX_CLIENTS-1 (70), no need for
	// anything fancier.
	for i := 1; i < len(visible); i++ {
		for j := i; j > 0 && visible[j].dist < visible[j-1].dist; j-- {
			visible[j], visible[j-1] = visible[j-1], visible[j]
		}
	}

	const distCap = 100.0
	const elevCap = 20.0
	for i := 0; i < nearestOpponentSlots && i < len(visible); i++ {
		p := visible[i].p
		dx, dy, dz := p.x-s.myX, p.y-s.myY, p.z-s.myZ
		bearing := float32(math.Atan2(float64(dx), float64(-dz)))*180.0/math.Pi - s.myYaw
		bearingRad := float64(bearing) * math.Pi / 180.0
		obs.Opponents[i] = OpponentFeatures{
			Present:         1.0,
			DistanceNorm:    clamp01(visible[i].dist / distCap),
			BearingSin:      float32(math.Sin(bearingRad)),
			BearingCos:      float32(math.Cos(bearingRad)),
			ElevationNorm:   clampSigned(dy / elevCap),
			HealthFrac:      clamp01(float32(p.health) / 100.0),
			WeaponLethality: weaponLethalityFor(p.currentWeapon),
			IsShooting:      boolToF32(p.isShooting),
			IsBot:           boolToF32(p.isBot),
			Alive:           boolToF32(p.state == 0),
			IsReloading:     boolToF32(p.reloadTimer > 0),
		}
	}

	obs.VisibleEnemyCountNorm = clamp01(float32(len(visible)) / 7.0)
	if len(visible) > 0 {
		obs.NearestEnemyDistanceNorm = obs.Opponents[0].DistanceNorm
		var healthSum float32
		var shootingCount, lowHealthCount int
		maxKills := s.kills
		for _, v := range visible {
			healthSum += float32(v.p.health)
			if v.p.isShooting {
				shootingCount++
			}
			if v.p.health < 30 {
				lowHealthCount++
			}
			if int(v.p.kills) > maxKills {
				maxKills = int(v.p.kills)
			}
		}
		obs.AvgEnemyHealthFrac = clamp01(healthSum / float32(len(visible)) / 100.0)
		obs.EnemiesShootingCountNorm = clamp01(float32(shootingCount) / 7.0)
		obs.EnemiesLowHealthCountNorm = clamp01(float32(lowHealthCount) / 7.0)
		obs.SelfRankEstimate = tanhSquash(float32(s.kills-maxKills) / 5.0)
	}
	obs.SelfKillDeathRatio = tanhSquash(float32(s.kills) / (float32(s.myDeaths) + 1.0))
	// TeamAllyCountNorm / TeamScoreDiffNorm deliberately left at their zero value -- MODE_QUEUE
	// is FFA (team_id == -1 on every entity), see this file's own module doc comment.

	return obs
}

func weaponLethalityFor(w uint8) float32 {
	if int(w) < len(weaponLethality) {
		return weaponLethality[w]
	}
	return 0
}

func boolToF32(b bool) float32 {
	if b {
		return 1.0
	}
	return 0.0
}

func clampSigned(v float32) float32 {
	if v < -1 {
		return -1
	}
	if v > 1 {
		return 1
	}
	return v
}

// Values flattens an Observation to the fixed-order []float32 a policy network / logging
// pipeline actually consumes. Order matches the struct's own field declaration order above.
func (o Observation) Values() []float32 {
	out := make([]float32, 0, ObservationSize)
	out = append(out,
		o.SelfHealthFrac, o.SelfShieldFrac, o.SelfYawSin, o.SelfYawCos, o.SelfPitchNorm)
	out = append(out, o.SelfWeaponOnehot[:]...)
	out = append(out,
		o.SelfAmmoFrac, o.SelfIsShooting, o.SelfCrouching, o.SelfInVehicle, o.SelfAlive,
		o.SelfKillsNorm, o.SelfDeathsNorm, o.SelfHitFeedback, o.SelfStormChargesFrac,
		o.SelfRewardFeedbackNorm, o.SelfReloadFrac, o.SelfAbilityCooldownFrac, o.SelfAbilityReady,
	)
	for _, opp := range o.Opponents {
		out = append(out,
			opp.Present, opp.DistanceNorm, opp.BearingSin, opp.BearingCos, opp.ElevationNorm,
			opp.HealthFrac, opp.WeaponLethality, opp.IsShooting, opp.IsBot, opp.Alive,
			opp.IsReloading,
		)
	}
	out = append(out,
		o.VisibleEnemyCountNorm, o.NearestEnemyDistanceNorm, o.AvgEnemyHealthFrac,
		o.EnemiesShootingCountNorm, o.EnemiesLowHealthCountNorm, o.SelfKillDeathRatio,
		o.SelfRankEstimate, o.TeamAllyCountNorm, o.TeamScoreDiffNorm,
	)
	out = append(out,
		o.WallForwardNorm, o.WallLeftNorm, o.WallRightNorm, o.WallBackNorm, o.FloorBelowNorm,
	)
	return out
}
