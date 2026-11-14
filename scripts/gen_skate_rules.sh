#!/usr/bin/env bash
# gen_skate_rules.sh -- regenerate packages/simulation/skate_rules.c from PARENA.
#
# The skateboard's feel -- push, roll, brake, carve, hills, what a landing is, trick points and the combo -- is
# decision logic, so it lives in PARENA:  PARENA/stdlib/shankpit/skate_rules.prn
# packages/simulation/skate.h holds the board's state and integrates it.
# Usage: scripts/gen_skate_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/skate_rules.prn" \
    -o "$root/packages/simulation/skate_rules.c"
echo "regenerated packages/simulation/skate_rules.c"
