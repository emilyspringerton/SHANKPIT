# SHANKPIT Bot Training — Observation/Reward Design (S459-45/46)

Founder real-time (SECTION 459, verbatim across two messages): "hand engineer between 50-100
features for the bots" and "design the reward system for the bots for now these will just be ffa
bots we will do new training for teams so i dunno if you want to plan team rewards into the
rewards system now? we want the training to be FFA to start lets leave the team complexity out but
please plan for it architecturally if it makes sense - so same thing as BP bots - we need to add a
timer to the game mode."

This doc covers the real, current state of two of the four pieces asked for (features, reward
design) plus the round timer. What's still explicitly missing before real training can start:
the actual RL training loop (`THE_LEAGUE`'s own registry, `scripts/rl_league.py`, has nothing to
register yet), and the Colab notebook (explicitly gated by the founder on everything else being
ready first). See `EMILY/BACKLOG.md` SECTION 459 for the item-by-item status.

**S459-47 update** (founder feedback on the first pass): "fix the shortcomings no is crouch or is
firing - also no raycast fix all that not sure about the round trip timer thing also there are 2
different ultimates there is sniper ultimate and there is the weapon 6 dash... make sure the
features understand that there are 2 different cooldowns... also a state for if storm arrows is
active and how many ammos left in it also gun reload state and current equiped gun." All fixed —
see §7 below for the real, checked-first mechanics (`ability_cooldown` is a SHARED timer gating
sniper-storm-activation, katana-dash, AR-ability, and shotgun-ability all at once; `storm_charges`
is a separate, persistent resource that survives independently of that shared cooldown) and §8 for
the real raycast implementation. The round-timer wire-field gap named in §1 stays open — the
founder flagged genuine uncertainty about it ("not sure about the round trip timer thing"), not a
fix request, so it's left as still-honest, unresolved scope.

## 1. A real, found-live protocol bug had to be fixed first (S459-44)

Before any feature could be trusted, `apps2/emily-bot/main.go`'s own `PacketSnapshot` decode
turned out to be reading the wrong bytes entirely. It assumed a legacy 18-byte flat entity
(id/scene_id/x/y/z/yaw) starting at buffer offset 2. The real, live wire format — confirmed both
by reading `apps/server/src/main.c`'s `server_broadcast()` and by compiling a real
`sizeof`/`offsetof` probe against this exact build (not guessed from the header alone) — is a
12-byte `NetHeader`, a redundant 1-byte entity count, then `entity_count` real `NetPlayer` entries
of **64 bytes each**, starting at offset 13.

Every peer position the bot ever computed (and its own self-correction via the `myID` branch) had
therefore been reading garbage: `buf[2]` is the low byte of `NetHeader.sequence`, not an entity
id. Aim/targeting logic built on `nearest.x/y/z` had effectively been aiming at noise the entire
time this bot has existed. Fixed in `apps2/emily-bot/snapshot.go`, with a real byte-exact unit
test (`snapshot_test.go`) building a synthetic buffer matching the compiled layout and asserting
the decoder recovers the real values. Also newly decoded, never read before: `health`, `shield`,
`current_weapon`, `state`, `team_id`, `is_bot`, `ammo`, `kills`, `deaths`, `is_shooting`,
`crouching`, `in_vehicle`, and `reward_feedback`/`hit_feedback` (see §3).

## 2. The observation vector (`apps2/emily-bot/observation.go`) — 84 features

`ObservationSize = 84` (grew from an initial 72 in S459-47, closing three real gaps the founder
flagged directly — see §7/§8), inside the requested 50-100 range with no artificial padding —
every feature is real and named. Four blocks:

**Self (26 features)** — health/shield fraction, yaw as sin/cos (not raw degrees — avoids the
360°→0° wraparound discontinuity a raw-degree feature would hand a policy network), pitch,
current-weapon onehot (8), ammo fraction, real is_shooting/crouching (S459-47 — the bot's own
outbound button state, not a server round-trip; see §7), in_vehicle/alive flags, kills/deaths
normalized against the real round frag limit (`SERVER_QUEUE_FRAG_LIMIT=20`, §4), hit_feedback
(server-flagged "you were just hit" this tick), a tanh-squashed real-time reward signal (§3), and
four real dual-cooldown features (§7): storm-charge fraction, reload fraction, ability-cooldown
fraction, and an ability-ready binary flag.

**Nearest-opponent block (4 slots × 11 features = 44)** — nearest-first by real 3D distance, each
slot self-relative (bearing expressed as sin/cos of the angle *relative to the bot's own yaw*, not
world-absolute — the standard convention so learned weights generalize across map position/
orientation instead of memorizing absolute coordinates): presence flag, distance, bearing,
elevation, health fraction, a precomputed per-weapon lethality score (derived from
`protocol.h`'s real `WPN_STATS` table: damage/rate-of-fire, normalized against the missile
launcher's own real max), is_shooting, is_bot, alive, and is_reloading (S459-47 — a real, direct
vulnerability signal). Zero-filled past however many opponents are actually visible.

