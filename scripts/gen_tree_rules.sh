#!/usr/bin/env bash
# Regenerates packages/simulation/tree_rules.c from PARENA/stdlib/shankpit/tree_rules.prn (trees + wood: the shared rule file the
# PARENA wasm survival mode links directly). Same shape as the other gen_*_rules.sh scripts.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
if [ -x "$parena_dir/.parena-bootstrap" ]; then parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then parena_bin="$parena_dir/parena"
else (cd "$parena_dir" && make .parena-bootstrap >/dev/null); parena_bin="$parena_dir/.parena-bootstrap"; fi
"$parena_bin" build "$parena_dir/stdlib/shankpit/tree_rules.prn" -o "$root/packages/simulation/tree_rules.c"
echo "regenerated packages/simulation/tree_rules.c"
