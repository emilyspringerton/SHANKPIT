#!/usr/bin/env bash
# gen_drone_rules.sh -- regenerate packages/simulation/drone_rules.c from PARENA.
#
# The two quadcopters' airframes are decision logic (response rate, top speed, climb, yaw, weather
# drift, bounce, hull), so they live in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/drone_rules.prn
# packages/simulation/drone_flight.h keeps the immediate-thrust integration and collision; this is
# the numbers. Usage: scripts/gen_drone_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/drone_rules.prn" \
    -o "$root/packages/simulation/drone_rules.c"
echo "regenerated packages/simulation/drone_rules.c"
