#!/usr/bin/env bash
# gen_chat_bridge_rules.sh -- regenerate packages/chat/chat_bridge_rules.c from PARENA.
#
# The global chat bridge (every world shows every other world's public lines over IDUNA's chat bus): every decision --
# sources, who shows what, what goes out, tags, pacing, the bridge's REFLUX rows --
# lives in PARENA/stdlib/shankpit/chat_bridge_rules.prn; packages/chat/chat_bridge.c is plumbing.
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/chat_bridge_rules.prn" \
    -o "$root/packages/chat/chat_bridge_rules.c"
echo "regenerated packages/chat/chat_bridge_rules.c"
