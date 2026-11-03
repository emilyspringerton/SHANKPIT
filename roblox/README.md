# SHANKPIT on Roblox — the 460 layer

`shankpit_roblox.rbxl` (repo root) is the base place. This directory is the **SHANKPIT-460 DNA** that sits beside
it: a real sign-in, a queue, real connect tickets, and a read-only board. Nothing here is NOOBTOPIA.

## Scope: narrow on purpose

| | NOOBTOPIA | this layer |
|---|---|---|
| What it is | the whole DEADWEIGHT game on Roblox — cards, decks, ladder, stage, tournament, noob life | the **esports front door** for SHANKPIT |
| Rules | Luau ports of `card_rules.prn`, `rating.prn`, `stage.prn` | the queue/ticket thresholds only (from PARENA); it owns no rating |
| Gameplay | played in Roblox | played on the C server; Roblox matches you and hands you a ticket |
| Persistence | per-server ladder | identity only (DataStore: player_id + guest secret). No ladder of its own |

| File | Class | Owns |
|---|---|---|
| `src/login460.lua` | ModuleScript | a real IDUNA identity: guest-register, guest-login, token freshness, what may be persisted |
| `src/ticket460.lua` | ModuleScript | reading a real IDUNA connect ticket: shape, expiry, claimed player. **Never** verifies one |
| `src/queue460.lua` | ModuleScript | casual vs ranked, the closest-pair search, seat bookkeeping |
| `src/net460.lua` | ModuleScript | the published ladder: schema support, the ladder's own order, staleness, rows |
| `src/lobby.server.lua` | Script | sign-in, minting, seating, the tick, the board push |
| `src/lobby.client.lua` | LocalScript | the screen: SIGN IN, queue buttons, ladder, the match prompt with its ticket |
| `src/{queue460,ticket460}_rules.lua` | ModuleScript (**generated**) | the thresholds — `PARENA/stdlib/shankpit/{queue_rules,ticket_rules}.prn` |

The hand-written modules are pure Lua — no `game`, no `workspace`, an injected HTTP transport and an injected
clock — which is why they run under plain Lua on this box and not only in Studio.

## Sign-in is real, and it is not OAuth

A Roblox place cannot run Google OAuth (no browser tab, no redirect back) and must never ask for an IDUNA
password. IDUNA's generic **game accounts** are the answer, and this change gives SHANKPIT its own registry row
(`shankpit`) for them:

```
POST https://iam.okemily.com/api/v1/games/shankpit/guest-register   {"display_name":"..."}
  -> {"player_id":"<uuid>", "guest_secret":"<64 hex>", "token":"<ES256 JWT>", "expires_at":<unix>}
POST .../guest-login   {"player_id","guest_secret"}                 -> the same, without a new secret
```

A real `players` row, a real ES256 token. **What a guest account is, said once:** no email, no recovery — the
secret *is* the account (the founder's own DEADWEIGHT convention). The place stores `player_id` + `guest_secret`
per Roblox `UserId` in DataStore (best-effort, pcall-guarded, the token is never stored) and signs back in on
return; a saved secret IDUNA refuses is reported, not silently replaced with a second account.

**Found and fixed while wiring this:** IDUNA's game player token has `sub="guest:<uuid>"` and carries the identity
in its `player_id` claim. `/api/v1/shankpit/ticket` read only `sub` and did `uuid.Parse(sub)`, so the one login a
Roblox player can perform minted a token that endpoint then refused (`400 token subject is not a player id`). The
handler now resolves `player_id` first and falls back to a UUID — bare or provider-prefixed — subject, still
refusing `local:<uid>` accounts and agent names.

## The ticket is real, not invented

```
POST /api/v1/shankpit/ticket        (Bearer IDUNA player token)
  -> {"ticket": "<72 hex>", "expires_at": <unix s>, "player_id": "<uuid>"}

ticket = player_id (16 bytes, raw UUID) || expires_at (4 bytes, LITTLE-endian u32) || hmac_sha256(secret)[:16]
TTL    = 5 minutes — presented once, at PACKET_CONNECT
```

`ticket460.parse` reads the uuid and the little-endian expiry out of the bytes, and `from_mint` refuses an answer
whose JSON fields **disagree** with the bytes IDUNA signed. It never verifies the MAC: the secret is the C
server's, and a place every client can read is the last place a signing secret belongs. A test greps the module's
code (comments stripped) to keep it that way.

## Verified end to end, against a real IDUNA

`roblox/scripts/live_ticket_check.sh` builds and boots the real IDUNA binary on a throwaway SQLite file and a
private port (`IDUNA_ADDR`), signs a player up exactly the way the place does, signs back in with the guest
secret, mints a connect ticket, verifies the MAC the way the C server does, and hands IDUNA's own JSON answer to
`live_mint_test.lua` — so the module the place runs is proven against a live ticket, not a fixture.

```bash
make test-roblox                        # net460 / queue460 / ticket460 / login460 under plain Lua + luac -p
make regen-roblox-rules                 # queue_rules.prn + ticket_rules.prn -> src/*_rules.lua (needs ../PARENA)
make roblox-model                       # -> roblox/SHANKPIT_ROBLOX_460.rbxmx
roblox/scripts/live_ticket_check.sh     # real IDUNA, real ticket, verified MAC
```

## Loading it into the base place

The base place is a **binary** `.rbxl` and there is no rbx-dom/rojo toolchain on this box, so nothing here rewrites
it — hand-writing binary INST/PROP/PRNT chunks would be a good way to corrupt the one file hardest to reconstruct.

1. Open `shankpit_roblox.rbxl` in Studio.
2. **Insert from file** → `roblox/SHANKPIT_ROBLOX_460.rbxmx` → it lands as a `SHANKPIT460` folder (9 instances).
3. Drag the **Script** (`lobby`) and the ModuleScripts into **ServerScriptService**.
4. Drag **`lobby_client` (a LocalScript) into StarterPlayerScripts** — leaving it beside the server Script is the
   mistake that makes a lobby look finished with nothing on screen.
5. Enable **HttpService** and **API Services → Studio Access to APIs** (Game Settings → Security) — DataStore needs
   the second one, and without it identity persistence quietly becomes "a new guest every session".
6. Fill in `CONFIG` in `lobby.server.lua`, un-tick **Disabled**, publish.

## Honest limits

- **Nothing has been opened in Studio.** No Studio and no rbx tooling here: the model is XML-parsed, the modules
  are Lua-tested, and the ticket/login paths were proven against a live IDUNA — but no pixel has been seen.
- **The URLs are production hostnames** (`iam.okemily.com`). Roblox cannot reach `127.0.0.1`; a local IDUNA needs a
  public tunnel. `CONFIG.GameUrl` / `TicketUrl` / `NetUrl` are the three to change.
- **No rating linkage.** The queue seats everyone at the ladder's `rating_start` (1000): the world's ladder is keyed
  by its own pid and no Roblox↔pid binding exists. The board is read-only for the same reason.
- **Identity is per-place, not per-person.** A guest secret persisted here does not follow a player to another
  place, and there is no link-email flow in this UI (WOTAN has one; it is not wired here).
- **The C verifier is not in this checkout.** The harness reimplements the documented algorithm
  (`hmac_sha256.h` lives in the separate shankpit-460 repo) — verified logic, not the C function itself.
- **No gameplay.** A pair hands out a ticket; the arena stays the C server, which is what a 460 ticket is for.
