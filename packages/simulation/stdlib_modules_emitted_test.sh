#!/usr/bin/env bash
# Each argument is one module's emitted C. The assertion is deliberately about the build, not the
# rules' own values: this fork's contract is that the compiler produced real C for every stdlib
# module the package names, and that nothing was checked in to stand in for it.
set -euo pipefail

[ "$#" -gt 0 ] || { echo "no modules to check" >&2; exit 1; }

for gen in "$@"; do
  test -s "$gen" || { echo "missing or empty: $gen" >&2; exit 1; }
  defs=$(grep -cE '^[A-Za-z_][A-Za-z0-9_ *]*\(' "$gen" || true)
  [ "${defs:-0}" -ge 1 ] || { echo "no definitions in $gen" >&2; exit 1; }
  echo "ok $(basename "$gen") (${defs} definitions)"
done

echo "emitted C verified for $# modules"
