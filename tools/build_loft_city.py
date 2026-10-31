#!/usr/bin/env python3
"""build_loft_city.py -- the lofts, authored as recursive levels, flattened into the zombies city.

Founder: "take the primatives of the city that already build the city these are recursive levels ...
the lofts building is made up of floors that are actually 2 floors that are actually built out of 3
levels that are actually each 2 levels ish ... make variations ... throw the combinations together to
make like 8 unique lofts with different numbers of floors etc and add them to the default zombies map
... build them as levels just like the current lofts is built so we can edit the sub levels and the
refrerences all update ... make sure its all built to play nice with the new destructable environments
tech".

THE MODEL IS IDUNA'S, NOT A NEW ONE. A level owns `walls` and `objects`; an object is
{ref_level_id, x, y, z, rot_y} and composition is recursive (IDUNA/internal/shankpit/level_store.go's
flattenObjects). This script is the in-repo equivalent of that Export pass, because the native client
only ever loads ONE flat level (which is exactly why IDUNA flattens server-side):

    levels/lofts/*.json      the authored tree -- edit these, re-run this script
    levels/lofts/manifest.json   id -> file (IDUNA ids, so the same tree can be imported there)
    levels/lofts/placements.json where the eight buildings stand in the city (frozen once chosen)
    var/zombie/nextown_zombies.json   the playable city export (flat, ids renumbered)

Composition rules copied from IDUNA deliberately, including the ugly parts: RotY is 0/90/180/270 only,
rotation swaps sx/sz, y and sy are never rotated, materials and names are carried through (a composed
wall keeping its material is what keeps destructible brick working), and a cycle is an error rather
than a hang.

Destructibility: walls are authored with `brick`, `glass`, `wood`, `concrete` and `metal`. brick_fracture
turns brick/concrete/wood/glass boxes into carveable parents; metal frames, floors and lights stay solid
(the same `brick_world_box_is_slab` rule the engine applies). Thin partitions are therefore the carvable
ones by construction.

Usage:
    python3 tools/build_loft_city.py            # author (if missing) + flatten into the city
    python3 tools/build_loft_city.py --check    # verify only: refs resolve, materials valid, idempotent
    python3 tools/build_loft_city.py --author   # rewrite the authored tree from the blueprint
"""
import argparse
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOFT_DIR = os.path.join(ROOT, "levels", "lofts")
CITY = os.path.join(ROOT, "var", "zombie", "nextown_zombies.json")
MANIFEST = os.path.join(LOFT_DIR, "manifest.json")
PLACEMENTS = os.path.join(LOFT_DIR, "placements.json")

# ids 201.. are the loft tree; IDUNA's own next ids are far below this, and the manifest is explicit.
ID_BAY_GLASS = 201
ID_BAY_CORE = 202
ID_BAY_OPEN = 203
ID_FLOOR = 204
ID_PAIR = 205
ID_LOBBY = 206
ID_ROOF = 207
VARIANT_IDS = {name: 211 + i for i, name in enumerate(
    ["loft_a", "loft_b", "loft_c", "loft_d", "loft_e", "loft_f", "loft_g", "loft_h"])}

# Interior detail pass: the shells are shared, the rooms are not. One props module per theme, placed
# on every storey of the building that owns it -- so editing loft_props_cafe restyles only the cafe,
# and the eight lofts read as eight different buildings from the inside. Ids 220.. keep clear of the
# bay/floor/pair modules (201..207) and the variants (211..218) above.
THEMES = ["office", "cafe", "lounge", "workshop", "lab", "shop", "clinic", "warehouse"]
ID_PROPS = {name: 220 + i for i, name in enumerate(THEMES)}

# Every theme is authored inside ONE 20x20 bay (x,z -10..10, deck top at y=1) so the same module can
# be dropped into the glass bay of any storey without ever touching the stair well, which lives in the
# core bay (x -4..4, z -10..2) and is not where any of these coordinates reach. Materials stay inside
# the carvable set (wood/brick/glass) or the fixtures (metal/hps/ips) so props carve the same way the
# shell does.
PROPS_WALLS = {
    "office": [
        (-4.0, 2.2, -5.0, 5.0, 2.4, 2.4, "wood"),          # desk
        (2.0, 2.2, -5.0, 5.0, 2.4, 2.4, "wood"),            # desk
        (8.3, 3.5, 3.0, 1.4, 5.0, 8.0, "metal"),             # filing run
        (0.0, 3.0, 6.5, 10.0, 4.0, 0.4, "brick"),            # partition
        (-6.0, 7.4, 0.0, 2.0, 0.4, 2.0, "hps_light"),
    ],
    "cafe": [
        (0.0, 2.2, -7.0, 14.0, 2.4, 2.0, "wood"),           # counter
        (-6.0, 3.8, -7.0, 2.6, 2.0, 2.0, "metal"),           # espresso machine
        (3.0, 1.6, 2.0, 2.4, 1.4, 2.4, "wood"),              # table
        (-3.0, 1.6, 2.0, 2.4, 1.4, 2.4, "wood"),             # table
        (0.0, 4.6, 9.4, 6.0, 2.0, 0.3, "ips_light"),         # menu board
    ],
    "lounge": [
        (-3.5, 2.0, 4.0, 6.0, 2.0, 2.6, "wood"),             # couch
        (4.0, 2.0, 4.0, 6.0, 2.0, 2.6, "wood"),              # couch
        (0.0, 1.4, 0.5, 5.0, 1.4, 3.0, "wood"),              # coffee table
        (9.0, 3.5, -6.0, 0.6, 6.0, 0.6, "metal"),            # floor lamp
        (-9.0, 3.5, -6.0, 0.6, 6.0, 0.6, "metal"),
    ],
    "workshop": [
        (0.0, 2.6, -5.0, 12.0, 3.0, 2.6, "wood"),            # workbench
        (8.3, 3.5, 1.0, 1.4, 5.0, 9.0, "metal"),             # tool rack
        (-6.0, 2.7, 5.5, 2.6, 2.6, 2.6, "wood"),             # crates
        (-6.0, 2.7, 8.5, 2.6, 2.6, 2.6, "wood"),
        (0.0, 7.4, 3.0, 2.0, 0.4, 2.0, "hps_light"),
    ],
    "lab": [
        (-2.0, 2.6, -4.5, 6.0, 3.0, 2.4, "wood"),            # bench
        (5.0, 2.6, -4.5, 6.0, 3.0, 2.4, "wood"),             # bench
        (8.0, 4.0, 5.0, 3.0, 6.0, 3.0, "metal"),             # fume hood
        (-6.0, 3.0, 6.0, 3.0, 4.0, 3.0, "glass"),            # sample tank
        (0.0, 7.4, 0.0, 2.0, 0.4, 2.0, "ips_light"),
    ],
    "shop": [
        (8.3, 3.5, -2.0, 1.4, 5.0, 12.0, "wood"),            # shelving run
        (-8.3, 3.5, -2.0, 1.4, 5.0, 12.0, "wood"),           # shelving run
        (0.0, 2.2, 7.0, 8.0, 2.4, 2.0, "wood"),              # till counter
        (0.0, 5.0, -9.5, 10.0, 2.0, 0.3, "ips_light"),       # window sign
        (4.0, 1.6, 2.0, 2.4, 1.4, 2.4, "wood"),
    ],
    "clinic": [
        (-3.0, 1.7, -3.0, 6.0, 1.4, 2.6, "wood"),            # bed
        (4.0, 1.7, -3.0, 6.0, 1.4, 2.6, "wood"),             # bed
        (8.3, 3.0, 4.0, 1.4, 6.0, 6.0, "metal"),             # cabinet
        (0.0, 4.0, 6.5, 8.0, 8.0, 0.3, "glass"),             # screen
        (0.0, 7.4, 0.0, 2.0, 0.4, 2.0, "ips_light"),
    ],
    "warehouse": [
        (-6.0, 2.2, -6.0, 5.0, 2.4, 5.0, "wood"),            # pallet
        (1.0, 2.2, -6.0, 5.0, 2.4, 5.0, "wood"),             # pallet
        (-2.5, 5.0, -6.0, 5.0, 2.4, 5.0, "wood"),            # stacked
        (8.3, 4.5, 4.0, 1.4, 7.0, 10.0, "metal"),            # racking
        (-7.0, 1.6, 6.5, 5.0, 1.4, 5.0, "wood"),
    ],
}

