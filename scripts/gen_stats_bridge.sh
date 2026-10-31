#!/usr/bin/env bash
# gen_stats_bridge.sh -- regenerate packages/simulation/stats_bridge.c from PARENA.
#
# GFD's job table and the mapping into SHANKPIT's gameplay units live in
#   PARENA/stdlib/shankpit/stats_bridge.prn
# packages/simulation/stats_bridge.h carries identical C defaults, and `make test-stats-bridge` compares
# the two copies sample for sample, so a retune cannot reach only one of them.
# Usage: scripts/gen_stats_bridge.sh   (needs ../PARENA, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/stats_bridge.prn" \
    -o "$root/packages/simulation/stats_bridge.c"
echo "regenerated packages/simulation/stats_bridge.c"
