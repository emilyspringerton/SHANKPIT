#!/usr/bin/env bash
# gen_citizen_actor_rules.sh -- regenerate packages/simulation/citizen_actor_rules.c from PARENA.
#
# What a citizen in MODE_ZOMBIES does with its body (jump, crouch, slide, slide-jump, wield, fire)
# and how it chooses between them is decision logic, so it lives in PARENA:
#   PARENA/stdlib/shankpit/citizen_actor_rules.prn
# witness_ai.c / local_game.h keep the integration -- the body, the clock, the shared physics.
# Usage: scripts/gen_citizen_actor_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/citizen_actor_rules.prn" \
    -o "$root/packages/simulation/citizen_actor_rules.c"
echo "regenerated packages/simulation/citizen_actor_rules.c"
