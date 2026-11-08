#!/usr/bin/env bash
# gen_level_manager_rules.sh -- regenerate packages/simulation/level_manager_rules.c from PARENA (T0 step 4).
#
# The level manager's decisions (chords, the state machine, which lifecycle row a transition announces, who
# owns input, the fixed-step accumulator, SDL key -> DOM keyCode) live in
# PARENA/stdlib/shankpit/level_manager_rules.prn; apps/lobby/src/level_shell.c is the plumbing that calls them.
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/level_manager_rules.prn" \
    -o "$root/packages/simulation/level_manager_rules.c"
echo "regenerated packages/simulation/level_manager_rules.c"
