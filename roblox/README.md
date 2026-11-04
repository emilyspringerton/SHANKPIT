# SHANKPIT on Roblox — the 460 layer

`shankpit_roblox.rbxl` (repo root) is the base place. This directory is the **SHANKPIT-460 DNA** that sits beside
it: a real sign-in, a queue, real connect tickets, and a read-only board. Nothing here is NOOBTOPIA.

## Scope: narrow on purpose

| | NOOBTOPIA | this layer |
|---|---|---|
| What it is | the whole DEADWEIGHT game on Roblox — cards, decks, ladder, stage, tournament, noob life | the **esports front door** for SHANKPIT |
| Rules | Luau ports of `card_rules.prn`, `rating.prn`, `stage.prn` | the queue/ticket thresholds only (from PARENA); it owns no rating |
| Gameplay | played in Roblox | played in Roblox too -- on the place's own Laser Tag content (Blaster/AutoBlaster Tools, Teams, SpawnLocations); the C server still owns the rating and the ticket |
| Persistence | per-server ladder | identity only (DataStore: player_id + guest secret). No ladder of its own |

| File | Class | Owns |
|---|---|---|
| `src/login460.lua` | ModuleScript | a real IDUNA identity: guest-register, guest-login, token freshness, what may be persisted |
| `src/ticket460.lua` | ModuleScript | reading a real IDUNA connect ticket: shape, expiry, claimed player. **Never** verifies one |
| `src/queue460.lua` | ModuleScript | casual vs ranked, the closest-pair search, seat bookkeeping |
| `src/stats460.lua` | ModuleScript | the authorities' stats as a **cache**: optimistic write, reconcile, rollback, fan-out. Never invents a stat |
| `src/arena460.lua` | ModuleScript | the arena the place already ships: which pad per team, which blaster, which side of a pair, who won. Reads, never invents; re-reads without losing a usable arena |
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

## Keeping the account

A guest is throwaway by design — no email, no recovery. **LINK EMAIL** in the lobby calls IDUNA's
`guest-upgrade`, which adds credentials to the SAME player row: the `player_id` does not change, so seats and
any history stay theirs, and the answer is a fresh player token (subject `email:<uuid>`) that the ticket mint
accepts like any other. The password is sent once, used by IDUNA, and never stored here; the persisted form
carries `player_id`, the guest secret and the email, never a token or a password.

