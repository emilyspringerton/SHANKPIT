#!/usr/bin/env bash
# gen_trapx_rules.sh -- emit TRAPX VS0's C from PARENA. THIS IS THE GLUE.
#
#   PARENA/stdlib/trapx/trapx_rules.prn --> packages/simulation/trapx_rules.c   (SHANKPIT's C host)
#                                        --> WOTAN/trapx/trapx-rules.js         (the browser sandbox, via WOTAN/scripts/build-trapx.sh)
#
# Both are generated. Edit a threshold here and the C and the browser follow; nowhere else may state one.
# Usage: scripts/gen_trapx_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"

if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    echo "gen_trapx_rules: no parena binary at $parena_dir (build it with: cd ../PARENA && make .parena-bootstrap)" >&2
    exit 1
fi

"$parena_bin" build "$parena_dir/stdlib/trapx/trapx_rules.prn" \
    -o "$root/packages/simulation/trapx_rules.c"
echo "regenerated packages/simulation/trapx_rules.c"
