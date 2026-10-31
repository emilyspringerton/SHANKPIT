#!/usr/bin/env bash
# gen_widget_rules.sh -- regenerate packages/simulation/widget_rules.c from PARENA.
#
# Composable widget circuits (levers, plates, delays, transistors, motors) are decision logic, so they
# live in PARENA with the rest of the rules:
#   PARENA/stdlib/shankpit/widget_rules.prn
# packages/simulation/widget_circuit.h is the host that runs them and mirrors their values into EduVM
# registers. Usage: scripts/gen_widget_rules.sh   (needs ../PARENA, or PARENA_DIR)
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/widget_rules.prn" \
    -o "$root/packages/simulation/widget_rules.c"
echo "regenerated packages/simulation/widget_rules.c"
