# BIG_O → SHANKPIT engine merge — NORTHSTAR (S536, 2026-09-22)

Founder real-time (remote control), in sequence: *"port the BIG_O thech into the shankpit repo -
all of it the lighting the systems all of it - bring it clean into shankpit upgrading the current
engine to support tese new features use parena duh"* → *"BIG_O replaces shankpit STORY"* → *"first
class citizen"* → *"not an app the tech comes into the repo"* → *"do not add a licens to SHANKPIT"*
→ *"parena powered bring in reflux into shankpit"* → *"the shaders the way the sun and moon look
the phone in story mode everything"* → *"all the events and messages on the phone"*.

Per THE_EMILY_WAY Principle 19 this is a big, unscoped ask spanning many real subsystems: this doc
investigates what exists in both repos, cuts what's actually landed in this pass, and phases the
rest into `EMILY/BACKLOG.md` SECTION 536 — it does not claim the whole thing is done in one pass.

**Direction, confirmed:** BIG_O's systems become native SHANKPIT engine tech — not a sixth
SHANKPIT OS launchable app (the IDUNA.GAME/EDITOR.GAME/REDGARDEN precedent does NOT apply here).
BIG_O's own day/night/lab loop becomes what SHANKPIT's `MODE_STORY` (105) actually plays, once
enough of the underlying systems have landed. No `LICENSE` file gets added to SHANKPIT (BIG_O has
one; SHANKPIT does not and stays that way).

## 1. What existed in each repo before this pass (checked, not assumed)

