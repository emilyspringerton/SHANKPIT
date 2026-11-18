# SHANKOS + the Lab — NORTHSTAR (design only, 2026-11-15)

Founder direction, in order: continue SHINKIT and SHANKOS as an **Electron replacement**; a **fully dogfooded, self-hosted
PARENA that compiles itself**, with **Madman** (the self-compiling tool) and **in-game visualisation of how all of it works**;
**Osaka Garage and the Lab are one thing** — the garage is the nexus of the lab.

Names checked on 2026-11-15: none of SHANKOS, SHINKIT, Madman or "Lab" exists in any repo yet. They are new. Everything below
that names an existing thing was checked in the tree; everything else is marked **new**.

## 1. What exists today (checked)

| Piece | Where | State |
|---|---|---|
| PARENA self-host front end | `PARENA/selfhost/{lexer,parser,region,emit,main}.prn`, 4.5k lines | Real, tested (`make test-selfhost-*`, incl. multifile). Compiled BY the C `parena-c`; **no fixpoint yet** |
| Phone-in-game browser / OS menu | `packages/pellegrino_embed`, `pellegrino_browser_embed` | Real; limits: mouse, h3, washed-out render |
| Osaka Garage | `server/system/portal.go` `SceneGarageOsaka = 0`, 5-portal hub | Real scene; routes to Stadium, Voxworld, Dust, Tanker, Poo Poo Island |
| Cross-engine hub scoping | `docs2/NORTHSTAR.md` (Osaka Garage as multiverse hub) | Doc only; blocked on PAPERCRAFT host code |

## 2. The Lab = Osaka Garage

One place, two names. The garage stays `SceneGarageOsaka` (scene id 0 — nothing renumbers). The Lab is what the garage *is for*:
the room where you watch the toolchain work. The five portals stay; the lab adds a **sixth surface**, the bench: a set of
in-world displays (**viz stations**) fed by real data from the compiler and the apps. Signage and in-lore naming first.

## 3. Self-hosting PARENA (the dogfood ladder)

Rungs, each with a test that fails loudly:
1. **Stage 0** — the C `parena-c` builds `selfhost/*.prn` into `parena1` (exists, minus a driver that does it end to end).
2. **Stage 1** — `parena1` compiles the same sources into `parena2`.
3. **Fixpoint** — `parena2` compiles them again into `parena3`; **`parena2` and `parena3` outputs must be byte-identical.**
   This is the definition of "self-compiling". CI gate: `make test-selfhost-fixpoint` (**new**).
4. **Dogfood** — the stdlib, the WoW collection packager, SHANKPIT's `.prn` rules and **Madman itself** build only with
   the self-built compiler, in CI, from `main`.
Known gap to close first: `emit.prn` must cover every construct `stdlib/` uses, or stage 1 cannot build them.

## 4. Madman (new)

The self-compiling build driver: reads a project's `.prn` graph, builds it with the self-built compiler, and **emits an event
stream** (file → lex → parse → region → emit, timings, errors) as JSON lines. Madman is also the first thing built by the
fixpoint compiler. The event stream is the single feed for the Lab's viz stations — no second instrumentation path.

## 5. Viz in game (new)

Each station reads Madman's event stream (and the apps' own logs) and draws one stage live: token stream pouring through the
lexer, the AST growing, regions, emitted C. A station per toolchain stage plus one for SHANKOS itself (processes, windows,
frame time). Rendering uses the existing GL embed path; data arrives over the same server channel as scene state. Read-only:
the lab shows, it never edits sources in v1.

## 6. SHANKOS / SHINKIT as an Electron replacement (new)

Electron's job: ship a web UI as a desktop app with native hooks. The replacement: **SHINKIT** is the app kit (window, input,
file/net hooks, an HTML/CSS subset renderer) and **SHANKOS** is the shell that launches kit apps — both PARENA, single static
binary, no Chromium. PELLEGRINO already supplies the window, TLS fetch and page streaming; the build-out is a
**app manifest + sandboxed host API** so an app is a folder (page + `.prn`) rather than a repo. First app: the lab's own
viewer, so the OS replaces itself as its own first customer. Honest limit: the rendering subset is small; apps needing full
web compatibility are out of scope for v1.

## 7. Order of work (each a separate PR)

1. This doc. 2. Fixpoint driver + CI gate (PARENA). 3. Madman event stream (PARENA). 4. Lab scene: signage + first viz
station fed by a recorded stream (SHANKPIT; `go test ./server/system/...` stays green). 5. Live feed. 6. SHINKIT app manifest
and host API; lab viewer as app one. Items 2–3 unblock everything; do them first.

## 8. Open questions for the founder

- Does Madman replace `make`/Bazel for PARENA projects, or sit beside them? (Assumed: beside, in v1.)
- Is the viz fed from a live server or from a recorded session first? (Assumed: recorded first.)
- Scope of the HTML/CSS subset SHINKIT must render for app one.
