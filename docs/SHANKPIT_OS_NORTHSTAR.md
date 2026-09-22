# SHANKPIT OS — NORTHSTAR (scoping only, no code yet)

## Founder ask (verbatim, 2026-09-22, real-time chat)

> "shankpit has nock editor built in we can allow users to author levels in the browser very
> easily it loads very quikly / shankpit can be the canonical SSO experience for the IDUNA games
> ecosystem / shankpit is the product - deadspace [deadweight] is just a menu / our interface is
> an operating system / brawlpuit (smash like guy) can be a menu item / we built the interface
> out of the EmilyOs affordances guide / our os is a game"

A fundamental pivot: stop treating SHANKPIT, DEADWEIGHT, and BRAWLPIT as three separate game
launches, and instead treat SHANKPIT as the platform — the shell, the identity provider, the
level-authoring surface — with DEADWEIGHT and BRAWLPIT as first-party "apps" launched from inside
it. This reframes the just-written `SLOWBOT_LEAGUE/NORTHSTAR.md` (a SHANKPIT spin-off is now a
menu item inside SHANKPIT, not a peer product) and DEADWEIGHT's own standalone Itch.io/Steam
launch framing.

**This doc audits what's real to build on, what's genuinely missing, and names the open questions
that change the whole shape of the build — several of which are large enough that guessing past
them risks real rework.**

## Real capability audit (checked, not assumed)

1. **The NOCK level editor is real, fast, browser-based (React/Vite), and it works** — this
   session used it directly. But it is **not a public authoring surface today**: every write
   route (`/admin/nock/api/shankpit-levels...`) is gated by `iduna.admin`, the same permission
   that gates the entire Back Office. "Allow users to author levels in the browser very easily"
   currently means "allow *admins*." A read-only, unauthenticated *browsing* API already exists
   (`shankpit_levels_public.go`) — publishing already-made levels for the native client to fetch
   was designed for, editing by ordinary players was explicitly not ("gating it would mean
   solving a full IDUNA-login-for-SHANKPIT-players question that hasn't been asked for yet," per
   that file's own doc comment — written before this pivot, and exactly the question this pivot
   now asks).
2. **SHANKPIT already has a more complete identity story than DEADWEIGHT does.**
   `IDUNA/internal/http/handlers/shankpit_auth.go` is a real, coded Google OAuth flow
   (`GET /api/v1/auth/google/shankpit` → consent → callback → upserts a real `players` row with
   `provider='google'`) — a real account, not a guest token. DEADWEIGHT's model (this session's
   own work) is the reverse: guest-first, email/password linked later via Claim Account. SHANKPIT
   becoming the canonical SSO is directionally natural — its auth model is already the "real
   account first" shape an SSO provider needs.
   **But it's dark in production right now**: `GOOGLE_CLIENT_ID`/`GOOGLE_CLIENT_SECRET` are unset
   in the live IDUNA environment (checked directly, `~/.config/iduna/env`) — `handleInitiate`
   503s until a human does the Google Cloud Console OAuth-credential step. Same class of external,
   human-only dependency already named for `IDUNA_PRO`/`JEWEL` elsewhere in this monorepo.
3. **SHANKPIT's own player/auth/ticket handlers predate IDUNA's newer generic per-game tenant
   system.** `shankpit_auth.go`/`shankpit_queue.go`/`shankpit_ticket.go` are their own dedicated
   handler files — SHANKPIT is NOT one of the entries in `internal/games/games.go`'s `Registry`
   (that generic `/api/v1/games/{game}/...` system DEADWEIGHT uses and `SLOWBOT_LEAGUE/
   NORTHSTAR.md` proposed reusing). This is a real, load-bearing architectural seam: **does
   SHANKPIT get migrated onto the newer generic system, or does the generic system stay
   DEADWEIGHT/SLOWBOT_LEAGUE-only, with SHANKPIT's own bespoke handlers becoming the thing
   everything else defers identity to?** Not a detail — this decides where new code goes for
   every future integration.
4. **BRAWLPIT had no player-facing IDUNA identity integration** — checked directly, no
   `brawlpit_auth`-equivalent handler existed; the only BRAWLPIT-IDUNA surface was
   agent-auth-gated checkpoint upload (`brawlpit_checkpoints.go`, M2M only, not a player login).
   **Closed 2026-09-22**: `brawlpit` is now a real `internal/games.Registry` entry (`brawlpit.play`,
   IDUNA commit following Apple #20332) riding the exact same generic guest-register/guest-login/
   guest-upgrade API DEADWEIGHT and BIG_O already use, plus a public `/play/brawlpit` page (BIG_O
   got the identical treatment for its own card, 123214231 — both pages now share one reusable
   `GameSignupPageHandler`, game-parameterized, rather than two copies). Neither game's own native
   client consumes this yet — that's the real, remaining, separate integration work ("BRAWLPIT can
   be a menu item" still needs its own C client to actually call this API instead of a browser
   page standing in for it), not just a launcher entry.