- **SHANKPIT** already had: a mature `story_ai.c`/`.h` AI system (1418 lines — patrol/investigate/
  combat/flee/squad, MODE_STORY=105/MODE_STORY_CAVE=107), `humanness.c` (the same MISHRI-derived
  primitives BIG_O's own `core/humanness.c` was vendored FROM), a real GOLDENBAND skeletal
  character renderer, a real PARENA runtime integration (`packages/world/parena_runtime.c`,
  `packages/simulation/parena_runtime.h`, one prior PARENA-generated mod — `cutscene_effect_mod.c`,
  from `PARENA/stdlib/tyler/cutscene_mod.prn`, not yet wired into a live host), and its own native
  REFLUX pub/sub port (`packages/reflux/reflux_runtime.c`, S485, hand-written host log — see §3).
  Its sky/lighting was `packages/render/retro_sky.c` (`RetroSky`): a fixed, non-configurable,
  weather-blind fast orbit (`time_sec * 0.025`, a full day every ~4 minutes), feeding
  `retro_lighting.c`'s `RETRO_LIGHTING_DYNAMIC` preset for scene ambient/sun/moon/fog too.
- **BIG_O** had built, since its own 2026-09-18 NORTHSTAR pass: a PARENA-authored Attention/Witness
  rules module (`core/witness_rules.c`, from `PARENA/stdlib/big_o/witness_rules.prn`, VS0 domain
  3), a day/night+weather clock and zombie population (`core/world.c`/`.h`, rules from
  `PARENA/stdlib/big_o/world_rules.prn`), a citizen/Men AI-brain layer (`core/npc_archetype.c`) and
  a zombie-specific values module (`core/zombie_values.c`) both plain C (not yet PARENA), a
  cloning-facility lab simulation (`core/lab_sim.c`, 17 tests, plain C), pheromone command tools
  (`day/packages/common/bigo_pheromone.h`) with a real server dispatch loop for The Men
  (`apps/server/src/main.c`'s `server_tick_witness`/`server_tick_dispatch`), a live NPC entity
  system over the wire, and a genuinely more advanced weather-aware sky renderer
  (`day/packages/common/bigo_sky.h`/`bigo_skycfg.h` — config-file-driven, 4 weather states,
  clouds, rain, lightning, screen grading) than SHANKPIT's own `retro_sky.c`. BIG_O's own
  `core/reflux_runtime.c` is a separate, simpler, hand-written REFLUX copy (not PARENA-generated
  either) — a real fork divergence from SHANKPIT's, not a more-advanced version.
- **PARENA** already had a real, standalone, engine-agnostic `stdlib/reflux/reflux.prn` (thin
  `#target inline-c` wrappers over `reflux_host_*`) that neither repo's own REFLUX copy had
  actually been wired up to — see §3.

## 2. Landed this pass (verified: compiles clean, `make lobby`/`make server` both build, real
tests pass) — the day/night clock, the weather-aware sky, and REFLUX becoming genuinely
PARENA-powered

**2a. Day/night + weather clock — `packages/simulation/day_night_clock.{h,c}` +
`packages/simulation/world_rules.c`.** `world_rules.c` is real, PARENA-generated output (built via
`parena build stdlib/shankpit/world_rules.prn -o packages/simulation/world_rules.c`) from
`PARENA/stdlib/shankpit/world_rules.prn` — a new SHANKPIT stdlib domain, copied verbatim from
`PARENA/stdlib/big_o/world_rules.prn` (identical logic; SHANKPIT now owns its own copy rather than
cross-repo-importing BIG_O's). `day_night_clock.c` is the host wrapper (rng, minute clock, weather
scheduling) — a **deliberate v0 scope cut** vs. BIG_O's own `World`: no zombie population, no
per-player area/harvest, no `Sim*` coupling, since SHANKPIT has no equivalent player/Sim model and
that gameplay layer arrives with the witness-rules port (§4, not this pass). 6 real tests
(`day_night_clock_test.c`, plain `assert()` harness matching `humanness_test.c`'s own convention) —
phase-band transitions, day rollover, the weather cycle never skipping a step, `force_weather`,
name helpers — all pass (`gcc -Wall -Wextra -O2 ... && ./dnc_test` → `ALL PASS`).

**2b. Weather-aware sky — `packages/render/sky_weather.{h,c}` + `sky_weather_cfg.h` +
`assets/skybox/default.cfg`.** Ported in from BIG_O's `bigo_sky.h`/`bigo_skycfg.h`, renamed
(`Bigo*`/`bs_`/`bsc_` → `SkyWeather*`/`sw_`/`skw_`) and split into a real `.c`/`.h` pair (BIG_O had
it header-only-inline; `retro_sky.c`'s own convention is a real compiled unit, matched here).
Upgrades `retro_sky`'s fixed fast-orbit dome with a real, config-file-driven, 4-weather-state sky:
day/golden/dawn/twilight/night palettes, clouds, rain streaks, lightning bolts, screen-space night/
storm grading. Wired into `apps/lobby/src/main.c`: initialized alongside the existing
`retro_sky_init` call, ticked off wall-clock time (`DAY_NIGHT_MINUTES_PER_REAL_SEC = 1.0`, a real,
named v0 choice — **server-authoritative broadcast is not yet built**, see §5.1), and drawn in
place of the old `retro_sky_draw` call — it reads the current OpenGL modelview matrix directly
(captured for GOLDENBAND skinning immediately above the call site) and strips its own translation,
so it no longer needs the explicit `cam_x/y/z` SHANKPIT's old call site computed by hand.
`retro_sky.c`/`.h` themselves are **left in place, unchanged** — `retro_lighting.c`'s
`RETRO_LIGHTING_DYNAMIC` preset still calls `retro_sky_eval_sun_dir`/`retro_sky_eval_fog_rgb` for
scene ambient/sun/moon/fog lighting; wiring THAT to `sky_weather`'s own real weather state (so a
storm actually darkens the walls, not just the sky dome) is named, not done — §5.2. Verified: the
full `LOBBY_SRC` compiles clean under `-Wall` (only pre-existing, unrelated warnings elsewhere in
the file) and links; `make lobby` produces a fresh `bin/shank_lobby`. **Cannot be visually
verified** in this sandbox (no GL driver, same standing limitation every prior SHANKPIT/BIG_O
client-render pass already carries) — compiled-and-linked-correctly is the real bar this pass
clears, not a live screenshot.

**2c. REFLUX becomes genuinely PARENA-powered — `packages/reflux/reflux_mod.c` +
`reflux_mod_host.h` + `parena_runtime.h`.** Founder-named explicitly: *"parena powered bring in
reflux into shankpit."* Real finding: SHANKPIT's own `reflux_runtime.c` (S485) already implemented
the *exact* `reflux_host_dispatch`/`reflux_host_log_size`/`reflux_host_action_*_at` contract
`PARENA/stdlib/reflux/reflux.prn`'s own `#target inline-c` bodies expect — it was simply never
compiled against the real `.prn` module. `reflux_mod.c` is real, PARENA-generated output (`parena
build stdlib/reflux/reflux.prn -o packages/reflux/reflux_mod.c`); `reflux_mod_host.h` is a real,
hand-written extern-declaration header for the generated file's own 6 entry points, ported in from
ECOWAR's identical, already-shipped precedent (`ECOWAR/packages/reflux/reflux_mod_host.h` — same
repo that originated REFLUX). Wired into both `LOBBY_SRC` and `SERVER_SRC` in the Makefile, and
into `packages/reflux/BUILD.bazel`. Verified: a real test (`reflux_mod_test.c`, 6 checks) confirms
`reflux_dispatch`/`reflux_log_size`/`reflux_action_type_at`/etc — the actual PARENA-compiled
mod-facing API — round-trip correctly through the existing native host log; `make lobby`/`make
server` both rebuild clean with it linked in.

## 2d. Phase 2 landed — witness/attention rules, as a standalone primitive (not yet wired into gameplay)

**Real, checked-first finding that corrected this phase's own original plan:** the BACKLOG's phase
2 entry originally speculated wiring `witness_rules.c` into `story_ai.c`'s own `AIMode`/`AIRole`
state machine. Reading both confirmed that's a category error — `story_ai.c`'s roster (Rift Hound,
Shambler Trooper, Guard, ...) is hostile combat AI; BIG_O's witness system is a different NPC
concept entirely (ambient citizens/The Men reacting to being *witnesses* of an event). There is no
existing "citizen" NPC slot in `story_ai.c` to attach this to. Corrected plan, landed instead:

`packages/simulation/witness_sim.{h,c}` + PARENA-generated `witness_rules.c` (from new
`PARENA/stdlib/shankpit/witness_rules.prn`, copied verbatim from `PARENA/stdlib/big_o/
witness_rules.prn`) — a faithful, renamed port of BIG_O's own `core/sim.{h,c}` (`Sim`/`SimPlayer`/
`SimNpc` → `WitnessSim`/`WitnessPlayer`/`WitnessNpc`, `sim_*` → `witness_sim_*`). Genuinely
self-contained: zero dependency on BIG_O's `World`/zombie layer, `reflux_runtime.h`, or anything
else beyond the pure rules functions — this is the same "prove the primitive in isolation first"
discipline BIG_O's own NORTHSTAR already used for `npc_archetype.c`/`zombie_values.c`/`lab_sim.c`.
Covers the full mechanic: witness escalation (UNAWARE→DENIAL→SILENCING/PANIC/ENGAGE, COMPROMISED
absorbing), per-player Decorum (0..100, OK/SUSPICION/HYSTERIC/CANCELLED bands), costume×zone
trespass (SUIT/LAB_SMOCK/JANITOR/STREET × PUBLIC/LAB/EXEC/GENERATOR/VAULT), crew attribution (1-3
players, attributed vs. "seen together"), and hunt resolution (a SILENCING/ENGAGE hunt persists
through lost line of sight, only ends on a real memory-wipe or target-eliminated event).

