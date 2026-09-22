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

## 3. Not landed this pass — named, phased into `EMILY/BACKLOG.md` SECTION 536

The founder's own follow-up messages during this pass ("the shaders the way the sun and moon look
the phone in story mode everything", "all the events and messages on the phone") named real,
additional scope beyond the sky/clock/REFLUX slice above. None of the below is built yet:

1. ~~Witness/Attention rules~~ — **landed, see §2d.** As a standalone, tested primitive
   (`witness_sim.{h,c}`), not yet wired into a live NPC spawn/render layer (that's phase 7).
2. ~~Humanness AI-brain extensions~~ — **landed, see §2e.**
3. **Lab simulation** (`core/lab_sim.c`, 17 tests, plain C) — the cloning-facility equipment
   pipeline (centrifuge/PCR/sequencer/CRISPR splice/breeding/incubation). Real, headless, already
   proven in BIG_O; needs a SHANKPIT-side UI (the phone screen, §4) and server wiring, neither of
   which exist in either repo yet (BIG_O's own NORTHSTAR §9 names the same gap).
4. **Pheromone command tools** (`day/packages/common/bigo_pheromone.h`) + **The Men's dispatch
   loop** (`server_tick_witness`/`server_tick_dispatch` in BIG_O's `apps/server/src/main.c`) — real,
   live-verified in BIG_O, needs porting into `apps/server/src/main.c` alongside a new wire packet
   (matching BIG_O's own `PC_PACKET_PHEROMONE_THROW`) in `packages/common/protocol.h`.
5. **The phone: events and messages** (founder: *"the phone in story mode everything"*, *"all the
   events and messages on the phone"*) — BIG_O's `day/packages/common/bigo_phone.h` +
   `world_alerts_mod.prn`/`world_alerts.c` (already PARENA — `PARENA/stdlib/big_o/
   world_alerts_mod.prn`, reacting to REFLUX-logged world events and raising phone message ids).
   This is a real, standalone UI feature (a phone screen showing a message list) that SHANKPIT has
   no equivalent of today — needs its own scoping pass (what renders it, HUD toggle, message
   content) rather than a blind file copy.
6. **`MODE_STORY` content cutover** — the actual replacement of SHANKPIT's existing story-mode
   content/roster with BIG_O's day/night/lab loop. Blocked on enough of items 1-5 landing first to
   have real content to cut over to; `story_ai.c`'s existing `AI_ROLE_*` roster (Rift Hound,
   Shambler Trooper, etc.) is a real, separate asset that a witness-rules integration should
   account for rather than silently orphan.

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

## 4. Ownership going forward

SHANKPIT now has its own PARENA stdlib domain (`PARENA/stdlib/shankpit/`, currently just
`world_rules.prn`) — future ports of BIG_O's other `.prn`-shaped systems (witness_rules,
world_alerts_mod, and any new conversions from §3.2) land there too, not as a cross-repo import
from `stdlib/big_o/`, matching the "BIG_O tech becomes first-class SHANKPIT tech" direction.
