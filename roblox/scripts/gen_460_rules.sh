#!/usr/bin/env bash
# gen_460_rules.sh -- emit the 460 layer's rules from PARENA into Luau.
#
# The Roblox side is plumbing; its THRESHOLDS and its ticket arithmetic are not. Both are written once:
#
#   PARENA/stdlib/shankpit/queue_rules.prn  --> roblox/src/queue460_rules.lua
#   PARENA/stdlib/shankpit/ticket_rules.prn --> roblox/src/ticket460_rules.lua
#
# Same glue as NOOBTOPIA's scripts/gen_deadweight.sh (rating.prn / stage.prn): if the Luau and the .prn ever
# disagree, the .prn is right and the copy is the bug.
#
# Usage: roblox/scripts/gen_460_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
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

emit() {
    local prn="$1" out="$2"
    "$parena_bin" build "$parena_dir/stdlib/shankpit/$prn" -o "$root/roblox/src/$out"
    # The emitter writes nothing on a construct it cannot express (no partial file), so an empty output is the
    # real failure signal -- parena's own exit code is not enough, which this check makes explicit.
    if [ ! -s "$root/roblox/src/$out" ]; then
        echo "gen_460_rules: $out was not produced (an unsupported PRN form, not a silent pass)" >&2
        exit 1
    fi
}

emit queue_rules.prn  queue460_rules.lua
emit ticket_rules.prn ticket460_rules.lua
echo "regenerated roblox/src/{queue460_rules,ticket460_rules}.lua"
