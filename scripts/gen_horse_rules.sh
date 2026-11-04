#!/usr/bin/env bash
# gen_horse_rules.sh -- regenerate packages/simulation/horse_rules.c from PARENA.
#
# The tölt mount's handling is decision logic (top speed, stamina drain/regen, the jump, the carrot),
# so it lives in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/horse_rules.prn
# local_game.h keeps the integration and collision (horse_tick_all); this is the numbers.
# Usage: scripts/gen_horse_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/horse_rules.prn" \
    -o "$root/packages/simulation/horse_rules.c"
echo "regenerated packages/simulation/horse_rules.c"
