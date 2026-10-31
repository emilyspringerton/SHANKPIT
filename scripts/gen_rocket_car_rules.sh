#!/usr/bin/env bash
# gen_rocket_car_rules.sh -- regenerate packages/simulation/rocket_car_rules.c from PARENA.
#
# The rocket car's handling is decision logic (thrust, the air tank, the fan suspension), so it lives
# in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/rocket_car_rules.prn   ported from Bedrock Racers' br_phys.prn
# physics.h keeps the integration and collision (simulate_rocket_car_state); this is the numbers.
# Usage: scripts/gen_rocket_car_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/rocket_car_rules.prn" \
    -o "$root/packages/simulation/rocket_car_rules.c"
echo "regenerated packages/simulation/rocket_car_rules.c"
