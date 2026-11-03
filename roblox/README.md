# SHANKPIT on Roblox — the 460 layer

`shankpit_roblox.rbxl` (repo root) is the base place. This directory is the **SHANKPIT-460 DNA** that sits beside
it: a lobby, a queue, real connect tickets, and a read-only board. Nothing here is NOOBTOPIA.

## Scope: narrow on purpose

| | NOOBTOPIA | this layer |
|---|---|---|
| What it is | the whole DEADWEIGHT game on Roblox — cards, decks, ladder, stage, tournament, noob life | the **esports front door** for SHANKPIT |
| Rules | Luau ports of `card_rules.prn`, `rating.prn`, `stage.prn` | **none** — it reads the world's numbers and mints tickets, it owns neither |
| Gameplay | played in Roblox | played on the C server; Roblox matches you and hands you a ticket |
| Persistence | per-server ladder | none — the world is the only writer |

| File | Class | Owns |
|---|---|---|
| `src/queue460.lua` | ModuleScript | casual vs ranked, the closest-pair search, seat bookkeeping |
| `src/ticket460.lua` | ModuleScript | reading a real IDUNA connect ticket: shape, expiry, claimed player. **Never** verifies one |
| `src/net460.lua` | ModuleScript | the published ladder: schema support, the ladder's own order, staleness, rows |
| `src/lobby.server.lua` | Script | minting, seating, the tick, the board push |
| `src/lobby.client.lua` | LocalScript | the screen: queue buttons, ladder, and the match prompt with its ticket |
| `src/queue460_rules.lua` | ModuleScript (**generated**) | the queue thresholds — `PARENA/stdlib/shankpit/queue_rules.prn` |
| `src/ticket460_rules.lua` | ModuleScript (**generated**) | the ticket shape/expiry — `PARENA/stdlib/shankpit/ticket_rules.prn` |

The hand-written modules are pure Lua — no `game`, no `workspace`, no HTTP, every clock passed in — which is why
they run under plain Lua on this box and not only in Studio.

## Rules live in PARENA

Nothing here restates a threshold. `PARENA/stdlib/shankpit/queue_rules.prn` holds the window (100 at join, +50 per
whole second, capped 600), the strict shorter-waiting comparison, the two-minute expiry and the casual-only 30 s
fill. `PARENA/stdlib/shankpit/ticket_rules.prn` holds the 72-character shape and the exact-second expiry. Both are
`I32`-in/`I32`-out and both are emitted to Luau:

```bash
make regen-roblox-rules   # after editing either .prn (needs a sibling ../PARENA)
```

Same one-source-two-targets rule NOOBTOPIA's `rating.prn`/`stage.prn` follow: if the Luau and the `.prn` ever
disagree, the `.prn` is right and the copy is the bug.

## The ticket is real, not invented

Checked against `IDUNA/internal/http/handlers/shankpit_ticket.go` rather than guessed:

```
POST /api/v1/shankpit/ticket        (Bearer IDUNA JWT)
  -> {"ticket": "<72 hex>", "expires_at": <unix s>, "player_id": "<uuid>"}

ticket = player_id (16 bytes, raw UUID) || expires_at (4 bytes, LITTLE-endian u32) || hmac_sha256(secret)[:16]
TTL    = 5 minutes — presented once, at PACKET_CONNECT
```

So `ticket460.parse` reads the UUID and the little-endian expiry out of the bytes themselves, and
`ticket460.from_mint` refuses an answer whose JSON fields **disagree** with the bytes IDUNA signed — two sources of
truth for one ticket must match, or the player is not sent anywhere.

What this layer will never do is verify the MAC: the secret is the C server's (`SHANKPIT_TICKET_SECRET`,
`shankpit-460/packages/common/hmac_sha256.h`), and a place every client can read is the last place a signing secret
belongs. A test greps the module's **code** (comments stripped) to keep it that way.

## The flow

1. A player authenticates with IDUNA out of band (device flow) and hands the place its **token**.
2. The server mints a ticket with that token as a Bearer and validates the answer (`ticket460.from_mint`).
3. The player takes a seat in casual or ranked (`queue460`), keyed by Roblox `UserId`.
4. A tick pairs bodies by the widening window; each side receives **its own** ticket, with the time it actually
   has left, in `SHANKPIT460_MatchFound`.
5. The board is re-read from the world every 5 s and pushed to `SHANKPIT460_Board`.

## Build and test

```bash
make regen-roblox-rules   # queue_rules.prn + ticket_rules.prn -> src/*_rules.lua
make test-roblox          # net460 / queue460 / ticket460 under plain Lua, plus luac -p on both scripts
make roblox-model         # -> roblox/SHANKPIT_ROBLOX_460.rbxmx
```

## Loading it into the base place

The base place is a **binary** `.rbxl` and there is no rbx-dom/rojo toolchain on this box, so nothing here rewrites
it — hand-writing binary INST/PROP/PRNT chunks to be "helpful" would be a good way to corrupt the one file hardest
to reconstruct.

1. Open `shankpit_roblox.rbxl` in Studio.
2. **Insert from file** → `roblox/SHANKPIT_ROBLOX_460.rbxmx` → it lands as a `SHANKPIT460` folder (8 instances).
3. Drag the **Script** (`lobby`) and the ModuleScripts into **ServerScriptService**.
4. Drag **`lobby_client` (a LocalScript) into StarterPlayerScripts** — leaving it beside the server Script is the
   mistake that makes a lobby look finished with nothing on screen.
5. Enable **HttpService** (Game Settings → Security), fill in `CONFIG` in `lobby.server.lua`, and un-tick
   **Disabled** on it.
6. Hand the server a player token: `_G.SHANKPIT460_IDUNA_TOKEN` on the client is what `lobby.client.lua` forwards
   today — replace that with whatever real login the place grows.

## Honest limits

- **Nothing has been opened in Studio.** No Studio and no rbx tooling on this box: the model is XML-parsed, the
  modules are Lua-tested, both scripts are `luac -p`-clean. No pixel has been seen.
- **There is no login screen.** A token must arrive from somewhere; the device flow is named, not built.
- **The mint call has never run against a live IDUNA.** The request shape is read off the handler, and the response
  is validated by `from_mint`, but no live round trip has happened here.
- **No Roblox `UserId` ↔ SHANKPIT pid binding exists**, so a player queues at the default rating (1000) and the
  board is read-only. Inventing a binding would put a made-up number on a screen.
- **Roblox cannot reach `127.0.0.1`** — `NetUrl`/`TicketUrl` must be public endpoints.
- **No gameplay.** A pair hands out a ticket; the arena stays the C server, which is what a 460 ticket is for.
