#!/usr/bin/env bash
# gen_queue460.sh -- emit the 460 queue's rules from PARENA into Luau.
#
# The queue on the Roblox side is a list manager; its THRESHOLDS are arithmetic, and arithmetic is written once:
#
#   PARENA/stdlib/shankpit/queue_rules.prn --> roblox/src/queue460_rules.lua
#
# Same glue as NOOBTOPIA's scripts/gen_deadweight.sh (which emits rating.prn / stage.prn the same way): if this
# output and the .prn ever disagree, the .prn is right and the copy is the bug.
#
# Usage: roblox/scripts/gen_queue460.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"

if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    (cd "$parena_dir" && make .parena-bootstrap >/dev/null)
    parena_bin="$parena_dir/.parena-bootstrap"
fi

out="$root/roblox/src/queue460_rules.lua"
"$parena_bin" build "$parena_dir/stdlib/shankpit/queue_rules.prn" -o "$out"
# The emitter writes nothing on a construct it cannot express (no partial file), so an empty output is the real
# failure signal -- parena's own exit code is not enough, which this check makes explicit.
if [ ! -s "$out" ]; then
    echo "gen_queue460: $out was not produced (an unsupported PRN form, not a silent pass)" >&2
    exit 1
fi
echo "regenerated roblox/src/queue460_rules.lua"
