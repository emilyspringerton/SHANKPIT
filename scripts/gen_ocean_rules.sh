#!/usr/bin/env bash
# gen_ocean_rules.sh -- regenerate packages/simulation/ocean.c from PARENA.
#
# Water is decision-shaped: sea state, wave height, planing lift, drag, how much of a hull is wet. So it lives in
# PARENA with the rest of the rules (stdlib/shankpit/ocean_rules.prn), and packages/simulation/ocean.h is the host
# that samples and integrates. See docs/ARCHIPELAGO_NORTHSTAR.md.
#
# Usage: scripts/gen_ocean_rules.sh   (needs a sibling ../PARENA checkout with .parena-bootstrap)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/ocean_rules.prn" \
    -o "$root/packages/simulation/ocean.c"
echo "regenerated packages/simulation/ocean.c"
