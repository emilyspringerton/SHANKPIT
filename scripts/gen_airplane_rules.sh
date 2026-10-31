#!/usr/bin/env bash
# gen_airplane_rules.sh -- regenerate packages/simulation/airplane_rules.c from PARENA.
#
# The fixed-wing transport's aerodynamics are decision logic (stall, lift curve, drag, thrust, hull), so
# they live in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/airplane_rules.prn
# packages/simulation/airplane_flight.h keeps the integration and collision; this is the numbers.
# Usage: scripts/gen_airplane_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/airplane_rules.prn" \
    -o "$root/packages/simulation/airplane_rules.c"
echo "regenerated packages/simulation/airplane_rules.c"
