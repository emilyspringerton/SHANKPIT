# T0 — BRAWLPIT v0 as a WASM level: contract, dependencies, plan (2026-11-08)

Governed by `specs/2026-11-08-0000.md` (Yggdrasil Doctrine). Source brief: "VS0 Build Process:
Claude Code Brief", T0. Order is contract -> test -> implement (Root III). **Nothing below is built yet.**

Rev 2 (same day): a dependency audit (5 subsystem readers, each adversarially re-checked against source;
31 of 33 claims survived, 2 refuted) found the rev-1 contract wrong on import/export names, the REFLUX
cursor, and missing whole steps (in-game compile, VS0 overlay, assets, input). Everything here cites source.

**Which BRAWLPIT:** the PARENA port, `PARENA/stdlib/brawlpit/*.prn`, parity-tested against the C engine
(EMILY/BACKLOG.md BP-WASM-WOTAN-02), shipped as `WOTAN/BRAWLPIT/brawlpit.wasm`. Not the separate C repo
(not checked out). That repo's `EMILY/docs/BRAWLPIT_MOD_API_NORTHSTAR.md` routes REFLUX through a
BRAWLPIT-local `reflux_runtime.c` over `:c`; T0 instead uses SHANKPIT's single host-side log over wasm
imports. Recorded divergence, not an accident.

---

## 1. M0 audit — what exists today (checked)

| Question | Finding | Evidence |
|---|---|---|
| BRAWLPIT builds to WASM? | Yes: `parena build -> opt -> llc -> wasm-ld`, 8 MB fixed memory, no allocator | `PARENA/scripts/build_brawlpit_wasm.sh` |
| Prebuilt artifact? | Yes, `WOTAN/BRAWLPIT/brawlpit.wasm` (135149 B, 0 imports, 846 exports, 23 of them `bp_*`) | WOTAN commit 1068920 |
| `level_*` exports? | **No.** `bp_init`, `bp_frame(f64)`, `bp_key`, `bp_pointer`, `bp_pad(i32,f32×6,i32)`, `bp_blur`, `bp_fb_addr/w/h`, `bp_{sprite,duck,hat}_{addr,set}`, `bp_hatname_addr`, `bp_phase`, `bp_ticks`, `bp_sim_frames`, `bp_rand`, `bp_seed_bang`, `bp_mem_bytes`, `bp_pad_gone` | `bp_main.prn` |
| REFLUX imports? | **No**, and the web host instantiates with an empty import object, and `tests/brawlpit_engine_test.mjs:26` asserts zero imports | `bp_host.prn:69` |
| Export/import naming | `mangle_name` maps `-`→`_`, `!`→`_bang`, `?`→`_p`; `(export)/(import)` forms are no-op; `--allow-undefined` puts every undefined extern under module `env` | `emit_llvm.c:83-90, 1576-1600` |
| Existing REFLUX LLVM binding | Yes: `stdlib/reflux/reflux.prn:37-75` declares `@reflux_host_*` with `:llvm` bodies → wasm imports `env.reflux_host_*` | verified in scratch build |
| Zero-import check | Runs **only if `node` is present**, yet always prints "0 imports" | `build_brawlpit_wasm.sh:58-66` |
| Host WASM runtime | None. `dw_engine_attach_wasm` (`packages/simulation/dw_engine.h:80`) is a second consumer waiting for one; `docs/DEADWEIGHT_IN_SHANKPIT_NEXT_STEPS.md:107-112` already leaned to wasm3 | |
| Embeddable VS0 | **No.** `PARENA/examples/editor_main.c` is a standalone `main()` with its own window (:1020, :1084). SHANKPIT's overlay bridge (`apps/lobby/src/editor_widget_bridge.c`) needs `editor_widget_*` from sibling `EDITOR.GAME` (absent → `editor_widget_stub.o`, `Makefile:161-165`) and is routed only in `STATE_LOBBY` (`main.c:18201`) | |
| In-game compile | **No path.** No direct wasm emitter; pipeline is bash + LLVM 18 at `~/.local/opt/llvm-toolchain`; VS0 save is `prnfmt` + write (`editor_main.c:995-1018`) | |
| Build bar | Makefile is `-O2 -Wall -D_REENTRANT`. Under the strict bar 5 legacy files fail (`humanness.c` M_PI, `level_boxes.h:2037` popen, `coop_host.c` nanosleep, `gband_skel_npc.c` pedantic); `reflux_runtime.c`/`reflux_mod.c` pass | |
| Windows build | `build_win.bat` is **dead** (targets nonexistent `apps/shank-fps`). Real path: `.github/workflows/release.yml` mingw cross-build with a hand-kept source list, and `make check-windows` | release.yml:148+, 201-202 |

