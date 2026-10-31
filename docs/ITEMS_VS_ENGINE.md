# BIG_O items vs. what the engine actually has

Read `BIG_O/docs/ITEMS_AND_TECH_TIERS.md` (filed 2026-10-31) alongside this. That document is canon
for the IP; this one is the honest bridge: what already exists in SHANKPIT, what the document's
systems would attach to, and what is genuinely absent.

Rule of thumb used throughout: **an item is a new way of perceiving something already in the world**,
so an item is only real here if there is a channel underneath it to perceive. Where there is no
channel yet, this says so instead of inventing a menu entry.

## What exists, and what it maps to

| Canon idea | Live in the engine today | Real gap |
|---|---|---|
| Items gate what you can read, on keys | `packages/common/kit.h` — 14 bind slots (1-9, 0, `` ` ``, `-`, `=`, `C`), three prefabs, a builder draft, three phone apps (`KITS`, `KIT MAP`, `KIT BUILD`) painted as HTML+CSS by PELLEGRINO | The four items are engine **inputs** (`PHEROMONE`, `ABILITY`, `SECONDARY`, `RELOAD`), not objects. A kit cannot yet "hold" a Pocket Watch |
| Tech tree / unlock ladder | `BP_APP_TECHTREE` with real spendable levels (`PHEROMONE IMMUNITY`, 5 × +2 s, paid in XP) | One node. No era gating, no Black/Green consent step |
| Pheromone canister | `packages/common/pheromone.h` + `pheromone_grenade.h` + `witness_ai_try_throw_pheromone`; lure feeds hunger/aggression, thrower grace window | It is a thrown object, not a "signal channel" — no reading layer |
| Channels: lamps, taps, puffs, Morse | **Nothing.** No Morse, no signal capture, no recording | This is the document's actual core, and it is the biggest missing system |
| Stakeouts / watching NPCs | `witness_ai.h` — citizens, The Men, zombies, birds; witness states (DENIAL/PANIC/SILENCING/ENGAGE), memory wipes, REFLUX events, perception radii | There is no *player-facing* observation verb: no binocular lock, no capture timer, no "you were seen" |
| Lookouts spotting a capture | Citizen flee + The Men dispatch exist as reactions to zombies | No reaction to the player watching. No vulnerable-action model |
| Destructible vaults, break-ins | `brick_world.h`, `brick_fracture.h`, `structure.h` (cells, joints, load cascade), the hammer | Vaults/doors/locks do not exist; brick is the only destructible material |
| Sealed vs released log entries | **Nothing** | Whole subsystem, and a cheap one to prototype (it is a flag plus a read gate) |
| Five colors (Black/Green/Red/Yellow/White) | **Nothing** | No NPC colour state, no consent/refusal verbs, no cost register |
| Loans with named costs | **Nothing** | Needs a register table plus a cost that persists until return |
| Era / time-jump ladder (1916 → 1993 → endgame) | `day_night_clock` (phase only); `SCENE_*` levels; NOCK-authored levels | No eras, no jumps, no 72-minute superposition |
| Meal / courses (coffee, tea, soup, milk) | Food system: `food_items.h`, `food_pickup.h`, CARGO app, `FOOD_CAKE` smash distraction | No course structure, no "no spoon on Sunday" style cost |
| Lab tier (splice, vats, shadow war) | `lab_station_host.c` (SPLICE/CENTRIFUGE/PCR/VAT/FRIDGE/CONSOLE/WAR), `shadow_war_host.c` + Elo | Already the closest thing to an item-gated progression; not wired to the canon item list |
| Blue Mage (learn by observing) | `npc_archetype.h`/`humanness.h` give NPCs mood and vigilance to observe | No skill capture, no spellbook |
| Hunters / puppets (companions) | `witness_ai` birds; giant zombie bugs are tameable in spirit (authorized by The Men) | No companion ownership, no bond cost |

## Three slices that are actually buildable now

Ordered by "what unblocks the most document text per unit of work", not by drama.

1. **The five colours as NPC state (Black/Green/Red/Yellow/White) plus a false colour.**
   Every NPC already has mood, vigilance and a witness state. A colour is a *readable projection* of
   existing state, which is exactly the document's own thesis, and it makes stakeouts legible without
   adding a single new entity. The tell (a Suit showing Green while really in Black) is a mismatch
   between shown and real colour — a five-line rule, testable headless.

2. **Sealed vs released entries.**
   A flag and a read gate on an existing log. It is the cheapest piece of the document with real
   gameplay consequence (reading something gives up the option to keep it secret), and it gives the
   cipher layer a cost before any cipher exists.

3. **One real channel: the telegraph/lamp Morse layer.**
   A channel is a sequence with timing, a capture action and a tell. SHANKPIT has timing (`SDLK` pulse,
   tick-based sim) and observation radii; what it lacks is a signal table. One carrier — lamp flashes
   in a window — proves the shape (visible tell, readable only with the right item, capture is
   vulnerable) before the other three carriers (wire, flag, radio) become content.

## What this document does NOT justify building yet

- **Eras and time jumps.** There is no era data model, no level set per era, and no answer to the
  document's own open question (clock follows progress vs. player choice). Building a jump before that
  answer exists would bake in the wrong one.
- **The grimoire tier as mechanics.** As written it is lore-plus-tables; the only engine-shaped part
  is "named channel with a frequency and a cost", which the colour/loan work would already provide.
- **The 1993 and endgame item tables.** They are content for systems that do not exist (scanners,
  recorders, implants). Filing them now is right; implementing them now would be inventing seven
  subsystems at once.
- **The Eight-Year Ladder table**, which did not arrive — see the canon doc's own note.

## Invariant to protect

The document's own rule: *the surface game must stay fully playable without any of the cipher layer.*
Everything above must be additive — a player who never opens the phone, never throws a canister and
never reads a colour still gets the zombie sandbox. Any slice that starts gating movement or combat
behind the cipher layer has broken the document, not implemented it.