That is what makes an account portable: `email-login` from any place lands on the same player. What is still
missing is the *form* for it on this screen — a returning player who did not bring their published secret is
signed up as a new guest, which the limits below say plainly.

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
make test-roblox                        # every suite + the installer, under plain Lua, plus luac -p
make regen-roblox-rules                 # queue_rules.prn + ticket_rules.prn -> src/*_rules.lua (needs ../PARENA)
make roblox-model                       # -> SHANKPIT_ROBLOX_460.rbxmx AND SHANKPIT_460_INSTALL.luau
make roblox-install                     # -> just the Command Bar paste script
roblox/scripts/live_ticket_check.sh     # real IDUNA, real ticket, verified MAC
```

## CI: the same checks, on every push

`.github/workflows/roblox460.yml` runs on every push and PR that touches `roblox/` or the `Makefile` (and on
demand), with no stored secrets:

- **`suite`** installs Lua 5.4 *as `lua` and `luac`* — Ubuntu's `lua5.4` package installs neither name, so a CI
  step without this would silently take the target's `SKIP` path and report green for a suite it never ran — then
  runs `make test-roblox` and fails on the `SKIP` line or on a skipped `luac -p` pass. It then asserts the two
  things the suite itself cannot see: every `roblox/tests/*_test.lua` on disk appears in the log as a command
  `make` actually ran (`live_mint_test.lua` excepted — it needs a live mint), and the number of reporting suites
  equals the number of test files. Both of this layer's silent failures were inventory failures, not code
  failures: `login460_test.lua` was committed and never in the target, `stats460.lua` was committed and never in
  the builders' `PARTS`.
- **`the committed artifacts are what src/ builds`** rebuilds the model and the install script from `src/` and
  fails unless the committed bytes come back unchanged, so a hand-edited or stale artifact cannot ship.
- **`live`** (after `suite`) checks IDUNA out of its own public repo, builds the real binary on a throwaway SQLite
  file with a throwaway ticket secret, and runs `live_ticket_check.sh` — guest register, guest login, ticket mint,
  the MAC verified twice, email link onto the same player, email login. No token is needed for any of it.

The published kit in `site/` is a copy, never a rebuild, so `pages.yml` also watches `roblox/**` and
`shankpit_roblox.rbxl`: a change to the layer republishes it, and `site/check.mjs` asserts the copied bytes are the
committed ones.

## Getting it into the base place

The base place is a **binary** `.rbxl` and there is no rbx-dom/rojo toolchain on this box, so nothing here rewrites
it — hand-writing binary INST/PROP/PRNT chunks would be a good way to corrupt the one file hardest to reconstruct.
Two routes, the same eleven instances, built from one `PARTS` list.

### Paste it in (one step, nothing to drag)

`roblox/SHANKPIT_460_INSTALL.luau` is generated from the same `PARTS` list as the model. Open
`shankpit_roblox.rbxl` in Studio, **View → Command Bar**, paste the whole file, press Enter once. It:

- creates `ServerScriptService.SHANKPIT460` holding the nine server-side instances (`lobby` is a `Script`, and it
  arrives **Disabled** so it cannot run blind before you have looked at it);
- creates `StarterPlayer.StarterPlayerScripts.lobby_client` — a LocalScript runs nowhere else, which is the mistake
  that makes a lobby look finished with nothing on screen;
- **replaces** a previous install instead of duplicating it, so re-pasting is safe;
- prints the two settings it deliberately does not change: HTTP requests, and Studio API access.

Then, when you are ready: un-tick **Disabled** on `lobby`, and publish. `make roblox-install` regenerates the paste
script; `roblox/tests/install460_test.lua` runs the committed file against a stubbed Roblox API and compares every
embedded source to `src/` byte for byte, so a stale paste script fails the suite instead of a Studio session.

### Or insert the model

1. Open `shankpit_roblox.rbxl` in Studio.
2. **Insert from file** → `roblox/SHANKPIT_ROBLOX_460.rbxmx` → it lands as a `SHANKPIT460` folder (11 instances).
3. Drag the **Script** (`lobby`) and the ModuleScripts into **ServerScriptService**.
4. Drag **`lobby_client` (a LocalScript) into StarterPlayerScripts** (see above for why).
5. Enable **HttpService** and **API Services → Studio Access to APIs** (Game Settings → Security) — DataStore needs
   the second one, and without it identity persistence quietly becomes "a new guest every session".
6. To point the place at a dev backend instead of production, set the attributes `IdunaBase` / `WotanBase` on the
   `lobby` Script (for the IDUNA Space: `https://<space>.hf.space/api/v1`). Unset means the production hosts, and
   the stats stay a cache of them either way (`src/stats460.lua`). There is nothing to fill in in `CONFIG`: it is
   derived from those two attributes.
7. Un-tick **Disabled** on `lobby`, publish.

## Honest limits

- **Nothing has been opened in Studio.** No Studio and no rbx tooling here: the model is XML-parsed, the modules
  are Lua-tested, and the ticket/login paths were proven against a live IDUNA — but no pixel has been seen.
- **The URLs are production hostnames** (`iam.okemily.com`). Roblox cannot reach `127.0.0.1`; a local IDUNA needs a
  public tunnel. `CONFIG.GameUrl` / `TicketUrl` / `NetUrl` are the three to change.
- **No rating linkage.** The queue seats everyone at the ladder's `rating_start` (1000): the world's ladder is keyed
  by its own pid and no Roblox↔pid binding exists. The board is read-only for the same reason.
- **The ACCOUNT is portable; the SESSION is not.** A player who links an email can sign in from any place with
  `email-login` — but this UI has no sign-in-by-email form yet, so a returning player on a fresh place still
  starts as a new guest until the published secret is present.
- **The C verifier is not in this checkout.** The harness reimplements the documented algorithm
  (`hmac_sha256.h` lives in the separate shankpit-460 repo) — verified logic, not the C function itself.
- **The round is played here; its result is still not the ladder's.** The arena adapter reads the place's own Laser
  Tag content (pads, tools, teams) and `arena460.result` says who won from final tag counts — but nothing publishes
  a Roblox round to WOTAN or IDUNA: `shankpit_netd` is read-only (`POST` answers 405) and there is no ingest
  endpoint, so the board stays a cache of the matches the world itself played (`src/stats460.lua`). A round that
  ends in this place changes nothing on the ladder, and no code here pretends otherwise.
- **Seating sets two engine properties; the engine does the placing.** On a match the server sets `Player.Team`
  and `Player.RespawnLocation` from `arena460.placement`, so Roblox puts the body on its team's pad — but only on
  the engine's own next respawn. Nothing teleports a character, forces a respawn, hands out a Tool, or counts a
  tag: that is the base place's own Laser Tag behaviour, and this layer deliberately owns none of it.
- **A re-read can be worse than the last read.** `arena460.refresh` keeps the last usable arena when Workspace is
  briefly empty (a place rebuild, a team edit before its pad lands) and says what the re-read could not see. That
  keeps a lobby from refusing rounds it can host, but it also means `arena_ok` can describe a pad that a publish
  has just removed, for as long as the place stays unusable.
