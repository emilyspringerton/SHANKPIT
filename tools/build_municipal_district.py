#!/usr/bin/env python3
"""build_municipal_district.py -- the civic buildings, authored as recursive levels, flattened into the
zombies city beside the lofts.

Founder, 2026-11-05: "in the same way that we added multiple lofts as recursive levels to the default
zombies map lets expand it by building municiple buildings using the same recursive level building
technique multiple variations of the municiple building smultiple variations of the sub levels so we
have good variation and cohesion have them different in style than the lofts more sprawling like court
houses etc add any materials for building like institutional tiles etc use parena procedural texture
generation add multiple woods with different densities".

THE MODEL IS THE LOFT CITY'S, AND IT IS LITERALLY THE SAME CODE. Composition is
{ref_level_id, x, y, z, rot_y} inside a level's `objects`, flattened recursively
(IDUNA/internal/shankpit/level_store.go's flattenObjects, exported in-repo by
tools/build_loft_city.py) -- so this script imports build_loft_city and calls ITS flatten,
transform_wall, footprint and overlap test rather than reimplementing them. A second copy of that
composition would be a second set of rules for the same tree, which is exactly what "the same
recursive level building technique" rules out.

    levels/municipal/*.json      the authored tree -- edit these, re-run this script
    levels/municipal/manifest.json   id -> file
    levels/municipal/placements.json where the six buildings stand (frozen once chosen)
    var/zombie/nextown_zombies.json  the playable city export (municipal walls + its own materials)

STYLE, DELIBERATELY NOT THE LOFTS. A loft is a narrow tower: three bays wide, 2..5 pairs tall, a stair
core threading glass storeys. A municipal building is the opposite shape -- one or two storeys, and
WIDE: ranges of public rooms laid back from a street colonnade, tile floors, plaster walls, marble
columns, oak and mahogany furniture, a records hall you can shoot through. Six variations, each a
different combination of the same eight sub-levels (two courtrooms, two public halls, a records range, a
cell block, a stair, a colonnade, a roof), and each wing is placed at its own rotation -- so editing one
sub-level restyles every building that references it and no two buildings flatten to the same geometry.

BUDGETS ARE REAL AND THIS FILE RESPECTS THEM RATHER THAN DISCOVERING THEM AT LOAD TIME.
  * LEVEL_BOXES_MAX 2048 (level_boxes.h == IDUNA MaxWalls) is the whole city's wall budget, and the
    eight lofts already spend 1659 of it. The six municipal buildings therefore add ~220 and the
    `--check` pass refuses an export that crosses the cap, because the loader would silently truncate.
  * LEVEL_BOXES_MAX_CHESTS 32 / CHEST_MAX: the lofts author 24, so the district authors one chest per
    building (30 total) rather than a chest per room.
  * The city's 69 simultaneous character slots (MAX_CLIENTS-1) are SHARED with the lofts, not added to:
    the district staffs two bodies per building out of `loft.CIVIC_NPC_RESERVE` (12), and the loft plan
    is written against the remaining 57. Before the district existed the lofts spent all 69; the split
    lives in ONE constant (build_loft_city.CIVIC_NPC_RESERVE) and both tools' --check passes assert
    their own half plus the total, because a 70th body is authored, saved and never spawned.

Destructibility: tile, plaster, marble and the three wood species are all in the engine's carveable set
(brick_world_kind_for_material maps wood_oak/wood_pine/wood_mahogany onto BF_KIND_WOOD and tile/plaster/
marble onto the masonry kinds), so a courthouse comes apart the same way a loft does -- and the three
woods carry their own densities, so a mahogany bench is not a pine crate.

Usage:
    python3 tools/build_municipal_district.py            # author (if missing) + flatten into the city
    python3 tools/build_municipal_district.py --check    # verify only: refs, materials, caps, idempotence
    python3 tools/build_municipal_district.py --author   # rewrite the authored tree from the blueprint
    python3 tools/build_municipal_district.py --replace-placements
"""
import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_loft_city as loft  # noqa: E402  -- the SAME composition code, deliberately

ROOT = loft.ROOT
MUN_DIR = os.path.join(ROOT, "levels", "municipal")
CITY = loft.CITY
MANIFEST = os.path.join(MUN_DIR, "manifest.json")
PLACEMENTS = os.path.join(MUN_DIR, "placements.json")

BAY_W, BAY_D, STOREY = loft.BAY_W, loft.BAY_D, loft.STOREY
SCALE = loft.LOFT_SCALE          # the same phi^3 the lofts are exported at: one city, one scale

# ids 301.. are the municipal tree, clear of the loft tree's 201..224.
ID_SPINE = 301
ID_HALL = 302
ID_HALL_OPEN = 303
ID_COURT = 304
ID_COURT_SMALL = 305
ID_RECORDS = 306
ID_CELLS = 307
ID_STAIR = 308
ID_PORTICO = 309
ID_ROOF = 310
WING_IDS = {name: 320 + i for i, name in enumerate(
    ["mun_wing_court", "mun_wing_court_small", "mun_wing_hall", "mun_wing_hall_open",
     "mun_wing_records", "mun_wing_cells"])}
BUILDINGS = ["mun_courthouse", "mun_city_hall", "mun_records_annex",
             "mun_precinct", "mun_clinic", "mun_library"]
BUILDING_IDS = {name: 340 + i for i, name in enumerate(BUILDINGS)}

# story_ai.h's own AIRole range, as numbers this table may author. 11 is AI_ROLE_MAGISTRATE -- the civic
# boss. AIKit 0..5 (AUTO/MANNEQUIN/STAN/MIKE/LEELA/GEORGE) is six models for seven jobs, so exactly one
# kit repeats; that is a real ceiling, asserted in check(), not an oversight.
AI_ROLE_MAGISTRATE = 11
AI_KIT_COUNT = 6

