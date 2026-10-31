#!/usr/bin/env python3
"""import_gfd_levels.py -- bring GoblinFoxDragon's own maps into SHANKPIT's level format.

The founder: "including bringing in the GFD exclusive levels like OG office and the plateau", then "no
GFD has different levels ... via the menu". The GFD tree was cloned and read rather than guessed at:

  * TOWN is real 3D content and it IS a level: packages/world/town_map.c holds 17 named buildings
    (auction house, town hall, blacksmith ...) as x/z/w/d/h seeds at TOWN_WORLD_SCALE 3.0, plus a
    generated expansion table of blocks in packages/world/generated/town_expansion_blocks.inl. That is
    enough to emit a real SHANKPIT level: one solid box per building, a ground plane, materials and
    spawners. This script does exactly that.
  * MEADOW has no geometry to import. data/crystal_seed_meadow.json is a 44x44 goblin SIMULATION seed
    (tick, goblins with kinds and grid coordinates) -- there is no wall, floor or building in it, so
    there is nothing to convert. It is left where it is and documented as a data seed, not a level.
  * The MUD / Go state server is not a map at all; it is a separate unification phase (see
    docs/GFD_UNIFICATION.md), not something this converter should pretend to do.

Usage:
    python3 scripts/import_gfd_levels.py [--gfd ../GoblinFoxDragon] [--out var/maps]
Writes <out>/gfd_town.json and prints what it found.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

SCALE = 3.0  # TOWN_WORLD_SCALE in GFD's packages/world/town_map.c

CORE_RE = re.compile(
    r'\{\s*BLD_[A-Z_]+,\s*"([^"]*)",\s*(-?[\d.]+)f?,\s*(-?[\d.]+)f?,\s*(-?[\d.]+)f?,\s*(-?[\d.]+)f?,\s*(-?[\d.]+)f?'
)
EXPANSION_RE = re.compile(
    r'\{\s*BLD_MISC_COUNT,\s*"([^"]*)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f'
)


def parse_seeds(path: Path, pattern: re.Pattern[str]) -> list[dict]:
    if not path.exists():
        return []
    out = []
    for m in pattern.finditer(path.read_text(encoding="utf-8", errors="replace")):
        name, x, z, w, d, h = m.groups()
        out.append({"name": name, "x": float(x), "z": float(z),
                    "w": float(w), "d": float(d), "h": float(h)})
    return out


def build_level(buildings: list[dict], name: str) -> dict:
    walls = []
    for b in buildings:
        # A building seed is a footprint and a height; SHANKPIT's LevelBox.y is the box CENTRE.
        walls.append({
            "x": b["x"] * SCALE,
            "y": b["h"] * SCALE * 0.5,
            "z": b["z"] * SCALE,
            "sx": b["w"] * SCALE,
            "sy": b["h"] * SCALE,
            "sz": b["d"] * SCALE,
            "r": 0.42, "g": 0.40, "b": 0.38,
            "material": "concrete",
        })

    if not walls:
        raise SystemExit("no building seeds found -- is --gfd pointing at a GoblinFoxDragon checkout?")

    xs = [w["x"] for w in walls]
    zs = [w["z"] for w in walls]
    margin = 40.0
    min_x, max_x = min(xs) - margin, max(xs) + margin
    min_z, max_z = min(zs) - margin, max(zs) + margin

    # A perimeter wall, so a player cannot walk off the imported town into empty space.
    h = 18.0
    t = 4.0
    mid_x = (min_x + max_x) * 0.5
    mid_z = (min_z + max_z) * 0.5
    span_x = max_x - min_x
    span_z = max_z - min_z
    for (cx, cz, sx, sz) in (
        (mid_x, min_z, span_x, t),
        (mid_x, max_z, span_x, t),
        (min_x, mid_z, t, span_z),
        (max_x, mid_z, t, span_z),
    ):
        walls.append({"x": cx, "y": h * 0.5, "z": cz, "sx": sx, "sy": h, "sz": sz,
                      "r": 0.30, "g": 0.30, "b": 0.32, "material": "metal"})

    spawners = []
    for i, b in enumerate(buildings[:8]):
        spawners.append({"x": b["x"] * SCALE, "y": 0.0, "z": b["z"] * SCALE,
                         "yaw": float((i * 45) % 360), "team": -1, "id": i})

    return {
        "version": 1,
        "name": name,
        "width": span_x,
        "height": h,
        "depth": span_z,
        "ground_plane_enabled": True,
        "ground_plane_squares": 128,
        "enclosed": False,
        "materials": [
            {"name": "concrete", "shader_name": "standard", "specular": 0.10,
             "shininess": 18.0, "friction": 0.34, "density": 2.4},
            {"name": "metal", "shader_name": "standard", "specular": 0.55,
             "shininess": 90.0, "friction": 0.26, "density": 3.1},
        ],
        "walls": walls,
        "spawners": spawners,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--gfd", default="../GoblinFoxDragon")
    ap.add_argument("--out", default="var/maps")
    args = ap.parse_args()

    gfd = Path(args.gfd)
    out_dir = Path(args.out)
    if not (gfd / "packages/world/town_map.c").exists():
        raise SystemExit(f"not a GoblinFoxDragon checkout: {gfd}")

    core = parse_seeds(gfd / "packages/world/town_map.c", CORE_RE)
    expansion = parse_seeds(gfd / "packages/world/generated/town_expansion_blocks.inl", EXPANSION_RE)
    buildings = core + expansion
    level = build_level(buildings, "GFD TOWN")
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "gfd_town.json"
    out.write_text(json.dumps(level, indent=1), encoding="utf-8")

    meadow = gfd / "data/crystal_seed_meadow.json"
    print(f"gfd_town: {len(core)} named buildings + {len(expansion)} expansion blocks -> "
          f"{len(level['walls'])} boxes, {len(level['spawners'])} spawners -> {out}")
    if meadow.exists():
        seed = json.loads(meadow.read_text(encoding="utf-8"))
        print(f"meadow:   {seed.get('width')}x{seed.get('height')} goblin sim seed, "
              f"{len(seed.get('goblins', []))} goblins -- no geometry to import (not a level)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
