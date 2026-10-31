#!/usr/bin/env bash
# gen_br_stadium.sh -- regenerate packages/simulation/br_stadium.c from PARENA.
#
# The stadium's shape (a rounded box with 70-unit fillets, four upside-down goal shafts, the rally
# loop's scale) is shared by the wasm game and the native SHANKPIT client:
#   PARENA/stdlib/bedrockracers/web/br_stadium.prn
# Usage: scripts/gen_br_stadium.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/bedrockracers/web/br_stadium.prn" \
    -o "$root/packages/simulation/br_stadium.c"
echo "regenerated packages/simulation/br_stadium.c"