# One bay is 20 x 16 footprint, 8 units storey height. A "floor" is three bays (60 x 16), a "pair" is
# two floors (the founder's "floors that are actually 2 floors"), a building is 2..5 pairs plus lobby/roof.
BAY_W, BAY_D, STOREY = 20.0, 20.0, 8.0

# How big a loft is IN THE CITY, relative to how it is authored. The authored tree stays at human
# authoring scale (a 20-unit bay is easy to reason about and to edit); the flatten scales it on the way
# out, so one constant resizes every building without touching a single sub-level.
#
# Founder, on the first export: "the new lofts are tiny ... scale up by double using phi". Phi twice is
# phi^2 = 2.618, i.e. a bit more than double, and it is the scale applied here: a pair goes from 60x20 to
# 157x52 and one storey from 8 to 21 units, which is what the city's own slabs are built at.
PHI = (1.0 + 5.0 ** 0.5) / 2.0
LOFT_SCALE = PHI * PHI
R, G, B, FRICTION = 0.6, 0.6, 0.65, 0.8

# The city's own material table already has brick/concrete/hps_light/ips_light/metal/wood. Glass is the
# one material the lofts need that nextown does not carry yet (lofts_1 uses it for every facade).
# Exactly NOCK's own glass material, as every existing baked level carries it (see
# baked_levels_gen.h's lofts_1 export) -- same shader_name, so the client's glass pass draws these panes
# the same way it draws the original lofts' facades.
GLASS_MATERIAL = {"name": "glass", "shader_name": "glass", "specular": 0.05, "shininess": 8,
                  "friction": 0.3, "texture_url": "/admin/nock/api/textures/14/image"}


def wall(x, y, z, sx, sy, sz, material=None, name=None, ramp=False):
    w = {"x": round(x, 3), "y": round(y, 3), "z": round(z, 3),
         "sx": round(sx, 3), "sy": round(sy, 3), "sz": round(sz, 3),
         "r": R, "g": G, "b": B, "friction": FRICTION}
    if material:
        w["material"] = material
    if ramp:
        w["ramp"] = True
    if name:
        w["name"] = name
    return w


def obj(ref, x, y, z, rot_y=0):
    return {"ref_level_id": ref, "x": round(x, 3), "y": round(y, 3), "z": round(z, 3), "rot_y": rot_y}


def level(name, lid, width, height, depth, walls, objects=None):
    return {"version": 1, "id": lid, "name": name,
            "width": width, "height": height, "depth": depth,
            "ground_plane_enabled": False, "ground_plane_squares": 0,
            "walls": walls, "objects": objects or []}


# ---- the primitives (depth 1) ----------------------------------------------------------------
def bay_glass():
    """A two-storey glass bay: wood deck, concrete ceiling, glass on both long faces, brick stubs at the
    ends, and two metal columns. The glass is the carvable front; the columns are the frame."""
    walls = [
        wall(0, 0.5, 0, BAY_W, 1.0, BAY_D, "wood"),                 # floor deck (slab: solid)
        wall(0, STOREY - 0.5, 0, BAY_W, 1.0, BAY_D, "concrete"),    # ceiling
        wall(0, STOREY / 2.0, BAY_D / 2.0 - 0.15, BAY_W, STOREY - 1.0, 0.3, "glass"),
        wall(0, STOREY / 2.0, -BAY_D / 2.0 + 0.15, BAY_W, STOREY - 1.0, 0.3, "glass"),
        wall(BAY_W / 2.0 - 0.3, STOREY / 2.0, 0, 0.6, STOREY - 1.0, BAY_D - 1.0, "brick"),
        wall(-BAY_W / 2.0 + 0.3, STOREY / 2.0, 0, 0.6, STOREY - 1.0, BAY_D - 1.0, "brick"),
        wall(BAY_W / 2.0 - 1.0, STOREY / 2.0, BAY_D / 2.0 - 1.0, 1.0, STOREY, 1.0, "metal"),
        wall(-BAY_W / 2.0 + 1.0, STOREY / 2.0, BAY_D / 2.0 - 1.0, 1.0, STOREY, 1.0, "metal"),
        wall(-6.0, 7.4, 0.0, 2.0, 0.4, 2.0, "hps_light"),       # ceiling fixture (the furniture is themed)
    ]
    return level("loft_bay_glass", ID_BAY_GLASS, BAY_W, STOREY, BAY_D, walls)


