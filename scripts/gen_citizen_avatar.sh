#!/usr/bin/env bash
# gen_citizen_avatar.sh -- regenerate packages/simulation/citizen_avatar_rules.c from PARENA.
#
# A citizen's body and clothes are a total function of the persona it already has (its traits, its
# path, its own seed), authored in:
#   PARENA/stdlib/forgedinface/citizen.prn
# Usage: scripts/gen_citizen_avatar.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/forgedinface/citizen.prn" \
    -o "$root/packages/simulation/citizen_avatar_rules.c"
echo "regenerated packages/simulation/citizen_avatar_rules.c"
