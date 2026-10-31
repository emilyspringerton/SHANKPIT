#!/usr/bin/env bash
# gen_slide_rules.sh -- regenerate packages/simulation/slide_rules.c from PARENA.
#
# The slope-slide curve (how steep a face is, how much help a downhill heading earns, how much of the
# slide drag a slope keeps) is decision logic, so it lives in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/slide_rules.prn
# packages/common/physics.h integrates it; packages/simulation/slide_rules.h holds matching defaults.
# Usage: scripts/gen_slide_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/slide_rules.prn" \
    -o "$root/packages/simulation/slide_rules.c"
echo "regenerated packages/simulation/slide_rules.c"
