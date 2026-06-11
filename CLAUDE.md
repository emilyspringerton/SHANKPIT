# SHANKPIT — UDP FPS + DragonsNShit Persistent World

SHANKPIT is a server-authoritative fast-paced FPS with a persistent world backend. The FPS
game runs on SHANKPIT. The persistent world (DragonsNShit) runs on Dragonfly (Minecraft
Bedrock Protocol fork) as the backend. Both share entity state, season lineage, and world events.

## North Star

`docs2/NORTHSTAR.md` — where the system is going and what "done" means at each layer.

## Architecture

```
SHANKPIT FPS (UDP game server)
  ↕ entity sync
DragonsNShit / Dragonfly (Bedrock Protocol, Go)
  ↕ world state, zone evolution, season lineage
Persistent World Database
```

## Key Concepts

- **Season lineage** — world history carries forward across resets; old civilizations leave ruins
- **Zone evolution** — zones change state based on player activity and time
- **BedWars** — mini-game layer inside the persistent world
- **SHANKPIT↔MPT bridge** — TYLER generates episode scripts; MPT compiles them to video

## Related Repos

- `TYLER` — game narrative + episode scripts
- `MoneyPrinterTurbo` — flat stream video compilation for TYLER episodes
- `EMILY` — Emily Prime (RSI loop drives SHANKPIT development tasks)

## Coding Conventions

- Run `go test ./...` before committing
- Update `CHANGELOG.md` with a dated entry for any meaningful change
- Document northstar implications before adding new systems
