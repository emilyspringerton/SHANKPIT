# CAPTCHA-FPS / SHANKPIT-Physics-in-PARENA — NORTHSTAR

*Written 2026-09-22. Founder real-time: "we need to dog food all of the physics for shankpit
into parena - the new product is a CAPTCHA that spawns an in browser fps and you have to compete
to prove that you are a human to continue to the protected site."*

Per Principle 19 ("a big, unscoped ask gets scoped, not swallowed whole") — this is two real,
large asks folded into one sentence, and both get a real, checked-first capability audit before
any phased plan, not a full build attempted in one pass.

## 1. Real, checked-first finding: the gap is bigger than "port some code"

**SHANKPIT's real physics is 3427 lines** (`packages/common/physics.h`) of hand-written,
struct-heavy, pointer-heavy C: `PlayerState`/`Box`/`Vec2`/`Projectile`/`ServerState` structs,
full 3D vector math, `trace_map`/`check_hit_location` raycasting, `resolve_collision`,
`apply_friction`/`accelerate` (real quake-style air/ground movement), weapon update logic, and a
real **lag-compensation rewind system** (`phys_store_history`/`phys_resolve_rewind` — per-client
position history buffers, arguably the single hardest-to-port piece here).

**PARENA's real in-browser compile target (`src/emit_ts.c`) is scalar-only v0** — checked
directly against the compiler source, not assumed: `defn`/`module`/`export`/`import` only,
`I32`/`F64`/`Bool`/`String`/`Unit` types only, **no `defstruct`, no `loop`, no arrays/`Vec`, no
`match`** (same real "unsupported form" errors the C target's own richer construct set never
hits). This is the exact same real boundary already named for the Java target in `LO/
NORTHSTAR.md` and `DEADWEIGHT/NORTHSTAR.md`'s own capability audits — not a new discovery, but
the first time it's been checked against something this physics-heavy. Every real, dogfooded TS
win so far (`bezier_interp.prn`, `DEADWEIGHT/stdlib/deadweight/card_rules.prn` and
`fx_rules.prn`) is **pure scalar decision logic** — branching arithmetic over `I32`/`F64`/`Bool`,
never a struct, an array, or a loop. Porting `physics.h` to `.prn` and compiling it to run in a
browser is not reachable on this target as it exists today — a real, large compiler feature gap
(`defstruct`/`loop`/`Vec` support in `emit_ts.c`), not a content-porting problem alone.

**No in-browser 3D FPS renderer exists anywhere in this monorepo today.** `DEADWEIGHT/web/` is
the only real prior art for "PARENA logic running in a browser," and it's a 2D card game UI
(hand-written TypeScript + PARENA-generated scalar decision snippets) — no WebGL/Three.js/canvas
3D rendering pipeline, no browser-side input/prediction/reconciliation netcode, nothing to extend
directly. SHANKPIT's own real client is SDL2 + OpenGL (native), a different rendering stack
entirely from anything a browser tab can run without a real, separate WebGL (or WASM+WebGL) port.

