# Story System Phase 1 -- example door script

A real, working example of the `door-tick` contract from `docs/STORY_SYSTEM_NORTHSTAR.md` Part 2.
Opens when a player gets within 3 units, closes once they're more than 5 units away (the 3-5 gap
is deliberate hysteresis so the door doesn't flicker open/closed right at one fixed threshold).

Compile it the same two real steps `internal/nock/procgen.go`'s Java-target texture pipeline
already automates for its own target (this is the C target instead):

```bash
parena build door_tick.prn -o door_tick.c
gcc -shared -fPIC -o door_tick.so door_tick.c $PARENA_REPO/runtime/parena_runtime.c -lm
```

Reference it from a level's own `doors` array (`box_index` + `script_path`, see
`packages/world/level_boxes.h`'s own `LevelDoor` doc comment):

```json
"doors": [
  { "box_index": 0, "script_path": "/absolute/path/to/door_tick.so" }
]
```
