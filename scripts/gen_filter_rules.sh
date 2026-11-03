#!/usr/bin/env bash
# gen_filter_rules.sh -- regenerate packages/render/filter_rules.c from PARENA.
#
# The PROGRAM FEED's look (presets: sharpen/blur kernel, levels, gamma, saturation, vignette, grain) is a
# RULES table, not renderer code: PARENA/stdlib/shankpit/filter_rules.prn. stream_filters.h owns the
# 256-byte LUT build and the pixel loop; this is the numbers.
#
# Usage: scripts/gen_filter_rules.sh [out.c]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
out="${1:-$root/packages/render/filter_rules.c}"
if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    (cd "$parena_dir" && make .parena-bootstrap >/dev/null)
    parena_bin="$parena_dir/.parena-bootstrap"
fi
"$parena_bin" build "$parena_dir/stdlib/shankpit/filter_rules.prn" -o "$out"
echo "regenerated $out"
