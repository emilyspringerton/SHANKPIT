#!/usr/bin/env python3
"""Pack roblox/src/*.lua into one .rbxmx model Studio can insert into the base place.

Why a model and not the place itself: `shankpit_roblox.rbxl` is a 300 KB BINARY place (128 classes, 3059
instances) and there is no rbx-dom/rojo toolchain on this box, so rewriting it in place would mean hand-writing
binary INST/PROP/PRNT chunks and hoping. A model is the honest unit here -- your place stays exactly as it is,
and the 460 layer arrives as one insertable object. Named limit, not a hidden one.

Usage: python3 roblox/tools/build_rbxmx.py [out.rbxmx]
"""
import os
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# (file, class, instance name) -- order is the folder's child order in Studio
PARTS = [
    ("net460.lua", "ModuleScript", "net460"),
    ("queue460.lua", "ModuleScript", "queue460"),
    ("ticket460.lua", "ModuleScript", "ticket460"),
    ("queue460_rules.lua", "ModuleScript", "queue460_rules"),
    ("ticket460_rules.lua", "ModuleScript", "ticket460_rules"),
    ("lobby.server.lua", "Script", "lobby"),
    ("lobby.client.lua", "LocalScript", "lobby_client"),
]

def main(argv):
    out_path = argv[1] if len(argv) > 1 else os.path.join(ROOT, "SHANKPIT_ROBLOX_460.rbxmx")

    root = ET.Element("roblox", {
        "xmlns:xmime": "http://www.w3.org/2005/05/xmlmime",
        "xmlns:xsi": "http://www.w3.org/2001/XMLSchema-instance",
        "xsi:noNamespaceSchemaLocation": "http://www.roblox.com/roblox.xsd",
        "version": "4",
    })
    ET.SubElement(root, "External").text = "null"
    ET.SubElement(root, "External").text = "nil"

    folder = ET.SubElement(root, "Item", {"class": "Folder", "referent": "RBX0"})
    props = ET.SubElement(folder, "Properties")
    ET.SubElement(props, "string", {"name": "Name"}).text = "SHANKPIT460"

    total = 0
    for i, (fname, cls, name) in enumerate(PARTS, start=1):
        path = os.path.join(SRC, fname)
        with open(path, "r", encoding="utf-8") as f:
            source = f.read()
        if "]]>" in source:
            raise SystemExit(f"{fname}: source contains ]]> and cannot be CDATA")
        item = ET.SubElement(folder, "Item", {"class": cls, "referent": f"RBX{i}"})
        p = ET.SubElement(item, "Properties")
        ET.SubElement(p, "string", {"name": "Name"}).text = name
        ET.SubElement(p, "ProtectedString", {"name": "Source"}).text = source
        if cls == "Script":
            # Disabled so the model can be inserted before the config is filled in without it running blind.
            ET.SubElement(p, "bool", {"name": "Disabled"}).text = "true"
        total += len(source)

    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(out_path, encoding="utf-8", xml_declaration=True)

    # Round-trip it: a model Studio cannot parse is worse than no model.
    reparsed = ET.parse(out_path)
    items = reparsed.getroot().findall(".//Item")
    if len(items) != len(PARTS) + 1:
        raise SystemExit(f"expected {len(PARTS) + 1} items, wrote {len(items)}")
    print(f"{os.path.relpath(out_path, ROOT)}: {len(items)} instances, {total} bytes of Luau")

if __name__ == "__main__":
    main(sys.argv)
