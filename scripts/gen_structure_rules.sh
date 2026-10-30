#!/usr/bin/env bash
# gen_structure_rules.sh -- regenerate packages/simulation/structure_rules.c from PARENA.
#
# Structural behaviour is decision logic, so it lives in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/structure_rules.prn   material profiles, joint strength, load check
# packages/simulation/structure.h is the graph (nodes, joints, the load cascade); this is the numbers.
# Usage: scripts/gen_structure_rules.sh   (needs a sibling ../PARENA checkout with .parena-bootstrap)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/structure_rules.prn" \
    -o "$root/packages/simulation/structure_rules.c"
echo "regenerated packages/simulation/structure_rules.c"