**Aggregate/contextual (9 features)** — visible-enemy count, nearest-enemy distance (redundant
with slot 0 but always populated even if the opponent block itself changes shape later), average
enemy health, count of enemies currently shooting (a real, direct danger signal), count of
low-health enemies (a real, direct opportunity signal), self kill/death ratio, a rank estimate
(self kills vs. the best-performing visible opponent, tanh-squashed), and two **team-architected**
placeholders (§5) that read as a real, always-zero value in FFA.

**Geometry block (5 features, S459-47, §8)** — real AABB raycast distances to the nearest wall
straight ahead / left / right / behind, plus straight down (floor/ledge awareness). Closes the
"no raycast" gap named in the original pass.

**Still a real, honest, open gap:**
- **No round-timer feature.** The server's own `SERVER_QUEUE_ROUND_MS` clock (§4) isn't
  transmitted anywhere a client can read it — `NetHeader.timestamp` is a raw server clock, not
  "time since round start." A real fix needs a new wire field. The founder flagged genuine
  uncertainty about this one ("not sure about the round trip timer thing") rather than asking for
  a fix, so it stays open by design, not by oversight.

## 3. The reward system (`apps2/emily-bot/reward.go`)

The single biggest real find here: `packages/common/physics.h` **already computes a real, dense,
server-authoritative reward signal per player** — `PlayerState.accumulated_reward`, +150.0 on a
confirmed kill (`phys_enter_death_state`), +0.5 per point of damage actually dealt
(`katana_apply_damage`, despite the name — every weapon's damage funnels through it). It reaches
the wire as `NetPlayer.reward_feedback`, reset to 0 the instant it's read into a snapshot or a
respawn is granted. This had simply never been decoded before S459-44/45 — it's the exact signal
a reward function wants, computed authoritatively server-side, and far less noisy than any
client-side health-delta proxy would be (health also moves from healing/shield regen, unrelated
to combat).

Four tiers, same overall shape as BRAWLPIT's own `compute_reward`
(`BRAWLPIT/scripts/rl_env_packet.py`) adapted to SHANKPIT's real signals instead of BRAWLPIT's
stock/damage-percent model:

1. **Outcome** — `cur.RewardFeedback` scaled down (÷50, to keep it in the same rough numeric range
   as the shaping terms below rather than a bare +150 dwarfing everything else by two orders of
   magnitude), plus a real, explicit death penalty (`accumulated_reward` only credits the
   *attacker*; the victim needs its own term).
2. **Low-health shaping** — a real, direct incentive to disengage under 30% health (matched to
   the existing commander-posture retreat threshold in `packages/simulation/local_game.h`, not a
   new unrelated number): small penalty for staying engaged, small credit for creating distance.
3. **Survival** — a numerical-stability nudge only, two orders of magnitude below a real reward
   tick, same rationale as BRAWLPIT's own `REWARD_ALIVE_PER_TICK`.
4. **Engagement** — a small credit for closing distance on a visibly weaker (lower-health)
   opponent, the real fix for a policy that could otherwise learn "hide forever" as a degenerate
   local optimum (QUEUE's continuous respawns mean there's no single "did nothing all match"
   terminal condition the way a timed 1v1 has, so this is framed as a positive pull rather than a
   standing inactivity penalty).

Verified with real unit tests (`observation_test.go`): a kill scores positive, a death scores
negative, and disengaging while low-health scores strictly higher than continuing to engage.

## 4. QUEUE round timer (S459-43)

`apps/server/src/main.c` gained `SERVER_QUEUE_FRAG_LIMIT=20` / `SERVER_QUEUE_ROUND_MS=4min` and a
`server_advance_queue_round()` function, wired into the same tick-loop pattern the existing DM
round/map-rotation check already uses — except QUEUE never reloads the level (it always plays the
one admin-flagged default, S459-41), so a round-end just resets kills/deaths/health/shield and
continues, no map-rotation call, no wasted registry round-trip.

## 5. Team reward, architected not built (S459-46)

Per the founder's own explicit instruction, MODE_QUEUE stays FFA-only for now. `reward.go` defines
a real, typed `TeamRewardContext` parameter (`{Enabled bool}`) threaded through `computeReward`
today as a genuine no-op — not a guessed-at implementation. The real additions a team variant
would need, once one exists: a team-kill-assist credit (damage/kill by an ally counts as a
smaller, real positive), a team-score-differential shaping term (same shape as the low-health
tier, keyed to `local_state.team_scores[my_team] - team_scores[enemy_team]`, which
`MODE_TDMO` already populates), and a terminal team-win/loss outcome. `team_id` already flows over
the exact same wire field the observation vector's `Opponents[i].` block reads (`NetPlayer.team_id`,
decoded since S459-44) — just always -1 in FFA — so a team variant needs no observation-shape
change, only real, non-zero values starting to appear in fields that already exist.

## 7. The two real ultimate/ability cooldowns (S459-47)

Founder real-time, precise and worth quoting in full: "there are 2 different ultimates there is
sniper ultimate and there is the weapon 6 dash which is very powerful and can allow insane
movement so make sure the features understand that there are 2 different cooldowns and you can
activate sniper cooldown and let the cooldown reset and then dash on weapon 6 and if you never
shot the sniper projectiles from storm they are still activated - also a state for if storm arrows
is active and how many ammos left in it also gun reload state and current equiped gun."