## 2. Contract (rev 2) — names as they appear on the wire

New PARENA file `PARENA/stdlib/brawlpit/bp_level.prn` wraps the existing `bp-*` functions; game logic reused.
New build `PARENA/scripts/build_brawlpit_level_wasm.sh` = existing SRC list + `bp_level.prn` + `stdlib/reflux/reflux.prn`.
**The web build (`build_brawlpit_wasm.sh`, `bp_*` exports, zero imports) stays untouched**, so WOTAN and the node tests keep passing.

### Exports (explicit `--export=` list, not `--export-all`; build asserts the set is exact)

| PARENA | Wire | Signature | Meaning |
|---|---|---|---|
| `level-enter` | `level_enter` | `(cursor : I32) -> I32` | host passes current `reflux_host_total_dispatched()`; 0 ok, nonzero = failure code (level not shown, session intact) |
| `level-tick` | `level_tick` | `() -> Unit` | exactly **one** 60 Hz sim step + render. Host owns the accumulator (an I32-ms dt would drift from the parity-tested 16.666 ms step). Paused = host stops calling |
| `level-exit` | `level_exit` | `() -> Unit` | host may then drop the instance |
| `level-seed` | `level_seed` | `(s : I32) -> Unit` | replaces the wall-clock RNG seed `bp-frame` took from `now` |
| `level-key` | `level_key` | `(code : I32, down : I32) -> Unit` | **DOM keyCode**, +256 for right-hand Shift/Ctrl/Alt (`bp_input.prn:89-111`) |
| `level-pointer` | `level_pointer` | `(x : I32, y : I32, buttons : I32) -> Unit` | fb pixel coords |
| `level-blur` | `level_blur` | `() -> Unit` | clear held keys; host calls on every Shift+M, overlay open, text focus (else the Shift of Shift+M leaves P1 shield held) |
| `level-fb-addr/w/h` | `level_fb_addr/_w/_h` | `() -> I32` | 960×540, bytes R,G,B,A (`SDL_PIXELFORMAT_RGBA32`), stride 3840 |
| `level-asset-addr` | `level_asset_addr` | `(slot : I32) -> I32` | 0 = sprites, 1 = duck atlas (870400 B) |
| `level-asset-set` | `level_asset_set` | `(slot : I32, loaded : I32) -> Unit` | host writes decoded RGBA then sets |
| `memory` | `memory` | exported, 8 MB fixed | |

Gamepad (`bp_pad`, F32 sticks): **deferred by name** for T0. Hats (browser-rasterized emoji): deferred.

### Imports (module `env`; exactly these, enforced by `--allow-undefined-file=reflux.syms` + a non-node checker)

Names match `packages/reflux/reflux_runtime.h:228-248` 1:1 and `stdlib/reflux/reflux.prn` (PARENA-first, no new FFI):
`reflux_host_dispatch(i32,i32,i32,i32)`, `reflux_host_action_{type,a,b,c}_at(i32)->i32`,
**plus the absolute-cursor trio** `reflux_host_total_dispatched()`, `reflux_host_index_for_abs(i32)`,
`reflux_host_oldest_abs()` — to be added to `reflux.prn` with `:c` and `:llvm` keys.
`reflux_host_log_size()` is **not** used for cursors: it saturates at `REFLUX_LOG_CAPACITY` 256 and a cursor taken
from it goes silent forever (`reflux_runtime.h:231-235`, the horse_sync bug). Level cursors are absolute; the module
handles "overtaken" (`oldest_abs` > cursor).

### Action-type range (reserve before step 4)

Live types are 1-5 and 101-128; a door subscriber opens on type 1 (`main.c:3564`, `story_doors.h:195`). Reserve
**1000-1099 level lifecycle / 1100-1199 BRAWLPIT** in `reflux_runtime.h` with FIRST/LAST macros and
`reflux_action_is_level()`. The host's `reflux_host_dispatch` import **rejects** types outside the module's range:
the module is untrusted code.

