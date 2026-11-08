# wasm3 — vendored STOPGAP

**This is a stopgap, not the answer.** Core deps are PARENA-first (MONOREPO `CLAUDE.md`, "Core Deps Are
PARENA-First"). SHANKPIT runs level modules (T0: BRAWLPIT, `docs/T0_LEVEL_WASM_CONTRACT.md`) through this
interpreter only until the PARENA-native wasm interpreter exists: `EMILY/BACKLOG.md` **PARENA-WASM-INTERP**.
Its first brick is already PARENA: the module's import/export surface is read by
`PARENA/stdlib/wasm/surface.prn` before this runtime sees a byte.

| | |
|---|---|
| Upstream | https://github.com/wasm3/wasm3 |
| Commit | `28ecb9af6d2040e474a70f7cb7f43666740141fb` ("Implement Atomics", 2026-09-29) |
| License | MIT (`LICENSE`, copied unchanged) |
| Files | `source/`: the 11 translation units the interpreter needs (`m3_bind m3_code m3_compile m3_core m3_env m3_exec m3_function m3_info m3_module m3_parse m3_validate`) and the 23 headers they include. No WASI, libc, tracer, uvwasi or extension files. |
| Modified | **No.** Byte-for-byte from that commit. |
| Flags | Compiled inside `LOBBY_SRC` with the lobby's own flags (0 warnings at `-O2 -Wall` on gcc 13 and mingw-w64 13); `make test-wasm-host` builds it with `-std=c99 -O2 -w`. Exempt from the doctrine's strict bar by design: 18 of 19 upstream files fail `-pedantic` (object↔function pointer casts in `m3_exec_defs.h`). |
| Who calls it | Only `packages/wasm_host/wasm_host.c`. No other file includes a wasm3 header, so swapping the runtime touches one file. |

Why wasm3 over WAMR (decision A, working default 2026-11-08, see the contract doc): measured on the real
BRAWLPIT module, 2-core box, init + 600 frames — wasm3 ≈ 13-16 ms/frame, WAMR fast-interp ≈ 19-21 ms/frame;
wasm3 needs no CMake and no C++ on Windows, and WAMR's default `SHRUNK_MEMORY=1` traps BRAWLPIT's fixed-address
init.

Updating: copy the same file list from a newer upstream commit, record the commit here, run
`make test-wasm-host` and `make check-windows`. Do not patch files in place; if a fix is needed upstream first.
