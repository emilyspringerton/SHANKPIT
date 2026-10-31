#!/usr/bin/env bash
# gen_vehicle_burn_rules.sh -- regenerate packages/simulation/vehicle_burn_rules.c from PARENA.
#
# A car's destruction ladder (smoke, fire, detonation) and the fire LOOK (Bedrock Racers'
# br_fx.prn swell-fast/die-slow curve, its light flicker, its drama scalar) are rules, not physics:
#   PARENA/stdlib/shankpit/vehicle_burn_rules.prn
# packages/simulation/vehicle_burn.h keeps the per-tick state machine; this is the numbers.
# Usage: scripts/gen_vehicle_burn_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/vehicle_burn_rules.prn" \
    -o "$root/packages/simulation/vehicle_burn_rules.c"
echo "regenerated packages/simulation/vehicle_burn_rules.c"
