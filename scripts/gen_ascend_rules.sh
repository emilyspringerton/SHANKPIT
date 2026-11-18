#!/usr/bin/env bash
# gen_ascend_rules.sh -- regenerate packages/collection/ascend_rules.c from PARENA.
#
# /ascend (your WoW guilds' Discords, from GuildHerald's feed): every decision -- which feed rows count, how many
# invites one /ascend join opens, what a valid invite code is, its REFLUX row -- lives in
# PARENA/stdlib/shankpit/ascend_rules.prn; packages/collection/ascend.c is plumbing.
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
"$parena_bin" build "$parena_dir/stdlib/shankpit/ascend_rules.prn" \
    -o "$root/packages/collection/ascend_rules.c"
echo "regenerated packages/collection/ascend_rules.c"
