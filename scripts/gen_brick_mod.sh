#!/usr/bin/env bash
# gen_brick_mod.sh -- regenerate packages/simulation/brick_mod.c from PARENA.
#
# SHANKPIT's destructible brick (founder real-time, 2026-10-01: "get papercraft tech shipped to
# shankpit i want brick to be destructable") keeps its decision logic in PARENA, composed from
# PAPERCRAFT's real Paper Engine mods plus one SHANKPIT-specific module:
#   PARENA/stdlib/papercraft/paper_fragment_mod.prn   material resistance + HP tiers
#   PARENA/stdlib/papercraft/interact_falloff_mod.prn distance falloff
#   PARENA/stdlib/shankpit/brick_rules.prn            weapon-vs-brick damage, debris, brick HP
# Usage: scripts/gen_brick_mod.sh   (needs a sibling ../PARENA checkout with a built ./parena)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
[ -x "$parena_dir/parena" ] || (cd "$parena_dir" && make build >/dev/null)
"$parena_dir/parena" build \
  "$parena_dir/stdlib/papercraft/paper_fragment_mod.prn" \
  "$parena_dir/stdlib/papercraft/interact_falloff_mod.prn" \
  "$parena_dir/stdlib/shankpit/brick_rules.prn" \
  -o "$root/packages/simulation/brick_mod.c"
echo "regenerated packages/simulation/brick_mod.c"
