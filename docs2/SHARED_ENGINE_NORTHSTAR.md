# Shared Engine Northstar — SHANKPIT / REDGARDEN / GFD

**Status:** design only, no code moved yet. Golden-indexed as `SHARED-ENGINE-NORTH`.

## 0) Thesis

Founder real-time, 2026-10-06: *"SHANKPIT REDGARDEN GFD — they are all actually the same
product at different scales, we need to full share the technology of the platform and engine
improvements across them."*

Frame-break: the specific ask ("share tech across three games") is one instance of a general
pattern — this monorepo keeps re-deriving the same core engine primitives per product instead of
building them once. Three live precedents already named the symptom without fixing the cause:

- `SHANKPIT-NETCODE` / `SHANKPIT-PREDICT` are marked **"canonical (GFD is a reference copy)"** —
  i.e. GFD hand-copies SHANKPIT's wire-protocol/prediction *semantics* into its own Go code. This
  is a manual-sync convention, not shared code. It has already drifted once (`SHANKPIT-CAPTCHA-FPS`
  found `emit_ts.c` scalar-only when a shared physics dogfood was attempted).
- `REDGARDEN-HUMANNESS` ports MISHRI's bot-mood/reaction-delay model as *shapes, explicitly not
  code* — the same humanness primitive SHANKPIT's own AI (`SHANKPIT-AI`) and GFD's NM/mob AI will
  all need, each reimplementing it from the written spec rather than calling one module.
- `GFD-INVENTORY` (16-slot FFXI-era gear + stat computation) and REDGARDEN's item/consumable
  system (`REDGARDEN-COOKING`, starting-item roster) are the same shape — stat-bearing items in
  slots, resolved against a formula — built twice, independently, in two languages.

Genre differs (SHANKPIT = moment-to-moment FPS, REDGARDEN = card-deck RTS skirmish, GFD = async
persistent MMO), but the underlying engine contract is the same at three different time
constants: **who's authoritative, how state reconciles, how bots behave like humans, how items/
abilities resolve against stats.** That contract should be written once.

## 1) What "once" means — PARENA-first (per Core Deps standing instruction)

Per this repo's own `CLAUDE.md` ("Core Deps Are PARENA-First", founder 2026-10-01): any core dep
missing across products gets implemented in PARENA stdlib first, feature second. Shared engine
primitives are exactly this category — they are core deps of all three products, not
feature code of any one of them. Today `PARENA/stdlib/` already has per-game mod directories
(`shankpit/`, `gfd/`, `ecowar/` carrying forward REDGARDEN's card-effect logic, `papercraft/`) —
real, but every one of them is *leaf* mod logic (a fireball, a nm bonus, a town-cap rule), not
*engine*. There is no shared base layer underneath them. This northstar proposes one:
`PARENA/stdlib/engine/` — new, not yet created — holding the primitives below, with each
product's existing per-game directory becoming the thin, product-specific layer written *on top*
of it, same relationship `stdlib/papercraft/level_mod.prn` already has to `stdlib/sdl2.prn`.

### 1.1 `engine/netcode` — wire protocol + authority state machine
Lift `SHANKPIT-NETCODE`'s contract (client state machine, `WELCOME`-is-only-`client_id`-source,
anti-ghost active-flag rule, snapshot sequencing) out of prose and into a real PARENA module:
connection lifecycle, snapshot encode/decode, ack tracking. SHANKPIT consumes it via PARENA's C
emitter (hot path, already proven — PAPERCRAFT/ECOWAR both run PARENA-emitted C in a live loop).
REDGARDEN and GFD's Go backends consume the *same source* via BURROW's Go emission target, not a
hand-port. This is the real fix for the "reference copy" drift risk: one `.prn` source, two real
emitted outputs, not one C original and one human transcription.

