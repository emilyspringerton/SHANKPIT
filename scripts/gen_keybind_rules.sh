#!/usr/bin/env bash
# gen_keybind_rules.sh -- regenerate packages/simulation/keybind_rules.c from PARENA.
#   PARENA/stdlib/shankpit/keybind_rules.prn  (WoW-style key bindings: 3 slots/op, contexts, conflicts)
# Usage: scripts/gen_keybind_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/keybind_rules.prn" -o "$root/packages/simulation/keybind_rules.c"
echo "regenerated packages/simulation/keybind_rules.c"