**Verified against BIG_O's own canonical scenarios, not just "it compiles."** `witness_sim_test.c`
replays three of BIG_O's real `scenarios/*.txt` test scripts (`01_lone_witness_denial`,
`02_five_witness_silencing`, `04_decorum_is_per_player`) as direct C assertions against this port,
plus a fourth check for the hunt-persists-through-loslost behavior — all pass. Deliberately **not**
wired into `Makefile`'s `LOBBY_SRC`/`SERVER_SRC` or a `BUILD.bazel` target yet, matching
`cutscene_effect_mod.c`'s own already-established precedent in this same package: a real, tested,
not-yet-consumed module gets no build-graph entry until something actually calls it, rather than
inventing build coverage ahead of real usage. The real consumer is phase 7 (MODE_STORY content
cutover) — a citizen-NPC spawn/render layer that doesn't exist yet.

## 2e. Phase 3 landed — humanness AI-brain extensions, with the two decision formulas moved to PARENA

`packages/simulation/npc_archetype.{h,c}` and `packages/simulation/zombie_values.{h,c}` — ported
in from BIG_O's `core/npc_archetype.c` (citizen/The Men archetype-differentiated vigilance, a thin
real layer over SHANKPIT's own already-native `humanness.c` — diffed clean against BIG_O's own
copy except one build-toolchain-only `M_PI` literal that doesn't apply to this repo's Makefile
build) and `core/zombie_values.c` (zombies' own hunger/aggression/decay vocabulary, a 4-state mood
arc, deliberately NOT sharing `NpcBrain`'s human mood enum — the founder's own explicit "more
zombie values and behaving, you know?").

**Per "use parena duh":** the two real, pure scalar decision formulas —
`npc_brain_effective_vigilance` (base vigilance + mood/fatigue/energy deltas, clamped 0..100) and
`zombie_effective_alertness` (per-mood baseline + hunger/aggression contribution, clamped 0..100)
— moved to a new PARENA rules module, `PARENA/stdlib/shankpit/ai_brain_rules.prn`
(`npc-effective-vigilance`/`zombie-alertness-formula`, generated into `ai_brain_rules.c`), the same
"designer-tunable decision logic" shape `witness_rules.prn` already holds. **Everything else stays
host C** — `HumannessState`/`ZombieState` mood ticking, RNG-jittered reaction-delay/lunge-noise
sampling — matching `humanness_tick_mood`'s own already-established precedent in this exact
codebase (stateful/time-driven code doesn't move, pure decisions do). A real, found naming
collision along the way: the PARENA export and the host wrapper both wanted the name
`zombie_effective_alertness` — resolved by naming the PARENA-side formula
`zombie_alertness_formula`, keeping the host API's original BIG_O-facing name unchanged.

**Verified against BIG_O's own real test assertions** (`core/npc_archetype_test.c`,
`core/zombie_values_test.c`), not just "it compiles" — important here specifically because the
formulas moved through PARENA along the way, a real behavior-preserving risk this checks:
archetype-differentiated defaults, effective vigilance bounded across 500 fatigue/energy trials,
STARTLED raising vigilance by exactly +25, TIRED+fatigue lowering it, zombie alertness bounded and
strictly increasing across the real DORMANT→AGITATED→HUNTING→FRENZIED mood arc, and real hunger/
aggression drift direction. All pass (`ai_brain_test.c`). Deliberately not wired into
`Makefile`/`BUILD.bazel` yet, same `cutscene_effect_mod.c` precedent §2d already used — no live
consumer exists (phase 7).

## 2f. Phase 4 landed — lab simulation, ported verbatim (kept plain C, not PARENA)

`packages/simulation/lab_sim.{h,c}` — a verbatim port of BIG_O's `core/lab_sim.{h,c}` (the
cloning-facility equipment pipeline: centrifuge, PCR thermocycler, sequencer/bioinformatics
readout, CRISPR splice bench, repressor/kill-switch install, breeding/genetic drift, embryo
incubation). No renaming needed — zero BIG_O-specific type names in this module to begin with.

