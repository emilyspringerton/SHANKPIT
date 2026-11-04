#!/usr/bin/env bash
# gen_cozy_rules.sh -- regenerate packages/simulation/cozy_rules.c from PARENA.
#
# Kicking a dandelion and hugging a tree are shared decisions: when they are allowed, what they give back, and how
# fast the comfort goes away again. SHANKPIT compiles them to C here; NOOBTOPIA emits the same .prn to Luau
# (scripts/gen_rules.sh). Edit a cooldown here and both worlds change; nowhere else may state one.
#
# Usage: scripts/gen_cozy_rules.sh   (needs a sibling ../PARENA checkout with .parena-bootstrap, or PARENA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"

if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    echo "gen_cozy_rules: no parena binary at $parena_dir (build it with: cd ../PARENA && make .parena-bootstrap)" >&2
    exit 1
fi

"$parena_bin" build "$parena_dir/stdlib/shankpit/cozy_rules.prn" \
    -o "$root/packages/simulation/cozy_rules.c"
echo "regenerated packages/simulation/cozy_rules.c"
