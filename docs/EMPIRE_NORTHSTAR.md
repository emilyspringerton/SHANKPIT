# EMPIRE in SHANKPIT — doctrine, phone affordances, and the sphere

**Status:** first slice implemented (influence economy in PARENA, phone app surface designed here).
Repos checked out: `EMPIRE`, `ECOWAR`, `REDGARDEN`, `TRAPX` now sit beside `SHANKPIT`/`PARENA`/`IDUNA`.

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