5. **The EmilyOS affordances guide is real and already has a live precedent elsewhere in this
   monorepo.** `EmilyOS/docs/legacy-archive/gui-v0.1-design-capture.md` — "the game interface IS
   the filesystem UI," directory tiles (EGSHELL/near-white, container semantics) vs. colored
   button tiles (always darker than EGSHELL, a fixed non-white palette), intent-declared
   interaction (no bare single-click actions), single-shot-only animation. This exact doc is
   already named as the real UX foundation for `DUNG` (the planned BURROW-based terminal/editor).
   Not yet applied to SHANKPIT, DEADWEIGHT, or BRAWLPIT's own client chrome anywhere — a real,
   full visual-language build, not a reskin.

## The single biggest open question: what IS the shell, technically?

Everything above can be built regardless of the answer, but the answer changes where most of the
work goes, so it needs a real decision, not an assumption:

- **Option A — a browser web shell.** NOCK's own React/Vite stack already exists, is fast, and is
  the one piece of this whole ecosystem that's already "in a browser, loads quickly." A
  browser-based OS shell could live there, launching DEADWEIGHT/BRAWLPIT as embedded web views,
  downloadable native launches, or (much further out) actual browser ports. Cheapest path to
  something demoable; furthest from "our OS is a game" if the shell itself has no game feel.
- **Option B — SHANKPIT's own native SDL2 client becomes the shell.** SHANKPIT already has a real
  native client (`apps/client`/`apps/lobby`); DEADWEIGHT and BRAWLPIT are both native (SDL2/C,
  matching this monorepo's own universal client-architecture precedent). A native shell keeps
  everything in one consistent "this is a game, not a webpage" feel, matches "our OS is a game"
  most literally, but means launching other native apps as subprocesses/overlays from within a
  game client — a real, unfamiliar pattern with no existing precedent in this monorepo to lean on.
- **Option C — both, unified by identity/data only, not a literal embedding shell.** SHANKPIT
  becomes the SSO + the place discovery/social happens (leaderboards, friends, UGC levels), but
  DEADWEIGHT/BRAWLPIT keep launching as their own separate native processes/downloads — "the OS"
  is the account and content graph, not a literal window manager. Lowest engineering risk, but
  arguably doesn't deliver "our interface is an operating system" as literally as A or B do.

**This doc does not pick one.** It's the one open question most likely to make a half-built
answer expensive to reverse.

## Real, genuinely new work (regardless of which shell option is chosen)

1. **Public NOCK authoring needs a real permission model.** Today, any write access = full
   `iduna.admin` (Back Office access to *everything*, not just levels). Opening this to ordinary
   players needs a new, narrower permission (e.g. `nock.author`, granted by default to any real
   account, separate from `iduna.admin`), plus real content concerns that don't exist for an
   admin-only internal tool: per-level ownership/attribution, a moderation/report path, and rate
   limits (`MaxDoors`/`MaxWalls`/etc. already cap a single level's size, but nothing caps how many
   levels one account can create).
2. **Turn on SHANKPIT's Google OAuth for real** — a human-only GCP Console step (same gate named
   for `IDUNA_PRO`/`JEWEL`), then decide the DEADWEIGHT/BRAWLPIT identity-unification direction
   named in the audit above (migrate SHANKPIT onto `games.Registry`, or the reverse).
3. ~~BRAWLPIT player identity from scratch~~ — **closed 2026-09-22**, see the audit item 4 update
   above. Remaining: wiring BRAWLPIT's own native C client to actually call it.
4. **The EmilyOS affordance system**, actually built as reusable chrome (palette, tile
   components, intent-declared interaction pattern) — currently only a design doc, applied
   nowhere.
5. **Whatever Option A/B/C above turns into**, once decided.

## What this does NOT change (named explicitly, not silently dropped)

- `SLOWBOT_LEAGUE/NORTHSTAR.md`'s own technical findings (the PFSP/Elo math, the live checkpoint
  578 anchor, `games.Registry` as the per-game tenant mechanism) are all still accurate and
  reusable — this pivot changes SLOWBOT_LEAGUE's *position* (a menu item inside SHANKPIT, not a
  peer product needing its own "totally separate tenant") more than its technical plan.
- DEADWEIGHT's own current guest-auth/Claim Account/25-ticket economy (this session's own work,
  live in production) keeps working exactly as it is — nothing here proposes ripping it out,
  only a future integration question (does a DEADWEIGHT guest eventually link to/become a
  SHANKPIT account, the same *shape* of question Claim Account already solved for "guest → email,"
  just one level up).

## Open questions for the founder (in priority order — #1 blocks real scoping of everything else)

1. **Shell surface: Option A (browser), B (native SDL2), or C (identity/data only, no literal
   embedding)?**
2. Does the DEADWEIGHT Itch.io/Steam standalone launch plan proceed on its current timeline in
   parallel, or does it pause pending the shell decision?
3. Migration direction: SHANKPIT's bespoke auth/queue/ticket handlers move onto the generic
   `games.Registry` system, or the generic system's future members (DEADWEIGHT, SLOWBOT_LEAGUE)
   move under SHANKPIT's own identity instead?
4. For public NOCK authoring: is there an existing moderation/reporting appetite/plan, or should
   that be scoped as its own follow-up before opening writes beyond admins?