# ---- staffing ------------------------------------------------------------------------------------
# Two bodies per building, and each building's pair is its own JOB rather than twelve copies of one
# clerk. The first pass staffed all twelve as `role 7, kit 0, archetype 2` -- a bare zombie -- which
# is a defensible "the district is infested" read and a bad level: six public buildings that fight
# identically read as one building repeated.
#
#   (archetype, role, kit, faction, orders)
#
# archetype/faction/orders are EMPIRE's own vocabulary (PARENA stdlib/shankpit/empire_units.prn, in
# SHANKPIT via empire_units.h). The archetype owns the health pool, the default skin and the order
# priorities; the authored `role` picks the BRAIN (story_ai.h's AIRole, honoured over the archetype's
# by story_ai_spawn_unit_role) and so the weapon and the fight: a shotgun bailiff, an AR sergeant, a
# sniper archivist, an AR officer down long sightlines, a knife orderly, a leashed shotgun warden.
# Faction 2 (hostile) is authored explicitly rather than left to the archetype's default, because two
# of these bodies are worker/citizen-shaped and those archetypes default NEUTRAL.
#
# Every role here is a value the engine defines (0..11, 11 being AI_ROLE_MAGISTRATE), every kit is a
# real AIKit (0..5), and every
# orders mask contains ENGAGE -- asserted in check() below, so a future edit cannot quietly staff a
# building with a body that cannot fight. Keep this table in sync with PARENA's archetype/kit numbers:
# if an archetype is renumbered there, this file is a stale reference and check() should fail, not
# reinterpret it.
BUILDING_STAFF = {
    # building:          [(archetype, role, kit, faction, orders)] -- one tuple per body, in
    #                    staff_points order (the first is the one you meet at the door).
    "mun_courthouse":     [(5, 2, 2, 2, 36),     # bailiff:   guard body, GORE_BRUTE brain -- shotgun, holds the gallery
                           (4, 11, 5, 2, 36)],   # MAGISTRATE: the civic boss, at the bench behind him
    "mun_city_hall":      [(3, 4, 5, 2, 52),     # sergeant: soldier body, GUARD brain -- AR, and it CLAIMS the floor
                           (3, 4, 5, 2, 52)],
    "mun_records_annex":  [(6, 5, 0, 2, 36),     # archivist: worker body, STORM_CALLER brain -- sniper down the stacks
                           (6, 5, 0, 2, 36)],
    "mun_precinct":       [(3, 1, 1, 2, 36),     # officer:  soldier body, SHAMBLER_TROOPER brain -- AR, long sightlines
                           (3, 1, 1, 2, 36)],
    "mun_clinic":         [(2, 7, 4, 2, 4),      # orderly:  zombie body, RELENTLESS_PURSUER brain -- knife, always closes
                           (2, 7, 4, 2, 4)],
    "mun_library":        [(6, 8, 3, 2, 36),     # warden:   worker body, TERRITORIAL_BEAST brain -- leashed shotgun upstairs
                           (6, 8, 3, 2, 36)],
}

# ---- the civic boss -------------------------------------------------------------------------------
# The courthouse's second body is AI_ROLE_MAGISTRATE (story_ai.h, story_ai.c's ai_combat_magistrate):
# it holds the bench instead of charging, it carries WPN_MAGNUM -- the one weapon no clerk in the
# district has -- and every five seconds, with the player in sight, it "holds them in contempt",
# writing the same real stunned_until_ms the host's own input gate reads.
#
# This costs no extra slot. The city spends exactly MAX_CLIENTS-1 = 69 simultaneous bodies and the
# district's share is 12, so a thirteenth body could not spawn at all -- the boss is one of the twelve,
# which is also the right shape: you meet the bailiff at the door and the magistrate is behind him, in
# the second (records) range. Its archetype is 4 because the ALLY row owns the district's largest
# health pool (120, PARENA eu-archetype-health) -- a boss that dies like a bailiff is a clerk with a
# title -- and its faction is authored HOSTILE (2), overriding that archetype's friendly default.

# The city's tint, warmer and paler than the lofts' cool 0.6/0.6/0.65 -- institutional paint over tile,
# not a glass office block. It multiplies each material's own texture, so this is the "one civic
# palette" the district reads as from the street.
R, G, B = 0.68, 0.65, 0.58
FRICTION = 0.8

# ---- materials -----------------------------------------------------------------------------------
# The four materials the district needed that the city did not carry. Each one's LOOK is authored in
# PARENA (stdlib/shankpit/textures: institutional_tile.prn for `tile`, courthouse_marble.prn,
# wood_oak/pine/mahogany.prn) and each one's MASS is the `density` field level_boxes.h already parses
# into LevelBoxMaterial.density -> brick_world parent mass (kg/m^3). The three woods are the point of
# "multiple woods with different densities": oak 720, mahogany 580, pine 460, so a bench, a bench top
# and a crate are three different weights of debris.
#
# Deliberately no texture_url: brick/concrete/wood already ship without one (the native client resolves
# a look from the material NAME via its own procedural generators), and inventing NOCK library ids that
# no NOCK store has would be a lie in the file.
MATERIALS = {
    # name:        (shader,    specular, shininess, friction, density)
    "tile":        ("standard", 0.14, 26.0, 0.30, 2300.0),
    "plaster":     ("standard", 0.02,  2.0, 0.32,  900.0),
    "marble":      ("standard", 0.38, 52.0, 0.22, 2700.0),
    "wood_oak":    ("standard", 0.07, 10.0, 0.36,  720.0),
    "wood_mahogany": ("standard", 0.09, 14.0, 0.34, 580.0),
    "wood_pine":   ("standard", 0.04,  6.0, 0.30,  460.0),
}

KNOWN_MATERIALS = {"brick", "concrete", "metal", "glass", "wood",
                   "hps_light", "ips_light", "gradient"} | set(MATERIALS)


def mun_wall(x, y, z, sx, sy, sz, material=None, name=None, ramp=False):
    """A wall with the district's own tint -- loft.wall() is the lofts' colour and stays the lofts'."""
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


obj = loft.obj
level = loft.level


# ---- the sub-levels (depth 1) --------------------------------------------------------------------
# Every module is authored in ONE 20x20 bay with its deck top at y=1 and its ceiling at y=STOREY-0.5,
# the same authoring frame the loft bays use, so the two trees are the same kind of object and a
# municipal wing can be dropped where a loft bay goes.

def shared_shell(material_floor="tile", material_wall="plaster", back_door=True):
    """The four boxes every room shares: a tiled deck, a plaster ceiling, a solid outer wall, and a back
    wall built as TWO pieces with a 4-unit doorway between them.

    The doorway is why a range of rooms is walkable: a building is a stack of these along -z, and the
    only way through is the gap. The +z face is left OPEN on purpose -- that is where the next range (or
    the street colonnade) meets it, and authoring a wall there would put two coincident planes in the
    same place for every pair of stacked wings.
    """
    walls = [
        mun_wall(0, 0.5, 0, BAY_W, 1.0, BAY_D, material_floor),                        # deck
        mun_wall(0, STOREY - 0.5, 0, BAY_W, 1.0, BAY_D, material_wall),                # ceiling
        mun_wall(-BAY_W / 2.0 + 0.3, STOREY / 2.0, 0, 0.6, STOREY - 1.0, BAY_D, material_wall),
    ]
    if back_door:
        walls.append(mun_wall(-5.5, STOREY / 2.0, -BAY_D / 2.0 + 0.3, 7.0, STOREY - 2.2, 0.6, material_wall))
        walls.append(mun_wall(5.5, STOREY / 2.0, -BAY_D / 2.0 + 0.3, 7.0, STOREY - 2.2, 0.6, material_wall))
    return walls