def bay_core():
    """The stair core -- and the only reason the upper floors are reachable on foot.

    A real stair well is cut through BOTH the deck and the ceiling (each is three pieces around an
    8 x 12 opening on the bay's -z half), and a NOCK ramp box climbs through it from this floor's
    deck top (+1) to the next floor's deck top (+9). Because every storey instantiates the same bay,
    the openings line up all the way up the building, so a building is a stairwell with floors on it
    rather than a stack of closed slabs. The ramp is `ramp: true`, which the client draws and collides
    as a wedge (`obb.h`: the slope rises toward local +z, solid below it).
    """
    walls = [
        # deck, three pieces around the well (x -4..4, z -10..2 is open)
        wall(-7.0, 0.5, 0.0, 6.0, 1.0, BAY_D, "concrete"),
        wall(7.0, 0.5, 0.0, 6.0, 1.0, BAY_D, "concrete"),
        wall(0.0, 0.5, 6.0, 8.0, 1.0, 8.0, "concrete"),
        # ceiling, the same three pieces, so the flight continues past this storey
        wall(-7.0, STOREY - 0.5, 0.0, 6.0, 1.0, BAY_D, "concrete"),
        wall(7.0, STOREY - 0.5, 0.0, 6.0, 1.0, BAY_D, "concrete"),
        wall(0.0, STOREY - 0.5, 6.0, 8.0, 1.0, 8.0, "concrete"),
        # the flight: 8 units of rise over 12 of run, deck top (+1) to next deck top (+9)
        wall(0.0, 5.0, -4.0, 8.0, 8.0, 12.0, "concrete", ramp=True),
        # enclosing sides (the neighbouring bays own the ends)
        wall(-BAY_W / 2.0 + 0.3, STOREY / 2.0, 0.0, 0.6, STOREY - 1.0, BAY_D, "concrete"),
        wall(BAY_W / 2.0 - 0.3, STOREY / 2.0, 0.0, 0.6, STOREY - 1.0, BAY_D, "brick"),
        wall(-7.0, 6.0, 0.0, 3.0, 2.0, 3.0, "hps_light"),
        # ---- interior: the clutter of a stair core, kept clear of the well (x -4..4, z -10..2) ----
        wall(-7.5, 2.7, -5.0, 2.6, 2.6, 2.6, "wood"),       # crate stack
        wall(-7.5, 2.7, -1.8, 2.6, 2.6, 2.6, "wood"),
    ]
    return level("loft_bay_core", ID_BAY_CORE, BAY_W, STOREY, BAY_D, walls)


def bay_open():
    """Open plan: a wood deck, a half-height brick partition, a light and a glass strip."""
    walls = [
        wall(0, 0.5, 0, BAY_W, 1.0, BAY_D, "wood"),
        wall(0, 2.0, 0, BAY_W - 2.0, 3.0, 0.5, "brick"),            # waist-height infill
        wall(BAY_W / 2.0 - 0.2, STOREY / 2.0, 0, 0.4, STOREY - 1.0, BAY_D, "glass"),
        wall(0, STOREY - 0.4, 0, BAY_W - 2.0, 0.4, BAY_D - 2.0, "ips_light"),
        wall(-BAY_W / 2.0 + 1.0, STOREY / 2.0, BAY_D / 2.0 - 1.0, 0.8, STOREY, 0.8, "metal"),
        # (furniture lives in this building's own loft_props_<theme> module, not in the shared bay)
    ]
    return level("loft_bay_open", ID_BAY_OPEN, BAY_W, STOREY, BAY_D, walls)


# ---- the composition (depth 2..4) ------------------------------------------------------------
# Loot per theme, in chests.h's own tiers (0 wooden / 1 reinforced / 2 rare). A lab is worth breaking
# into, a cafe is not -- and because the tiers belong to the theme, two buildings carry different loot
# instead of the same 0,1,2 rotation repeated eight times.
# The authored crowd cannot exceed the SIMULTANEOUS slots, and those are not
# LEVEL_BOXES_MAX_CHARACTERS: an NPC occupies a player slot, and protocol.h's MAX_CLIENTS is 70 with
# slot 0 reserved for the player. So 69 is the real ceiling for every character in one level file,
# including the ones a level already authored before the lofts -- which is why the population below is
# allocated against a BUDGET rather than one-per-floor-and-hope. Kept in sync with protocol.h by hand;
# packages/simulation/loft_city_test.c asserts the file stays inside it and that every one spawns.
MAX_AUTHORED_CHARACTERS = 69

THEME_LOOT = {
    "office": (0, 1, 1),
    "cafe": (0, 0, 1),
    "lounge": (1, 1, 2),
    "workshop": (0, 1, 2),
    "lab": (2, 2, 1),
    "shop": (0, 1, 1),
    "clinic": (1, 2, 2),
    "warehouse": (0, 0, 1),
}


def props_module(theme):
    """One building's interior: a 20x20 bay's worth of furniture, dropped on every storey of it."""
    return level("loft_props_" + theme, ID_PROPS[theme], BAY_W, STOREY, BAY_D,
                 [wall(*t) for t in PROPS_WALLS[theme]])


def floor_unit():
    """One floor plate: glass bay + core + open bay, three objects side by side."""
    objects = [
        obj(ID_BAY_GLASS, -BAY_W, 0, 0),
        obj(ID_BAY_CORE, 0, 0, 0),
        obj(ID_BAY_OPEN, BAY_W, 0, 0),
    ]
    # a thin edge beam so the plate reads as one building, not three rooms
    walls = [wall(0, 1.0, BAY_D / 2.0, BAY_W * 3.0, 1.0, 0.4, "metal"),
             wall(0, 1.0, -BAY_D / 2.0, BAY_W * 3.0, 1.0, 0.4, "metal")]
    return level("loft_floor", ID_FLOOR, BAY_W * 3, STOREY, BAY_D, walls, objects)


def pair_unit():
    """The founder's 'floors that are actually 2 floors': two floor plates, one above the other."""
    objects = [obj(ID_FLOOR, 0, 0, 0), obj(ID_FLOOR, 0, STOREY, 0)]
    return level("loft_pair", ID_PAIR, BAY_W * 3, STOREY * 2, BAY_D, [], objects)


