# EMPIRE units: archetypes, inheritance, and programmable orders

Founder: *"we need to be able to spawn in allies like mike etc and they need their own ais etc for exploring
the environment engaging enemies following allies taking over territory gathering resources etc ... the ais
also need some generic stuff that all humans will inherit but not all ais will be human ... this is almost
like a level building affordance i should be able to spawn in citizens zombies soldiers etc and i should be
able to have different ais"*.

This document is the contract. Two EMPIRE modules exist in this engine and they are siblings, not the same
file:

| Module | What it is | Files |
|---|---|---|
| **influence economy + district sphere** | what a troop costs, what a clone costs, when placing is refused, the 72-district sphere behind the phone's EMPIRE app | `empire_rules.prn` → `empire_rules.c`, declared by `empire_rules_host.h` |
| **unit model** (this doc) | what a spawned thing IS, what it inherits, what it will do | `empire_units.prn` → `empire_units_rules.c`, `empire_units.h`, `empire_units_rules_host.h` |

Numbers live in PARENA; `make regen-empire-units`. Each C header carries matching defaults so tests and
binaries run without the generated file, and `make test-empire-units` asserts the two agree sample for
sample. Hosts call `eu_rules_install()` once at startup (lobby and server both do).

## Archetypes — the thing you spawn

`archetype` 0 means "not an EMPIRE unit": the character spawns exactly as it did before this layer existed
(its authored `role` and `kit`). 1–6 are complete spawns:

| # | Archetype | Brain (AIRole) | Default skin (AIKit) | Faction | Default orders | HP |
|---|---|---|---|---|---|---|
| 1 | citizen | WANDERING_BOT | Mannequin | neutral | explore, gather, guard | 60 |
| 2 | zombie | RELENTLESS_PURSUER | auto (zombie clip set) | hostile | engage | 40 |
| 3 | soldier | GUARD | Stan | hostile | engage, claim, guard | 100 |
| 4 | ally | STORY_ALLY | **Mike** | ally | follow, explore, engage, gather | 120 |
| 5 | guard | GUARD | George | hostile | engage, guard | 100 |
| 6 | worker | WANDERING_BOT | Leela | ally | follow, explore, gather | 70 |

`faction` and `orders` are `-1` when unauthored, meaning "use the archetype's own". `orders` **0** is a real
value: takes no orders, does whatever its brain does. An authored `kit` always beats the archetype's default,
so an imported skin (see `MIXAMO_PIPELINE.md`) is a legal ally body.

## Inheritance — the generic stuff every human gets

`species` is an inheritance switch, not a flavour label. Humans (citizen, soldier, ally, guard, worker)
inherit one baseline row; the zombie inherits the beast row:

| | courage | aggression | loyalty | greed | curiosity |
|---|---|---|---|---|---|
| human | 600 | 500 | 700 | 500 | 500 |
| beast | 900 | 900 | 0 | 0 | 300 |

A zombie is not a human with different numbers — it has no loyalty or greed to spend. Any future non-human
archetype gets the beast row by existing, without touching a table of special cases.

## Orders — the programmable half

Bitmask, and the level JSON/editor use these exact values:

| bit | order | meaning |
|---|---|---|
| 1 | follow | keep within follow distance of the player (20 units) |
| 2 | explore | walk to points inside the explore radius (60 units) around home |
| 4 | engage | fight what the unit can see — checked *first*, always wins |
| 8 | gather | walk to the nearest resource node and take from it (25 units a node, one per 250 ms) |
| 16 | claim | stand on the nearest unowned cell for 30 seconds and it becomes the unit's faction's |
| 32 | guard | hold the post (the standing fallback, needs no trigger) |

Every archetype scores every order, and the mask is the only gate — so "a citizen that fights" is a mask
with `engage` in it, not a code change. When several accepted orders are triggered, the highest priority
wins; ties go to the lower order index. When nothing is triggered the decision returns **none**, which means
"fall back to the role's own brain" — never "stand still forever". That is what lets a soldier hold a post
while a citizen wanders off to gather.

