# EMPIRE in SHANKPIT — doctrine, phone affordances, and the sphere

**Status:** influence economy + sphere geometry implemented in PARENA (§4), phone app surface designed
here (§2–3). The city-scale engine — population, economy, casinos, scrutiny, federal oversight,
neighborhoods, the Spire, automata — is codified as PARENA rules and tested from C (§6, 2026-11-04),
with **no running host consuming it yet**.

Repos beside `SHANKPIT`/`PARENA`/`IDUNA` on this box: `ECOWAR` (checked out at `aff6ce0`, §6.7) and
`TRAPX`. `EMPIRE` and `REDGARDEN` are described in §1 from an earlier pass and are **not** checked
out here.

This is not a fourth RTS. It is the layer that lets SHANKPIT *host* one, and it was written after
reading what the four repos actually contain, not from their names.

## 1. What each repo really is (checked, not guessed)

| Repo | Role | What exists today |
|---|---|---|
| **EMPIRE** | Integration layer | `bridge/faction_bridge.c` gates ECOWAR card play through BIG_O's decorum rules; `bridge/faction_state.c` is the shared faction record with a revision counter; `deck/ui.c` is a DECK and a UNITS placement app in exactly the shape SHANKPIT's own phone apps use. No world, no map, no influence currency. |
| **ECOWAR** | The game (hard fork of REDGARDEN) | 16 real cards resolved by `PARENA/stdlib/ecowar/card_effect_mod.prn`, server-authoritative UDP arena, own matchmaker/bot pool. Confirmed gap in its own README: **no deck/hand UI and no unit-placement wiring in-match** — cards are callable and tested, nothing plays one. |
| **REDGARDEN** | Hero UI + the AI | Arena MOBA with real per-hero kits and, relevant here, **team-based friendly/enemy AI and the fractal-commander work** (`apps/arena_bot`, `NORTHSTAR.md` §29). That AI is the thing EMPIRE needs and TRAPX doctrine describes. |
| **TRAPX** | Doctrine | BLOCK ZERO: one neighbourhood grid, cells with alignment (Neutral / Player / Rival / Institutional) and *signed pressure*, influenced by a deck of operations — **no avatar, no direct unit control, no win condition**. |
| **SHANKPIT** | Operating system | Engine, netcode, level format, phone/OS surfaces, and the only place a real world exists at all. |

The dependency chain EMPIRE's own NORTHSTAR states, kept:

```
TRAPX (doctrine) -> BIG_O (faction/tech sim) -> ECOWAR (game) -> DEADWEIGHT (card language)
-> REDGARDEN (hero UI) -> SHANKPIT (engine/OS)
```

SHANKPIT's job in that chain is *substrate plus a surface a player can reach*. This document is
only about that job.

## 2. The phone is the whole control scheme (and that is deliberate)

TRAPX says no avatar, no direct unit control. SHANKPIT has an avatar, so the compromise is: **you
walk, but you do not command**. Troops are not selected and ordered; they are *bought into
existence* where your influence already reaches, and then REDGARDEN-style AI fights for them.

The app is `BP_APP_EMPIRE`, pulled up with **P**, and it does three things:

1. **A minimap of the nearby area.** Nine districts (your district and its eight neighbours on the
   sphere), drawn from real state: who holds each one, where your bodies are, where a controlled
   building is. It is a map, not a radar sweep — nothing is hidden from you inside your own
   neighbourhood, and everything outside it is.
2. **Spawn a troop near you.** Cost in influence, a squad cap, and a spawn that must land on ground
   you can actually reach: within `empire-spawn-radius-milli` of you, falling back to a building you
   control if you are indoors or on a ledge.
3. **Clone.** A clone keeps the veterancy of the body it copies and costs 130% of a recruit. You
   cannot clone an empty roster; `/dev/null` is not a veteran. This is the founder's "clone your
   troops in PARENA first" — the *numbers* are PARENA, the *act* is the phone.

### Influence, not money

Influence is the only currency, and it is a TRAPX pressure value with a spend button:

