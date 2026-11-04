#!/usr/bin/env bash
# gen_motorsport_rules.sh -- regenerate packages/simulation/motorsport_rules.c from PARENA.
#
# Usage: scripts/gen_motorsport_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/motorsport_rules.prn" \
    -o "$root/packages/simulation/motorsport/motorsport_rules.c"
echo "regenerated packages/simulation/motorsport/motorsport_rules.c"
