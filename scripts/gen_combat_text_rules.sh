#!/usr/bin/env bash
# gen_combat_text_rules.sh -- regenerate packages/simulation/combat_text_rules.c from PARENA.
#
# Floating combat text numbers live in PARENA/stdlib/shankpit/combat_text_rules.prn
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/combat_text_rules.prn" \
    -o "$root/packages/simulation/combat_text_rules.c"
echo "regenerated packages/simulation/combat_text_rules.c"
