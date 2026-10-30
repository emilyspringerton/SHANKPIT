#!/usr/bin/env bash
# gen_lighting_rules.sh -- regenerate packages/simulation/lighting_rules.c from PARENA.
#
# The unified environment matrix (founder real-time, 2026-10-30: "all the lighting needs to talk to
# each other ... it always looks too dark everywhere") is decision logic, so per this monorepo's own
# rule it lives in PARENA, not host C:
#   PARENA/stdlib/shankpit/lighting_rules.prn   occlusion / light bleed / floor / exposure /
#                                               emissive + emitter scales
# packages/render/env_matrix.c only converts units, applies the results to RetroLightingState and
# publishes the two hardware-facing globals; the numbers themselves are here.
# Usage: scripts/gen_lighting_rules.sh   (needs a sibling ../PARENA checkout with .parena-bootstrap)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/lighting_rules.prn" \
    -o "$root/packages/simulation/lighting_rules.c"
echo "regenerated packages/simulation/lighting_rules.c"
