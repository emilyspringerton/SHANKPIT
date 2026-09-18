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

## RSI Tooling (use these after any meaningful change)

```bash
# File a completion Apple to IDUNA (required after any meaningful commit)
emily apples post -t completion -repo SHANKPIT "<description>"

# Mark an item done in the golden backlog (if applicable)
# → edit EMILY/BACKLOG.md, mark [x] with Apple ID + date, then:
cd /home/fatbaby/EMILY && git add BACKLOG.md && git commit -m "backlog: ✓ <item>" && git push
```

## Go Server (Dragonfly backend)

- Module: `dragonsnshit` (`go.mod`)
- UDP server: `apps2/server-go/main.go` — runs on `:6969`
- Build: `go build ./apps2/server-go/`
- Tests: `go test ./...` (all pass as of 2026-06-12)
- Portal system: `server/system/portal.go` — 8 scenes, 10 portals, full routing table
- Bridge protocol: `docs2/specs/THE_BRIDGE_SPEC.md` — voxel data via `PACKET_VOXEL_DATA`
- Northstar: `docs2/NORTHSTAR.md` — current status: Milestones 1+2 complete, Milestone 3 next

## Apple Filing Protocol

After any meaningful change, file an Apple:
```bash
emily apples post -t completion "<title>" "<body with commit hash>"
```
Then mark the item done in EMILY/BACKLOG.md and commit: `git add BACKLOG.md && git commit && git push`

## Golden Doc Registration

If you create a new NORTHSTAR.md, architecture spec, or mission-critical design doc in this repo,
append a row to `EMILY/context/golden-docs-index.md` so Emily Prime picks it up on the next cycle:
```
| NAME | <repo>/path/to/doc.md | 1 | <budget-or-0> | one-line description |
```
Then commit and push EMILY:
```bash
cd /home/fatbaby/EMILY && git add context/golden-docs-index.md && git commit -m "golden-index: add NAME" && git push
```

## Level Registry Doubles as Living Documentation (standing instruction)

Founder real-time, 2026-09-17: "write into your claude files that when you verify levels write
them into the registry and leave them there as an example designers can use to try to figure out
how to use the feature without blowing a bunch of tokens asking for help." When verifying any
level-editor feature (doors, widgets, materials, exits, nav nodes, characters, spawners, etc.),
create the verification as a REAL level (or widget) in the live IDUNA NOCK registry
(`shankpit_levels`/`shankpit_widgets` — same DB the `/admin/nock` UI reads) instead of a
throwaway local JSON file in a scratchpad directory. Give it a clear, self-describing name (e.g.
`TUTORIAL_DOOR`) so a designer opening the level list can find and inspect a working, minimal
example of the feature without asking a question that burns tokens. This registry is currently
being treated as a development/staging registry — "assume this is the development level registry
we are developing in the open." When a separate registry is stood up later for real
user-created content, the founder may clone this one forward (including our own story levels) —
that's a future decision, not something to build now.

**Levels are never story-mode-only.** "Either way levels are story mode so never hard code
anything to only work in story mode that doesnt make sense — if something is happening in
multiplayer and it is bad i will let you know — having characters in multiplayer is totally
normal as an idea think about bosses in fortnite etc." Do not gate a general level feature
(doors, characters, exits, widgets, etc.) to `MODE_STORY` only. The only legitimate uses of a
`MODE_STORY` check are genuinely mode-specific: entry-point selection (`is_story_start`),
cutscene HUD rendering, and similar — never "should this feature work at all."

## Founder Real-Time Direction

Whenever the founder gives real-time direction — a new ask, a correction, a "can we also..." —
route it through `emily observe -s info "Founder real-time: <summary>"` first, even if it isn't
this repo's usual domain, then sprint-plan it into `EMILY/BACKLOG.md` (`emily backlog curate`,
scoped into a real SECTION/sub-item, not just a one-line log), and only then implement. See
`EMILY/docs/THE_EMILY_WAY.md` Principle 18 ("Pave the Cow Paths").

## README Reality — SAGA reconciliation (standing instruction, monorepo-wide)

Founder real-time, 2026-09-18: if a change of yours **substantially changes the claim of this project's core README**,
then per SAGA protocols (`EMILY/docs/SAGA_SYSTEM_AUDIT_2026-07-18.md`, HQ-SPEC-DOC-102: intent ↔ claim ledger ↔ reality)
you **must update `README.md` in the same unit of work** so it reflects current reality. The README is the project's public
claim; it must not lag behind the code.

- **When it applies:** a capability is added or removed; status moves ("design only" → "working", "planned" → "shipped");
  the stack, build, run or install steps change; a claim in the README is now false or stale; or you add a **meaningful,
  genuinely interesting piece of kit** (a new tool, engine capability, protocol, pipeline, game system). For that last case
  especially: put it in the README — what it is, how to run it, and its honest status and limits.
- **When it does not:** ordinary fixes, refactors and small features that leave the README's claims true.
- **How:** re-read the README against what you just changed; fix or delete stale lines (including "not built yet" notes that
  are now built); verify any new claim by actually running it, and mark anything untested as untested; commit the README
  with (or immediately after) the change, and mention it in the CHANGELOG entry.

## Frame-Break Reframing

Founder-sourced prompting technique (REDGARDEN/NORTHSTAR.md §28, full origin in
REDGARDEN/docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md §5): given a request, name the underlying
structural/systemic pattern it's one instance of — one level of abstraction up — as an added
lens during planning/triage/judgment calls. Use it to spot the general case behind a specific
ask. It augments judgment, it does not replace doing the work: direct, concrete execution of
the literal task asked for still happens every time.

## CONSTRUCT File Generation (standing instruction, monorepo Principle 21)

**SHANKPIT auto-generates a CONSTRUCT file on every master push via GitHub Actions** (`release.yml`, lines 165–197). This is a deterministic, plaintext snapshot of all repo source files with clear boundaries — used for offline access, audit trails, and release artifacts.

The CONSTRUCT generation already happens in CI; no manual work needed. See the main `CLAUDE.md`'s "Principle 21: CONSTRUCT Files" section for the full rationale and how other repos implement this pattern. For local verification:

```bash
# Manual generation (same as CI does):
OUT="SHANKPIT_CONSTRUCT.txt"
echo "SHANKPIT BUILD LOCAL CONSTRUCT" > "$OUT"
FILES=$(find packages apps services docs -type f \
  \( -name "*.c" -o -name "*.h" -o -name "*.go" -o -name "*.md" -o -name "Makefile" -o -name "*.yml" \) \
  ! -path "*/.git/*" ! -path "*/vendor/*" ! -path "*/node_modules/*" ! -path "*/build/*" | sort)
for file in $FILES; do
  echo "--- FILE START: $file ---" >> "$OUT"
  cat "$file" >> "$OUT"
  echo "" >> "$OUT"
  echo "--- FILE END: $file ---" >> "$OUT"
  echo "" >> "$OUT"
done
```

## Commit Protocol (standing instruction)

Always commit and push completed work immediately — don't wait to be asked. This is the default for every repo in this monorepo.

Every commit — human-written or produced by automated code paths (git-commit helpers in emily-agent, emily.cli, IDUNA handlers, etc.) — must carry the active `emily session` fingerprint as a `session: <tag>` trailer (blank line, then the trailer). This was silently missing from several independently-implemented automated commit helpers across the monorepo until an audit on 2026-08-10 (founder, real-time: "where in the fuck is my llm session id anywhere"). If you add a new automated git-commit code path anywhere, wire in the session tag the same way — don't assume an existing helper already does it.
