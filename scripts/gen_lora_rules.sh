#!/usr/bin/env bash
# gen_lora_rules.sh -- regenerate packages/protocol/lora_rules.c from PARENA.
#
# The LoRa session's numbers (tick length per modem preset, grid size, hit chance, damage, xp) are a
# RULES table, not engine code: PARENA/stdlib/shankpit/lora_rules.prn. lora_wire.h owns the bytes; this
# is the arithmetic.
#
# Usage: scripts/gen_lora_rules.sh [out.c]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
out="${1:-$root/packages/protocol/lora_rules.c}"
if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    (cd "$parena_dir" && make .parena-bootstrap >/dev/null)
    parena_bin="$parena_dir/.parena-bootstrap"
fi
"$parena_bin" build "$parena_dir/stdlib/shankpit/lora_rules.prn" -o "$out"
echo "regenerated $out"
