#!/usr/bin/env bash
# gen_empire_rules.sh -- regenerate packages/simulation/empire_rules.c from PARENA.
#
# The EMPIRE influence economy (what a troop costs, what a clone costs, when you may place one) and
# the sphere's district geometry are decision logic, not rendering:
#   PARENA/stdlib/shankpit/empire_rules.prn
# Usage: scripts/gen_empire_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/empire_rules.prn" \
    -o "$root/packages/simulation/empire_rules.c"
echo "regenerated packages/simulation/empire_rules.c"
