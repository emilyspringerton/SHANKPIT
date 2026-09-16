# Story System demo: a hallway with two real, differently-scripted doors

A real, runnable level showing Story System Phase 1 (`docs/STORY_SYSTEM_NORTHSTAR.md`, S459-81/
82) end to end: a straight hallway with two doors, each running a real, different compiled
PARENA script. Walk it yourself and watch both behaviors trigger live.

## Layout

```
spawn (z=2) --- door 1 (z=12) --- door 2 (z=26) --- end of hallway (z=40)
                proximity door    one-way door
```

- **Door 1** (`door_tick.prn`) is the same real hysteresis door from S459-81: opens once you're
  within 3 units, closes again once you're past 5 units. Walk up to it, it opens; back away, it
  closes behind you.
- **Door 2** (`one_way_door.prn`) is a real, *different* behavior on purpose, to show the
  contract is yours to define: opens once you're within 3.5 units and then **stays open
  forever** (sticky state) — the same real `(dist-to-player, state) -> new-state` two-input
  contract, just different logic inside it.

## Run it

```bash
cd examples/story-doors/demo_level
./build.sh                    # real parena build + gcc -shared, both scripts
cd ../../..                   # back to the SHANKPIT repo root
./bin/shank_server --level examples/story-doors/demo_level/level.json
```

Then connect with the real client (`bin/shank_lobby`) or any real UDP client speaking the wire
protocol (see `scripts/rl_env_packet.py`'s own `PacketClient` for a minimal example) and walk
forward down the hallway. Both doors will visibly let you through — server-side collision only in
this v0 pass, so you won't see the door itself move (yet — see `STORY_SYSTEM_NORTHSTAR.md`'s own
"what still doesn't exist" note), but you'll walk straight through where a solid wall was.

## What to change to build your own

- **Different door logic**: edit either `.prn` file — the only real contract is
  `(defn door-tick [(dist-to-player : F64) (state : F64)] : F64 ...)`, everything else is up to
  you (`examples/story-doors/door_tick.prn`'s own comment shows the hysteresis pattern; this
  folder's `one_way_door.prn` shows a sticky-state pattern).
- **More doors**: add more entries to `level.json`'s own `walls` array (one box per door) and a
  matching entry in `doors` (`box_index` pointing at that wall's own index, `script_path` to your
  compiled `.so`).
- **NOCK-hosted scripts instead of local files**: `doors` entries can use `script_url` instead of
  `script_path` — point it at `https://<your-iduna>/api/v1/nock-door-scripts/:id/download` (see
  `IDUNA/internal/nock/door_script_compile.go`) after creating the script through NOCK's own
  `POST /admin/nock/api/door-scripts` API, and the server downloads + caches it automatically at
  level load instead of needing a local `.so` at all.
