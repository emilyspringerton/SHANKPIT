# GFD frontend VS0 — SHANKPIT as the client, the 1700s start, and a wasm rule surface

**Status (2026-11-04):** the RULES of VS0 are built, generated into the C host, and running as a real
wasm32 module with 143 passing checks. The 3D frontend in wasm is **not** built and is named at the end.
This continues `docs/GFD_UNIFICATION.md` (phases 1–4b shipped) and `docs2/SHARED_ENGINE_NORTHSTAR.md`
(the shared `stdlib/engine/` proposal, still design only).

Founder, 2026-11-04: *"CONTINUE TO MERGE GFD AND SHANKPIT IMPLEMENT SHANKPIT AS THE FRONTEND FOR THE GFD
MMO TRAPX DOCTRINE BIG_O SANDBOX BUILD IT IN WASM VS0 MASSIVE LOW BANDWIDTH REDGARDEN (*merge it down)
USE THE SHANKPIT CITIZEN GENERATION AFFORDANCES (the vroid avatar generation stuff) AND THE CITIZENS HAVE
THEIR OWN LIVES LIKE THE SIMS ADD RESOURCE COLLECTION VIA TRADITIONAL MINING AFFORDANCES START IN THE
1700S TRAIN TECH TREE"* — then, mid-slice: *"dont worry about implementing finish your train scope do that
work and then make a plan for all of the requests"*.

## 1. The split, stated once

| Layer | Repo | Owns |
|---|---|---|
| Frontend | **SHANKPIT** | render, input, client-side prediction, the phone UI, the level format, the wasm build |
| MMO authority | **GFD** (`../GoblinFoxDragon`) | persistent world, jobs/state services, the mud/state engine, towns |
| Match combat | **ECOWAR** (fork of **REDGARDEN**) | 1v1/card/arena rules, matchmaker, bot pool — **merged down** into SHANKPIT's arena rather than kept as a parallel client |
| Doctrine | **TRAPX** | indirect control, pressure not orders, receipts, Watchers, Field Offices |
| Sandbox | **BIG_O** | the demo/creative layer and the witness loop |

The rule from `GFD_UNIFICATION.md` still protects the merge: **nothing from GFD overwrites a SHANKPIT
file.** GFD contributes content and state; SHANKPIT's engine is the one that runs.

## 2. What "massive low bandwidth" means as numbers, not adjectives

The engine's own precedent is already the answer: **the wire carries identity, not geometry.**
`PACKET_ZONE_STATE` sends a zone id and a seed at 1 Hz and both sides generate the same dungeon;
`PACKET_EMPIRE_STATE` sends only the mutable half (stockpiles, node amounts, cell owners) and never
positions. GFD's MMO frontend inherits that budget:

| Thing | On the wire | Rate |
|---|---|---|
| Terrain / level geometry | a seed + level id | once per zone change |
| District ownership, population, pressure | the mutable half of the cell record | 1 Hz |
| Other bodies | 12-byte net header + quantised transform, interest-managed by district | 10 Hz inside interest radius, 0 outside |
| Player intent | commands (move/act), never positions from the client | event-driven |
| Citizens' internal lives | **nothing** — every client runs the same `citizen_day_rules.prn` from the same clock and seed | 0 |

That last row is the whole trick: a citizen walking to work costs zero bytes because both ends compute
the same day. It is the same contract `proc_dungeon.h` and `trapx` already rely on.

## 3. The 1700s start (built this slice)

`PARENA/stdlib/shankpit/era_tech_rules.prn` — 4 eras (1700/1800/1900/2000), 6 nodes
(TRACK, STEAM, FREIGHT, PASSENGER, SIGNAL, ARMORED), a bitmask `known`, and gates that are decisions
rather than timers:

- a train exists only when TRACK **and** STEAM are known (`train-unlocked`); capacity is 4 seats + 2 per
  carrying node; speed is 700 permille in the 1700s, +100 per era, +150 for passenger service;
- an era advances only on research (`era-can-advance`): 1700→1800 is "there is a railway", 1800→1900 is
  "freight AND passenger" (an economy, not a hobby), 1900→2000 needs the signal network;
- rails get cheaper per tile as eras industrialise (3 → 1) — the one axis that gets easier, so the map
  does not become a museum.

## 4. What pays for it: mining and the citizens' day (built this slice)

- `mining_rules.prn` — traditional mining: pick tiers (stone/copper/iron/steel) with a real tradeoff
  (faster swings cost more stamina), yield per strike, deterministic `ore-from-variant`, vein depletion
  and a ratio for the face, tool wear, and coal as **fuel** rather than a bar (gems are cut, not smelted).
- `citizen_day_rules.prn` — the Sims half: four six-hour phases, five paths mirroring
  `citizen_life.h`'s `CILIFE_*` (CRAFT/TRADE/DUEL/ENDGAME/IDLE), hunger/energy/mood drift, wages, and
  zone *kinds* (home/workplace/market/bar/street/plaza/road/anywhere) so a host can place the activity
  without the rules knowing any zone ids. Every citizen sleeps at night; a low mood sends them to people.
