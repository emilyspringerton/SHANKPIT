#!/usr/bin/env bash
# gen_ambient_lines.sh -- generate packages/simulation/ambient_lines.inc from NOOBTOPIA's data.
#
# NOOBTOPIA owns the WORDS (NOOBTOPIA/data/noob_lines.lua); PARENA owns the RULES
# (stdlib/noobtopia/ambient_life.prn); SHANKPIT owns the engine. So the line table is generated from the data file
# rather than hand-copied into C: edit a noob's line in NOOBTOPIA, re-run this, and the same line is what a SHANKPIT
# NPC says. A line that exists in one world and not the other becomes impossible.
#
# Deliberately sed, not a Lua parser: the data file's format is one `{cat = N, text = "..."},` per line, and a
# generator that needs a parser is a generator that will one day silently emit nothing.
#
# Usage: scripts/gen_ambient_lines.sh   (needs a sibling ../NOOBTOPIA checkout, or NOOBTOPIA_DIR)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="${NOOBTOPIA_DIR:-$root/../NOOBTOPIA}/data/noob_lines.lua"
out="$root/packages/simulation/ambient_lines.inc"

if [ ! -f "$src" ]; then
    echo "gen_ambient_lines: missing $src (clone NOOBTOPIA next to SHANKPIT, or set NOOBTOPIA_DIR)" >&2
    exit 1
fi

pairs="$(sed -n 's/^[[:space:]]*{cat = \([0-9][0-9]*\), text = "\([^"]*\)"}[,]\{0,1\}[[:space:]]*$/\1|\2/p' "$src")"
count="$(printf '%s\n' "$pairs" | grep -c '|' || true)"
if [ "${count:-0}" -lt 1 ]; then
    echo "gen_ambient_lines: $src contains no readable lines (expected '{cat = N, text = \"...\"},' per line)" >&2
    exit 1
fi

{
    echo "/* Generated from NOOBTOPIA/data/noob_lines.lua by scripts/gen_ambient_lines.sh -- do not edit by hand."
    echo " * The words live in NOOBTOPIA; the categories and timing live in PARENA/stdlib/noobtopia/ambient_life.prn. */"
    echo "static const AmbientLine g_ambient_lines[] = {"
    printf '%s\n' "$pairs" | awk -F'|' '{ printf "    { %s, \"%s\" },\n", $1, $2 }'
    echo "};"
    echo "static const int g_ambient_line_count = $count;"
} > "$out"

echo "regenerated packages/simulation/ambient_lines.inc ($count lines)"
