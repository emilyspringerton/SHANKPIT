#!/usr/bin/env bash
# gen_trapx_city_rules.sh -- emit TRAPX's city-scale engine C from PARENA.
#
#   PARENA/stdlib/trapx/city_engine_rules.prn --> packages/simulation/trapx_city_rules.c
#
# The street's own rules are trapx_rules.prn (scripts/gen_trapx_rules.sh). This is the other half: the
# city that keeps running whether or not anyone is playing -- population, economy, casinos, scrutiny,
# federal oversight, neighborhoods, the Spire. Same rule: edit a threshold here and every host follows;
# nowhere else may state one.
#
# Usage: scripts/gen_trapx_city_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"

if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    echo "gen_trapx_city_rules: no parena binary at $parena_dir (build it with: cd ../PARENA && make .parena-bootstrap)" >&2
    exit 1
fi

"$parena_bin" build "$parena_dir/stdlib/trapx/city_engine_rules.prn" \
    -o "$root/packages/simulation/trapx_city_rules.c"
echo "regenerated packages/simulation/trapx_city_rules.c"