def lobby():
    """The street level: glass storefront, metal portal frame, concrete deck."""
    walls = [
        wall(0, 0.5, 0, BAY_W * 3, 1.0, BAY_D, "concrete"),
        wall(0, STOREY / 2.0, BAY_D / 2.0 - 0.15, BAY_W * 3 - 4.0, STOREY - 2.0, 0.3, "glass"),
        wall(0, STOREY / 2.0, -BAY_D / 2.0 + 0.3, BAY_W * 3 - 4.0, STOREY - 2.0, 0.6, "brick"),
        wall(BAY_W * 1.5 - 1.0, STOREY / 2.0, 0, 1.0, STOREY, 1.0, "metal"),
        wall(-BAY_W * 1.5 + 1.0, STOREY / 2.0, 0, 1.0, STOREY, 1.0, "metal"),
        wall(0, STOREY - 0.4, 0, BAY_W * 3, 0.8, BAY_D, "concrete"),
    ]
    return level("loft_lobby", ID_LOBBY, BAY_W * 3, STOREY, BAY_D, walls)


def roof():
    """Parapet roof with a metal edge and a light well."""
    walls = [
        wall(0, 0.5, 0, BAY_W * 3, 1.0, BAY_D, "concrete"),
        wall(0, 2.5, BAY_D / 2.0 - 0.3, BAY_W * 3, 3.0, 0.6, "brick"),
        wall(0, 2.5, -BAY_D / 2.0 + 0.3, BAY_W * 3, 3.0, 0.6, "brick"),
        wall(0, 2.0, 0, 8.0, 2.0, 6.0, "hps_light"),
    ]
    return level("loft_roof", ID_ROOF, BAY_W * 3, 6.0, BAY_D, walls)


# ---- the eight buildings ---------------------------------------------------------------------
# Each variant is just a different COMBINATION of the same primitives (the founder's "throw the
# combinations together"): different pair counts, some with a lobby, some with an exposed roof, one
# with a stepped-back tower. Editing any module changes every building that references it, because
# nothing here duplicates module geometry -- only references it.
VARIANTS = {
    "loft_a": {"lobby": True, "pairs": 2, "roof": True, "step": 0, "theme": "office"},
    "loft_b": {"lobby": True, "pairs": 3, "roof": True, "step": 0, "theme": "cafe"},
    "loft_c": {"lobby": False, "pairs": 2, "roof": True, "step": 1, "theme": "lounge"},
    "loft_d": {"lobby": True, "pairs": 4, "roof": False, "step": 0, "theme": "workshop"},
    "loft_e": {"lobby": False, "pairs": 3, "roof": False, "step": 0, "theme": "lab"},
    "loft_f": {"lobby": True, "pairs": 5, "roof": True, "step": 1, "theme": "shop"},
    "loft_g": {"lobby": False, "pairs": 2, "roof": False, "step": 2, "theme": "clinic"},
    "loft_h": {"lobby": True, "pairs": 3, "roof": True, "step": 2, "theme": "warehouse"},
}


def variant(name, spec):
    objects = []
    y = 0.0
    props = ID_PROPS[spec["theme"]]
    if spec["lobby"]:
        objects.append(obj(ID_LOBBY, 0, y, 0))
        # the ground floor gets the theme too: a cafe opens onto the street, a workshop onto its yard
        objects.append(obj(props, -BAY_W, y, 0))
        y += STOREY
    for i in range(spec["pairs"]):
        # a step-back every `step` pairs: the upper stack shifts and loses a bay, which is what makes
        # these read as eight different buildings rather than eight heights of one.
        setback = spec["step"] > 0 and i >= spec["step"]
        stack_x = BAY_W if setback else 0
        objects.append(obj(ID_PAIR, stack_x, y, 0))
        # this building's interior on BOTH floors of the pair, in the glass bay (the core bay holds
        # the stair well, so nothing is placed there)
        objects.append(obj(props, stack_x - BAY_W, y, 0))
        objects.append(obj(props, stack_x - BAY_W, y + STOREY, 0))
        y += STOREY * 2
    if spec["roof"]:
        objects.append(obj(ID_ROOF, BAY_W if spec["step"] > 0 else 0, y, 0))
    return level(name, VARIANT_IDS[name], BAY_W * 3 + BAY_W, y + 6.0, BAY_D, [], objects)


MODULES = [bay_glass(), bay_core(), bay_open(), floor_unit(), pair_unit(), lobby(), roof()]
MODULES += [props_module(t) for t in THEMES]
AUTHORED = {m["name"]: m for m in MODULES}
AUTHORED.update({n: variant(n, s) for n, s in VARIANTS.items()})


def rotate_y90(x, z, sx, sz, rot):
    """IDUNA's own rotation: Y only, 90-degree steps, extents swap. Copied, not reinvented."""
    rot %= 360
    if rot == 90:
        return -z, x, sz, sx
    if rot == 180:
        return -x, -z, sx, sz
    if rot == 270:
        return z, -x, sz, sx
    return x, z, sx, sz


def transform_wall(w, dx, dy, dz, rot):
    """Rotate + translate one authored wall into world space.

    A ramp is a wedge in its OWN local frame, so it cannot be rotated by swapping extents the way a
    cube can -- swapping sx/sz would leave it still rising toward +z and produce a stair pointing the
    wrong way. A rotated ramp therefore keeps its extents and gets a per-box `rot_y`, which the engine
    already handles (obb.h) and which brick_fracture already keeps solid. Everything else is
    pre-rotated into a plain AABB, which is what keeps it carveable.
    """
    if w.get("ramp"):
        rx, rz, _, _ = rotate_y90(w["x"], w["z"], 0.0, 0.0, rot)
        nw = dict(w)
        nw.update({"x": dx + rx, "y": dy + w["y"], "z": dz + rz})
        if rot % 360:
            nw["rot_y"] = rot % 360
        return nw
    rx, rz, rsx, rsz = rotate_y90(w["x"], w["z"], w["sx"], w["sz"], rot)
    nw = dict(w)
    nw.update({"x": dx + rx, "y": dy + w["y"], "z": dz + rz, "sx": rsx, "sz": rsz})
    return nw


def load_tree():
    manifest = json.load(open(MANIFEST))
    levels = {}
    for ent in manifest["levels"]:
        path = os.path.join(LOFT_DIR, ent["file"])
        lv = json.load(open(path))
        if lv.get("id") != ent["id"]:
            raise SystemExit("%s: id %r does not match manifest %r" % (path, lv.get("id"), ent["id"]))
        levels[lv["id"]] = lv
    return levels


