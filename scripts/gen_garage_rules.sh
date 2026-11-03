#!/usr/bin/env bash
# gen_garage_rules.sh -- regenerate packages/simulation/garage_rules.c from PARENA.
#
# What a garage accepts (by the car's origin and the objective it is serving) and what it pays are RULES,
# not gameplay code:
#   PARENA/stdlib/shankpit/garage_rules.prn
# packages/simulation/garage.h keeps the plate/garage state; this is the numbers.
#
# Usage: scripts/gen_garage_rules.sh [out.c]
#   (needs a sibling ../PARENA checkout, or PARENA_DIR; prefers PARENA's .parena-bootstrap, which is the
#    compiler that works on a box with no SDL2)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
out="${1:-$root/packages/simulation/garage_rules.c}"
if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    (cd "$parena_dir" && make .parena-bootstrap >/dev/null)
    parena_bin="$parena_dir/.parena-bootstrap"
fi
"$parena_bin" build "$parena_dir/stdlib/shankpit/garage_rules.prn" -o "$out"
echo "regenerated $out"