**Deliberately kept plain C, not moved to PARENA**, unlike phase 3's formulas — checked, not
assumed: this module is saturated with float math (`expf`/`powf` for diminishing-returns/
exponential-amplification curves) and RNG (Box-Muller sequencer noise, splice-outcome probability
rolls), none of which fits PARENA's scalar I32/Bool, no-RNG VS0 model. This matches the module's
own already-documented reasoning ("not currently expected to need mod-author tuning") and is a
real, different judgment call than phase 3's — not a blanket "everything becomes PARENA" policy.

**Verified with a representative subset of BIG_O's own 17 real tests** (`lab_sim_test.c`, 7
checks covering every equipment function's real contract at least once) — since this is a
verbatim, zero-logic-transformation port (unlike phase 3), full re-derivation of all 17 wasn't
needed to establish confidence the same way it was for the PARENA move. One real, live-found bug
in this test's own first draft, not the port: a genetic-drift-never-decreases check that bred a
growing-drift line against a fixed low-drift constant partner every generation, which pulls the
*average* down even though each individual breeding step's own gain is one-way positive — fixed by
matching BIG_O's own real test methodology (the partner's drift is set to match the line's current
drift each generation before breeding). All 7 checks pass. Deliberately not wired into
`Makefile`/`BUILD.bazel` yet, same precedent §2d/§2e already used — no live consumer (phase 6/7).

## 2g. Phase 5 landed — pheromone command primitive + reserved wire packet (dispatch loop still blocked)

`packages/common/pheromone.h` — a verbatim port of BIG_O's `day/packages/common/bigo_pheromone.h`
(pure, header-only targeting/steering: marker expiry, slot-claiming with soonest-expiring
eviction, nearest-marker-within-radius search, overshoot-free step-toward movement), renamed away
from the `Bigo*` prefix per "first class citizen." Verified with a faithful port of all 8 of
BIG_O's own real `bigo_pheromone_test.c` checks (`pheromone_test.c`) — all pass.

`PACKET_PHEROMONE_THROW = 11` reserved in `packages/common/protocol.h`, matching that same file's
own already-established "reserved, not yet implemented" precedent (packets 8/9 for BEDWARS). No
struct defined for it — checked this repo's own convention first: `protocol.h`'s own
`RacingTelemetry` comment explicitly warns that a direct struct-cast onto the wire buffer doesn't
match the real byte layout once the compiler pads for alignment, so packets here are parsed by
hand at the real call site, not via a cast-a-struct-onto-the-buffer pattern BIG_O's own
`PcPheromoneThrowPacket` used. The real byte layout gets decided when a real consumer parses it.

**The Men's dispatch loop itself is genuinely not portable yet, not just deferred by choice.**
BIG_O's `server_tick_witness`/`server_tick_dispatch` (`apps/server/src/main.c`) operate on a live
`ServerNpc` array with `witness_state` fields — SHANKPIT's own server has no equivalent zombie/
citizen NPC entity concept at all (`story_ai.c`'s `AIController` is combat-AI, a different thing,
same finding §2d already made for witness rules). This is the same real blocker phases 2-4's own
primitives share: no consumer exists until phase 7 builds a live NPC spawn/tick layer. Deliberately
not wired into `Makefile`/`BUILD.bazel` yet, same precedent already used throughout.

