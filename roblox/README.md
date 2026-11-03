# SHANKPIT on Roblox — the 460 layer

`shankpit_roblox.rbxl` (repo root) is the base place. This directory is the **SHANKPIT-460 DNA** that sits
beside it: a lobby, a queue, ticket handling and a read-only board. Nothing here is NOOBTOPIA.

## Scope: narrow on purpose

| | NOOBTOPIA | this layer |
|---|---|---|
| What it is | the whole DEADWEIGHT game on Roblox — cards, decks, ladder, stage, tournament, noob life | the **esports front door** for SHANKPIT |
| Rules | Luau ports of `card_rules.prn`, `rating.prn`, `stage.prn` | **none** — it reads the world's published numbers, it does not own any |
| Gameplay | played in Roblox | played on the C server; Roblox matches you and hands you a ticket |
| Persistence | per-server ladder | none — the world is the only writer |

Four modules, and the split is what keeps it testable:

| File | Class | Owns |
|---|---|---|
| `src/queue460.lua` | ModuleScript | the queue: casual vs ranked, the widening rating window, deterministic pairing, expiry, who may bot-fill |
| `src/ticket460.lua` | ModuleScript | a ticket's **shape** only. The verdict is IDUNA's |
| `src/net460.lua` | ModuleScript | reading the published ladder: schema support, ranking, staleness, rows |
| `src/lobby.server.lua` | Script | the glue that needs a running server: `Players`, `HttpService`, `RemoteEvent`s, the loop |
| `src/queue460_rules.lua` | ModuleScript (**generated**) | the thresholds themselves — emitted from `PARENA/stdlib/shankpit/queue_rules.prn`; do not hand-edit |

The first three are pure Lua — no `game`, no `workspace`, no HTTP, every clock passed in — which is why they run
under plain Lua on this box and not only in Studio.

## Rules live in PARENA

The queue's arithmetic is not Luau. `PARENA/stdlib/shankpit/queue_rules.prn` holds the window (100 at join, +50
per whole second, capped 600), the strict shorter-waiting comparison, the two-minute expiry and the
casual-only 30 s fill — typed `I32` in, `I32` out — and `roblox/scripts/gen_queue460.sh` emits it to
`src/queue460_rules.lua`. `src/queue460.lua` keeps only the lists, the closest-pair search and the bookkeeping.

```bash
make regen-roblox-rules   # after editing the .prn (needs a sibling ../PARENA)
```

Same one-source-two-targets rule NOOBTOPIA's `rating.prn`/`stage.prn` follow: if the Luau and the `.prn` ever
disagree, the `.prn` is right and the copy is the bug.

## The 460 rules that are actually implemented

- **Casual vs ranked.** Two lines. A body may stand in both.
- **The window widens with waiting** — 100 rating points at join, +50 per whole second, capped at 600. A pair is
  legal only when the gap fits the window of the **shorter-waiting** body, so a newcomer cannot drag a patient
  body into a lopsided game it did not wait for.
- **Casual may fill with citizens after 30 s; ranked never does.** A fillable body **stays queued** — a human who
  arrives first still wins. A rating moved against a bot would be a lie about a person.
- **Two minutes in line releases the seat.**
- **A ticket is not believed locally.** The place checks the shape (`s460:<pid>:<hex signature>`) so garbage
  costs no round trip, then asks IDUNA. There is no signing secret and no HMAC in a place any client can read —
  if one appears in `ticket460.lua`, that file has failed at its only job.
- **The board is the world's.** Rating desc / wins desc / pid asc — the ladder's own order, the one WOTAN's page
  and `dw_stage`'s bracket already use. The snapshot ships no pre-sorted array on purpose, so sorting here is
  not a second opinion; inventing a tie-break would be.

## Build and test

```bash
make regen-roblox-rules   # PARENA/stdlib/shankpit/queue_rules.prn -> src/queue460_rules.lua
make test-roblox          # the three pure modules, under plain Lua (no Studio, no game)
make roblox-model         # -> roblox/SHANKPIT_ROBLOX_460.rbxmx
```

## Loading it into the base place

The base place is a **binary** `.rbxl` and there is no rbx-dom/rojo toolchain on this box, so nothing here
rewrites it — hand-writing binary INST/PROP/PRNT chunks to be "helpful" would be a way to corrupt the one file
that is hardest to reconstruct.

1. Open `shankpit_roblox.rbxl` in Studio.
2. **Insert from file** → `roblox/SHANKPIT_ROBLOX_460.rbxmx` → it lands as a `SHANKPIT460` folder.
3. Drag that folder into **ServerScriptService** (the Script and its three ModuleScript siblings must stay
   together).
4. Enable **HttpService** (Game Settings → Security).
5. Fill in `CONFIG` in `lobby.server.lua` and un-tick `Disabled` on the script:
   - `NetUrl` — the world's **public** netd endpoint, e.g. `https://okemily.com/api/v1/shankpit-net/deadweight`.
   - `TicketUrl` — IDUNA's connect-ticket mint. Left empty, the lobby **refuses everyone** rather than inventing
     identities; that is the only safe default for a place anyone can join.

## Honest limits

- **Roblox cannot reach `127.0.0.1`.** `NetUrl` has to be the public endpoint in front of
  `tools/shankpit_netd.c`, not the local daemon a dev box runs. The board will read "stale" until it is.
- **No gameplay here.** A pairing fires `SHANKPIT460_MatchFound` with the opponent and the gap; the arena is
  still the C server, which is what a 460 ticket is for.
- **The ticket round trip is unverified against a live IDUNA** — the request is an opaque `POST` and only
  `ok`/`pid`/`name`/`ticket` are read. Named as the next piece rather than guessed at.
- **Nothing has been opened in Studio.** This box has no Roblox Studio and no rbx tooling, so the model is
  XML-parsed and the modules are Lua-tested, but no pixel of it has been seen.
- **`os.clock()` is the loop's clock**, which is monotonic and per-server; that is right for waiting in line and
  wrong for "when did that match happen", which is the world's timestamp and not this server's.
