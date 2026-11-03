#!/usr/bin/env bash
# gen_zombie_brain_rules.sh -- regenerate the per-class brain shaping from PARENA.
#
# The OG train-mode net is one set of weights (brain_weights.h, from shank_brain_horde.pth). What makes a
# HORDE out of it is that each class reads its own body and its own range differently -- those numbers are
# PARENA/stdlib/shankpit/zombie_brain_rules.prn, emitted here, the same discipline ai_brain_rules.c and
# zombies_pheromone_rules.c already follow.
#   PARENA/stdlib/shankpit/zombie_brain_rules.prn -> packages/simulation/zombie_brain_rules.c
# Usage: scripts/gen_zombie_brain_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/zombie_brain_rules.prn" \
    -o "$root/packages/simulation/zombie_brain_rules.c"
echo "regenerated packages/simulation/zombie_brain_rules.c"
