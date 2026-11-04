#!/usr/bin/env bash
# gen_horse_poop_rules.sh -- regenerate packages/simulation/horse_poop_rules.c from PARENA.
#
# When a loose horse drops a pile, where behind its tail it lands, how long it stays and how many
# the paddock can hold is decision logic, so it lives in PARENA:
#   PARENA/stdlib/shankpit/horse_poop_rules.prn
# local_game.h's horse tick keeps the integration -- the clock, the pile slots, the world.
# Usage: scripts/gen_horse_poop_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/horse_poop_rules.prn" \
    -o "$root/packages/simulation/horse_poop_rules.c"
echo "regenerated packages/simulation/horse_poop_rules.c"
