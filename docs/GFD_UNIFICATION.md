# Unifying GoblinFoxDragon with SHANKPIT — plan, and what phase 1 actually shipped

The founder: *"we need to start unifying gfd and shankpit archipelligo ... shankpit is the frontend for the
MMO ... GFD is a fork of SHANKPIT maybe its time to unfork it and just merge GFD all of it needs to be
brought into shankpit and the shankpit engine improvements that are already in place dont get overwritten
... including bringing in the GFD exclusive levels ... all mods based PARENA based."*

GFD has been **cloned and read** (`../GoblinFoxDragon`, not guessed at). What is actually in it:

| Path | What it is |
|---|---|
| `apps/mud`, `apps2/mud` | the text MUD / state engine (jobs, combat text, progression) — **no spatial data at all** |
| `apps2/battlegrounds_gui`, `apps/lobby` | a 3D client, the fork of SHANKPIT's lobby |
| `packages/world/town_map.c` + `generated/town_expansion_blocks.inl` | **Town**: 17 named buildings + expansion blocks as x/z/w/d/h seeds, scale 3.0 — real geometry |
| `data/crystal_seed_meadow.json` | **Meadow**: a 44×44 goblin *simulation* seed (goblins, kinds, grid) — **not** geometry |
| `server/`, `server-go/`, `services/` | Go services (field office, etc.) |

## The rule that protects SHANKPIT

**Nothing from GFD overwrites a SHANKPIT file.** GFD contributed data and ideas; the engine
(XPBD `grb.c`, `structure.h`, `brick_*`, lighting, vehicles, widgets) is untouched. Where GFD had a
parallel implementation, SHANKPIT's is the one that runs and GFD's content is imported *into* its schema.

## Phases

**Phase 1 — shipped (this change).**
- `zone_rules.prn`: the shared-zone lifecycle (900 s grace, 8 zones) and the dungeon layout numbers, all
  PARENA, with identical C defaults.
- `zone_registry.h`: the non-instanced zone table — first entrant generates, later entrants join, expiry
  after fifteen empty minutes, re-entry inside the window keeps the layout.
- `proc_dungeon.h`: one seed → one `CustomLevelData`, byte-identical on every machine, saved as real level
  JSON, with a gate that is a real lever → relay → machine circuit.
- `scripts/import_gfd_levels.py`: Town → `var/maps/gfd_town.json` (boxes, perimeter, spawners). Meadow is
  reported as a sim seed, not a level, rather than fabricated.
- Reachable: **F7** in game and `--dungeon [zone]` on the command line.**Phase 2 — shipped.** `PACKET_ZONE_STATE` (id, seed, players, state, idle_ms) plus
`packages/simulation/zone_sync.h`: the server hosts a zone (`--dungeon [zone]`) and broadcasts once a
second; a client decodes the packet, and reloads geometry **only** when the zone id or the seed actually
changed, so a 1 Hz repeat never drops decals or restarts the world. The wire carries identity, not
geometry — both sides run the same deterministic generator. `make test-zone-sync` covers the refusals
(short datagram, wrong type, unterminated id) and the no-op-on-repeat rule.

**Phase 3 — shipped (host side).** `./bin/shank_lobby --coop-host [zone]` hosts and joins in one command:
`packages/simulation/coop_host.c` starts the real `bin/shank_server` for that zone as a child process
(same fork/exec and `CreateProcessA` pattern as `lobby_launch_nock`), points the client at loopback, and
enters `MODE_QUEUE` — the host plays the *same* netcode an ally uses, not a local special case. An ally
joins by the host's LAN IP the ordinary way (`--host <ip>`). Single-player is that path with nobody else
in it, which is the Minecraft model the founder named.

Two decisions in there are load-bearing rather than convenience. First, the UDP port probe: a UDP port
has no `accept`, so `coop_host_port_busy` binds it briefly and lets go. That is what makes **relaunching
attach** to a world that is still up instead of spawning a second server that cannot bind and dies.
Second, zone ids are validated at the flag (`[A-Za-z0-9_-]`, not starting with `-`) because the id is a
real filename component (`var/maps/proc_<zone>.json`) *and* an argv element.

Still missing from phase 3, and named here rather than implied: **no LAN broadcast/discovery** (an ally
types the IP; nothing announces the world), and **no in-process server** — it is a child process, so
quitting the lobby leaves the world up until it is killed. That is deliberate for now (a hosted world
outliving one person's session is the point), but it is a policy that needs a decision, not an oversight.

`make test-coop-host` covers the flag parsing/refusals, the exact child argv, the port probe against a
really-bound socket, and — the part that matters — spawns the actual server, waits for the zone file it
writes, checks the port is held, checks a second start attaches instead of spawning, then stops it and
checks the port is released.

**Phase 4 — GFD state server.** Take the MUD's job/stat/inventory model, and **port its rules to PARENA**
(`stats_bridge.prn`: job → cooldown/haste/evasion/max HP, equipment → affinities). The C side consumes a
flat struct; it must not learn about 23 jobs. The dungeon's `difficulty` tier is the first input.
Persistence is the state server's job (name-keyed JSON saves), not the client's.

**Phase 5 — Meadow, properly.** Meadow has a *simulation* seed, not geometry. Turning it into a 3D zone
means either authoring it as a NOCK level or teaching `proc_dungeon.h` a second layout kind (outdoor
cells with heightfield sampling). Do that only when a scene needs it — inventing a conversion now would be
a fake import.

## Honest state of "multiplayer"

A LAN client of a `--dungeon` server learns the zone and rebuilds the same dungeon, and reloads when the
zone expires and regenerates. `--coop-host` now makes the host side one command too, on the same
`MODE_QUEUE` netcode the ally uses. What is **not** there yet: LAN broadcast/discovery (the ally needs the
host's IP, typed), a world that stops when the host quits (the server is a child process and outlives the
lobby), and an explicit "enter zone" action — on the server, occupancy is whoever is connected, so the zone
generates when the first client joins rather than when someone walks through a door. Those are the next
slices, not claims.
