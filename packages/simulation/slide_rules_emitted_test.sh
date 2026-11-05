#!/usr/bin/env bash
# The emitted C is the artifact this fork builds: assert the real PARENA compiler produced it,
# that it carries the module's exports, and that the scalar-only strip happened (a surviving
# #include "parena_runtime.h" would drag SDL2/POSIX headers into a package that has none).
set -euo pipefail

gen="$1"

test -s "$gen" || { echo "missing or empty: $gen" >&2; exit 1; }

grep -q 'slide_rules' "$gen" || { echo "no slide_rules symbols emitted into $gen" >&2; exit 1; }

if grep -q '#include "parena_runtime.h"' "$gen"; then
  echo "scalar-only module still includes parena_runtime.h: $gen" >&2
  exit 1
fi

defs=$(grep -cE '^[A-Za-z_][A-Za-z0-9_ *]*\(' "$gen" || true)
[ "${defs:-0}" -ge 5 ] || { echo "expected >=5 emitted definitions, found ${defs:-0} in $gen" >&2; exit 1; }

echo "slide_rules: emitted C ok (${defs} definitions, runtime include stripped)"
