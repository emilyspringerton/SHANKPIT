#!/usr/bin/env bash
# gen_chat_channel_rules.sh -- regenerate packages/chat/chat_channel_rules.c from PARENA.
#
# WoW-style chat channels (SHANKPIT as the telepresence app): every decision -- chat kinds, wire ops, notices,
# channel numbering, name rules, who hears what, flood control, colours, sticky types, the REFLUX chat family --
# lives in PARENA/stdlib/shankpit/chat_channel_rules.prn; packages/chat/chat_client.c and chat_server.c are plumbing.
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/chat_channel_rules.prn" \
    -o "$root/packages/chat/chat_channel_rules.c"
echo "regenerated packages/chat/chat_channel_rules.c"
