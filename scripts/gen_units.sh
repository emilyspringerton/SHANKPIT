#!/usr/bin/env bash
# gen_units.sh -- regenerate packages/common/units_rules.c from PARENA.
#
# The unit affordance layer's factors and its arithmetic (lengths, masses, volumes, metric <-> imperial,
# box volume/mass, grid snapping) are PARENA source: PARENA/stdlib/shankpit/units.prn. The C header
# (units_rules.h) is the contract; this file is generated and checked in, so a build never needs the
# PARENA toolchain.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
out="${1:-$root/packages/common/units_rules.c}"
if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    (cd "$parena_dir" && make .parena-bootstrap >/dev/null)
    parena_bin="$parena_dir/.parena-bootstrap"
fi
"$parena_bin" build "$parena_dir/stdlib/shankpit/units.prn" -o "$out"
echo "regenerated $out"