- `trapx/citizen_contact_rules.prn` — the social layer TRAPX asked for: affinity, gift worth, contact
  sharing (affinity + the city's trust in *you*, host-rolled), and the four services with affinity floors
  and friend discounts floored at half price (a free favour is a debt).

## 5. The wasm VS0 (built this slice, and what it is not)

`PARENA/scripts/build_frontend_vs0_wasm.sh` builds those four `.prn` files — **the same sources the C
host compiles** — into one wasm32 module: 5,582 bytes, **0 imports**, fixed 8 MB memory, no allocator, no
`memory.grow`. `PARENA/scripts/frontend_vs0_wasm_test.mjs` asserts **143 hand-derived values** against the
module in node, including one full run of the loop: buy track → buy steam → train exists in 1700 → the
century turns over.

It is a **rules** module, not the game. What it proves is the seam that matters: a train cannot unlock in
the browser and stay locked in the game.

### 5.1 A real compiler bug this found — and the fix it got the same day

`(if (<= b 0) 0 (/ a b))` **compiled and ran in C, and trapped with "divide by zero" in wasm** — PARENA's
LLVM/wasm emitter evaluated both arms of an `if` (reproduced on this box 2026-11-04 with a two-function
scratch module). The cause was a wrong test, not a missing case: the emitter decided `select` (evaluate both
arms, pick one) versus real basic blocks with an `expr_is_pure` predicate, and a division has no effects —
so it counted as pure. Purity answers "can this be reordered/duplicated", not "is this safe to run when its
result is thrown away"; a division can trap. Effects and trapping are two questions.

**Fixed in PARENA the same day** (`emit_llvm.c`): the predicate is now `expr_is_speculable` — pure *and*
`head_may_trap` clean, where `head_may_trap` is exactly `/` and `mod` (integer division also traps on
INT_MIN / -1). A trapping arm now gets basic blocks, so only the taken arm runs. Verified three ways:

- `tests/test_emit_llvm.c` asserts a dividing `if` emits `br i1` and **no** `select`, and that a
  non-trapping `if` still selects (the fix costs nothing in the common case); `make test` 374/374;
- the same source built end to end through `parena build` → `opt` → `llc wasm32` → `wasm-ld` now returns
  `safe_div(7,0) = 0` instead of trapping (4/4 hand-derived cases, negatives included);
- this module rebuilt with the guard clauses still in the rules: **143/143** node checks, and it grew from
  5,568 to **5,582 bytes** — the branch structures, no other change.

**Still open, named rather than assumed away:** `and`/`or` remain honestly non-short-circuiting (`and i1`/`or
i1` over two already-evaluated operands), so `(and (> b 0) (> (/ a b) 2))` still traps at `b = 0` where a
short-circuiting backend would never reach the division. That is a semantic change to a documented operator,
not a bug fix, so it stayed out of this pass; the rule for authors until then is the one this slice already
follows — guard the divisor at the division, don't rely on `and`/`or` ordering.

## 6. What is built, exactly

| Artifact | Where |
|---|---|
| 4 rule modules | `PARENA/stdlib/shankpit/{era_tech,mining,citizen_day}_rules.prn`, `PARENA/stdlib/trapx/citizen_contact_rules.prn` |
| C host | `packages/simulation/{era_tech,mining,citizen_day,citizen_contact}_rules.c` via `scripts/gen_frontend_rules.sh` (`make regen-frontend-rules`) |
| C header + test | `packages/simulation/frontend_rules_host.h`, `frontend_rules_test.c`, `make test-frontend-rules` |
| wasm | `PARENA/scripts/build_frontend_vs0_wasm.sh`, `PARENA/scripts/frontend_vs0_wasm_test.mjs` |

Both targets assert the same hand-derived numbers, the way `trapx_parity.txt` pins TRAPX's street.

## 7. Named, not built (the honest list)

- **No 3D frontend in wasm.** Compiling the C engine (XPBD, GL, SDL2) to wasm is a different project with
  a different toolchain; the module above is rules only.
- **No GFD service integration.** No client of the mud/state engine, no job persistence, no town map
  streaming beyond phase 1's `var/maps/gfd_town.json` import.
- **REDGARDEN is not merged down.** It is cloned (`../REDGARDEN`) and its per-tick order/`livingmap/` are
  borrowed by the city engine, but no REDGARDEN code runs inside SHANKPIT; the shared `stdlib/engine/`
  layer is still a proposal.
- **No mining, citizen or tech-tree host integration.** Nothing in `apps/lobby` calls these rules; the
  tests are the only consumers. No pickaxe item, no ore in the cargo app, no train entity, no rail build.
- **No auction house, no culture/priority system** — planned in `docs/ONGOING_DEVELOPMENT_REPORT_2026-11-04.md`.
- **No VRM/VRoid citizen bodies at runtime.** `stdlib/forgedinface/rig.prn` is real (41 assertions) but
  nothing renders, and no host generates a citizen body from it yet.
- The wasm `if`-arm bug above is open.

## 8. Phases from here

1. **Host slice**: a `TrapxCitySim`-style `FrontendWorld` (cells + citizens + the tech mask) ticked at
   1 Hz, calling these rules; one pickaxe, one ore, one rail tile, one train body.
2. **Wire**: population/pressure/tech-mask into `PACKET_EMPIRE_STATE`, and a citizen record derived from
   seed (zero bytes).
3. **Phone**: TECHTREE fed from `tech_*`, CARGO fed from `strike_yield`/`carry_capacity`.
4. **Frontend-in-wasm spike**: measure the C engine's wasm portability before promising it (the rules
   module is evidence the pipeline works; the renderer is not).