**The bot-vs-human irony is real and load-bearing, not a throwaway observation.** SHANKPIT
already has real, live, high-skill bots (`scripts/rl_league.py`'s PFSP pool, `SLOWBOT_LEAGUE/
NORTHSTAR.md`'s own gen-0 checkpoint at Elo 1564) that already outplay many humans at exactly
this game. A CAPTCHA whose pass condition is "win/compete in an FPS" needs a **humanness signal
that is NOT raw skill** (reaction-time jitter, mouse-movement micro-noise, imperfect tracking
under load — the same class of signal MISHRI's own `HumannessLayer.ts` already fakes, for the
opposite purpose: passing AS human, not detecting one) or a sufficiently bot-resistant bot will
defeat this CAPTCHA the same day it ships. This is real, unsolved design work, not an
implementation detail to backfill later.

## 2. What's real and reusable right now

- **REFLUX, PARENA-in-browser (scalar), and dual-target decision logic are all real, proven
  precedent** for the *lightweight* half of this idea: a challenge/verdict decision (did this
  session pass?) is exactly the shape `fx_rules.prn`/`card_rules.prn` already prove out — pure
  scalar branching, real TS output, already live in a browser (`DEADWEIGHT/web/`).
- **IDUNA's device-auth bridge** (this session's own `IDUNA.GAME` work) is the real, live
  precedent for "a short-lived challenge/session token, issued and verified against a real
  backend" — the CAPTCHA's own session/verdict token could plausibly reuse this shape rather than
  inventing a new auth primitive.
- **ECOWAR's matchmaker/bot-pool split** and **SLOWBOT_LEAGUE's own bot-vs-human Elo tracking**
  are the closest real precedent for "spin up a short, fair, bounded 1-round match on demand" —
  the CAPTCHA's own match lifecycle (spawn, time-box, verdict, teardown) is the same shape,
  applied to a single anonymous, unauthenticated challenger instead of a matched pair of known
  players.

## 3. Real, phased plan — smallest real slice first

- **Phase 0 (this doc)** — DONE. Real capability audit, not a plan written on top of assumptions.
- **Phase 1 — `emit_llvm.c` gains real `defstruct` + fixed-size-array support** (RETARGETED
  2026-09-22, see `PARENA/docs/LLVM_BACKEND_NORTHSTAR.md`'s own "WebAssembly: real, live, and
  free" section — founder real-time: "we can go full wasm with parena we need to dog food that
  anyways"). Originally scoped against `emit_ts.c`; moved to the LLVM backend because `llc`
  already registers `wasm32`/`wasm64` as real targets (`make wasm-smoke`, live-verified: a real
  F64 clamp compiled through PARENA → LLVM IR → `llc -mtriple=wasm32-unknown-unknown` → `wasm-ld`
  → executed for real in Node's `WebAssembly.instantiate`, zero new compiler code needed for the
  scalar case) — one compiler investment here reaches native (already proven: AVR, x86_64) AND
  the browser simultaneously, instead of `emit_ts.c` alone. The actual compiler work this whole
  idea is gated on is unchanged in kind: enough `defstruct`/array support to represent `Vec2`/
  `Box`/a small fixed-N `PlayerState` array, NOT full dynamic collections or `match`/`loop` yet (a
  second, smaller compiler pass can follow). Verify the same way `wasm-smoke` already does: a
  real `.prn` struct compiled to real LLVM IR, lowered to `wasm32`, executed under `node`, checked
  bit-for-bit against the same struct's C behavior. `emit_ts.c` itself stays real and correct for
  its own already-proven lane (DEADWEIGHT's scalar decision logic) — not deprecated, just no
  longer the load-bearing path for this specific idea.
- **Phase 2 — port the SMALLEST real physics slice, not all 3427 lines.** Movement + AABB
  collision only (`accelerate`/`apply_friction`/`resolve_collision`) — no weapons, no hit
  detection, no rewind. Enough to prove "a PARENA-authored player can walk around a PARENA-
  authored box in a browser," a real, demoable milestone, before committing to the much larger
  raycasting/rewind/weapon surface.
- **Phase 3 — a real, minimal browser renderer + input loop.** Raw WebGL or a thin Three.js
  layer (new real dependency, not yet used anywhere in this monorepo — named, not assumed) around
  Phase 2's movement, enough to literally see and move a box around a small level in a tab.
  Real, separate design question, not resolved here: single-player-vs-timer, or does a real
  human-vs-bot 1v1 need to exist for the "compete" framing in the founder's own ask to be literal.
- **Phase 4 — the actual CAPTCHA product surface.** An embeddable challenge widget + a real
  verification API a "protected site" calls server-side (the actual reCAPTCHA-shaped contract:
  issue challenge → user completes it in an iframe/popup → site's own backend verifies a token
  server-to-server, never trusting a client-reported "I passed"). Needs the humanness-signal
  design named in §1, not skill alone. Real, separate repo almost certainly warranted here (same
  "new product gets its own repo" convention `WOTAN`/`MIXFORGE`/`EMILY_FOR_BUSINESS` already
  follow) — not created yet; founder's own call per that same convention.
- **Phase 5 — weapons/hit-detection/rewind**, the hardest remaining slice of real physics.h, only
  once Phase 1-4 have proven the whole pipeline end-to-end on the simple case.

## 4. Status

| # | Phase | Status |
|---|---|---|
| 0 | This doc | DONE |
| 1 | `emit_llvm.c` defstruct + fixed-array support (retargeted from emit_ts.c, wasm32/wasm64 proven free via llc) | NOT STARTED |
| 2 | Movement + AABB collision ported to `.prn`, compiled to TS | NOT STARTED |
| 3 | Minimal browser renderer + input loop | NOT STARTED |
| 4 | CAPTCHA product surface (widget + verification API + humanness signal) | NOT STARTED |
| 5 | Weapons / hit detection / rewind netcode | NOT STARTED |

## Related

- `PARENA/STDLIB.md`'s own `emit_ts` dogfooding section — the real, current TS emitter track
  record (`bezier_interp.prn`, `card_rules.prn`, `fx_rules.prn`) this doc's Phase 1 builds on.
- `DEADWEIGHT/web/` — the only real prior "PARENA logic running in a browser" precedent, scalar
  decision logic only, no rendering pipeline to extend.
- `MISHRI/README.md` / `HumannessLayer.ts` — the real, existing prior art for the humanness-signal
  problem Phase 4 needs to solve, from the opposite side (passing as human vs. detecting one).
- `SLOWBOT_LEAGUE/NORTHSTAR.md`, `ECOWAR/docs/NORTHSTAR_MAP_LEAGUE.md` — real precedent for
  short, bounded, on-demand match lifecycles this idea's own match spawn/teardown resembles.