- it trickles (`empire-influence-per-second`, 2/s) and caps at 400;
- **held districts pay it** (`empire-influence-from-district`: 100% yours, a third neutral, 0 a
  rival's), so territory is income and the app's map is an economy readout;
- every troop spends it, and the squad cap (6) means influence cannot be banked into an army.

A district flips at 600‰ pressure (`empire-district-state`), which is TRAPX's own alignment rule
with one state removed: Institutional has no owner in this build, so it stays Neutral rather than
being faked.

## 3. The sphere: an actually spherical map, not a curved plane

Founder: *"the map itself should be a big sphere ... procedurally generated ... like the maps that
zombie already uses mixed with the city built in map that survival uses to generate a sphere of
procedural cities in districts or neighborhoods."*

Design, in the order it has to be built:

1. **Geometry (in PARENA, already written).** Rings of districts on a sphere: 6 / 12 / 18 / 18 / 12
   / 6 = **72 districts**, radius 240 units, each district ~16 units across. Ring counts rise toward
   the equator so districts stay roughly the same *area* — the reason a sphere beats a square grid,
   and the reason "the nearby area" is always the same handful of cells at any latitude. A uniform
   lat/long grid would pinch at the poles and the phone's minimap would lie to players there.
2. **Generation (next).** Each district seeds a **city block generator** — the survival city planner,
   which already produces streets and buildings from a seed — and the *spacing and street rhythm* of
   the zombie map generator, which is what makes a district read as a place people lived rather than
   a grid of boxes. A district is generated the first time anyone comes within streaming range, from
   `(world_seed, ring, index)` alone, so two clients generate the same neighbourhood with no
   exchange. Unloaded districts keep only their owner and their influence, which is a handful of
   integers.
3. **Streaming.** Same rule the archipelago doc already set: unload distant geometry, keep the
   simulation. The sphere's own curvature is only felt at district boundaries, where neighbouring
   districts tilt by one ring step.

Deliberately **not** in this slice: the generator itself. It is a bigger piece than the phone app,
and a sphere you can see but that generates nothing would be worse than no sphere at all.

## 4. What was built in this slice

- `PARENA/stdlib/shankpit/empire_rules.prn` — influence economy, troop costs, clone premium, squad
  cap, spawn radius, district alignment + income, and the 72-district sphere geometry. Generated to
  `packages/simulation/empire_rules.c`; installed by `empire_rules_host.h`. **Rules first**, as asked.
- `packages/simulation/empire_rules_test.c` — headless proof of the economy and the geometry.

## 5. What is named but not built

- **No troop bodies yet.** SHANKPIT has bodies (story AI slots, the two drone kinds) but no faction
  field on them: REDGARDEN's friendly/enemy AI is in REDGARDEN, and porting its team model into
  SHANKPIT's `PlayerState`/`DroneState` is the next real piece. Until then the app can show cost,
  capacity and refusal honestly, but "spawn" has nothing loyal to hand the body to.
- **No procedural sphere generation** (§3.2) and no streaming of districts.
- **No wire format** for influence or district ownership, so this is single-player/sandbox state.
- **BIG_O/ECOWAR cards are not playable from this app**; EMPIRE's own deck UI is the right home
  for that, and it already exists there.

## 6. The city engine: the clockwork under the districts (2026-11-04)

Founder, pasting a tick-by-tick simulation spec: *"write tick-by-tick simulation pseudocode, define
formulas for each loop."* Eleven subsystems — population, citizens, items/intel, factions,
infrastructure, economy, casinos, local scrutiny, federal oversight, neighborhoods, Spire events —
plus a Conway-style automata overlay, each with a concrete formula.

This is **not** a fourth RTS, the same guardrail §1 sets. It is the layer that makes a district
expected to be held: EMPIRE gives you territory, ECOWAR gives you a fight, and this is what the
territory *is* while nobody is fighting over it. §2's influence economy and this engine are the
same idea at two timescales — influence is pressure you can spend, the city is pressure that
accrues whether or not you are online.

### 6.1 What is codified today

| File | What it is |
|---|---|
| `PARENA/stdlib/trapx/city_engine_rules.prn` | The spec's formulas, as **36 exported rules**. I32 + permille (1000 = 1.0), total functions, no allocation, no state, **no RNG** — where the spec rolls a die the host pre-rolls a 0..999 value and passes it in. |
| `packages/simulation/trapx_city_rules.c` | Generated (`scripts/gen_trapx_city_rules.sh`). Never hand-edited. |
| `packages/simulation/trapx_city_rules_host.h` | The C host's view, with the tick order written down as the contract. |
| `packages/simulation/trapx_city_rules_test.c` | `make test-trapx-city` — **60 checks**, every expected value hand-derived from the `.prn`, including the spec's own feedback loop as behaviour (money 640 → casino → corruption 392 → money 486, and the scrutiny the casino bought in the same tick). |

It sits *beside* `trapx_rules.prn`, not inside it: that module answers "what does the city do about
this street" (alertness, trust, heat, Field Offices, K9, receipts). This one answers "what does the
city do on its own". Two observers, two modules, and no threshold stated twice — including the
district conversion threshold, which is a **parameter** so the caller passes
`empire_district_threshold_milli()` and the sim and the phone app cannot disagree about who owns a
district.

Deliberately **not** in `LOBBY_SRC`/`SERVER_SRC` yet: no running host consumes it (§6.7), and this
repository has paid for hand-copied source lists drifting. The test is the only consumer today.

### 6.2 The tick, in the order that matters

One tick = **60 s of world time** (`ce-tick-ms`); the automata overlay runs every **30 ticks**, not
every tick.

```
1 population  2 citizens  3 items+intel  4 factions  5 infrastructure  6 economy
7 casinos     8 local scrutiny  9 federal oversight  10 neighborhoods  11 Spire events
(12 automata overlay, every ce-automata-period-ticks)
```

The order is load-bearing and is the spec's own: scrutiny must read *this* tick's corruption (casinos
already ran), oversight must read *this* tick's casino take, and the overlay must run on settled
state — otherwise one district can convert and corrupt in the same tick off one seed. A host that
reorders these gets different cities from the same inputs, which is exactly the class of bug
`trapx_parity.txt` exists to catch on the street side.

### 6.3 The map: every subsystem, where it lands, and what is really there

Checked against this checkout on 2026-11-04. `EUWorld`/`PACKET_EMPIRE_STATE`
(`packages/simulation/empire_units.h`, `empire_sync.h`) is the real territory layer: **3 factions**
(NEUTRAL/ALLY/HOSTILE), 32 claim cells, 16 resource nodes, per-faction stockpiles, on the wire at
1 Hz. That is what most of this engine has to attach to.

| Spec subsystem | PARENA rule (built now) | Real system it plugs into | What actually exists | The real gap |
|---|---|---|---|---|
| population | `ce-pop-growth-milli`, `ce-pop-step` | EMPIRE claim cells (`EUWorld.cells`), 72-district sphere (`empire_rules.prn`) | cell owner/claimant + faction stockpiles, 1 Hz; ownership on the phone's minimap | **no population number anywhere** — per-cell pop is new host state, and it is not on the wire |
| citizens | planned: weighted-neighbour move (host pre-rolls), no rule yet | `citizen_life.c` — five traits, five MMO-player paths (CRAFT/TRADE/DUEL/ENDGAME/IDLE), deterministic personas, rare spies; `story_ai.c` perception/FSM | citizens already have lives, homes and something they are trying to be | movement is persona-driven; nothing makes a citizen walk toward *money flow* or away from *corruption*. That is a host change, not a formula |
| items + intel | `ce-intel-chance-milli`, `ce-intel-transfer` | phone Cargo (`BP_APP_CARGO`), the receipt ledger (`trapx-receipt-digest`), REFLUX | receipts are real and chained; cargo is a count | no carried intel payload, and no per-cell intel store for a handoff to land in |
| factions | `ce-faction-spread-force`, `ce-faction-convert`, `ce-pressure-decay` | `EUWorld` factions + claim cells + claimant progress; `empire-district-state` on the phone | ownership and claim progress are real and replicated | pressure is a binary-ish owner/claimant, not a per-cell signed pressure that decays; adding it is wire work (a new field in `PACKET_EMPIRE_STATE`) |
| infrastructure / power | `ce-infra-stability-step` | **nothing** — checked: no power grid in `packages/` | global lighting env matrix, `zone_rules` lifecycle | the whole system. A "node" here is a design object until something owns load/capacity |
| economy | `ce-money-flow`, `ce-faction-siphon` | influence economy (`empire_rules.prn`) + faction stockpiles (`stockpile[]`/`gathered[]`) | influence is personal and spendable; stockpiles are gathered resources | neither is a per-cell `money_flow`. This is the first number the host has to invent storage for |
| casinos | `ce-casino-inflow/-processed/-banker-share/-oversight-pressure/-corruption/-scrutiny` | no casino entity; canon is `docs2/PARTY_STORES.md` (VS2, not VS1) | — | a casino needs a building, a throughput and a take. `LevelBox` + a widget can *place* one today; the numbers are ready, the object is not |
| local scrutiny | `ce-scrutiny`, `ce-crime-step` | `trapx_rules.prn` (alertness/trust/heat/enforcement, K9) via `trapx_city_enforcement` | the street's own enforcement ladder is real and tested | scrutiny is the *municipal* counterpart of the street's enforcement and must not restate it — the host reads both, the rules stay separate |
| federal oversight | `ce-federal-pressure-step/-investigation/-seizure/-arrest/-investigation-shock` | TRAPX media press + enforcement; IDUNA as the durable receipt ledger | media press is a boolean; the ladder tops out at level 4 | no federal layer at all. The pressure meter, its 0.995 decay and its investigation are new state |
| neighborhoods | `ce-neighborhood-stage/-grows/-decays` | `docs2/TRAPX_NEIGHBORHOOD_PERSONALITIES.md` (designed, not implemented); ECOWAR `livingmap/` towns | a classification, not behaviour | the personality axes (fear/trust/fatigue) are still prose. Stage is a real gate; what a BOOM *spawns* has no building system to spawn into |
| Spire events | `ce-spire-event` | **REFLUX** (`stdlib/reflux/reflux.prn`, `packages/reflux/reflux_runtime.c`) — the shared append-only action log; `world_alerts_mod.prn` is the worked subscriber precedent | a real cross-mod log plus ordered mod delivery (`trapx_events.c`) | no Spire mod publishes anything yet; the rule returns which event, the event vocabulary does not exist |
| automata overlay | `ce-automata-step` | ECOWAR `livingmap/creep.c` + `stdlib/ecowar/biome_mod.prn` (corruption spread, already real there) | a proven corruption-spread precedent in the sibling repo | no SHANKPIT district runner; nothing calls the overlay |

### 6.4 The spec's floats, in permille (a few, to show the conversion is exact)

| Spec | Codified |
|---|---|
| `growth = base + stability*0.05 + train - corruption*0.1 - crime*0.05` | `50 + stability*50/1000 + train - corruption*100/1000 - crime*50/1000`, clamped ±300 milli-people (0.3/tick), then `ce-pop-step` clamps to `0..ce-pop-max` |
| `money_flow = population * stability * (1 - corruption)` | `pop * stability * (1000 - corruption) / 1_000_000` |
| `chance = 0.1 + instability*0.2` | `100 + instability/5` permille (10%..30%) |
| `processed = inflow * multiplier` | `inflow * multiplier_milli / 1000`, multiplier clamped to 0..4000 |
| `pressure += casino_money*0.01 …; pressure *= 0.995` | `(pressure + casino_money/100 + scandals*5 + leaks*10) * 995 / 1000` |
| `scrutiny = base - corruption*0.5 - casino*0.8 + bias` | `base + bias - (corruption/2 + casino*8/10)`, clamped 0..1000 |
| `if 3+ neighbours same alignment … if corruption >= 4 … if population < threshold` | `ce-automata-step`, in that priority **reversed on purpose**: empty beats corrupt, corrupt beats convert — rot spreads faster than allegiance |

### 6.5 One open decision, named instead of guessed: what is a "cell"?

Three granularities already exist and they are not the same object:

- **32 claim cells** per loaded EMPIRE world — real, wired, replicated, but small: a "city" of 32 cells.
- **72 sphere districts** (`empire_rules.prn`) — real geometry, the phone's minimap, but no per-district
  state beyond owner and influence, and no wire format.
- **16 resource nodes** — placement-adjacent, not a neighbourhood.

Recommendation, not decided in code: **the city cell is a claim cell inside a loaded world, and a
district is an aggregate of claim cells for the phone.** That keeps every number the sim writes
replicable on the wire that already exists (a new pressure/population field, not a new packet), at
the cost of a small city. The alternative — simulating the 72 districts — is a bigger, lonelier
world with no way to show anyone else the result yet.

### 6.6 Vertical slice on real SHANKPIT surfaces

The spec's own minimal cut: population, money_flow, factions, one train, one casino, an oversight
meter. Here is what each of those is in *this* engine, and none of it is built yet:

1. `packages/simulation/trapx_city.{c,h}` — a `TrapxCitySim` holding one array of cells (pop,
   stability, corruption, crime, scrutiny, pressure, money_flow), seeded from `EUWorld.cells`.
   Same host shape as `trapx.c`, which is the precedent to copy.
2. Tick it server-side at **1 Hz** — the rate `PACKET_EMPIRE_STATE` already runs at, so no new
   clock and no interpolation problem.
3. One casino: a `LevelBox` building with a widget as its door (`machine_type` 6 lever, or a
   `MACHINE`), throughput from `ce-casino-inflow`. Placement works today; the take does not.
4. One "train": there is **no transit system** in this repo (checked — no `transit`/`freight`/`rail`
   anything outside protocol comments), so v0's route is a fixed list of claim-cell indices walked
   on a timer. Honest name: a route, not a train. Art and a moving body come later.
5. One oversight meter on `BP_APP_EMPIRE` — one new host-fed row beside the influence figure, and a
   phone Messages id raised when `ce-federal-investigation` first goes true.
6. Spire events published to REFLUX with a documented action type, so any mod can subscribe the way
   `world_alerts_mod.prn` already does.

### 6.7 ECOWAR, and why it was cloned into this box

`ECOWAR` is checked out at `aff6ce0` ("BIG_O faction bridge — decorum gates and write-back"). What it
brings this engine, rather than the other way round:

- A **real per-tick order already shipped**: `arena_tick_resources` / `arena_tick_creeps` /
  `arena_tick_towers` in `packages/simulation/arena_game.c`. The city engine's own order should be
  written in that style, not invented beside it.
- `packages/livingmap/` — hex grid, creep spread, towns, `living_map_events.c` — the corruption-spread
  precedent `ce-automata-step` needs, and the reason automata is in this engine at all.
- 16 cards resolved by `PARENA/stdlib/ecowar/card_effect_mod.prn`, and its own confirmed gap (no deck
  or unit-placement wiring in-match). A city state is exactly what a card would *target*.

Why the engine stays **out** of ECOWAR's match loop: a match is minutes, a district is days. ECOWAR
consumes city state (what is worth fighting over) and does not own it. The same split §1 already
states for the dependency chain.

Still **not** checked out on this box: `EMPIRE` and `REDGARDEN`. §1's table describes them from the
earlier pass; nothing in this section re-verified them, and `EMPIRE/NORTHSTAR.md` (the golden doc
`EMPIRE-NORTH`) is not present here to read.

### 6.8 Named, not built

- No host calls any of these 36 rules yet; there is no `TrapxCitySim`, no 1 Hz tick, no wire field.
- No per-cell population, money, pressure, intel or scrutiny storage, and nothing in
  `PACKET_EMPIRE_STATE` carries them — that packet change is the real prerequisite for the slice.
- No casino, train, ship, power grid or neighborhood *building* object. `ce-ship-resource-shock` is
  written and unreachable: there are no ships.
- Citizens do not move by economy yet (`citizen_life.c` is persona-driven, and correctly so).
- No Spire mod, no automata runner, no WOTAN sandbox mirror of these rules (the street's rules have
  one; these do not), and no phone readout.
- `TRAPX_NEIGHBORHOOD_PERSONALITIES.md`'s fear/trust/fatigue axes are still unimplemented, so
  `ce-neighborhood-stage` is a three-state classification with nothing underneath it.
