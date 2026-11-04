#!/usr/bin/env bash
# gen_sky_rules.sh -- regenerate packages/simulation/sky_rules.c from PARENA.
#
# The sky is a shared decision: day/night, what each weather does to the light, and how far you can see through
# fog. SHANKPIT compiles this to C; NOOBTOPIA emits the same file to Luau (scripts/gen_rules.sh). Editing a
# weather intensity here changes both worlds, and nowhere else may state one.
#
# Usage: scripts/gen_sky_rules.sh   (needs a sibling ../PARENA checkout with .parena-bootstrap, or PARENA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"

if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    echo "gen_sky_rules: no parena binary at $parena_dir (build it with: cd ../PARENA && make .parena-bootstrap)" >&2
    exit 1
fi

"$parena_bin" build "$parena_dir/stdlib/shankpit/sky_rules.prn" \
    -o "$root/packages/simulation/sky_rules.c"
echo "regenerated packages/simulation/sky_rules.c"
