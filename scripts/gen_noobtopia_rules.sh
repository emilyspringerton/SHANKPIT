#!/usr/bin/env bash
# gen_noobtopia_rules.sh -- regenerate the C half of the NOOBTOPIA three-way merge.
#
# The decisions live in PARENA, once, and are emitted twice:
#   PARENA/stdlib/noobtopia/ambient_life.prn  -> packages/simulation/ambient_life.c  (this script)
#                                             -> NOOBTOPIA/src/generated/ambient_life.lua (NOOBTOPIA/scripts/gen_rules.sh)
#   PARENA/stdlib/noobtopia/arena_rules.prn   -> packages/simulation/arena_rules.c   (this script)
#                                             -> NOOBTOPIA/src/generated/arena_rules.lua
# Editing a threshold means editing the .prn and re-running both generators; there is no second copy to forget.
#
# Usage: scripts/gen_noobtopia_rules.sh   (needs a sibling ../PARENA checkout with .parena-bootstrap)
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

"$parena_bin" build "$parena_dir/stdlib/noobtopia/ambient_life.prn" \
    -o "$root/packages/simulation/ambient_life.c"
"$parena_bin" build "$parena_dir/stdlib/noobtopia/arena_rules.prn" \
    -o "$root/packages/simulation/arena_rules.c"
echo "regenerated packages/simulation/{ambient_life,arena_rules}.c"