def bay_spine():
    """The corridor half of a wing: open to its room on one side and to the hall beyond it at both ends."""
    walls = shared_shell(back_door=False)
    walls.append(mun_wall(6.0, 6.4, 0.0, 3.0, 0.4, 3.0, "ips_light"))
    return level("mun_spine", ID_SPINE, BAY_W, STOREY, BAY_D, walls)


def bay_hall():
    """A public hall: four marble columns down the room, tile underfoot, a light in the ceiling."""
    walls = shared_shell()
    for cx in (-6.0, 6.0):
        for cz in (-5.5, 5.5):
            walls.append(mun_wall(cx, STOREY / 2.0, cz, 1.1, STOREY - 1.0, 1.1, "marble"))
    walls.append(mun_wall(0.0, 7.4, 0.0, 2.0, 0.4, 2.0, "hps_light"))
    return level("mun_hall", ID_HALL, BAY_W, STOREY, BAY_D, walls)


def bay_hall_open():
    """The same hall with the colonnade replaced by two half-height PINE rails -- a licensing counter /
    waiting room, and the cheapest room in the district to shoot across. Pine, not oak: this is the one
    place the district uses softwood on purpose, so all three species are load-bearing in the city
    (oak in the courts and records, mahogany on the benches, pine in the cheap partitions) and their
    three densities are three different weights of debris rather than three names on one material."""
    walls = shared_shell(material_wall="plaster")
    walls.append(mun_wall(-3.0, 2.0, 0.0, 0.4, 2.0, 12.0, "wood_pine"))
    walls.append(mun_wall(3.0, 2.0, 0.0, 0.4, 2.0, 12.0, "wood_pine"))
    walls.append(mun_wall(0.0, 7.4, 0.0, 2.0, 0.4, 2.0, "ips_light"))
    return level("mun_hall_open", ID_HALL_OPEN, BAY_W, STOREY, BAY_D, walls)


def bay_court():
    """A courtroom: a raised mahogany bench against the outer wall, an oak rail, three gallery rows and
    a tiled floor. The bench is the one thing in the district that is deliberately heavier than it looks.
    """
    walls = shared_shell()
    walls.append(mun_wall(-7.4, 1.6, -1.0, 4.6, 1.2, 7.0, "marble"))          # dais
    walls.append(mun_wall(-7.4, 2.9, -1.0, 4.2, 1.4, 5.6, "wood_mahogany"))   # the bench itself
    walls.append(mun_wall(-1.5, 2.0, 0.0, 0.4, 2.0, 13.0, "wood_oak"))         # rail
    for i in range(3):
        walls.append(mun_wall(4.6, 1.6, -5.0 + i * 5.0, 6.4, 1.2, 1.8, "wood_oak"))   # gallery
    walls.append(mun_wall(0.0, 7.4, 0.0, 2.0, 0.4, 2.0, "hps_light"))
    return level("mun_court", ID_COURT, BAY_W, STOREY, BAY_D, walls)


def bay_court_small():
    """A magistrate's court: one bench, two rows, no rail. Same room at half the furniture, which is how
    six variations stay inside a 2048-wall city."""
    walls = shared_shell()
    walls.append(mun_wall(-7.4, 1.6, 0.0, 4.0, 1.2, 6.0, "marble"))
    walls.append(mun_wall(-7.4, 2.7, 0.0, 3.6, 1.2, 4.8, "wood_mahogany"))
    for i in range(2):
        walls.append(mun_wall(4.8, 1.6, -3.0 + i * 6.0, 6.0, 1.2, 1.8, "wood_oak"))
    walls.append(mun_wall(0.0, 7.4, 0.0, 2.0, 0.4, 2.0, "ips_light"))
    return level("mun_court_small", ID_COURT_SMALL, BAY_W, STOREY, BAY_D, walls)


def bay_records():
    """A records hall: four oak shelving stacks down the outer wall, tall enough to shoot through."""
    walls = shared_shell()
    for i in range(4):
        walls.append(mun_wall(-6.6, 3.0, -7.0 + i * 4.6, 2.2, 5.2, 3.4, "wood_oak"))
    walls.append(mun_wall(0.0, 7.4, 0.0, 2.0, 0.4, 2.0, "ips_light"))
    return level("mun_records", ID_RECORDS, BAY_W, STOREY, BAY_D, walls)


def bay_cells():
    """A cell block: three brick partitions and two steel bar runs. Brick and steel, in a district of
    tile and plaster, so a precinct reads as a precinct from the inside."""
    walls = shared_shell()
    for i in range(3):
        walls.append(mun_wall(-3.0, 2.2, -7.6 + i * 5.2, 7.0, 4.2, 0.5, "brick"))
    walls.append(mun_wall(0.6, 2.2, -7.0, 0.3, 4.2, 10.0, "metal"))
    walls.append(mun_wall(0.6, 2.2, 1.0, 0.3, 4.2, 10.0, "metal"))
    walls.append(mun_wall(5.0, 7.4, 0.0, 2.0, 0.4, 2.0, "hps_light"))
    return level("mun_cells", ID_CELLS, BAY_W, STOREY, BAY_D, walls)


def bay_stair():
    """The only reason a two-storey municipal building is not a sealed slab: a well cut through the deck
    AND the ceiling of one bay with a ramp climbing it -- loft_bay_core's own construction, retiled.

    Three deck pieces and three ceiling pieces around an 8x12 opening on the -z half, plus the wedge.
    Because every storey of the building instantiates this module at the same bay, the openings line up
    and the flight continues past every floor.
    """
    walls = [
        mun_wall(-7.0, 0.5, 0.0, 6.0, 1.0, BAY_D, "tile"),
        mun_wall(7.0, 0.5, 0.0, 6.0, 1.0, BAY_D, "tile"),
        mun_wall(0.0, 0.5, 6.0, 8.0, 1.0, 8.0, "tile"),
        mun_wall(-7.0, STOREY - 0.5, 0.0, 6.0, 1.0, BAY_D, "plaster"),
        mun_wall(7.0, STOREY - 0.5, 0.0, 6.0, 1.0, BAY_D, "plaster"),
        mun_wall(0.0, STOREY - 0.5, 6.0, 8.0, 1.0, 8.0, "plaster"),
        mun_wall(0.0, 5.0, -4.0, 8.0, 8.0, 12.0, "concrete", ramp=True),
        mun_wall(-BAY_W / 2.0 + 0.3, STOREY / 2.0, 0.0, 0.6, STOREY - 1.0, BAY_D, "plaster"),
        mun_wall(BAY_W / 2.0 - 0.3, STOREY / 2.0, 0.0, 0.6, STOREY - 1.0, BAY_D, "plaster"),
        mun_wall(0.0, 7.4, 3.0, 2.0, 0.4, 2.0, "hps_light"),
    ]
    return level("mun_stair", ID_STAIR, BAY_W, STOREY, BAY_D, walls)


