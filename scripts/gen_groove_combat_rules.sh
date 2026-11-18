#!/usr/bin/env bash
# gen_groove_combat_rules.sh -- regenerate packages/simulation/groove_combat_rules.c from PARENA.
#
# Groove combat's rules (moves, frame data, reach, the beat grade, combos, style, the input mapping) are decision
# logic, so they live in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/groove_combat_rules.prn, groove_sync_rules.prn
# packages/simulation/groove_combat.h is the host (fighter state, geometry, REFLUX_ACTION_COMBAT_*).
# Usage: scripts/gen_groove_combat_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
for m in groove_combat_rules groove_sync_rules; do
    "$parena_bin" build "$parena_dir/stdlib/shankpit/$m.prn" -o "$root/packages/simulation/$m.c"
done
echo "regenerated packages/simulation/groove_combat_rules.c and groove_sync_rules.c"
