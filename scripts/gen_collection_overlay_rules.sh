#!/usr/bin/env bash
# gen_collection_overlay_rules.sh -- regenerate packages/collection/collection_overlay_rules.c from PARENA.
#
# The Collection overlay mod's decisions (which REFLUX rows are the Collection feed's, what each does, the colour
# and order of a self-found status, when the stream shows a banner, how many lines each surface shows, reload and
# banner timing) live in PARENA/stdlib/shankpit/collection_overlay_rules.prn;
# packages/collection/collection_overlay.c and collection_feed.c are the plumbing that calls them.
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/collection_overlay_rules.prn" \
    -o "$root/packages/collection/collection_overlay_rules.c"
echo "regenerated packages/collection/collection_overlay_rules.c"