def flatten(levels, objects, origin_x, origin_y, origin_z, origin_rot, visited, depth, out,
            scale=None):
    """IDUNA flattenObjects, in Python: recursive, cycle-guarded, finite depth, Y-only rotation.

    `scale` is LOFT_SCALE by default and applies to AUTHORED distances only -- each clip's extents and each
    object's offset from its parent. The root origin (a building's place in the city) is passed in world
    units and is deliberately NOT scaled: scaling it too is what put the first scaled export 2.6x further
    from the spawn than the spot it was tested against.
    """
    if scale is None:
        scale = LOFT_SCALE
    if depth > 12:
        raise SystemExit("loft object nesting too deep (max 12)")
    for o in objects:
        ref = o["ref_level_id"]
        if ref in visited:
            raise SystemExit("level object cycle involving level %d" % ref)
        child = levels.get(ref)
        if child is None:
            raise SystemExit("object references unknown level %d (not in the manifest)" % ref)
        ox, oz, _, _ = rotate_y90(o["x"] * scale, o["z"] * scale, 0, 0, origin_rot)
        wx, wz = origin_x + ox, origin_z + oz
        wy = origin_y + o["y"] * scale
        wrot = (origin_rot + int(o.get("rot_y", 0))) % 360
        for w in child.get("walls", []):
            aw = dict(w)
            for k in ("x", "y", "z", "sx", "sy", "sz"):
                aw[k] = round(w[k] * scale, 3)
            out.append(transform_wall(aw, wx, wy, wz, wrot))
        flatten(levels, child.get("objects", []), wx, wy, wz, wrot,
                visited | {ref}, depth + 1, out, scale)
    return out


def footprint(walls):
    xs = [w["x"] - w["sx"] / 2 for w in walls] + [w["x"] + w["sx"] / 2 for w in walls]
    zs = [w["z"] - w["sz"] / 2 for w in walls] + [w["z"] + w["sz"] / 2 for w in walls]
    return min(xs), min(zs), max(xs), max(zs)


def overlaps(a, b):
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])


def choose_placements(city, levels):
    """Pick eight well-spread, genuinely empty spots. Deliberately simple and deterministic: scan a
    grid, reject anything that touches existing geometry in the playable height band, then keep the
    eight that are furthest apart (a greedy farthest-point pass). The bridge deck and the vehicle row
    on it are just walls here, so they are rejected the same way."""
    blocked = []
    for w in city["walls"]:
        top, bottom = w["y"] + w["sy"] / 2, w["y"] - w["sy"] / 2
        # The SAME band placement_conflicts verifies against (>1.0 .. <70). The old 2.0 floor made the
        # chooser free to stand a building on a low kerb/slab that the verifier then (rightly) refused --
        # harmless while buildings were 60 wide and there were plenty of spots, fatal at 157.
        if top <= 1.0 or bottom >= 70.0:
            continue  # ground plane, or a tower's upper floors: not in the way of a street building
        blocked.append((w["x"] - w["sx"] / 2 - 6, w["z"] - w["sz"] / 2 - 6,
                        w["x"] + w["sx"] / 2 + 6, w["z"] + w["sz"] / 2 + 6))
    spawn = city["spawners"][0]
    blocked.append((spawn["x"] - 30, spawn["z"] - 30, spawn["x"] + 30, spawn["z"] + 30))

    def placed_footprint(name, cx, cz, rot):
        """The plan the export will ACTUALLY occupy if this building is dropped at (cx,cz) at `rot`.

        Not extents-at-the-origin: a loft's authored geometry is centred on zero, so its footprint starts
        at a negative corner and the anchor is nothing like its minimum. Sizing candidates by extents
        therefore tested a box up to a building's own width away from where the walls landed -- invisible
        at 60 wide with a 220-unit gap, immediate at 157 with the gap removed, and the reason the export
        failed its own overlap check after the chooser had accepted the spot.
        """
        lid = VARIANT_IDS[name]
        return footprint(flatten(levels, levels[lid]["objects"], cx, 0.0, cz, rot, {lid}, 1, []))

    # 40-unit candidates, not 100: a phi^2 building is 157x52, and on the old coarse grid whole blocks of
    # genuinely empty street fell between the candidate points ("no room near the street grid for loft_b").
    candidates = [(gx, gz) for gx in range(-1600, 2400, 40) for gz in range(-1400, 2000, 40)]
    candidates.sort(key=lambda p: math.hypot(p[0] + 30 - spawn["x"], p[1] + 10 - spawn["z"]))

    placements = []
    rots = [0, 90, 180, 270]
    for i, name in enumerate(VARIANTS):
        chosen = None
        # nearest-first, and a different rotation per building so the street is not a row of clones
        for rot in [rots[i % 4], rots[(i + 1) % 4], rots[(i + 2) % 4], rots[(i + 3) % 4]]:
            for c in candidates:
                fp = placed_footprint(name, c[0], c[1], rot)
                box = (fp[0] - 6.0, fp[1] - 6.0, fp[2] + 6.0, fp[3] + 6.0)
                if any(overlaps(box, b) for b in blocked):
                    continue
                bad = False
                for other in placements:
                    # Not overlapping is the requirement; the old 220-unit ANCHOR gap stopped meaning
                    # "the street breathes" once a building was 157 wide, and became a reason to refuse
                    # a spot that had room. Keep a 40-unit margin so they are neighbours, not one block.
                    if overlaps(box, other["_box"]) or math.hypot(c[0] - other["x"], c[1] - other["z"]) < 40:
                        bad = True
                        break
                if bad:
                    continue
                chosen = (c, rot, box)
                break
            if chosen:
                break
        if chosen is None:
            raise SystemExit("no room near the street grid for %s" % name)
        placements.append({"level": name, "x": chosen[0][0], "y": 0.0, "z": chosen[0][1],
                           "rot_y": chosen[1], "_box": chosen[2]})
    for p in placements:
        del p["_box"]
    return placements


def placement_conflicts(kept, placements, levels):
    """Verify the export: no loft overlaps another loft or any pre-existing wall in the playable band.
    Uses the ROTATED footprint, which is what actually lands in the level."""
    boxes = [(w["x"] - w["sx"] / 2, w["z"] - w["sz"] / 2, w["x"] + w["sx"] / 2, w["z"] + w["sz"] / 2)
             for w in kept if w["y"] + w["sy"] / 2 > 1.0 and w["y"] - w["sy"] / 2 < 70.0]
    for p in placements:
        lid = VARIANT_IDS[p["level"]]
        walls = flatten(levels, levels[lid]["objects"], p["x"], p["y"], p["z"], int(p["rot_y"]),
                        {lid}, 1, [])
        fp = footprint(walls)
        for b in boxes:
            if overlaps(fp, b):
                raise SystemExit("%s at (%.0f,%.0f) overlaps existing geometry" % (p["level"], p["x"], p["z"]))
        boxes.append(fp)


