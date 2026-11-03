#!/usr/bin/env bash
# gen_vehicle_salvage_rules.sh -- regenerate packages/simulation/vehicle_salvage_rules.c from PARENA.
#
# A car's component table (engine, chassis, wheels, logic, tank), what losing each one costs in
# handling, and the salvage tier/quantity tables are RULES, not physics:
#   PARENA/stdlib/shankpit/vehicle_salvage_rules.prn
# packages/simulation/vehicle_salvage.h keeps the hull-conserving split and the state; this is the
# numbers. Same split vehicle_burn_rules.prn / vehicle_burn.h already uses.
#
# Usage: scripts/gen_vehicle_salvage_rules.sh [out.c]
#   (needs a sibling ../PARENA checkout, or PARENA_DIR; builds .parena-bootstrap if there is no
#    ./parena, which is the case on a box with no SDL2 -- the full build wants SDL2, the bootstrap
#    compiler does not)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
out="${1:-$root/packages/simulation/vehicle_salvage_rules.c}"
if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    (cd "$parena_dir" && make .parena-bootstrap >/dev/null)
    parena_bin="$parena_dir/.parena-bootstrap"
fi
"$parena_bin" build "$parena_dir/stdlib/shankpit/vehicle_salvage_rules.prn" -o "$out"
echo "regenerated $out"
