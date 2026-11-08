# T0 — BRAWLPIT v0 as a WASM level: contract + M0 audit (2026-11-08)

Governed by `specs/2026-11-08-0000.md` (Yggdrasil Doctrine). Source brief: "VS0 Build Process:
Claude Code Brief", T0. Order is contract -> test -> implement (Root III); nothing below is built yet.

## M0 audit (the brief's first subtask), checked against source

| Question | Finding | Evidence |
|---|---|---|
| Does BRAWLPIT already build to WASM? | Yes, wasm32 via `parena build` -> opt -> llc -> wasm-ld, 8 MB fixed memory | `PARENA/scripts/build_brawlpit_wasm.sh` |
| Does it export `level-enter/tick/exit`? | **No.** Exports are `bp_init`, `bp_frame(now:F64)`, `bp_key`, `bp_pad`, `bp_pointer`, `bp_fb_addr`, `bp_hat_*`; the page host draws the framebuffer | `PARENA/stdlib/brawlpit/bp_main.prn` |
| Does it accept REFLUX imports? | **No.** The build asserts *zero imports* and `--export-all`; no allocator | build script, same file |
| Does PARENA's LLVM target emit `export`/`import`? | `(module)/(export)/(import)` forms are **no-op metadata** in v0; exports come from linker `--export-all` | `PARENA/src/emit_llvm.c` ~L1576 |
| Does SHANKPIT embed a WASM runtime? | **No** (WAMR / wasm3 candidates, undecided) | no runtime in `packages/` |
| Does the build box have the WASM toolchain? | **No**: `~/.local/opt/llvm-toolchain` (LLVM 18), `wasm-ld`, `node` all absent here | shell check 2026-11-08 |

So the first subtask the brief predicted applies: the gap is real.

## Proposed contract (scalar-only, IFC-3.4)

Level lifecycle exports (a thin wrapper module over the existing `bp_*` entry points; game logic reused):

| Export | Signature | Meaning |
|---|---|---|
| `level-enter` | `() -> I32` | 0 ok, nonzero = failure code; host falls back, session stays up (BEH-5.5) |
| `level-tick` | `(dt-ms : I32) -> Unit` | advance by host-supplied fixed step; no wall clock inside the module |
| `level-exit` | `() -> Unit` | release; host may drop the instance |
| `level-fb-addr` | `() -> I32` | framebuffer address in linear memory (replaces `bp_fb_addr`) |

REFLUX imports (module `reflux`, all `i32`/unit, 1:1 with `reflux_runtime.h`): `reflux-dispatch(type,a,b,c)`,
`reflux-log-size()`, `reflux-action-{type,a,b,c}-at(i)`. Host holds cursors (INV-3R.3).

Input: `level-key(code, down)`, `level-pointer(x,y,buttons)` replace `bp_key`/`bp_pointer`; Shift+M and
Ctrl+Shift+J are consumed by the shell and never reach the level.

Failure modes: trap in `level-tick` -> shell closes the level, world keeps running; `level-enter` nonzero -> level not shown.

Open (needs a decision, kept to two): (1) WASM runtime for the host: WAMR vs wasm3 (Windows build + small C footprint);
(2) does BRAWLPIT's camera/input differ from the FPS setup (brief's open question).

## Build order (v0 first cut)

1. Toolchain on the build box (LLVM 18 + wasm-ld + node) -> reproduce current `build_brawlpit_wasm.sh` clean.
2. Host: embed runtime, load a module, call `level-enter/tick/exit` against a stub level (test first).
3. REFLUX imports bridged to `reflux_runtime.c`; verify by watching the log.
4. Wrap BRAWLPIT as a level mod; Shift+M toggle + pause; close/reload; Ctrl+Shift+J view source.

Deferred (named, not silent): Actor API, brains, online module delivery, difficulty ramp, engine control from levels.