### 1.2 `engine/reconcile` — client prediction + server reconciliation
Lift `SHANKPIT-PREDICT`'s rules (predict local player only, reconcile to server truth, never
trust client on authority-owned fields) the same way. REDGARDEN's unit/hero movement and GFD's
character movement are the same reconciliation shape at different tick rates — the module takes
tick rate as a parameter, not a rewrite.

### 1.3 `engine/humanness` — bot mood, reaction delay, imperfect compliance
Promote `REDGARDEN-HUMANNESS`'s MISHRI-derived shapes (temperament, `reactionDelay`/`chatDelay`
split, APM throttle, imperfect compliance, deterministic-hash-not-`rand()` variance keyed on
`(server_tick, owner_slot, purpose_tag)`) from spec prose into real PARENA code. This becomes the
one humanness primitive under SHANKPIT's evolution bots (`SHANKPIT-AI`), REDGARDEN's
`arena_bot`/ping-response bots, and GFD's NM/mob AI (`GFD-HERO-FRAMEWORK`'s NM window/respawn
model needs exactly this to feel alive, not scripted). Determinism contract ports as-is — it's
already written to be engine-agnostic.

### 1.4 `engine/itemstat` — slotted items, stat computation, resolution
Unify `GFD-INVENTORY` (16-slot gear, item definition registry, stat computation) and REDGARDEN's
item/card/consumable resolution (`ecowar/card_effect_mod.prn`, starting-item roster,
`REDGARDEN-COOKING`'s resource→cooked-buff chain) under one slotted-item + stat-formula + effect-
resolution module. SHANKPIT's own loadout/perk system (if/when it needs one) and GFD's
`loot.Pool`/`itemdef.Item` both become thin registries that *declare* items against this module
rather than re-deriving stat math independently.

### 1.5 `engine/matchmaking` — connect tickets, matchmaker, bot-pool
REDGARDEN already has a real, shipped matchmaker (`apps/matchmaker`, HMAC connect-ticket
accounts, shankpit-460 pattern — note the name: it was already ported from SHANKPIT once, by
hand). ECOWAR's own 1v1 matchmaker+bot-pool (`:9779`) and SLOWBOT_LEAGUE's planned PFSP bot pool
are the same shape again. This becomes the third module lifted to `engine/`, with SHANKPIT itself
as a consumer going forward instead of the unacknowledged original.

## 2) Emission targets and the real capability ceiling

Per-target status, checked against each module's real complexity, not assumed:

| Module | C (SHANKPIT, native) | Go via BURROW (REDGARDEN/GFD backends) | Java (MJOLNIR/SPIDERBEETLE, future) |
|---|---|---|---|
| `netcode` | Ready — PARENA C emitter mature, proven in PAPERCRAFT/ECOWAR | **Blocked** — BURROW is scalar + flat-struct only today (no `defenum`/`match`/`Vec`), and a connection state machine needs at least `defenum` + `match` | Not scoped |
| `reconcile` | Ready | Same BURROW ceiling — needs `Vec`/struct support for snapshot buffers | Not scoped |
| `humanness` | Ready | Same ceiling if mood/temperament is modeled as a struct+enum; a scalar-only first cut (temperament as an int, no `defenum`) is real and buys time | Not scoped |
| `itemstat` | Ready | Needs `Vec`+struct (slotted inventory) — blocked same as above | v0 precedent exists (`SPIDERBEETLE`'s two scalar functions) but nowhere near this module's real shape |
| `matchmaking` | N/A (REDGARDEN/GFD backends are the real consumers, not SHANKPIT) | Needs collections (queue/pool) — blocked | Not scoped |

This is the same real boundary `LO`'s capability audit already drew between `parena`
(defstruct/defenum/match/loop/Vec — mature) and `burrow` (scalar+flat-struct only). **The actual
blocking dependency for this whole northstar is BURROW's own Phase 3+ (struct/enum/match/Vec Go
emission), not anything specific to SHANKPIT/REDGARDEN/GFD.** Do not start cutting these modules
over to REDGARDEN/GFD before that lands — it will produce the same scalar-ceiling wall every other
BURROW-dependent repo has already hit. SHANKPIT itself is unblocked today (C emitter only) and
should go first.

## 3) Phased plan

- **Phase 0 (SHANKPIT only, C emitter, unblocked today):** write `engine/netcode` and
  `engine/reconcile` in PARENA from the existing `SHANKPIT-NETCODE`/`SHANKPIT-PREDICT` specs,
  compile to C, swap SHANKPIT's own hand-written netcode/prediction code to call the emitted
  module. Acceptance: SHANKPIT's existing netcode test suite passes unchanged against the
  PARENA-emitted implementation. This is the real proof the lift is faithful before anyone else
  depends on it.
- **Phase 1 (SHANKPIT only):** same lift for `engine/humanness`, consumed by `SHANKPIT-AI`'s
  evolution bots. Acceptance: existing bot-behavior tests (if any) pass; a live match shows the
  same deterministic-hash variance contract (same `(tick, slot, purpose_tag)` inputs → same bot
  choices across runs).
- **Phase 2 (BURROW dependency — blocked, tracked in `EMILY/BACKLOG.md`, not started here):**
  BURROW ships struct/enum/match/Vec Go emission (its own Phase 3-4, already named in
  `BURROW/NORTHSTAR.md` as DUNG's own blocker too — this is a second real consumer making the case
  for prioritizing it, not a new ask).
- **Phase 3 (REDGARDEN + GFD cutover, gated on Phase 2):** REDGARDEN's matchmaker/bot code and
  GFD's inventory/mob-AI code swap their hand-ported logic for calls into the same `engine/*`
  PARENA-emitted Go modules SHANKPIT already proved in Phase 0/1. Retire the "canonical / reference
  copy" convention on `SHANKPIT-NETCODE`/`SHANKPIT-PREDICT` at this point — there is no longer a
  copy to keep in sync, there's one source.
- **Phase 4 (itemstat unification):** `GFD-INVENTORY` and REDGARDEN's item/card resolution both
  migrate onto `engine/itemstat`. This is the highest-risk phase — GFD's stat formulas and
  REDGARDEN's card-effect model were designed independently and may not actually be the same
  shape under real scrutiny; the module's interface needs to be validated against both before
  either migrates, not assumed compatible because this doc says so.

## 4) What this northstar deliberately does not do

- Does not merge SHANKPIT/REDGARDEN/GFD into one repo or one binary. They stay three products;
  only the engine substrate underneath becomes one source.
- Does not touch gameplay-visible content (hero kits, item names, maps, narrative) — purely the
  authority/reconciliation/bot/stat *mechanism* layer.
- Does not start Phase 2 (BURROW work) itself — that belongs to `BURROW/NORTHSTAR.md`'s own
  phased plan; this doc only names the dependency honestly so Phase 3+ isn't scheduled against a
  BURROW capability that doesn't exist yet.
- Does not assume `itemstat` unification (Phase 4) is low-risk — flagged above as the one phase
  needing a real compatibility check before migration, not a lift-and-done like the others.

## 5) Open questions (not resolved here)

1. Does `engine/humanness`'s temperament model need to be a `defenum` from day one (cleaner, but
   blocked on BURROW), or is a scalar-int first cut (unblocked today) worth taking for GFD/REDGARDEN
   ahead of Phase 2, at the cost of a second migration later?
2. SHANKPIT's tick rate is real-time FPS-fast; GFD's MUD-style world may tick far slower. Does
   `engine/reconcile` take tick rate as a true runtime parameter, or does the reconciliation
   *shape* itself change at GFD's scale (e.g. no meaningful "prediction" at MUD latency)? Needs a
   real look at GFD's actual movement model before Phase 3, not assumed from SHANKPIT's shape.
3. Who owns `engine/*` module changes once three products depend on it — same open-question shape
   PARENA's own stdlib governance already has for any widely-depended-on module, not new to this
   doc.