def bay_portico():
    """The street elevation: six marble columns, an entablature, two side walls and a step up. This is
    the box that makes the district read as civic rather than industrial from a distance."""
    walls = [
        mun_wall(0.0, 0.5, 0.0, BAY_W - 0.6, 1.0, BAY_D, "tile"),
        mun_wall(0.0, STOREY / 2.0, -BAY_D / 2.0 + 0.3, BAY_W - 0.6, STOREY - 1.6, 0.6, "plaster"),
    ]
    for i in range(3):
        cx = -6.0 + i * 6.0
        for cz in (-5.0, 5.0):
            walls.append(mun_wall(cx, STOREY / 2.0, cz, 1.2, STOREY - 1.2, 1.2, "marble"))
    walls.append(mun_wall(0.0, STOREY - 0.4, 0.0, BAY_W - 0.6, 0.8, BAY_D, "marble"))   # entablature
    walls.append(mun_wall(0.0, 0.2, BAY_D / 2.0 + 1.0, 10.0, 1.0, 2.5, "concrete", ramp=True))  # step
    return level("mun_portico", ID_PORTICO, BAY_W, STOREY, BAY_D, walls)


def bay_roof():
    """A flat civic roof: a tile deck and a low parapet on the two long sides and the back."""
    walls = [
        mun_wall(0.0, 0.5, 0.0, BAY_W, 1.0, BAY_D, "tile"),
        mun_wall(-BAY_W / 2.0 + 0.4, 2.0, 0.0, 0.8, 3.0, BAY_D, "plaster"),
        mun_wall(BAY_W / 2.0 - 0.4, 2.0, 0.0, 0.8, 3.0, BAY_D, "plaster"),
        mun_wall(0.0, 2.0, -BAY_D / 2.0 + 0.4, BAY_W, 3.0, 0.8, "plaster"),
    ]
    return level("mun_roof", ID_ROOF, BAY_W, STOREY, BAY_D, walls)


# ---- the wings (depth 2) -------------------------------------------------------------------------
# A wing is a corridor bay plus one room, 40 wide: the founder's "multiple variations of the sub
# levels". Nothing here duplicates geometry -- a wing is two references, which is why retiling
# mun_records restyles every records hall in every building at once.
WING_ROOMS = {
    "mun_wing_court": ID_COURT,
    "mun_wing_court_small": ID_COURT_SMALL,
    "mun_wing_hall": ID_HALL,
    "mun_wing_hall_open": ID_HALL_OPEN,
    "mun_wing_records": ID_RECORDS,
    "mun_wing_cells": ID_CELLS,
}


def wing(name):
    objects = [obj(ID_SPINE, 0, 0, 0), obj(WING_ROOMS[name], BAY_W, 0, 0)]
    # an edge beam so the wing reads as one range rather than two rooms that happen to touch
    walls = [mun_wall(BAY_W / 2.0, 1.0, BAY_D / 2.0, BAY_W * 2.0, 1.0, 0.4, "metal")]
    return level(name, WING_IDS[name], BAY_W * 2, STOREY, BAY_D, walls, objects)


def _w(name, dx_bays, dy_storeys, dz_bays, rot=0):
    """One placed wing: (level, offset in bays and storeys, rotation). Rotations are what stop two
    buildings that share a records range from looking identical from the street.

    dy_storeys is almost always 0: `building()` raises each storey by STOREY itself, and a storey that
    ALSO offset itself would float its own floor above the building."""
    return obj(WING_IDS[name], dx_bays * BAY_W, dy_storeys * STOREY, dz_bays * BAY_D, rot)


# ---- the six buildings (depth 3) ----------------------------------------------------------------
# Each is only a combination of wings, plus whatever street furniture its own civic function wants. The
# variant spread is the point: one deep colonnade, one wide, one L, one two-storey, one without a
# portico at all, and one that is nothing but two records ranges back to back.
BUILDING_SPECS = {
    "mun_courthouse": {
        "storeys": [[
            obj(ID_PORTICO, 0, 0, BAY_D),          # the colonnade stands IN FRONT of the range
            _w("mun_wing_court", 0, 0, 0),
            _w("mun_wing_records", 0, 0, -1, 180),  # records behind, shelves facing the back
        ]],
        "roof": True,
    },
    "mun_city_hall": {
        "storeys": [[
            obj(ID_PORTICO, 0, 0, BAY_D),
            _w("mun_wing_hall_open", 0, 0, 0),
            _w("mun_wing_hall", 0, 0, -1),   # three ranges would cost 15 more walls than the city has
        ]],
        "roof": True,
    },
    "mun_records_annex": {
        "storeys": [[
            _w("mun_wing_records", 0, 0, 0),
            _w("mun_wing_records", 0, 0, -1, 180),
        ]],
        "roof": True,
    },
    "mun_precinct": {
        "storeys": [[
            obj(ID_PORTICO, 0, 0, BAY_D),
            _w("mun_wing_hall", 0, 0, 0),
            _w("mun_wing_cells", 0, 0, -1),
        ]],
        "roof": True,
    },
    "mun_clinic": {
        "storeys": [[
            _w("mun_wing_hall_open", 0, 0, 0),
            _w("mun_wing_court_small", 0, 0, -1, 90),
        ]],
        "roof": False,          # an open-roofed clinic: the one building you can drop into
    },
    "mun_library": {
        # the stair has its OWN bay beside the range, not inside it: a stair core dropped into a records
        # hall would cut its well through four shelf stacks. `building()` raises every storey by STOREY.
        "storeys": [
            [obj(ID_STAIR, 0, 0, 0), _w("mun_wing_records", 1, 0, 0)],
            [obj(ID_STAIR, 0, 0, 0), _w("mun_wing_records", 1, 0, 0)],
        ],
        "roof": True,
    },
}

