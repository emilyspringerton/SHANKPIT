#!/usr/bin/env bash
# gen_zone_rules.sh -- regenerate packages/simulation/zone_rules.c from PARENA.
#
# Shared (instanceless) zones: the 15-minute grace period and the procedural layout parameters live in
#   PARENA/stdlib/shankpit/zone_rules.prn
# packages/simulation/zone_registry.h runs the lifecycle and packages/simulation/proc_dungeon.h builds a
# layout from one seed, so a zone is reproducible on every machine running this build.
# Usage: scripts/gen_zone_rules.sh   (needs ../PARENA, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/zone_rules.prn" \
    -o "$root/packages/simulation/zone_rules.c"
echo "regenerated packages/simulation/zone_rules.c"
