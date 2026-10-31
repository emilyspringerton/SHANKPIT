#!/usr/bin/env bash
# gen_material_rules.sh -- regenerate packages/simulation/material_rules.c from PARENA.
#
# A material's animation (UV scroll, frame-loop sheets, palette cycling), Quake's light styles, the
# screen tints, and the voxel-probe axis lookup are all pure functions of the tick:
#   PARENA/stdlib/shankpit/material_rules.prn
# Usage: scripts/gen_material_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/material_rules.prn" \
    -o "$root/packages/simulation/material_rules.c"
echo "regenerated packages/simulation/material_rules.c"