# One chest per building, in chests.h's tiers. A courthouse and a precinct are worth breaking into; a
# library is not.

# Staffing: the district's half of the city's 69 simultaneous slots, taken from the loft tool's own
# reserve rather than invented here -- so the two tools cannot disagree about the split.
MUNICIPAL_NPC_BUDGET = loft.CIVIC_NPC_RESERVE

BUILDING_LOOT = {
    "mun_courthouse": 2,
    "mun_city_hall": 1,
    "mun_records_annex": 1,
    "mun_precinct": 2,
    "mun_clinic": 1,
    "mun_library": 0,
}


def building(name):
    spec = BUILDING_SPECS[name]
    objects = []
    for si, storey in enumerate(spec["storeys"]):
        for o in storey:
            d = dict(o)
            d["y"] = round(d["y"] + si * STOREY, 3)
            objects.append(d)
    if spec["roof"]:
        top = len(spec["storeys"]) * STOREY
        objects.append(obj(ID_ROOF, 0, top, 0))
    # storeys are what the `--check` pass counts, kept on the level so the two cannot drift
    return level(name, BUILDING_IDS[name], BAY_W * 4, STOREY * len(spec["storeys"]) + STOREY,
                 BAY_D * 4, [], objects)


MODULES = [bay_spine(), bay_hall(), bay_hall_open(), bay_court(), bay_court_small(), bay_records(),
           bay_cells(), bay_stair(), bay_portico(), bay_roof()]
MODULES += [wing(n) for n in WING_ROOMS]
AUTHORED = {m["name"]: m for m in MODULES}
AUTHORED.update({n: building(n) for n in BUILDINGS})


def load_tree():
    manifest = json.load(open(MANIFEST))
    levels = {}
    for ent in manifest["levels"]:
        path = os.path.join(MUN_DIR, ent["file"])
        lv = json.load(open(path))
        if lv.get("id") != ent["id"]:
            raise SystemExit("%s: id %r does not match manifest %r" % (path, lv.get("id"), ent["id"]))
        levels[lv["id"]] = lv
    return levels


def flatten_building(levels, name, x=0.0, y=0.0, z=0.0, rot=0):
    """The loft city's own flatten(), called with this tree -- one composition rule for both districts."""
    lid = BUILDING_IDS[name]
    return loft.flatten(levels, levels[lid].get("objects", []), x, y, z, rot, {lid}, 1, [], SCALE)


def place_point(levels, name, lx, ly, lz, x, y, z, rot):
    """A local authored point -> world, mirroring flatten's own rotate-then-translate order and scale."""
    rx, rz, _, _ = loft.rotate_y90(lx * SCALE, lz * SCALE, 0.0, 0.0, rot)
    return round(x + rx, 2), round(y + ly * SCALE, 2), round(z + rz, 2)


def choose_placements(city, levels):
    """Six spots, clustered: the first is the nearest genuinely empty street to the spawn, and every
    building after it is the nearest empty spot to the FIRST one -- a civic district that reads as one
    place instead of six municipal buildings scattered among the lofts like the lofts themselves are.
    """
    blocked = []
    for w in city["walls"]:
        top, bottom = w["y"] + w["sy"] / 2, w["y"] - w["sy"] / 2
        if top <= 1.0 or bottom >= 70.0:
            continue    # ground plane, or a tower's upper floors: not in a street building's way
        blocked.append((w["x"] - w["sx"] / 2 - 6, w["z"] - w["sz"] / 2 - 6,
                        w["x"] + w["sx"] / 2 + 6, w["z"] + w["sz"] / 2 + 6))
    spawn = city["spawners"][0]
    blocked.append((spawn["x"] - 30, spawn["z"] - 30, spawn["x"] + 30, spawn["z"] + 30))

    def box_for(name, cx, cz, rot):
        fp = loft.footprint(flatten_building(levels, name, cx, 0.0, cz, rot))
        return (fp[0] - 8.0, fp[1] - 8.0, fp[2] + 8.0, fp[3] + 8.0)

    candidates = [(gx, gz) for gx in range(-1600, 2400, 40) for gz in range(-1400, 2000, 40)]
    spawn_d = {c: math.hypot(c[0] + 30 - spawn["x"], c[1] + 10 - spawn["z"]) for c in candidates}
    candidates.sort(key=lambda p: spawn_d[p])

    placements = []
    anchor = None
    rots = [0, 90, 180, 270]
    for i, name in enumerate(BUILDINGS):
        chosen = None
        order = candidates if anchor is None else sorted(
            candidates, key=lambda p: math.hypot(p[0] - anchor[0], p[1] - anchor[1]))
        for rot in [rots[i % 4], rots[(i + 1) % 4], rots[(i + 2) % 4], rots[(i + 3) % 4]]:
            for c in order:
                box = box_for(name, c[0], c[1], rot)
                if any(loft.overlaps(box, b) for b in blocked):
                    continue
                bad = False
                for other in placements:
                    if loft.overlaps(box, other["_box"]) or math.hypot(c[0] - other["x"], c[1] - other["z"]) < 60:
                        bad = True
                        break
                if bad:
                    continue
                chosen = (c, rot, box)
                break
            if chosen:
                break
        if chosen is None:
            raise SystemExit("no empty civic ground for %s" % name)
        placements.append({"level": name, "x": chosen[0][0], "y": 0.0, "z": chosen[0][1],
                           "rot_y": chosen[1], "_box": chosen[2]})
        if anchor is None:
            anchor = chosen[0]
    for p in placements:
        del p["_box"]
    return placements


def verify_placements(city_walls, placements, levels):
    """The export's own overlap rule, checked against the ROTATED footprint of what actually lands."""
    boxes = [(w["x"] - w["sx"] / 2, w["z"] - w["sz"] / 2, w["x"] + w["sx"] / 2, w["z"] + w["sz"] / 2)
             for w in city_walls
             if w["y"] + w["sy"] / 2 > 1.0 and w["y"] - w["sy"] / 2 < 70.0]
    for p in placements:
        fp = loft.footprint(flatten_building(levels, p["level"], p["x"], p["y"], p["z"], int(p["rot_y"])))
        for b in boxes:
            if loft.overlaps(fp, b):
                raise SystemExit("%s at (%.0f,%.0f) overlaps existing geometry" % (p["level"], p["x"], p["z"]))
        boxes.append(fp)


