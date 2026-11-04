#!/usr/bin/env bash
# gen_redgarden_rules.sh -- emit REDGARDEN's arena/ability/AI/session rules from PARENA into this host's C.
#
#   PARENA/stdlib/redgarden/arena_rules.prn    --> packages/simulation/redgarden_arena_rules.c
#   PARENA/stdlib/redgarden/ability_rules.prn  --> packages/simulation/redgarden_ability_rules.c
#   PARENA/stdlib/redgarden/ai_rules.prn       --> packages/simulation/redgarden_ai_rules.c
#   PARENA/stdlib/redgarden/net_rules.prn      --> packages/simulation/redgarden_net_rules.c
#
# REDGARDEN predates PARENA (C/SDL2, July 2026), so its multiplayer and its AI come into SHANKPIT as rule
# mods rather than as merged C: the numbers and the decisions were read out of
# REDGARDEN/packages/simulation/arena_game.{h,c} and apps/arena_server/src/main.c, and the C sim stays the
# reference implementation they are checked against (see packages/simulation/redgarden_rules_test.c, which
# asserts the constants REDGARDEN actually defines, not values re-derived from this host).
#
# Four builds, one script: a fifth redgarden rule file is a line here, not a fifth near-identical script.
#
# Usage: scripts/gen_redgarden_rules.sh   (needs a sibling ../PARENA checkout, or PARENA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"

if [ -x "$parena_dir/.parena-bootstrap" ]; then
    parena_bin="$parena_dir/.parena-bootstrap"
elif [ -x "$parena_dir/parena" ]; then
    parena_bin="$parena_dir/parena"
else
    echo "gen_redgarden_rules: no parena binary at $parena_dir (build it with: cd ../PARENA && make .parena-bootstrap)" >&2
    exit 1
fi

emit() {
    local src="$1" out="$2"
    "$parena_bin" build "$parena_dir/$src" -o "$root/packages/simulation/$out"
    echo "regenerated packages/simulation/$out"
}

emit stdlib/redgarden/arena_rules.prn   redgarden_arena_rules.c
emit stdlib/redgarden/ability_rules.prn redgarden_ability_rules.c
emit stdlib/redgarden/ai_rules.prn      redgarden_ai_rules.c
emit stdlib/redgarden/net_rules.prn     redgarden_net_rules.c
