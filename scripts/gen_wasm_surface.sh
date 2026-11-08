#!/usr/bin/env bash
# gen_wasm_surface.sh -- regenerate packages/wasm_host/wasm_surface_gen.c from PARENA.
#
# The wasm binary format (what a module imports and exports) is a core dep, so it is PARENA:
#   PARENA/stdlib/wasm/surface.prn   (+ stdlib/bytes.prn, which it imports)
# packages/wasm_host/wasm_host.c uses it to refuse a level module whose imports the host does not
# offer, before the runtime ever links it.
#
# bytes.prn's seven functions are emitted as plain globals in every TU that is generated with it, and
# packages/world/png_decode_gen.c (already in LOBBY_SRC) carries the same seven, so they are renamed
# here to ws_bytes_* -- the same one-collision, one-rename fix the Makefile applies to EDITOR.GAME's
# arena_init/vec_i32_at. Only the definitions/calls of those exact names move; the runtime's *_impl
# helpers are untouched (the word boundary stops at "_impl").
#
# Usage: scripts/gen_wasm_surface.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    (cd "$parena_dir" && make .parena-bootstrap >/dev/null)
    parena_bin="$parena_dir/.parena-bootstrap"
fi
out="$root/packages/wasm_host/wasm_surface_gen.c"
tmp="$(mktemp)"; trap 'rm -f "$tmp"' EXIT
"$parena_bin" build "$parena_dir/stdlib/bytes.prn" "$parena_dir/stdlib/wasm/surface.prn" -o "$tmp" >/dev/null
# The include is pinned to packages/world's runtime header: from packages/wasm_host a bare
# "parena_runtime.h" resolves through the lobby's -I order to packages/simulation's copy first, which has
# no bytes_slice_impl (the lobby compile fails on it, not just a warning).
sed -E -e 's/\bbytes_(alloc|len|get|set_|from_string|to_string_lossy|slice)\b/ws_bytes_\1/g' \
       -e 's|^#include "parena_runtime.h"$|#include "../world/parena_runtime.h"|' "$tmp" > "$out"
echo "regenerated packages/wasm_host/wasm_surface_gen.c"