def staff_points(levels, name, p, walls=None):
    """[(x,y,z)] for one building's two bodies, in WORLD space.

    One at the room centre of the FIRST wing on the ground storey (a player walking in through the
    colonnade meets it), one at the LAST wing -- on the TOP storey when the building has more than one,
    so the library's second body is upstairs and climbing it is the fight.

    Rooms are located through the building's own blueprint rather than by fractions of the footprint: a
    fraction of a WIDE municipal building lands in a corridor or inside a wall, while a wing's room
    centre is somewhere a body can stand by construction. The wing's own rotation is applied to the room
    offset before the building's, which is the order flatten nests them in.

    Even so a room is mostly FURNITURE -- the clinic's magistrate's court puts gallery benches across the
    spot the first version of this function chose, and that body stood inside a bench (caught by the
    clearance assertion below it). So each body tries a short, ordered list of spots in its room and takes
    the first one that is actually clear, which stays deterministic: the list is fixed, so the export is
    still idempotent.
    """
    spec = BUILDING_SPECS[name]
    rot = int(p["rot_y"])
    wings = set(WING_IDS.values())
    top = len(spec["storeys"]) - 1
    if walls is None:
        walls = flatten_building(levels, name, p["x"], p["y"], p["z"], rot)
    # off the chest that owns the middle of the room, then progressively further round it
    spots = [(4.5, 3.5), (4.5, -3.5), (-4.5, 3.5), (-4.5, -3.5),
             (6.5, 1.0), (-6.5, -1.0), (1.0, 6.5), (-1.0, -6.5)]
    out = []
    for storey_idx, order in ((0, spots), (top, spots[2:] + spots[:2])):
        refs = [o for o in spec["storeys"][storey_idx] if o["ref_level_id"] in wings]
        if not refs:
            raise SystemExit("%s storey %d has no room to staff" % (name, storey_idx))
        refs.sort(key=lambda o: (o["z"], o["x"]))
        w = refs[0] if storey_idx == 0 else refs[-1]
        for ox, oz in order:
            rx, rz, _, _ = loft.rotate_y90(BAY_W + ox, oz, 0.0, 0.0, int(w.get("rot_y", 0) or 0))
            pt = place_point(levels, name, w["x"] + rx, w["y"] + storey_idx * STOREY + 1.05,
                             w["z"] + rz, p["x"], p["y"], p["z"], rot)
            if point_clear(walls, pt):
                out.append(pt)
                break
        else:
            raise SystemExit("%s storey %d has nowhere clear to stand a body" % (name, storey_idx))
    return out


def point_clear(walls, p, shrink=0.2):
    """False if a point is inside a wall box. 'On the deck' is not enough in a room full of columns,
    benches and records stacks -- a body authored into one of those never gets a chance to move."""
    x, y, z = p
    for w in walls:
        if (abs(x - w["x"]) < w["sx"] / 2.0 - shrink
                and abs(z - w["z"]) < w["sz"] / 2.0 - shrink
                and abs(y - w["y"]) < w["sy"] / 2.0 - shrink):
            return False
    return True


def municipal_footprint_boxes(placements, levels, margin=12.0):
    """The ground each civic building covers, world-scaled and expanded a little -- the district's half
    of the discriminator the loft tool uses to leave other people's zombies alone."""
    boxes = []
    for p in placements:
        fp = loft.footprint(flatten_building(levels, p["level"], p["x"], p["y"], p["z"],
                                             int(p["rot_y"])))
        boxes.append((fp[0] - margin, fp[1] - margin, fp[2] + margin, fp[3] + margin))
    return boxes


def strip_characters(city, recorded, boxes):
    """Remove what this tool staffed last time, and nothing else.

    By recorded value first (that survives a relocation), then by SHAPE **and position**: the district's
    staff is a bare zombie -- no name, role 7, archetype 2, the archetype's own kit/faction/orders --
    which is the same shape the lofts author, so shape alone would let either tool delete the other's
    population. Position is the discriminator: a bare zombie standing in a municipal footprint is this
    tool's, and anywhere else it is not.
    """
    if recorded:
        rec = {json.dumps(e, sort_keys=True) for e in recorded}
        city["characters"] = [e for e in city.get("characters", [])
                              if json.dumps(e, sort_keys=True) not in rec]

    def is_bare(e):
        return (e.get("name") in (None, "")
                and e.get("role") == 7
                and e.get("archetype") in (None, 2)
                and e.get("kit", 0) == 0 and e.get("faction", -1) == -1 and e.get("orders", -1) == -1)

    def inside(e):
        if boxes is None:
            return False    # no placements file to locate them by: never strip by shape alone
        x, z = float(e.get("x", 0.0)), float(e.get("z", 0.0))
        return any(b[0] <= x <= b[2] and b[1] <= z <= b[3] for b in boxes)

    city["characters"] = [e for e in city.get("characters", []) if not (is_bare(e) and inside(e))]


def strip_chests(city, recorded):
    if not recorded:
        return
    rec = {json.dumps(e, sort_keys=True) for e in recorded}
    city["chests"] = [e for e in city.get("chests", []) if json.dumps(e, sort_keys=True) not in rec]


def write_authored(force):
    os.makedirs(MUN_DIR, exist_ok=True)
    written = 0
    for name, lv in AUTHORED.items():
        path = os.path.join(MUN_DIR, name + ".json")
        if os.path.exists(path) and not force:
            continue
        with open(path, "w") as f:
            json.dump(lv, f, indent=1)
            f.write("\n")
        written += 1
    with open(MANIFEST, "w") as f:
        json.dump({"levels": [{"id": AUTHORED[n]["id"], "file": n + ".json"} for n in AUTHORED]}, f, indent=1)
        f.write("\n")
    return written


