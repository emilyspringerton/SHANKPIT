#!/usr/bin/env python3
"""Generate packages/ui/wow_weapons.h from the engine's own WPN_* defines.

The macro layer must not keep a second copy of the weapon ids: /cast magnum has to mean the same
number the number keys mean. This reads packages/common/protocol.h, keeps the plain integer defines,
dedupes by value (the first name wins), and writes a self-contained header. --check regenerates to a
temp file and diffs, so a drift fails CI instead of shipping a wrong table.
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "packages/common/protocol.h"
OUT = ROOT / "packages/ui/wow_weapons.h"

def collect():
    text = SRC.read_text()
    seen = {}
    for m in re.finditer(r"^[ \t]*#define[ \t]+(WPN_[A-Z0-9_]+)[ \t]+([0-9]+)[ \t]*(?:/\*.*)?$", text, re.M):
        name, val = m.group(1), int(m.group(2))
        if val >= 32 or name.startswith("WPN_MAX"):
            continue
        if val not in seen:
            clean = name[2:].lower()
            if clean.startswith("n_"):
                clean = clean[2:]
            seen[val] = clean
    return seen

def render(seen):
    lines = []
    lines.append("/* GENERATED FILE -- do not edit. scripts/gen_wow_weapons.py from packages/common/protocol.h.")
    lines.append(" *")
    lines.append(" * The names are the engine's own WPN_* identifiers lowercased, so /cast magnum and the")
    lines.append(" * number key that selects the magnum cannot disagree. Numbers 1..6 are the number keys' own")
    lines.append(" * 0..5, 7/8/9 are flashlight/hammer/spraycan, exactly as apps/lobby binds them. */")
    lines.append("#ifndef SHANKPIT_UI_WOW_WEAPONS_H")
    lines.append("#define SHANKPIT_UI_WOW_WEAPONS_H")
    lines.append("")
    lines.append("#include <string.h>")
    lines.append("")
    lines.append("typedef struct { const char *name; int id; } WowWeapon;")
    lines.append("")
    lines.append("static const WowWeapon wow_weapons[] = {")
    for val in sorted(seen):
        lines.append('    { "%s", %d },' % (seen[val], val))
    lines.append("};")
    lines.append("")
    lines.append("#define WOW_WEAPON_COUNT ((int)(sizeof wow_weapons / sizeof wow_weapons[0]))")
    lines.append("")
    lines.append("static inline char wow_wpn_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }")
    lines.append("")
    lines.append("static inline int wow_weapon_from_name(const char *name) {")
    lines.append("    int i;")
    lines.append("    size_t n, j;")
    lines.append("    if (!name || !*name) return -1;")
    lines.append("    n = strlen(name);")
    lines.append("    for (i = 0; i < WOW_WEAPON_COUNT; i++) {")
    lines.append("        if (strlen(wow_weapons[i].name) != n) continue;")
    lines.append("        for (j = 0; j < n; j++)")
    lines.append("            if (wow_wpn_lower(name[j]) != wow_weapons[i].name[j]) break;")
    lines.append("        if (j == n) return wow_weapons[i].id;")
    lines.append("    }")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")
    lines.append("static inline const char *wow_weapon_name(int id) {")
    lines.append("    int i;")
    lines.append("    for (i = 0; i < WOW_WEAPON_COUNT; i++)")
    lines.append("        if (wow_weapons[i].id == id) return wow_weapons[i].name;")
    lines.append("    return \"?\";")
    lines.append("}")
    lines.append("")
    lines.append("/* The number keys' own mapping, mirrored: 1..6 -> 0..5, 7 flashlight, 8 hammer, 9 spraycan. */")
    lines.append("static inline int wow_weapon_from_slot(int slot) {")
    lines.append("    if (slot >= 1 && slot <= 6) return slot - 1;")
    for slot, ident in ((7, "FLASHLIGHT"), (8, "HAMMER"), (9, "SPRAYCAN")):
        match = [v for v, n in seen.items() if n == ident.lower()]
        if match:
            lines.append("    if (slot == %d) return %d;" % (slot, match[0]))
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")
    lines.append("#endif /* SHANKPIT_UI_WOW_WEAPONS_H */")
    return "\n".join(lines) + "\n"

def main():
    seen = collect()
    if len(seen) < 6:
        print("gen_wow_weapons: only %d weapons found -- refusing to write a table that lost the rest" % len(seen))
        return 1
    out = render(seen)
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != out:
            print("check-wow-weapons: packages/ui/wow_weapons.h is stale -- run scripts/gen_wow_weapons.py")
            return 1
        print("check-wow-weapons: up to date (%d weapons)" % len(seen))
        return 0
    OUT.write_text(out)
    print("wrote packages/ui/wow_weapons.h (%d weapons)" % len(seen))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