def write_authored(force):
    os.makedirs(LOFT_DIR, exist_ok=True)
    written = 0
    for name, lv in AUTHORED.items():
        path = os.path.join(LOFT_DIR, name + ".json")
        if os.path.exists(path) and not force:
            continue
        with open(path, "w") as f:
            json.dump(lv, f, indent=1)
            f.write("\n")
        written += 1
    manifest = {"levels": [{"id": AUTHORED[n]["id"], "file": n + ".json"} for n in AUTHORED]}
    with open(MANIFEST, "w") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    return written


def build(city_path=CITY, placements_path=PLACEMENTS, write=True):
    levels = load_tree()
    city = json.load(open(city_path))

    # idempotent: drop the previous export before contributing the current one. Two markers, because a
    # LevelBox has no name field at all -- if a player re-saves the city from EDIT MAP (F6) the
    # `name` tags are gone, so the exact geometry this tool wrote is recorded in `loft_city` and used
    # as the fallback. Without that, a rebuild would stack a second copy of all eight buildings on top.
    prev = city.pop("loft_city", {}) or {}
    recorded = {tuple(round(float(v), 3) for v in e) for e in prev.get("walls", [])}
    prev_characters = prev.get("characters", [])
    prev_chests = prev.get("chests", [])
    prev_nodes = prev.get("resource_nodes", [])
    prev_cells = prev.get("claim_cells", [])

    if os.path.exists(placements_path):
        placements = json.load(open(placements_path)).get("buildings", [])
        # what the CURRENT placements would write, so a city re-saved from EDIT MAP (which loses both
        # `name` and `loft_city`) still cannot end up with two copies of the same building
        for p in placements:
            lid = VARIANT_IDS[p["level"]]
            for w in flatten(levels, levels[lid]["objects"], p["x"], p["y"], p["z"],
                             int(p["rot_y"]), {lid}, 1, []):
                recorded.add((round(w["x"], 3), round(w["y"], 3), round(w["z"], 3),
                              round(w["sx"], 3), round(w["sy"], 3), round(w["sz"], 3)))
    else:
        placements = None

    def is_ours(w):
        if str(w.get("name", "")).startswith("loft_"):
            return True
        return (round(w["x"], 3), round(w["y"], 3), round(w["z"], 3),
                round(w["sx"], 3), round(w["sy"], 3), round(w["sz"], 3)) in recorded

    city["walls"] = [w for w in city["walls"] if not is_ours(w)]

    def strip_authored(key, recorded_entries):
        """Same idempotence for the population and the loot: the entries this tool wrote last time are
        removed by exact value before it adds their replacements."""
        if not recorded_entries:
            return
        rec = {json.dumps(e, sort_keys=True) for e in recorded_entries}
        city[key] = [e for e in city.get(key, []) if json.dumps(e, sort_keys=True) not in rec]

    # The population is stripped by SHAPE as well as by recorded value. Recorded-only stripping is
    # enough for the loot (a chest is a tier and three floats) but not for people: a run that changed
    # the population plan left the entries it no longer recorded behind forever, so the city slowly
    # accumulated zombies nobody authored. A bare zombie -- the tool's own output, no name, the
    # archetype's own kit/faction/orders -- is the tool's to remove; anything a human placed through
    # EDIT MAP carries a name or explicit fields and is left alone.
    def is_generated_npc(e):
        # archetype None = an entry written before the EMPIRE fields existed: a nameless role-7 body with
        # every other field at its default is still this tool's own output, and twelve of those were
        # sitting in the city exactly like that.
        return (e.get("name") in (None, "")
                and e.get("role") == 7
                and e.get("archetype") in (None, 2)
                and e.get("kit", 0) == 0 and e.get("faction", -1) == -1 and e.get("orders", -1) == -1)

    strip_authored("characters", prev_characters)
    city["characters"] = [e for e in city.get("characters", []) if not is_generated_npc(e)]
    strip_authored("chests", prev_chests)
    strip_authored("resource_nodes", prev_nodes)
    strip_authored("claim_cells", prev_cells)
    if not any(m.get("name") == "glass" for m in city.get("materials", [])):
        city.setdefault("materials", []).append(dict(GLASS_MATERIAL))

    if placements is None:
        placements = choose_placements(city, levels)
        if write:
            with open(placements_path, "w") as f:
                json.dump({"buildings": placements}, f, indent=1)
                f.write("\n")

    stats = []
    groups = []
    npcs = []
    chests = []
    nodes = []
    cells = []
    plan = population_plan(placements)
    for p in placements:
        name = p["level"]
        lid = VARIANT_IDS[name]
        walls = flatten(levels, levels[lid]["objects"], 0, 0, 0, 0, {lid}, 1, [])

        # place the building: rotate about Y, then translate, mirroring flattenObjects' order
        placed = []
        for w in walls:
            nw = transform_wall(w, p["x"], p["y"], p["z"], int(p["rot_y"]))
            nw["name"] = "%s_%d" % (name, len(placed) + 1)
            placed.append(nw)
        # ---- population + loot, per STOREY ----
        # Each storey's own deck is used for its footprint, not the building's overall box: a
        # stepped-back building's upper floors sit over a narrower stack, so whole-building fractions
        # would put its people in the air where the lower stack used to be.
        fp = footprint(placed)
        spec = VARIANTS[name]
        # The storeys this variant actually builds: a pair level is two floors, plus a lobby when it
        # has one. Same count check()'s own want_npcs formula uses, so the two cannot drift.
        storeys = spec["pairs"] * 2 + (1 if spec["lobby"] else 0)

        # The placed walls are WORLD-scaled (flatten applies LOFT_SCALE), so every authored constant used
        # to find a deck, a storey height or a floor offset has to be scaled with them -- otherwise a
        # storey's own deck is never found and its people and loot land on the ground floor.
        def storey_box(deck_y):
            deck = [w for w in placed
                    if abs(w["y"] - (deck_y + 0.5 * LOFT_SCALE)) < 0.7 * LOFT_SCALE
                    and w["sy"] <= 2.0 * LOFT_SCALE and w["sx"] >= 4.0 * LOFT_SCALE]
            return footprint(deck) if deck else fp

        def spot(box, fx, fz, y):
            return (round(box[0] + fx * (box[2] - box[0]), 2), round(y, 2),
                    round(box[1] + fz * (box[3] - box[1]), 2))

        # loot: the theme's own tiers, spread up the building rather than all on the ground floor
        for i, tier in enumerate(THEME_LOOT[spec["theme"]]):
            floor = i % storeys
            x, y, z = spot(storey_box(floor * STOREY * LOFT_SCALE), (0.18, 0.82, 0.15)[i % 3],
                           (0.55, 0.50, 0.85)[i % 3], (1.05 + floor * STOREY) * LOFT_SCALE)
            chests.append({"tier": tier, "x": x, "y": y, "z": z})

        # EMPIRE ground: one resource node outside the front door to gather from, one claim cell on the
        # ground floor to take. A level that authors neither key falls back to story_ai's default ring at
        # the WORLD ORIGIN, which in this city is several hundred metres from any loft -- the units would
        # have their orders and nothing within reach to act on.
        deck = storey_box(0.0)
        nx = round((deck[0] + deck[2]) * 0.5, 2)
        nodes.append({"x": nx, "z": round(deck[3] + 6.0 * LOFT_SCALE, 2),
                      "amount": 60 + 15 * len(THEME_LOOT[spec["theme"]])})
        cells.append({"x": nx, "z": round((deck[1] + deck[3]) * 0.5, 2),
                      "radius": round(10 * LOFT_SCALE, 2)})

        # ground floor: two, at opposite ends. Every staffed floor above: one, so climbing a loft is a
        # fight. Which floors those are is population_plan's decision, not this loop's.
        ground = ((0.22, 0.28), (0.78, 0.72))
        upper = ((0.30, 0.30), (0.70, 0.70), (0.20, 0.70), (0.80, 0.30))
        per_floor = {}
        for _, floor in [(n, f) for (n, f) in plan if n == name]:
            idx = per_floor.get(floor, 0)
            per_floor[floor] = idx + 1
            if floor == 0:
                fx, fz = ground[idx % 2]
            else:
                fx, fz = upper[(floor + idx) % 4]
            x, y, z = spot(storey_box(floor * STOREY * LOFT_SCALE), fx, fz,
                           (1.05 + floor * STOREY) * LOFT_SCALE)
            # archetype 2 = EU_ARCH_ZOMBIE, role 7 = the "Zombie" pursuer brain, faction/orders -1
            # so the archetype's own values win (that is what an unauthored EMPIRE character means)
            npcs.append({"role": 7, "kit": 0, "archetype": 2, "faction": -1, "orders": -1,
                         "x": x, "y": y, "z": z})

        groups.append(placed)
        stats.append((name, len(placed), fp))

    placement_conflicts(city["walls"], placements, levels)

    for g in groups:
        city["walls"].extend(g)
    city.setdefault("characters", []).extend(npcs)
    city.setdefault("chests", []).extend(chests)
    city["resource_nodes"] = city.get("resource_nodes", []) + nodes
    city["claim_cells"] = city.get("claim_cells", []) + cells
    city["loft_city"] = {
        "buildings": placements,
        "walls": [[w["x"], w["y"], w["z"], w["sx"], w["sy"], w["sz"]] for g in groups for w in g],
        "characters": npcs,
        "chests": chests,
        "resource_nodes": nodes,
        "claim_cells": cells,
    }

    for i, w in enumerate(city["walls"]):
        w["id"] = i + 1

    if write:
        with open(city_path, "w") as f:
            json.dump(city, f)
            f.write("\n")
    return city, stats