def build(city_path=CITY, placements_path=PLACEMENTS, write=True):
    levels = load_tree()
    city = json.load(open(city_path))

    # idempotent: remove exactly what this tool wrote last time. Two markers for the same reason the
    # loft tool needs two -- a re-save from EDIT MAP (F6) drops no `name` here, but a level that was
    # saved by an older export still has to be matched by geometry.
    prev = city.pop("municipal_city", {}) or {}
    recorded = {tuple(round(float(v), 3) for v in e) for e in prev.get("walls", [])}
    prev_chests = prev.get("chests", [])
    prev_characters = prev.get("characters", [])

    if os.path.exists(placements_path):
        placements = json.load(open(placements_path)).get("buildings", [])
        for p in placements:
            for w in flatten_building(levels, p["level"], p["x"], p["y"], p["z"], int(p["rot_y"])):
                recorded.add((round(w["x"], 3), round(w["y"], 3), round(w["z"], 3),
                              round(w["sx"], 3), round(w["sy"], 3), round(w["sz"], 3)))
    else:
        placements = None

    def is_ours(w):
        if str(w.get("name", "")).startswith("municipal_"):
            return True
        return (round(w["x"], 3), round(w["y"], 3), round(w["z"], 3),
                round(w["sx"], 3), round(w["sy"], 3), round(w["sz"], 3)) in recorded

    city["walls"] = [w for w in city["walls"] if not is_ours(w)]
    strip_chests(city, prev_chests)
    strip_characters(city, prev_characters,
                     municipal_footprint_boxes(placements, levels) if placements else None)

    # materials: the district owns its six, by name, so a loft rebuild cannot drop them and a second run
    # cannot duplicate them.
    have = {m.get("name") for m in city.get("materials", [])}
    for name, (shader, spec, shine, fric, dens) in MATERIALS.items():
        if name not in have:
            city.setdefault("materials", []).append(
                {"name": name, "shader_name": shader, "specular": spec, "shininess": shine,
                 "friction": fric, "density": dens})

    if placements is None:
        placements = choose_placements(city, levels)
        if write:
            with open(placements_path, "w") as f:
                json.dump({"buildings": placements}, f, indent=1)
                f.write("\n")

    groups, chests, npcs, stats = [], [], [], []
    for p in placements:
        name = p["level"]
        placed = []
        for w in flatten_building(levels, name, p["x"], p["y"], p["z"], int(p["rot_y"])):
            nw = dict(w)
            nw["name"] = "municipal_%s_%d" % (name[len("mun_"):], len(placed) + 1)
            placed.append(nw)
        groups.append(placed)
        stats.append((name, len(placed), loft.footprint(placed)))

        # the chest goes in the FIRST wing's room, at the room's own authored centre -- computed through
        # the same rotate-then-translate as every wall above, so it cannot end up in the corridor or in
        # the air outside a rotated building.
        cx, cy, cz = place_point(levels, name, BAY_W, 1.05, 0.0, p["x"], p["y"], p["z"], int(p["rot_y"]))
        chests.append({"tier": BUILDING_LOOT[name], "x": cx, "y": cy, "z": cz})

        # ...and its staff: the district's share of the city's simultaneous slots, two per building,
        # one body per job tuple in this building's own entry in BUILDING_STAFF (the courthouse's two
        # are a bailiff and the civic boss).
        jobs = BUILDING_STAFF[name]
        for (sx, sy, sz), (archetype, role, kit, faction, orders) in zip(
                staff_points(levels, name, p), jobs):
            npcs.append({"role": role, "kit": kit, "archetype": archetype, "faction": faction,
                         "orders": orders, "x": sx, "y": sy, "z": sz})

    verify_placements(city["walls"], placements, levels)

    for g in groups:
        city["walls"].extend(g)
    city.setdefault("chests", []).extend(chests)
    city.setdefault("characters", []).extend(npcs)
    city["municipal_city"] = {
        "buildings": placements,
        "walls": [[w["x"], w["y"], w["z"], w["sx"], w["sy"], w["sz"]] for g in groups for w in g],
        "chests": chests,
        "characters": npcs,
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
    for name in BUILDINGS:
        walls = flatten_building(levels, name)
        if not walls:
            raise SystemExit("%s flattens to nothing" % name)
        mats = {w.get("material") for w in walls}
        bad = mats - KNOWN_MATERIALS
        if bad:
            raise SystemExit("%s uses unknown materials %s" % (name, bad))
        # the engine's own carveability rule (brick_world.h): a carveable box is thin enough not to be a
        # slab and not rotated. A civic building with nothing destructible is not this district.
        carveable = sum(1 for w in walls
                        if w.get("material") in ("tile", "plaster", "marble", "wood_oak", "wood_mahogany",
                                                "wood_pine", "brick", "concrete", "glass")
                        and not w.get("ramp") and not w.get("rot_y")
                        and not (w["sy"] <= 12.0 and min(w["sx"], w["sz"]) >= 20.0))
        if carveable == 0:
            raise SystemExit("%s has nothing destructible in it" % name)
        storeys = len(BUILDING_SPECS[name]["storeys"])
        ramps = [w for w in walls if w.get("ramp")]
        if storeys > 1 and not ramps:
            raise SystemExit("%s is %d storeys tall with no stairs" % (name, storeys))
        for r in ramps:
            if r["sy"] <= 0.0 or r["sz"] <= 0.0:
                raise SystemExit("%s has a ramp with no rise or no run" % name)
        key = tuple(sorted((round(w["x"], 2), round(w["y"], 2), round(w["z"], 2),
                            round(w["sx"], 2), round(w["sy"], 2), round(w["sz"], 2)) for w in walls))
        if key in seen:
            raise SystemExit("%s and %s flatten to the same building" % (seen[key], name))
        seen[key] = name
        # the district's own material story: every building is tiled underfoot and plastered over. Which
        # woods it is made of is a property of the district rather than of each room -- the precinct is
        # brick and steel on purpose -- so that is asserted once below.
        for want in ("tile", "plaster"):
            if want not in mats:
                raise SystemExit("%s has no %s in it" % (name, want))
        print("ok  %-18s %3d walls, %2d carveable, %d storey(s)" % (name, len(walls), carveable, storeys))

    city, stats = build(write=False)
    ids = [w["id"] for w in city["walls"]]
    if len(ids) != len(set(ids)):
        raise SystemExit("duplicate wall ids in the exported city")
    # LEVEL_BOXES_MAX 2048 is a TRUNCATION at load, not an error, so the cap is checked here.
    if len(city["walls"]) > 2048:
        raise SystemExit("city has %d walls, over LEVEL_BOXES_MAX 2048" % len(city["walls"]))
    if len(city.get("chests", [])) > 32:
        raise SystemExit("city authors %d chests, over LEVEL_BOXES_MAX_CHESTS 32"
                         % len(city.get("chests", [])))
    authored = city.get("municipal_city", {})
    if len(authored.get("buildings", [])) != len(BUILDINGS):
        raise SystemExit("expected %d buildings, found %d"
                         % (len(BUILDINGS), len(authored.get("buildings", []))))
    if len(authored.get("chests", [])) != len(BUILDINGS):
        raise SystemExit("expected one chest per building, found %d" % len(authored.get("chests", [])))
    # Staffing: two bodies per building, standing in ITS OWN rooms rather than in a wall. A civic
    # building is mostly furniture, so "on the deck" is not enough.
    civic = authored.get("characters", [])
    if len(civic) != MUNICIPAL_NPC_BUDGET:
        raise SystemExit("expected %d civic staff (two per building), found %d"
                         % (MUNICIPAL_NPC_BUDGET, len(civic)))
    # Distinct jobs, not twelve copies: each body carries its own (archetype, role, kit), and every job
    # in the table is different -- seven of them, because the courthouse's pair is a bailiff and the
    # boss. Six buildings, seven jobs: the boss costs a body, not an extra slot.
    expected = {job for jobs in BUILDING_STAFF.values() for job in jobs}
    for name, jobs in BUILDING_STAFF.items():
        if len(jobs) != 2:
            raise SystemExit("%s staffs %d bodies, but its room plan has two" % (name, len(jobs)))
    jobs = {(c["archetype"], c["role"], c["kit"]) for c in civic}
    roles = {c["role"] for c in civic}
    kits = {c["kit"] for c in civic}
    if len(jobs) != len(expected) or len(roles) != len(expected):
        raise SystemExit("the district staffs %d jobs / %d roles, not the table's own %d distinct jobs"
                         % (len(jobs), len(roles), len(expected)))
    # Six kits exist for seven jobs, so one necessarily repeats: this is a ceiling, not an equality.
    if len(kits) > AI_KIT_COUNT:
        raise SystemExit("the district authors %d distinct kits -- only %d exist"
                         % (len(kits), AI_KIT_COUNT))
    for c in civic:
        if not 0 <= c["role"] <= AI_ROLE_MAGISTRATE:
            raise SystemExit("civic body authored role %r, outside AIRole 0..%d"
                             % (c["role"], AI_ROLE_MAGISTRATE))
        if not 0 <= c["kit"] <= 5:
            raise SystemExit("civic body authored kit %r, outside AIKit 0..5" % (c["kit"],))
        if c["faction"] != 2:
            raise SystemExit("civic body at (%r,%r) is faction %r -- the district's staff are hostile"
                             % (c["x"], c["z"], c["faction"]))
        if not (c["orders"] & 4):
            raise SystemExit("civic body at (%r,%r) does not accept ENGAGE -- a clerk that cannot fight"
                             % (c["x"], c["z"]))
    # The civic boss: exactly one, and it stands in the courthouse. The characters record is written two
    # bodies per placement in placements order, so a boss authored into the clinic is caught here
    # rather than by a player walking into the wrong building looking for it.
    bosses = [i for i, c in enumerate(civic) if c["role"] == AI_ROLE_MAGISTRATE]
    if len(bosses) != 1:
        raise SystemExit("the district authors %d magistrates -- one civic boss, in the courthouse"
                         % len(bosses))
    boss_home = authored["buildings"][bosses[0] // 2]["level"]
    if boss_home != "mun_courthouse":
        raise SystemExit("the civic boss stands in %s -- it belongs in the courthouse" % boss_home)
    for p in authored["buildings"]:
        walls = flatten_building(levels, p["level"], p["x"], p["y"], p["z"], int(p["rot_y"]))
        fp = loft.footprint(walls)
        for pt in staff_points(levels, p["level"], p, walls):
            if not (fp[0] <= pt[0] <= fp[2] and fp[1] <= pt[2] <= fp[3]):
                raise SystemExit("%s has staff standing outside the building at %r" % (p["level"], pt))
            if not point_clear(walls, pt):
                raise SystemExit("%s has staff standing inside a wall at %r" % (p["level"], pt))
    # ...and the SHARED budget: the lofts and this district split the engine's simultaneous slots, so
    # the total (not just this tool's half) is what has to fit.
    loft_plan = loft.population_plan(json.load(open(loft.PLACEMENTS)).get("buildings", []))
    total = len(city.get("characters", []))
    if total != len(loft_plan) + len(civic):
        raise SystemExit("the city carries %d characters but the loft plan authors %d and the district %d"
                         % (total, len(loft_plan), len(civic)))
    if total > loft.NPC_BUDGET:
        raise SystemExit("the city authors %d characters, over the %d simultaneous slots"
                         % (total, loft.NPC_BUDGET))
    # the three woods really are three different masses, or "multiple woods with different densities" is
    # a comment rather than a fact.
    dens = {name: spec[4] for name, spec in MATERIALS.items()}
    if not dens["wood_oak"] > dens["wood_mahogany"] > dens["wood_pine"]:
        raise SystemExit("the wood densities are not distinct: %r" % dens)
    district = {w.get("material") for name in BUILDINGS for w in flatten_building(levels, name)}
    for want in ("marble", "wood_oak", "wood_mahogany", "wood_pine"):
        if want not in district:
            raise SystemExit("no municipal building uses %r -- the district is meant to" % want)
    mats = {m.get("name") for m in city.get("materials", [])}
    for name in MATERIALS:
        if name not in mats:
            raise SystemExit("the city does not carry the %r material" % name)
    if len(city.get("materials", [])) > 16:
        raise SystemExit("city carries %d materials, over LEVEL_BOXES_MAX_MATERIALS 16"
                         % len(city.get("materials", [])))
    # idempotent: a second export over the first has to be byte-identical or every rebuild stacks a
    # second district on top of the first.
    first = json.dumps(city, sort_keys=True)
    second, _ = build(write=False)
    if json.dumps(second, sort_keys=True) != first:
        raise SystemExit("the export is not idempotent -- a rebuild changed the city")
    for name, n, fp in stats:
        print("ok  %-18s placed %3d walls at x=%.0f..%.0f z=%.0f..%.0f" % (name, n, fp[0], fp[2], fp[1], fp[3]))
    print("ok  city: %d walls, %d materials, %d chests, %d characters"
          % (len(city["walls"]), len(city.get("materials", [])), len(city.get("chests", [])),
             len(city.get("characters", []))))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--author", action="store_true", help="rewrite the authored tree from the blueprint")
    ap.add_argument("--replace-placements", action="store_true")
    args = ap.parse_args()
    if args.replace_placements and os.path.exists(PLACEMENTS):
        os.remove(PLACEMENTS)
        print("placements: cleared, choosing civic ground again")
    if args.author:
        n = write_authored(force=True)
        print("authored %d level file(s) under levels/municipal" % n)
        return 0
    if not os.path.exists(MANIFEST):
        write_authored(force=False)
    if args.check:
        return check()
    city, stats = build()
    print("wrote %s: %d walls, %d materials"
          % (os.path.relpath(CITY, ROOT), len(city["walls"]), len(city.get("materials", []))))
    for name, n, fp in stats:
        print("  %-18s %3d walls  x=%7.1f..%-7.1f z=%7.1f..%-7.1f"
              % (name, n, fp[0], fp[2], fp[1], fp[3]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
