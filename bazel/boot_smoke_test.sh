#!/usr/bin/env bash
# The boot program's own gate: exit 0 AND the four decisions it reports. The exit code alone would pass
# a program that silently printed nothing, and the text alone would pass one that printed and then died.
set -uo pipefail

bin="${1:?usage: boot_smoke_test.sh <boot-binary>}"
out=$("$bin" 2>&1)
rc=$?

if [ "$rc" -ne 0 ]; then
  echo "boot exited $rc" >&2
  printf '%s\n' "$out" >&2
  exit 1
fi

for want in \
  "level: starter" \
  "level: geometry and spawner inside the arena" \
  "mode: zombies (MODE_ZOMBIE)" \
  "tick 1: shaped by zombie_brain_rules" \
  "loop: 16 ms fixed tick" \
  "render: 460 palette" \
  "sky: row 0 is the top band" \
  "sky: all 360 rows scanned" \
  "sim: a body leaves the spawner" \
  "horde: wave 1 from wave_rules" \
  "horde: its 3 bodies stand" \
  "horde: each body faces the arena" \
  "horde: the wave lands" \
  "horde: every body has hit points" \
  "horde: three AR rounds drop a shambler" \
  "horde: a shield soaks first" \
  "horde: combat goes through combat_api" \
  "boot plan:"; do
  printf '%s\n' "$out" | grep -Fq "$want" || { echo "missing line: $want" >&2; printf '%s\n' "$out" >&2; exit 1; }
done

echo "boot smoke ok (exit 0, 18 expected lines present)"