## 2h. Phase 6 landed — the phone, and a genuinely working event pipeline (not just another standalone primitive)

Founder, twice: *"the phone in story mode everything"*, *"all the events and messages on the
phone."* This phase goes one step further than phases 2/3/5's own "correct primitive, no live
consumer yet" pattern — it composes four pieces (three already shipped, one new this pass) into an
actually-working, tested, end-to-end pipeline, without editing any already-shipped file:

- **`packages/common/phone.h`/`phone_test.c`** — a verbatim port of BIG_O's `bigo_phone.h` (the
  real smartphone state machine: home grid, Messages/Contacts/Map/Camera/Notes/Lab/Cargo/Skills/
  Loadout/Wardrobe/Status apps, and the notification system with its real anti-spam rule — max 2
  banners per 30s, the rest queued and released as a batched summary). `BigoPhone`→`Phone`,
  `bigo_phone_*`→`phone_*`; the `Bp`/`BP_` prefix on enums/constants is kept (not overtly
  BIG_O-branded, dozens of call sites, no real value in a purely cosmetic rename). Verified with a
  faithful, verbatim port of BIG_O's own comprehensive single-file test — passes unchanged.
- **`PARENA/stdlib/shankpit/world_alerts_mod.prn`** — copied from `stdlib/big_o/
  world_alerts_mod.prn` (a real REFLUX subscriber: polls the shared action log, decides whether a
  world event deserves a phone message and which one — nightfall/daybreak/storm/brute-sighted),
  generated into `packages/simulation/world_alerts_mod.c`.
- **`REFLUX_ACTION_PHASE_CHANGED`/`WEATHER_CHANGED`/`ZOMBIE_SPAWNED`/`ZOMBIE_HARVESTED`** added to
  `packages/reflux/reflux_runtime.h` (101-104, matching `world_alerts_mod.prn`'s own expected
  numbering) — the zombie ones stay reserved-not-dispatched (no zombie population exists), matching
  that file's own already-established convention for `PROXIMITY_ENTER`/`EXIT`/`LOOK_AT`.
- **`packages/simulation/world_alert_bridge.{h,c}`** — new this pass, the real glue: on every real
  tick, detects a `day_night_clock` (phase 1) phase/weather transition, dispatches the matching
  REFLUX event (phase 1c's PARENA-powered REFLUX), polls the log, asks `world_alerts_mod` whether
  it deserves a message, and calls `phone_notify` if so. Composes four independently-shipped
  pieces without modifying any of them — day_night_clock.c, reflux_mod.c, and phone.h are all
  untouched by this file. **Real, named limitation, not papered over:** the polling cursor uses
  REFLUX's own PARENA-exposed relative indexing (no total-dispatched counter is exposed at that
  layer), so it can theoretically drift if 256+ *other* REFLUX events (from unrelated systems —
  the log is global) land between two of this bridge's own ticks; calling it every real game tick,
  as intended, makes that practically unreachable for the 1-2 events this bridge itself dispatches,
  but it's a real, structural property of the current REFLUX polling contract, stated plainly.

**Verified live, end to end, not just per-piece:** `world_alert_bridge_test.c` runs a real
`DayNightClock` through an actual DAWN→DAY transition and a forced STORM, confirming the message
genuinely round-trips through the real REFLUX log (not a mock) and lands on the real `Phone`
struct with the exact message ids `world_alerts_mod`'s own mapping specifies (3 = daybreak, 4 =
storm) — plus a negative check that a no-op tick raises nothing. All pass.

