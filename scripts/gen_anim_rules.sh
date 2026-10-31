#!/usr/bin/env bash
# gen_anim_rules.sh -- regenerate packages/simulation/anim_rules.c from PARENA.
#
# Which clip a player plays (priority, speed bands, melee windows, blend and loop flags, eye height)
# is decision logic, so it lives in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/anim_rules.prn
# packages/simulation/anim_state.h is the host (real PlayerState fields + the clip-name contract).
# Usage: scripts/gen_anim_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/anim_rules.prn" \
    -o "$root/packages/simulation/anim_rules.c"
echo "regenerated packages/simulation/anim_rules.c"
