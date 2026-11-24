#!/usr/bin/env bash
# gen_action_bar_rules.sh -- regenerate packages/simulation/action_bar_rules.c and action_bar_example_plugin.c from PARENA.
#   PARENA/stdlib/actionbar/action_bar_rules.prn, PARENA/stdlib/actionbar/example_plugin_mod.prn
# Usage: scripts/gen_action_bar_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/actionbar/action_bar_rules.prn" -o "$root/packages/simulation/action_bar_rules.c"
"$parena_bin" build "$parena_dir/stdlib/actionbar/example_plugin_mod.prn" -o "$root/packages/simulation/action_bar_example_plugin.c"
echo "regenerated action_bar_rules.c action_bar_example_plugin.c"
