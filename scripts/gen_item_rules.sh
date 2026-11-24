#!/usr/bin/env bash
# gen_item_rules.sh -- regenerate packages/simulation/item_rules.c from PARENA.
#   PARENA/stdlib/shankpit/item_rules.prn  (desk items: pickup, carry, use-A-on-B combos)
# Usage: scripts/gen_item_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/item_rules.prn" -o "$root/packages/simulation/item_rules.c"
echo "regenerated packages/simulation/item_rules.c"
