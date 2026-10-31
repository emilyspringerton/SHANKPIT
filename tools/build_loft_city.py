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

# One bay is 20 x 16 footprint, 8 units storey height. A "floor" is three bays (60 x 16), a "pair" is
# two floors (the founder's "floors that are actually 2 floors"), a building is 2..5 pairs plus lobby/roof.
BAY_W, BAY_D, STOREY = 20.0, 20.0, 8.0
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
    ]
    return level("loft_bay_open", ID_BAY_OPEN, BAY_W, STOREY, BAY_D, walls)


# ---- the composition (depth 2..4) ------------------------------------------------------------
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
    "loft_a": {"lobby": True, "pairs": 2, "roof": True, "step": 0},
    "loft_b": {"lobby": True, "pairs": 3, "roof": True, "step": 0},
    "loft_c": {"lobby": False, "pairs": 2, "roof": True, "step": 1},   # stepped tower
    "loft_d": {"lobby": True, "pairs": 4, "roof": False, "step": 0},
    "loft_e": {"lobby": False, "pairs": 3, "roof": False, "step": 0},
    "loft_f": {"lobby": True, "pairs": 5, "roof": True, "step": 1},
    "loft_g": {"lobby": False, "pairs": 2, "roof": False, "step": 2},
    "loft_h": {"lobby": True, "pairs": 3, "roof": True, "step": 2},
}


def variant(name, spec):
    objects = []
    y = 0.0
    if spec["lobby"]:
        objects.append(obj(ID_LOBBY, 0, y, 0))
        y += STOREY
    for i in range(spec["pairs"]):
        # a step-back every `step` pairs: the upper stack shifts and loses a bay, which is what makes
        # these read as eight different buildings rather than eight heights of one.
        setback = spec["step"] > 0 and i >= spec["step"]
        objects.append(obj(ID_PAIR, BAY_W if setback else 0, y, 0))
        y += STOREY * 2
    if spec["roof"]:
        objects.append(obj(ID_ROOF, BAY_W if spec["step"] > 0 else 0, y, 0))
    return level(name, VARIANT_IDS[name], BAY_W * 3 + BAY_W, y + 6.0, BAY_D, [], objects)


MODULES = [bay_glass(), bay_core(), bay_open(), floor_unit(), pair_unit(), lobby(), roof()]
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


def flatten(levels, objects, origin_x, origin_y, origin_z, origin_rot, visited, depth, out):
    """IDUNA flattenObjects, in Python: recursive, cycle-guarded, finite depth, Y-only rotation."""
    if depth > 12:
        raise SystemExit("loft object nesting too deep (max 12)")
    for o in objects:
        ref = o["ref_level_id"]
        if ref in visited:
            raise SystemExit("level object cycle involving level %d" % ref)
        child = levels.get(ref)
        if child is None:
            raise SystemExit("object references unknown level %d (not in the manifest)" % ref)
        ox, oz, _, _ = rotate_y90(o["x"], o["z"], 0, 0, origin_rot)
        wx, wz = origin_x + ox, origin_z + oz
        wy = origin_y + o["y"]
        wrot = (origin_rot + int(o.get("rot_y", 0))) % 360
        for w in child.get("walls", []):
            out.append(transform_wall(w, wx, wy, wz, wrot))
        flatten(levels, child.get("objects", []), wx, wy, wz, wrot,
                visited | {ref}, depth + 1, out)
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
        if top < 2.0 or bottom > 70.0:
            continue  # ground plane, or a tower's upper floors: not in the way of a street building
        blocked.append((w["x"] - w["sx"] / 2 - 6, w["z"] - w["sz"] / 2 - 6,
                        w["x"] + w["sx"] / 2 + 6, w["z"] + w["sz"] / 2 + 6))
    spawn = city["spawners"][0]
    blocked.append((spawn["x"] - 30, spawn["z"] - 30, spawn["x"] + 30, spawn["z"] + 30))

    sizes = {}
    for name in VARIANTS:
        walls = flatten(levels, levels[VARIANT_IDS[name]]["objects"], 0, 0, 0, 0,
                        {VARIANT_IDS[name]}, 1, [])
        sizes[name] = footprint(walls)

    # rotated plan extents: the anchor is the building's minimum corner, so a candidate is tested
    # against the SAME box the export will produce, not against an unrotated approximation
    sizes = {name: {0: (fp[2] - fp[0], fp[3] - fp[1]),
                    90: (fp[3] - fp[1], fp[2] - fp[0]),
                    180: (fp[2] - fp[0], fp[3] - fp[1]),
                    270: (fp[3] - fp[1], fp[2] - fp[0])}
             for name, fp in sizes.items()}

    candidates = [(gx, gz) for gx in range(-1600, 2400, 100) for gz in range(-1400, 2000, 100)]
    candidates.sort(key=lambda p: math.hypot(p[0] + 30 - spawn["x"], p[1] + 10 - spawn["z"]))

    placements = []
    rots = [0, 90, 180, 270]
    for i, name in enumerate(VARIANTS):
        chosen = None
        # nearest-first, and a different rotation per building so the street is not a row of clones
        for rot in [rots[i % 4], rots[(i + 1) % 4], rots[(i + 2) % 4], rots[(i + 3) % 4]]:
            rw, rd = sizes[name][rot]
            for c in candidates:
                box = (c[0], c[1], c[0] + rw, c[1] + rd)
                if any(overlaps(box, b) for b in blocked):
                    continue
                bad = False
                for other in placements:
                    if overlaps(box, other["_box"]) or math.hypot(c[0] - other["x"], c[1] - other["z"]) < 220:
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
        groups.append(placed)
        stats.append((name, len(placed), footprint(placed)))

    placement_conflicts(city["walls"], placements, levels)

    for g in groups:
        city["walls"].extend(g)
    city["loft_city"] = {
        "buildings": placements,
        "walls": [[w["x"], w["y"], w["z"], w["sx"], w["sy"], w["sz"]] for g in groups for w in g],
    }

    for i, w in enumerate(city["walls"]):
        w["id"] = i + 1

    if write:
        with open(city_path, "w") as f:
            json.dump(city, f)
            f.write("\n")
    return city, stats


def check():
    levels = load_tree()
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
    for name, n, fp in stats:
        print("ok  %-8s placed %3d walls at x=%.0f..%.0f z=%.0f..%.0f" % (name, n, fp[0], fp[2], fp[1], fp[3]))
    print("ok  city: %d walls, %d materials" % (len(city["walls"]), len(city.get("materials", []))))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--author", action="store_true", help="rewrite the authored tree from the blueprint")
    args = ap.parse_args()
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