The decision itself (`eu_choose_order`) is pure: it reads the unit, the world and a small view struct, so it
is unit-tested without a server. `AI_MODE_EXPLORE`, `AI_MODE_GATHER` and `AI_MODE_CLAIM` are the thin runs
that carry it out, using the same movement primitive every other mode already uses.

Two behaviours inside the tick are deliberate and were both found by the spawn test:

- **Any mode, not just story.** `story_ai_tick` used to return early unless `MODE_STORY`/`MODE_TYLER` was in
  its playing phase. EMPIRE units are level-authored, so a citizen in a NOCK map or an ally in `MODE_ZOMBIES`
  would have spawned and then stood still forever. A live EMPIRE unit now makes the tick legal on its own;
  characters without an archetype keep the old gate exactly.
- **An ally does not treat you as an enemy.** `ai_gather_perception` has exactly one target — `players[0]`,
  the hero. For an ally that body is its leader, so `enemy_visible` is forced to 0 for the ally faction;
  otherwise the first time an ally looked at the player it would pick `engage`. Hostile and neutral units read
  perception normally. A real multi-target perception is the honest long-term fix.

## Authoring it in a level (NOCK)

`characters[]` entries gained three keys, parsed and re-serialized by `packages/world/level_boxes.h`:

```json
{ "role": 3, "kit": 3, "archetype": 4, "faction": 1, "orders": 15, "x": 12, "y": 0, "z": -8 }
```

- NOCK's character inspector has **Archetype**, and once an archetype is chosen, **Faction** and **Orders**
  dropdowns. Picking an archetype also fills the role/skin fields, which stay overridable.
- Only an `archetype != 0` character writes the three keys, so pre-EMPIRE levels keep byte-identical JSON.
- `server_apply_custom_level` and the lobby's own story level loader spawn `archetype > 0` characters through
  `story_ai_spawn_unit(...)`; every other character goes through `story_ai_spawn_enemy` unchanged.

## The world it acts on

`EUWorld` owns resource nodes, claim cells and per-faction stockpiles, one per process (the module that ticks
units owns it: `story_ai_empire_world()`). `story_ai_reset` seeds a default contested ring — six nodes on a
40-unit ring, twelve claim cells on a 70-unit ring — because a level with authored units needs something to
gather and something to take.

Named limits: nodes and cells are **not level-authored yet** (the default ring is all there is), and there is
no HUD reading the stockpiles. Claims and stockpiles are per-host state; nothing replicates them over the
wire, so in a networked match two clients would each run their own copy.

## Verification

`make test-empire-units` — **50 checks**: PARENA's generated table equals the header's defaults across every
archetype field and order priority (and both inheritance rows and the world numbers), the archetype table's
faction/skin/role/health/orders are the documented ones, every human archetype inherits the human row while
the zombie inherits the beast row, an authored mask *replaces* the archetype's and its bits are usable, orders
0 is legal, an authored skin and faction win, the decision engages first / follows a distant leader / gathers
when a node is near / explores when idle / claims as a soldier / guards as the fallback / says none when
nothing applies, a node yields exactly its amount and no more, and a claim takes exactly 30 s with a second
faction starting its own clock.

`make test-empire-spawn` — **16 checks**, the wiring rather than the model: `story_ai_spawn_unit` turns an
archetype into a real player slot with the archetype's role, Mike's skin and the archetype's health; an
authored skin/faction/mask wins; with the leader far away the ally's order is `follow`, with the leader close
it is `gather`; standing on a node banks resources for the ally faction through the real gather run; thirty
seconds on a cell makes it the soldier's faction's ground; and a zombie with nothing in sight has **no** order
and falls back to its own brain. It runs in `MODE_ZOMBIES` on purpose, so the tick-gate widening is pinned
down by a test instead of a comment.

`make test-empire-rules` (the influence economy module) still passes, `make server` links, the lobby compiles
clean, and `make check-windows` (mingw) compiles the Windows build. **Not verified**: no windowed match was
played, so an ally has been driven by ticks in a test but not watched walking a level; nodes/cells are not
level-authored (the seeded ring is all there is); claims and stockpiles are per-host and do not replicate;
the NOCK editor change is type-reviewed only (no `node_modules` here); and the Windows CI job's full command
also needs `EDITOR.GAME` and the SDL2 mingw kit, neither of which is in this checkout.