### Failure modes

Unbound import → refused at load with a defined code (wasm3/WAMR otherwise trap only on first call). Trap in
`level_tick` → shell closes the level, world keeps running. Compile failure → previous `.wasm` stays, errors shown
in VS0 mapped back to file:line (the build concatenates 15 files).

## 3. Dependencies the plan needs (rev-1 gaps)

| # | Dependency | Needed by | How |
|---|---|---|---|
| D1 | LLVM 18 `opt`/`llc`/`wasm-ld` without sudo | step 1 | `apt-get download llvm-18 lld-18 libllvm18 liblld-18` + `dpkg-deb -x` into `~/.local/opt/llvm-toolchain`. **`lld-18` is where `wasm-ld` lives** and the documented recipe (`PARENA/docs/LLVM_BACKEND_NORTHSTAR.md:38`) omits it. Verified building brawlpit.wasm this way. Port `build_frontend_vs0_wasm.sh:20-29`'s fallback + missing-tool precheck |
| D2 | Non-node import/export checker | steps 1, 3 | small C lister in the host test (the runtime's own import enumeration); node optional |
| D3 | SDL2/TTF/GL/GLU/mbedtls `-dev` headers, `EDITOR.GAME` + `PELLEGRINO` siblings | steps 2, 4, Xvfb smoke | MONOREPO `BOOTSTRAP.md` "make lobby" recipe; no sudo here → `apt-get download` + `dpkg-deb -x`. Gate: `make print-editor-mode` ≠ stub |
| D4 | mingw-w64 + SDL2 2.30.10 / SDL2_ttf 2.22.0 mingw kits | "Windows confirmed" | define as green `release.yml` Windows job + `make check-windows`; add new files to both hand lists |
| D5 | Vendored WASM runtime at a pinned commit, own object list, own flags | step 2 | wasm3: 18/19 files fail `-pedantic` by design (object→function pointer, `m3_exec_defs.h:58`); builds with `-std=c99 -O2`, no CMake. WAMR: CMake, forces `gnu99`, needs C++ on Windows, **default `SHRUNK_MEMORY=1` traps BRAWLPIT's fixed-address init** (must pin `WAMR_BUILD_SHRUNK_MEMORY=0`). Precedent: sibling code already compiled `-std=c99 -w` (`Makefile:88-100`) |
| D6 | Embeddable VS0 widget | step 5 | PARENA-first: split `editor_main.c` behind `EDITOR_WIDGET_TEST_BUILD` (the guard `Makefile:93` already passes) exposing the 15 `editor_widget_stub.c` symbols + `editor_widget_open_file(path)`; Xvfb test like PARENA `test-editor-widget`; repoint SHANKPIT at `../PARENA` |
| D7 | Shell key filter + text-focus predicate | step 4 | bare `SDLK_m` cycles the M overlay with no modifier check (`main.c:18794`), M/J are editmap keys (`:4539`, `:4720`); focus is spread over `g_chat_open`, `g_orb_open`, `g_story_phone.open`, `phone_text_on`, `ui_edit_index`, editor panel. Relative mouse is forced every frame outside the lobby (`:18873`) |
| D8 | Assets from another repo | step 4 | `WOTAN/BRAWLPIT/{sprites,duck}.png` → decode with SHANKPIT's existing `packages/world/png_decode_gen.c` (verify RGBA/filter modes) or pre-convert to raw RGBA in the level build. Without them BRAWLPIT draws shape fallbacks (`bp_view.prn:319-321`) |
| D9 | SDL_Keycode → DOM keyCode table | step 4 | small host C file + test |
| D10 | Player-side compile toolchain | step 6 | see decision B |
| D11 | Level source manifest | steps 5, 6 | `PARENA/stdlib/brawlpit/level.manifest`: source list, entry file VS0 opens, output path; build reads it instead of a hardcoded SRC array; line map back from the concatenation |

## 4. Decisions

Decided (conventions, POL-4.6.6):
- Strict bar `-std=c99 -Wall -Wextra -pedantic -Werror` is enforced per file on **new** T0 host files and tests
  (`STRICT_CFLAGS` target, gcc and mingw). Vendored runtime and legacy `main.c` keep their flags; fixing the 5 legacy
  files is a separate item. New strict files must not include `level_boxes.h`.
- Export wire names are snake_case; imports are `env.reflux_host_*`.
- Toggle/close/load decisions live in a `.prn` level-manager mod (INV-1.2/2.1); the host only intercepts keys.
- Shortcuts: Shift+M checked before the bare-M branch (which gets `!(mod & KMOD_SHIFT)`) and before `macro_key_run`;
  Ctrl+Shift+J = `KMOD_CTRL|KMOD_SHIFT`, macOS `KMOD_GUI|KMOD_ALT`; both ignored while text focus is active.

For the founder (two at a time, POL-4.6.4):
- **A. Runtime: wasm3 or WAMR.** Measured on the real module, 2-core box, init + 600 frames: wasm3 ≈ 14-16 ms/frame,
  WAMR fast-interp ≈ 19-21 ms/frame — both at or over the 16.7 ms budget *before* the world renders. Recommendation:
  **wasm3** (prior lean in the DEADWEIGHT note, no CMake/C++, works unmodified) with a frame-budget acceptance item;
  if it misses on Windows, the escape is WAMR AOT, which adds `wamrc` (LLVM-based) to D1.
  **Either choice is a third-party core dep, so under "Core deps are PARENA-first" (MONOREPO `CLAUDE.md`) it ships
  labeled as a stopgap** in code and in `EMILY/BACKLOG.md`, with a PARENA-native wasm interpreter queued as the
  replacement. That interpreter is itself a candidate answer to decision B(b) and the frame-budget item, so it isn't
  a throwaway.
- **B. Where save→compile runs.** (a) ship `parena` + `opt`/`llc`/`wasm-ld` per OS and drive them from C, not bash;
  (b) a direct PARENA→wasm emitter (largest); (c) **T0 = dev-box hot reload only**, toolchain from D1, stated
  explicitly. Recommendation: **(c)** for T0, (a) queued. Also needs an INV-4.4.1 reading: compiled `.wasm` under
  `var/levels/` is a build output, not a user-file write.

Queued after A/B: BRAWLPIT camera/input vs FPS setup (brief's open question; moot while it renders its own 2D fb).

Proposed doctrine amendment (POL-7.1, founder decides): IFC-3R.2 says poll via `reflux-log-size`; that pattern is the
documented 256-saturation bug. Propose: poll via `total-dispatched` + `index-for-abs` from an absolute cursor.

## 5. Build order (rev 2)

Each step: contract row above → test → implement → strict build → record.

0. **Deps:** D1 (+ verify `build_brawlpit_wasm.sh` reproduces), D3. Commit the D1 recipe to MONOREPO `BOOTSTRAP.md`.
1. **Host runtime, in parallel with 0:** vendor the chosen runtime (A) under `third_party/`, own flags; strict
   `packages/wasm_host/` wrapper (load, bind-or-refuse imports, call by exact name). **First fixture is the committed
   `WOTAN/BRAWLPIT/brawlpit.wasm`** (`bp_init` + N×`bp_frame`, assert fb non-empty, time per frame) — needs no toolchain.
   Also serves `dw_engine_attach_wasm` later.
2. **PARENA level surface:** add the cursor trio to `reflux.prn`; write `bp_level.prn`; `build_brawlpit_level_wasm.sh`
   with explicit exports, `reflux.syms` allowlist, non-node checker (D2). Reserve the action range.
3. **REFLUX bridge:** import table → `reflux_runtime.c`, range-checked; test by dispatching from a stub level and
   watching the log.
4. **Shell (BRAWLPIT v0):** boot world + BRAWLPIT foreground; level-manager `.prn` mod; key filter + focus predicate
   (D7); keyCode table (D9); assets (D8); blit fb as RGBA32 texture; Shift+M toggle + pause + blur; close; load.
   Xvfb smoke injecting the chords. **This is the first cut the founder plays.**
5. **View source:** D6 widget split, overlay routed in game states with relative mouse released; D11 manifest; Ctrl+Shift+J.
6. **Save → compile → swap on next entry:** per decision B; failed compile keeps the old module.

Deferred (named): Actor API, brains, online module delivery + signing, difficulty ramp, engine control from levels,
gamepad, hats.

Refuted in audit (kept for the record): "and/or are eager in wasm, so levels trap" — accurate but BRAWLPIT's guarded
divisors are constant (`bp_view.prn:244,314`), so no T0 dependency; "must auto-start a match on boot" — the title
screen in the foreground satisfies the brief.
