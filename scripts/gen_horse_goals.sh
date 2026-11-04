#!/usr/bin/env bash
# gen_horse_goals.sh -- regenerate packages/simulation/horse_goals.c from PARENA.
#
# What a loose horse decides (goal, duration, walk speed, paddock radius, grazing recovery,
# startle radius, MISHRI reaction base) is decision logic, so it lives in PARENA:
#   PARENA/stdlib/shankpit/horse_goals.prn
# local_game.h's horse_goal_tick keeps the integration -- the roll, the clock, the moving.
# Usage: scripts/gen_horse_goals.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/horse_goals.prn" \
    -o "$root/packages/simulation/horse_goals.c"
echo "regenerated packages/simulation/horse_goals.c"
