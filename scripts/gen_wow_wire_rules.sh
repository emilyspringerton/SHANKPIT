#!/usr/bin/env bash
# gen_wow_wire_rules.sh -- regenerate packages/protocol/wow_wire_rules.c from PARENA.
#
# The macro/mod wire's numbers (target slots, cast queue depth, clause cap, ack and resend periods)
# are a RULES table, not engine code: PARENA/stdlib/shankpit/wow_wire.prn. wow_wire.h owns the bytes.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
out="${1:-$root/packages/protocol/wow_wire_rules.c}"
if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    (cd "$parena_dir" && make .parena-bootstrap >/dev/null)
    parena_bin="$parena_dir/.parena-bootstrap"
fi
"$parena_bin" build "$parena_dir/stdlib/shankpit/wow_wire.prn" -o "$out"
echo "regenerated $out"