Checked directly against `packages/common/physics.h` before building anything (not guessed):
`update_weapons()` and `katana_try_start_dash()` share **one single field**,
`PlayerState.ability_cooldown`, across FOUR different abilities — AR's ability (260 ticks),
shotgun's ability (340 ticks), katana's dash (`KATANA_DASH_COOLDOWN`=420), and sniper's storm
activation (480 ticks). Activating any one of them sets this one shared timer; every other
ability is gated on it reading 0, regardless of which weapon is currently equipped. This is
genuinely different from `PlayerState.storm_charges` (capped at 5): activating storm sets BOTH
`storm_charges=5` AND `ability_cooldown=480` in the same instant, but `storm_charges` then
persists on its own — a player who never actually fires a sniper shot keeps those 5 banked
charges available indefinitely, including well after `ability_cooldown` has already ticked back
to 0 and been spent again on a katana dash. The founder's own described sequence — activate
storm, let the shared cooldown expire, dash on weapon 6, still have unfired storm rounds banked —
is exactly this mechanic, confirmed in the real source, not assumed from the ask.

Neither `reload_timer` nor `ability_cooldown` was ever transmitted to any client before this
commit — a real, previously-unnoticed protocol gap. Added both to `NetPlayer`
(`packages/common/protocol.h`; struct grew from 64 to 68 bytes, re-verified via the same
compiled `sizeof`/`offsetof` probe technique this doc's §1 already established, never
hand-guessed) and populated in `server_broadcast()` (`apps/server/src/main.c`), clamped into
`unsigned short` (both real source fields are `int`, but every real value — max 480 — fits with
enormous headroom). The C client (`apps/lobby`) needed zero code change: its own snapshot decode
copies `sizeof(NetPlayer)` directly, so it picked up the new fields automatically on rebuild.
`apps2/emily-bot/snapshot.go` needed its hand-rolled offsets recomputed (`snapshot_test.go`'s own
`TestDecodePacketSnapshot_RealWireLayout` catches a future drift the same way).

Four new self-features result: `SelfStormChargesFrac` (now real — was fabricated to 0 in the
first pass), `SelfReloadFrac`, `SelfAbilityCooldownFrac`, and `SelfAbilityReady` (a direct binary
gate, not just the continuous fraction — a policy shouldn't have to learn "close to 0 means
ready" from a noisy value alone). A fifth, `Opponents[i].IsReloading`, gives the same real
vulnerability signal about visible opponents. `SelfIsShooting`/`SelfCrouching` were also fixed in
the same pass — they don't need a server round-trip at all, since the bot already knows its own
current button state the instant it decides `buttons` each tick (`main.go`'s tick function now
sets `botState.myIsShooting`/`.myCrouching` directly, read by `buildObservation`). Verified with
real unit tests (`TestSelfDualCooldownFeatures` reproduces the founder's own exact scenario:
storm charges stay at 5/5 through a full cooldown window, `SelfAbilityReady` correctly tracks the
shared timer independently).

## 8. Real raycast / level-geometry features (S459-47)

New `apps2/emily-bot/geometry.go`: fetches the real, currently-flagged QUEUE default level's box
list (mirroring `apps/lobby`'s own `client_load_queue_level`, S459-39/41 — same
registry-lookup-by-`is_default_queue` contract, independently fetched client-side since the wire
protocol only ever carries a `scene_id` byte, never geometry) via Go's real `encoding/json`
against IDUNA's actual export endpoint, cached once per process (`sync.Once` — a queue level
doesn't change mid-session in practice; a bot-pool process restart, already a standing
convention, is the real retry path on failure). A real AABB slab-method raycast
(`levelGeometry.raycast`) against that box list, verified with a direct unit test
(`TestRaycast_SimpleWall`) independent of any network fetch.

Five new geometry features: wall distance straight ahead / left / right / behind (relative to the
bot's own current yaw, same forward-vector convention the opponent-bearing math already
establishes), plus straight down (floor/ledge awareness). A bot with no geometry loaded yet reads
these as a real, honest 0 — not a fabricated "clear space" cap value that could mislead a policy
into thinking it's safe to advance.

## 9. What's still missing before training can actually start

- **The RL training loop itself.** `scripts/rl_league.py` (S459-35) is real checkpoint-
  registry/PFSP/ELO plumbing ported from BRAWLPIT, but nothing generates checkpoints for it to
  register yet — no packet-level training harness exists for SHANKPIT the way
  `BRAWLPIT/scripts/rl_env_packet.py` does for BRAWLPIT. This is the real, honest, single biggest
  remaining gap.
- **A round-timer wire field**, so a client-side observation can actually include match-clock
  urgency (BRAWLPIT's own `time_pressure_multiplier` precedent) — real, still open, the founder
  flagged uncertainty rather than asking for a fix (§2).
- **The Colab training notebook** — explicitly gated by the founder on the above being ready.
