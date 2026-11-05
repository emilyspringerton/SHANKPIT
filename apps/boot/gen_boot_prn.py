#!/usr/bin/env python3
"""Turn the level the fork boots into the constants boot.prn compiles against.

The level JSON is the source; this is the same build-time step the engine's own level loader does at
run time, done early so the boot program's numbers are the level's numbers rather than a guess. If a
token in the template has no counterpart here, that is a hard error -- a boot that silently defaults
is worse than one that fails.
"""
import json
import sys

src, level_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

with open(level_path) as f:
    level = json.load(f)

spawner = (level.get("spawners") or [{}])[0]
values = {
    "@LEVEL_NAME@": str(level.get("name", "unnamed")),
    "@LEVEL_WIDTH@": str(int(level.get("width", 0))),
    "@LEVEL_DEPTH@": str(int(level.get("depth", 0))),
    "@SPAWN_X@": str(int(spawner.get("x", 0))),
    "@SPAWN_Y@": str(int(spawner.get("y", 0))),
    "@SPAWN_Z@": str(int(spawner.get("z", 0))),
}

text = open(src).read()
missing = [t for t in values if t in text and not values[t]]
if missing:
    sys.exit("gen_boot_prn: empty level values for %s" % ", ".join(missing))
for token, value in values.items():
    text = text.replace(token, value)

leftover = [t for t in values if t in text]
if leftover:
    sys.exit("gen_boot_prn: token(s) left in output: %s" % ", ".join(leftover))

with open(out_path, "w") as f:
    f.write(text)
print("gen_boot_prn: %s -> %s (level %s, %dx%d)" % (level_path, out_path, values["@LEVEL_NAME@"], int(values["@LEVEL_WIDTH@"]), int(values["@LEVEL_DEPTH@"])))
