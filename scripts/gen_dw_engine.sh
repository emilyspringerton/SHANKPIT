#!/usr/bin/env bash
# gen_dw_engine.sh -- regenerate DEADWEIGHT's C engine from PARENA.
#
# DEADWEIGHT's rules and the citizens' play/deck policy are PARENA (the same source a wasm build
# compiles); SHANKPIT links the C emission of both, so the numbers cannot drift from the game.
#   PARENA/stdlib/deadweight/card_rules.prn      -> packages/simulation/dw_rules.c
#   PARENA/stdlib/deadweight/citizen_policy.prn  -> packages/simulation/dw_citizen_rules.c
# Usage: scripts/gen_dw_engine.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/deadweight/card_rules.prn" -o "$root/packages/simulation/dw_rules.c"
"$parena_bin" build "$parena_dir/stdlib/deadweight/citizen_policy.prn" -o "$root/packages/simulation/dw_citizen_rules.c"
echo "regenerated packages/simulation/dw_rules.c and dw_citizen_rules.c"
