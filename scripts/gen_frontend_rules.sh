#!/usr/bin/env bash
# gen_frontend_rules.sh -- emit the GFD-frontend VS0 rules from PARENA into this host's C.
#
#   PARENA/stdlib/shankpit/era_tech_rules.prn       --> packages/simulation/era_tech_rules.c
#   PARENA/stdlib/shankpit/mining_rules.prn         --> packages/simulation/mining_rules.c
#   PARENA/stdlib/shankpit/citizen_day_rules.prn    --> packages/simulation/citizen_day_rules.c
#   PARENA/stdlib/trapx/citizen_contact_rules.prn   --> packages/simulation/citizen_contact_rules.c
#
# One script for four modules on purpose: they are one slice (the 1700s start, what pays for it, who lives
# in it, and who will talk to you), they are built into one wasm module by
# ../PARENA/scripts/build_frontend_vs0_wasm.sh, and a fifth rule file joining the slice should be a line
# here, not a fourth near-identical script.
#
# Usage: scripts/gen_frontend_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"

if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    echo "gen_frontend_rules: no parena binary at $parena_dir (build it with: cd ../PARENA && make .parena-bootstrap)" >&2
    exit 1
fi

emit() {
    local src="$1" out="$2"
    "$parena_bin" build "$parena_dir/$src" -o "$root/packages/simulation/$out"
    echo "regenerated packages/simulation/$out"
}

emit stdlib/shankpit/era_tech_rules.prn     era_tech_rules.c
emit stdlib/shankpit/mining_rules.prn       mining_rules.c
emit stdlib/shankpit/citizen_day_rules.prn  citizen_day_rules.c
emit stdlib/trapx/citizen_contact_rules.prn citizen_contact_rules.c
