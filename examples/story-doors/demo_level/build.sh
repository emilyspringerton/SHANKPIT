#!/usr/bin/env bash
# Compiles this demo's two real door scripts. Run from anywhere -- paths are relative to this
# script's own location, and level.json's own script_path entries are relative to the SHANKPIT
# repo root (matching how you'd normally launch bin/shank_server from there).
set -euo pipefail
cd "$(dirname "$0")"

PARENA_REPO="${PARENA_REPO:-/home/fatbaby/PARENA}"
PARENA_BIN="${PARENA_BIN:-$PARENA_REPO/parena}"
RUNTIME_C="$PARENA_REPO/runtime/parena_runtime.c"
RUNTIME_H="$PARENA_REPO/runtime/parena_runtime.h"

for f in door_tick one_way_door; do
    cp "$RUNTIME_H" .
    "$PARENA_BIN" build "$f.prn" -o "$f.c"
    gcc -shared -fPIC -o "$f.so" "$f.c" "$RUNTIME_C" -lm
    echo "built $f.so"
done
rm -f parena_runtime.h

echo "done -- run from the SHANKPIT repo root:"
echo "  ./bin/shank_server --level examples/story-doors/demo_level/level.json"
