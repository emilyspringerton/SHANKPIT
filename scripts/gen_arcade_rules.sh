#!/usr/bin/env bash
# gen_arcade_rules.sh -- regenerate packages/simulation/arcade_rules.c from PARENA.
#
# The SHANKPHONE's PELLEGRINO games app is PARENA, mods first: the games are a mod
# (PARENA/stdlib/shankpit/arcade_games.prn -- add a game there) and the app's behaviour is rules
# (PARENA/stdlib/shankpit/arcade.prn). Both compile into one C file; packages/simulation/phone_arcade.h holds the state.
# Usage: scripts/gen_arcade_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/arcade_games.prn" "$parena_dir/stdlib/shankpit/arcade.prn" \
    -o "$root/packages/simulation/arcade_rules.c"
echo "regenerated packages/simulation/arcade_rules.c"
