#!/usr/bin/env bash
# gen_empire_units.sh -- regenerate packages/simulation/empire_units_rules.c from PARENA.
#
# The EMPIRE unit model lives in
#   PARENA/stdlib/shankpit/empire_units.prn
# packages/simulation/empire_units.h runs it: archetypes (citizen/zombie/soldier/ally/guard/worker), the
# human-vs-beast inheritance row, and the order mask + priority that make a unit's AI programmable.
# Usage: scripts/gen_empire_units.sh   (needs ../PARENA, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/empire_units.prn" \
    -o "$root/packages/simulation/empire_units_rules.c"
echo "regenerated packages/simulation/empire_units_rules.c"