# The engine's own simultaneous-character ceiling: MAX_CLIENTS (70, packages/common/protocol.h) minus
# the human's own slot 0. The authored population has to fit it, or the loader keeps every character in
# the file and the brains can only ever place 69 of them -- the rest would be authored, saved, and never
# exist in the world. LEVEL_BOXES_MAX_CHARACTERS (128) is the FILE's cap, not the live one.
NPC_BUDGET = 69


def population_plan(placements):
    """Which floors of which buildings get a zombie, as a flat [(level_name, floor)] list; a floor of 0
    is the ground floor and appears TWICE per building (a player walking in meets two).

    Ground floors are staffed first, then the upper floors are spread EVENLY across the whole city --
    the i-th of `keep` floors taken from the i*len/keep-th candidate -- rather than the first N of the
    list. Spread is the point: building one must not be packed while building eight is empty, and a
    building with fewer storeys must not be starved by the ones before it."""
    storeys = {n: s["pairs"] * 2 + (1 if s["lobby"] else 0) for n, s in VARIANTS.items()}
    plan = [(p["level"], 0) for p in placements for _ in (0, 1)]
    upper = [(p["level"], f) for p in placements for f in range(1, storeys[p["level"]])]
    keep = min(max(0, NPC_BUDGET - len(plan)), len(upper))
    if keep:
        plan += [upper[(i * len(upper)) // keep] for i in range(keep)]
    if len(plan) > NPC_BUDGET:
        raise SystemExit("population plan authors %d characters, over the %d simultaneous slots"
                         % (len(plan), NPC_BUDGET))
    return plan


def check():
    levels = load_tree()
    owners = {}
    for name, spec in VARIANTS.items():
        t = spec["theme"]
        if t in owners:
            raise SystemExit("%s and %s share the %r interior" % (owners[t], name, t))
        owners[t] = name
    if sorted(owners) != sorted(THEMES):
        raise SystemExit("expected the %d themes to be owned one each, got %d" % (len(THEMES), len(owners)))
    seen = {}
    for name, lid in VARIANT_IDS.items():
        walls = flatten(levels, levels[lid]["objects"], 0, 0, 0, 0, {lid}, 1, [])
        if not walls:
            raise SystemExit("%s flattens to nothing" % name)
        mats = {w.get("material") for w in walls}
        bad = mats - {"brick", "glass", "wood", "concrete", "metal", "hps_light", "ips_light", None}
        if bad:
            raise SystemExit("%s uses unknown materials %s" % (name, bad))
        # the engine's own rule (brick_world.h): kind in {brick,concrete,wood,glass}, and not a slab
        # (h <= 12 with both plan extents >= 20). Floors and ceilings must stay slabs so a carved room
        # still has a floor to stand on; the partitions and glass are the carvable ones.
        carveable = sum(1 for w in walls
                        if w.get("material") in ("brick", "concrete", "wood", "glass")
                        and not w.get("ramp") and not w.get("rot_y")
                        and not (w["sy"] <= 12.0 and min(w["sx"], w["sz"]) >= 20.0))
        if carveable == 0:
            raise SystemExit("%s has nothing destructible in it" % name)
        # every storey has to be reachable: the core contributes one ramp per floor it is placed on
        ramps = [w for w in walls if w.get("ramp")]
        if not ramps:
            raise SystemExit("%s has no stairs -- its upper floors would be sealed slabs" % name)
        for r in ramps:
            if r["sy"] <= 0.0 or r["sz"] <= 0.0:
                raise SystemExit("%s has a ramp with no rise or no run" % name)
        # its OWN interior, and nobody else's: this is what makes the eight read differently inside
        theme = VARIANTS[name]["theme"]
        refs = {o["ref_level_id"] for o in levels[lid].get("objects", [])}
        if ID_PROPS[theme] not in refs:
            raise SystemExit("%s does not place its own interior (%s)" % (name, theme))
        foreign = refs & (set(ID_PROPS.values()) - {ID_PROPS[theme]})
        if foreign:
            raise SystemExit("%s places another building's interior" % name)
        key = tuple(sorted((w["x"], w["y"], w["z"], w["sx"], w["sy"], w["sz"]) for w in walls))
        if key in seen:
            raise SystemExit("%s and %s flatten to the same building" % (seen[key], name))
        seen[key] = name
        print("ok  %-8s %3d walls, %2d carveable" % (name, len(walls), carveable))

    city, stats = build(write=False)
    ids = [w["id"] for w in city["walls"]]
    if len(ids) != len(set(ids)):
        raise SystemExit("duplicate wall ids in the exported city")
    if len(city["walls"]) > 2048:
        raise SystemExit("city has %d walls, over LEVEL_BOXES_MAX 2048" % len(city["walls"]))
    if len(city["walls"]) > 1600:
        print("warn city has %d walls -- close to LEVEL_BOXES_MAX 2048" % len(city["walls"]))
    # the engine's own caps: LEVEL_BOXES_MAX_CHARACTERS is 64 in level_boxes.h (== STORY_AI_MAX) and
    # LEVEL_BOXES_MAX_CHESTS is 32 (== CHEST_MAX). Exceeding either is a silent truncation at load.
    if len(city.get("characters", [])) > 128:
        raise SystemExit("city authors %d characters, over LEVEL_BOXES_MAX_CHARACTERS 128"
                         % len(city["characters"]))
    if len(city.get("chests", [])) > 32:
        raise SystemExit("city authors %d chests, over LEVEL_BOXES_MAX_CHESTS 32" % len(city["chests"]))
    authored = city.get("loft_city", {})
    want_npcs = len(population_plan([{"level": n} for n in VARIANTS]))
    if len(authored.get("characters", [])) != want_npcs:
        raise SystemExit("expected %d zombies (two per ground floor, one per staffed floor above), found %d"
                         % (want_npcs, len(authored.get("characters", []))))
    if want_npcs > NPC_BUDGET:
        raise SystemExit("%d zombies do not fit the %d simultaneous slots" % (want_npcs, NPC_BUDGET))
    # ...and the array the ENGINE actually loads, not just the tool's own record of it. Those two came
    # apart once already: the record said 61 while the file still carried 73, which would have loaded 12
    # zombies nobody authored and spent 12 of the 69 slots on them.
    if len(city.get("characters", [])) != want_npcs:
        raise SystemExit("the level carries %d characters but the plan authors %d (stale entries)"
                         % (len(city.get("characters", [])), want_npcs))
    want_chests = sum(len(v) for v in THEME_LOOT.values())
    if len(authored.get("chests", [])) != want_chests:
        raise SystemExit("expected %d chests (each theme's own), found %d"
                         % (want_chests, len(authored.get("chests", []))))
    # EMPIRE ground: one of each per loft, inside the loader's own caps, and the array the engine reads
    # (not just the record) must be exactly the plan -- the same drift check the population got.
    # LEVEL_BOXES_MAX_RESOURCE_NODES (16) and LEVEL_BOXES_MAX_CLAIM_CELLS (32) in level_boxes.h.
    for key, cap in (("resource_nodes", 16), ("claim_cells", 32)):
        if len(city.get(key, [])) != len(VARIANTS):
            raise SystemExit("%s: authored %d, one per loft is %d"
                             % (key, len(city.get(key, [])), len(VARIANTS)))
        if len(city.get(key, [])) > cap:
            raise SystemExit("%s: %d entries, over the loader's cap of %d"
                             % (key, len(city.get(key, [])), cap))
    if len({tuple(v) for v in THEME_LOOT.values()}) < 4:
        raise SystemExit("the per-theme loot tables are not distinct enough to feel different")
    if set(THEME_LOOT) != set(THEMES):
        raise SystemExit("every theme needs a loot table")
    for name, n, fp in stats:
        print("ok  %-8s placed %3d walls at x=%.0f..%.0f z=%.0f..%.0f" % (name, n, fp[0], fp[2], fp[1], fp[3]))
    print("ok  city: %d walls, %d materials" % (len(city["walls"]), len(city.get("materials", []))))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--author", action="store_true", help="rewrite the authored tree from the blueprint")
    ap.add_argument("--out", help="write the flattened city here instead of var/zombie/nextown_zombies.json "
                                  "(a FORK: same loft tree, its own map)")
    ap.add_argument("--replace-placements", action="store_true",
                    help="ignore the frozen placements and choose them again (needed when LOFT_SCALE changes)")
    args = ap.parse_args()
    global CITY, PLACEMENTS
    if args.out:
        CITY = os.path.abspath(args.out)
    if args.replace_placements and os.path.exists(PLACEMENTS):
        os.remove(PLACEMENTS)
        print("placements: cleared, choosing again against the current building size")
    if args.author:
        n = write_authored(force=True)
        print("authored %d level file(s) under levels/lofts" % n)
        return 0
    if not os.path.exists(MANIFEST):
        write_authored(force=False)
    if args.check:
        return check()
    city, stats = build()
    print("wrote %s: %d walls" % (os.path.relpath(CITY, ROOT), len(city["walls"])))
    for name, n, fp in stats:
        print("  %-8s %3d walls  x=%7.1f..%-7.1f z=%7.1f..%-7.1f"
              % (name, n, fp[0], fp[2], fp[1], fp[3]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