**Real, honest remaining gap:** none of this is drawn on screen. `phone.h`'s state machine and
`phone_set_world` (fed by `day_night_clock`'s own minute/day/phase/weather) are real and tested,
but `apps/lobby/src/main.c` has no 2D text/menu rendering path wired to actually display a phone
UI, and no input binding (a key to open it) exists yet. That's real, separate client-rendering
work, not named as its own phase number since it's a natural continuation of this one once a
render pass is scoped — tracked as a follow-up below.

## 2i. Phase 7a-7c landed — witness/zombie live-event glue, the live population/tick loop, and zone authoring (engine side)

Phase 7 (`MODE_STORY` content cutover) is far bigger than any phase landed so far — checked
directly before starting: `story_ai.c` is 1418 real lines (squads, patrol, nav graph, leash,
scripted holds, greet), wired into `local_game.h`, `apps/server/src/main.c`, and
`apps/lobby/src/main.c`, and real NOCK-authored levels already exist in the live level registry
built against its `AI_ROLE_*` roster (per `SHANKPIT/CLAUDE.md`'s own standing instruction, "levels
are never story-mode-only" — these are not throwaway test content). A blind, one-shot "replace it
all" is not a safe move against live, shipped content. Per Principle 19 (investigate, cut a real
V0, phase the rest), phase 7 is broken into sub-phases; this pass lands the first one.

- **`packages/simulation/witness_live.h`/`witness_live_test.c`** — ported from BIG_O's own
  `core/witness_live.h` (`bigo_*` → `witness_live_*`, a genuinely BIG_O-branded prefix unlike the
  `WS_`/`ZONE_` enums phase 2 left alone). Pure glue wiring a zombie's live mood
  (`zombie_values.h`, phase 3) into `witness_rules.c`'s own pure decision function
  (`npc_next_state`, phase 2): a HUNTING/FRENZIED zombie is a "loud event," witnessed by every
  human NPC within `WITNESS_LIVE_DETECTION_RADIUS`. Real finding, checked directly: even BIG_O's
  own original version deliberately stayed a pure, zero-`ServerNpc`-dependency header — porting it
  here is exactly the same real amount of "live" as BIG_O itself ever built, not a step behind it.
  Verified with a faithful, verbatim replay of BIG_O's own 8 real assertions (`witness_live_test.c`)
  — all pass, zero behavior drift. Standalone primitive, no Makefile/BUILD.bazel entry yet (same
  "no build-graph entry without a real consumer" discipline as phases 2/3/5), since the actual
  live population loop that calls it landed in the same pass — see 7b below.
- **`packages/simulation/witness_ai.h`/`.c`/`witness_ai_test.c`** (phase 7b) — the real
  `ServerNpc`-shaped population/tick loop 7a's own header named as its still-open follow-up.
  Checked directly before writing it: SHANKPIT already has the right shape for this — not a new
  concept, the same "bot occupies a real `PlayerState` slot" convention `story_ai.c`'s own
  `story_ai_spawn_enemy` already uses (`MAX_CLIENTS`=70 slots, `s->players[i]`), which every
  connected client already renders/networks for free. `witness_ai_spawn_citizen`/
  `witness_ai_spawn_zombie` allocate into that same slot pool (independent of `story_ai.c`'s own
  `g_story_ai` bookkeeping — the two systems share only the slot pool, nothing else).
  `witness_ai_tick` composes FOUR already-shipped pieces into one genuinely live pipeline, same
  "compose, don't just add another standalone primitive" discipline phase 6's own
  `world_alert_bridge` established: ticks each citizen's `NpcBrain` (phase 3) and writes its real,
  mood-modulated effective vigilance back into the owned `WitnessSim` (phase 2) npc entry; ticks
  each zombie's `ZombieState` (phase 3); then runs the actual `witness_live.h` (7a) integration
  pass — every zombie whose current mood is a loud event gets a real, radius-gated witness count
  against every live citizen, and each in-range citizen's own witness state updates accordingly.
  **Verified live, end to end:** `witness_ai_test.c` builds a real `ServerState`, spawns real
  citizens/zombies into real `PlayerState` slots, and proves the full chain — a DORMANT zombie
  raises nothing; a forced-HUNTING zombie witnessed by one low-arrogance citizen escalates it to
  DENIAL; `WITNESS_LIVE_DETECTION_RADIUS` genuinely excludes an out-of-range citizen while
  including a near one; 5 in-range low-arrogance witnesses escalate the whole group to SILENCING
  (`witness_rules.c`'s own real `silence_threshold()`); and the vigilance write-back is real, not
  discarded (asserts the exact citizen-archetype base, 35, distinct from the 40 passed at spawn
  time). All 6 checks pass. Standalone still — no Makefile/BUILD.bazel entry (no real gameplay
  trigger spawns these yet, that's 7c/7d/7e).

**Real, deliberate scope cuts in `witness_ai.c`, named plainly in its own header comment, not
papered over:** no movement/patrol/wander AI (citizens and zombies stand at their spawn point —
`pheromone.h`'s phase 5 steering primitive is a real, not-yet-wired candidate for a future pass);
`zombie_tick`'s own `has_target` is always passed 0 (no player-perception/line-of-sight system
exists yet, so a zombie only reaches HUNTING/FRENZIED via the test/debug
`witness_ai_force_zombie_mood` hook or a future trigger); no resolution/memory-wipe loop (The
Men's own dispatch loop — once a citizen escalates to SILENCING/PANIC/ENGAGE it stays there,
`witness_live_next_state_for_event`'s own real persistence rule, until something calls the
`resolved=1` path, which nothing does yet).

**Phase 7c landed — a real zone-authoring engine feature (native side only):**

- **`packages/world/level_boxes.h`'s new `LevelZone`** — a real, author-placed spherical trigger
  volume (`x,y,z,radius,zone_type`) tagging a region of a level with a `witness_sim.h` zone
  (`ZONE_PUBLIC`/`LAB`/`EXEC`/`GENERATOR`/`VAULT`, 0-4), added field-for-field in the exact same
  shape as the already-established `LevelExit` (this loader's own "smallest real thing" precedent
  — a sphere, not a box, needs no rotation authoring). `CustomLevelData` gained
  `zones[LEVEL_BOXES_MAX_ZONES]`/`zone_count`, and the JSON parser gained a `"zones"` array block
  mirroring `level_exits`' own parser exactly — an absent `"zones"` key is a real, honest "none
  authored" state, `zone_count` stays 0, never an error. Mode-agnostic by design, per
  `SHANKPIT/CLAUDE.md`'s own standing "levels are never story-mode-only" instruction — nothing
  about `LevelZone` or its parser is gated to `MODE_STORY`.
- **`level_boxes_zone_for_position(level, x, y, z)`** — the real query: first authored zone whose
  sphere contains the point (full 3D distance, unlike `witness_live.h`'s own deliberately flat
  (x,z) checks — zones need to tell a lab basement apart from a street-level plaza at the same
  x/z), or -1 if none. Verified: `packages/world/level_boxes_zone_test.c`, 7 checks (parses a real
  authored zones array; resolves a point inside a zone including exactly on its radius boundary;
  honestly misses outside every zone; correctly resolves against the SECOND zone, not just index
  0; a level with no zones parses clean; `NULL` is a safe miss) — all pass.
- **`packages/simulation/witness_ai.c`'s new `witness_ai_sync_zones(s, level)`** — the real live
  consumer: for every active citizen, resolves its CURRENT `PlayerState` position against the
  given level's zones and writes the result into the owned `WitnessSim`'s own npc entry
  (`WitnessNpc.zone`, same direct-write convention already used for `.vigilance`/`.state`). A
  citizen outside every authored zone keeps its LAST zone rather than snapping back to
  `ZONE_PUBLIC` — a real, deliberate choice: an unauthored gap in a level's zone coverage
  shouldn't read as a meaningful "the player stepped into public" fact. Verified live in
  `witness_ai_test.c` (now 8 checks): a citizen spawned in `ZONE_PUBLIC` but standing inside an
  authored LAB volume resolves to `ZONE_LAB` after one sync call, and keeps `ZONE_LAB` after
  moving outside every zone. Deliberately a separate function from `witness_ai_tick` (level data
  is per-level, reloaded on level transitions; the tick loop runs every frame regardless of
  whether a level is even loaded) — see the function's own header doc comment for the full
  reasoning.

**Real, honest, not landed this pass (named, not built):**

- **7c's own real, remaining gap: no IDUNA round-trip, no NOCK editor UI.** This pass is the
  native-engine half only — `LevelZone` can be parsed from a level's JSON, but nothing lets a
  designer actually AUTHOR one yet. `IDUNA/internal/shankpit/level_store.go` has no `LevelZone`
  Go type, no DB migration for a `zones_json` column, no `CreateLevel`/`UpdateLevel` parameter, no
  export shape, no `MaxLevelZones` constant — the entire `LevelExit`-shaped round-trip
  (`internal/http/handlers/shankpit_levels.go`, the DB schema, the export path) that would let a
  level SAVED through IDUNA's API actually carry zones. The NOCK web editor itself (a visual
  affordance to place/resize a zone sphere in a level, mirroring however exits/spawners are placed
  today) is real, separate frontend/UX design work on top of that — genuinely out of scope for a
  backend engine pass to blind-build without design input, named honestly rather than guessed at.
  Until this lands, a level can only carry zones via hand-written/scripted JSON, not the live NOCK
  UI at `/admin/nock`.
- **No live game-loop caller yet.** `witness_ai_sync_zones` has no real call site in
  `apps/server`/`apps/lobby` — that's 7d/7e's job, once a real MODE_STORY-replacing game loop
  calls it alongside `witness_ai_tick` each frame.
- **Phase 7d — the actual roster cutover.** Deciding what happens to `story_ai.c`'s existing,
  already-live `AI_ROLE_*` content (Rift Hound, Shambler Trooper, Guard, the S461/S462/S470 squad/
  leash/greet systems) and the real NOCK levels built against it — replace, park alongside, or
  fold in as BIG_O's own "Guard"-class encounters. Explicitly not decided or guessed at here.
- **Phase 7e — the day/night/lab turn structure itself.** The actual game-loop content (harvest →
  blend-in → lab) that makes this "BIG_O replaces STORY" rather than just new primitives sitting
  next to the old mode. Depends on 7a-7d landing enough real, live content to build a turn
  structure around.

## 3. Not landed this pass — named, phased into `EMILY/BACKLOG.md` SECTION 536

The founder's own follow-up messages during this pass ("the shaders the way the sun and moon look
the phone in story mode everything", "all the events and messages on the phone") named real,
additional scope beyond the sky/clock/REFLUX slice above. None of the below is built yet:

1. ~~Witness/Attention rules~~ — **landed, see §2d.** As a standalone, tested primitive
   (`witness_sim.{h,c}`), not yet wired into a live NPC spawn/render layer (that's phase 7).
2. ~~Humanness AI-brain extensions~~ — **landed, see §2e.**
3. ~~Lab simulation~~ — **landed, see §2f.** Still needs a SHANKPIT-side UI (the phone, phase 6)
   and server wiring, neither of which exist yet.
4. ~~Pheromone command tools~~ — **landed, see §2g**: the pure targeting/steering primitive +
   the reserved wire packet constant. **The Men's dispatch loop** itself
   (`server_tick_witness`/`server_tick_dispatch`) is still not ported — it operates on BIG_O's
   `ServerNpc` array, and SHANKPIT has no equivalent live zombie/citizen entity array to dispatch
   against yet. Real blocker for both this and the pheromone consumer: phase 7 needs to build that
   entity layer first.
5. ~~The phone: events and messages~~ — **landed, see §2h**: the real state machine, notification
   system, and a genuinely working event pipeline (day/night clock → REFLUX → alert decision →
   phone message). Not yet rendered on screen (no SDL2 draw path wired) — see §2h for the real,
   remaining gap.
6. **`MODE_STORY` content cutover** — the actual replacement of SHANKPIT's existing story-mode
   content/roster with BIG_O's day/night/lab loop. Real, checked finding this pass: this is bigger
   than every phase 1-6 combined and touches live, shipped NOCK level content, so it is itself
   broken into sub-phases rather than attempted in one shot — see §2i. **7a-7c landed** (the
   witness/zombie live-event glue, the real live NPC population/tick loop that consumes it, and a
   real zone-authoring engine feature — native `LevelZone` + query + live `witness_ai` wiring, all
   verified end to end against a real `ServerState`). **7c's own real gap**: no IDUNA round-trip
   or NOCK editor UI yet — zones can only be authored via hand-written JSON today. **7d-7e named,
   not built**: the actual `AI_ROLE_*` roster cutover decision, and the day/night/lab turn
   structure itself.

### 3.1 Smaller, named follow-ups to the work already landed in §2

- **Server-authoritative day/night sync.** §2a's clock currently ticks off client-local wall-clock
  time in `apps/lobby`; it is not yet ticked server-side (`apps/server/src/main.c`) nor broadcast
  in a snapshot packet, so two clients would see two different times of day. Real, honest v0
  limit, matching BIG_O's own repeated "named, not silently promised as more" convention.
- **`retro_lighting.c` weather integration.** §2b's sky visuals are real and weather-aware, but
  `RETRO_LIGHTING_DYNAMIC`'s scene ambient/sun/moon/fog still reads the OLD `retro_sky_eval_*`
  functions, unaware of weather. A storm currently darkens the sky dome but not the walls.
- **BIG_O's own `core/reflux_runtime.c` divergence.** Confirmed real and different from SHANKPIT's
  (`diff` non-empty) — BIG_O's own REFLUX never gets PARENA-powered by this pass; only SHANKPIT's
  does. Out of scope here since BIG_O isn't the target repo for this merge.
- **Phone rendering.** §2h's phone state machine/notification system is real and tested but not
  drawn on screen — no 2D text/menu render path in `apps/lobby/src/main.c`, no input binding to
  open it. Real, separate client-rendering scoping work.
- **`day_night_clock` server-side ticking.** §2h's `world_alert_bridge` needs a live
  `DayNightClock`/`Phone` pair to tick against; today only `apps/lobby` has one (client-local, per
  the day/night sync gap above), so the bridge has nothing to run against in a real multiplayer
  session yet either.

## 4. Ownership going forward

SHANKPIT now has its own PARENA stdlib domain (`PARENA/stdlib/shankpit/`, currently just
`world_rules.prn`) — future ports of BIG_O's other `.prn`-shaped systems (witness_rules,
world_alerts_mod, and any new conversions from §3.2) land there too, not as a cross-repo import
from `stdlib/big_o/`, matching the "BIG_O tech becomes first-class SHANKPIT tech" direction.
