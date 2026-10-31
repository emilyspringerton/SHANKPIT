#!/usr/bin/env bash
# gen_wave_rules.sh -- regenerate packages/simulation/wave_rules.c from PARENA.
#
# When a survival wave arrives and how big it is are decision logic, not rendering:
#   PARENA/stdlib/shankpit/wave_rules.prn
# Usage: scripts/gen_wave_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/wave_rules.prn" \
    -o "$root/packages/simulation/wave_rules.c"
echo "regenerated packages/simulation/wave_rules.c"
