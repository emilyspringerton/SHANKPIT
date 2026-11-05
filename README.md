# SHANKPIT Game Architecture Document

**Project page:** https://emilyspringerton.github.io/SHANKPIT/ — built from `site/` and published by
`.github/workflows/pages.yml` on every push to `master` that touches the site or the docs. `site/README.md` has the
pipeline and the one repository setting it needs.

## Current Status (2026-08-26)

CI is green (`ShankPit Factory` #784+) after a real fix: the cutscene system and spatial
audio engine (both real, shipped features) had never been added to the Windows client's hardcoded
mingw build command, so every CI run since build #721 failed with undefined references. Also
fixed: helicopter HUD text was overlapping into unreadable garbage — the real flight physics
(enter/fly/hover) were never actually broken, just unreadable.

**New since**: CTF-with-bots (MODE_CTFB) now randomly picks Voxworld or Oil Tanker instead of
always Oil Tanker; story-mode enemies got real abilities (2 new threat roles, RedGarden-style
squad AI and abilities — graphics stay stubs for now, abilities do not); GOLDENBAND skinned-mesh
support landed (S144-02 Stage B). See `CHANGELOG.md`.

## What SHANKPIT is right now

At a high level, SHANKPIT currently includes:

- a lobby/client app with local and networked play flows
- a UDP server-authoritative game server
- multiple local and networked game modes
- linked scenes connected by portals
- terrain-backed worlds in addition to box/block geometry
- team modes including team deathmatch and capture the flag
- bot support for local team modes and online cold-start scenarios
- a growing retro rendering stack with procedural textures, sky, fog, and stylized lighting
- persistent skin selection
- **sound engineering primitives from PARENA** (2026-09-27): `packages/audio/audio_dsp_gen.c` is C
  compiled from PARENA `stdlib/audio` (biquads, compressor/expander/de-esser/limiter, BS.1770
  loudness, plus DJ mixer/deck/MPC-sampler curves); `audio_chain.c` runs filter chains authored in
  IDUNA's NOCK Sounds tab. Set `SHANKPIT_SOUND_CHAIN=<name>` to run a NOCK chain over the whole
  engine mix (fetched from `SHANKPIT_SOUND_CHAIN_URL`, default
  `https://okemily.com/api/v1/nock-sound-filters`) -- with no chain set, the mixer path is
  byte-for-byte the old one. `make test-audio-chain` proves the C runner matches NOCK's TypeScript
  render bit-for-bit (max |diff| 0 over 24,000 samples). Not yet played through in a live session
  with a real chain against production IDUNA.
- test coverage for protocol and gameplay regressions. :contentReference[oaicite:2]{index=2} :contentReference[oaicite:3]{index=3} :contentReference[oaicite:4]{index=4}

---

## Current game modes

The current lobby/game flow exposes these core modes:

- **JOIN** — connect to the online deathmatch flow
- **TDMO** — online team deathmatch with bots supporting cold start
- **SOLO** — local single-player deathmatch flow
- **TRAIN** — local training-oriented flow
- **TDMB** — local team deathmatch with bots
- **CTFB** — local capture the flag with bots. :contentReference[oaicite:5]{index=5}

Internally, the client also recognizes additional local mode IDs such as `battle`, `tdm`, `training`, `recorder`, and `garage`, but the player-facing lobby is currently centered on the six modes above. :contentReference[oaicite:6]{index=6}

---

## OS menu = HTML + CSS (2026-10-15)

The lobby's tile grid is no longer hand-placed GL rectangles: each page is a small generated HTML document with a
plain stylesheet (`packages/pellegrino_embed/os_menu_embed.c`), laid out and painted by PELLEGRINO -- the PARENA
browser engine -- into an offscreen surface that the lobby draws as one textured quad. Clicks go through the
engine's link hit-test back to the tile index. `make test-os-menu` (headless) and `make test-os-menu-gl` (needs a GL
display) check it; `SHANKPIT_OS_MENU=classic` selects the old grid. Regenerate the vendored engine with
`scripts/gen_pellegrino_embed.sh` (needs sibling `../PELLEGRINO` and a built `../PARENA/parena`). Status: tested in
isolation; the whole lobby has not been built/run with it on the dev box yet, and the skin/level/spray overlays,
title and the small corner buttons are still hand-drawn GL.

## Boot behaviour (2026-10-16)
`./bin/shank_lobby` has no title screen or main menu: it drops straight into a ZOMBIES game. Esc goes to the legacy menu; `--menu` starts there instead. Explicit flags (`--survival`, `--lab`, `--city`, `--edit-map`) still win.

## In-phone PELLEGRINO browser (2026-10-16)

The in-fiction SHANKPHONE (open with `P` during a match) has a `PELLEGRINO` tile that runs a real,
embedded browser inside the phone panel — not the Apps-page launcher tile above (that one spawns a
separate window). `packages/pellegrino_browser_embed/` wraps PELLEGRINO's `embed-nav-*` API (TLS 1.2/1.3
and HTTP/3-over-QUIC; no HTTP/2) and is linked into `shank_lobby` by `make lobby` as pre-built objects
(mbedTLS fetched via `../PELLEGRINO/scripts/fetch_mbedtls.sh`; needs sibling `../PELLEGRINO`,
`../PARENA`, `../EDITOR.GAME`, and `libsdl2-ttf-dev`). Regenerate the package with
`scripts/gen_pellegrino_browser_embed.sh`. Status: verified under Xvfb in `--zombies` — page renders in
the panel (live `https://example.com/`), typing a URL navigates, Esc returns to the phone home grid.
Limits: keyboard-only (no mouse), `h3://` not yet confirmed in-game (a standalone harness did fetch
`h3://cloudflare-quic.com/`), rendering of the example.com page looks washed out, Windows/`release.yml`
not wired. Details: `docs2/specs/PELLEGRINO_PHONE_BROWSER_STATUS.md`.

## Apps page (2026-09-22, platform pivot)

The lobby menu now has a second page — a small "APPS >" button next to the title flips from the
GAMES grid above to an APPS grid listing other EINHORN_INDUSTRIAL games as launchable entries:
DEADWEIGHT, PITVIPER, IDUNA (IDUNA.GAME), REDGARDEN, EDITOR (EDITOR.GAME), PELLEGRINO, and
ZOMBIES. Selecting one (same double-click-to-activate convention the GAMES page already uses)
launches that game's own real, unmodified native binary as a child process — SHANKPIT never
reimplements another game's UI, it just launches it; PELLEGRINO's own real keybinds (address
bar, link navigation, CSS/markdown toggles, scroll/quit — see `PELLEGRINO/README.md`'s own "Keys"
section) carry over unmodified for the same reason. ZOMBIES is the one exception: it isn't a separate game, it's this
same `shank_lobby` client self-relaunched (`/proc/self/exe` on Linux, `GetModuleFileNameA` on the
Windows cross-build) with `--host 127.0.0.1 --port 6971` so it lands pointed at the zombie
sandbox server (`shankpit-zombie.service`) instead of the default queue port 6969 — you still
click Join from the fresh lobby it opens, same as launching REDGARDEN still needs its own
ticket/matchmaker step. This is the first concrete step of the "SHANKPIT is the platform, other
games are apps inside it" pivot — see `docs/SHANKPIT_OS_NORTHSTAR.md` for the full scoping.

**Honest current status (2026-09-25): real and working, including the CI-shipped binaries.**
Founder real-time: "the shankpit os apps dont work - can we get it building with the CICD
binaries for all the apps even tho we dont have the signing set up yet." `release.yml` now
checks out all five sibling app repos (DEADWEIGHT, PITVIPER, IDUNA.GAME, REDGARDEN, EDITOR.GAME —
all real, public, separate repos), cross-compiles each for Windows, and ships them in
`ShankPit_Client_*.zip`'s own `bundled/` directory. **PELLEGRINO is not yet in that CI list** —
wired into the lobby's launcher and dev sibling-repo path (2026-10-16), and real-verified by hand
(Linux build, no Bazel, under Xvfb) in that session, but `release.yml` still only cross-compiles
the original five; adding PELLEGRINO to CI's checkout/build/bundle steps is real, named follow-up
work, not done here. `bundled/` is the exact real path
`lobby_app_binary_path` already looked for. Verified for real: a byte-identical local dry run of
the workflow's own embedded build script (same commands, same directory layout, same SDL2/
SDL2_ttf mingw devel kit CI downloads) produced all five working Windows `.exe`s clean. The dev
sibling-repo fallback path (`../../DEADWEIGHT/build/dw_gui` etc.) is untouched and still works for
a local monorepo checkout — the CI-bundled path is checked first now, same
`lobby_app_binary_path` logic as before, just no longer finding nothing. **Known, accepted,
separate gap, named on purpose (not the scope of this fix)**: none of these binaries are
code-signed — Windows SmartScreen/macOS Gatekeeper will show an "unknown publisher" warning on
first run. Real, separate follow-up.

---

## Real UX screenshot testing for shank_lobby (2026-09-28)

Founder real-time: "shankpit levels is down its just a blank screen can we add some ux screenshot
testing." `scripts/ux_screenshot_test.sh` (`make ux-screenshot-test`) boots the real lobby binary
under headless Xvfb, sends a real synthetic Enter keypress via the X server's own XTEST extension
(`python3-xlib` — `xdotool` isn't assumed installed) to open the real `LOBBY_LEVEL_SELECT`
("LEVELS") overlay (it's the first, default-selected menu tile, so Enter alone opens it — no mouse
coordinates needed), screenshots both the boot screen and the overlay, and fails if either is
suspiciously close to solid black. That's the real, live symptom this repo has hit before (see
this file's own SDL_CreateWindow/MSAA note elsewhere in the CHANGELOG) and the one just reported —
not a pixel-diff, a real "did anything actually render" sanity check. Verified live just now: both
screens pass against the current `bin/shank_lobby`, real screenshots saved for review.

A companion surface with the same name — the `/admin/nock` web admin tool's own "SHANKPIT Levels"
tab — lives in `IDUNA`, not here; see that repo's own `frontend/nock/scripts/ux_screenshot_test.mjs`
and `docs/NOCK_NORTHSTAR.md`.

---

## Current worlds / scenes

SHANKPIT is now a multi-scene game rather than a single arena. The active scene list in the current construct includes:

- **Garage Osaka**
- **Stadium**
- **Voxworld**
- **Dust Compound**
- **Oil Tanker**
- **Poo Poo Island**. :contentReference[oaicite:7]{index=7} :contentReference[oaicite:8]{index=8} :contentReference[oaicite:9]{index=9}

These scenes are connected by explicit portal routing. The garage currently serves as a major hub and can route players into multiple destination worlds, while some worlds also provide return routes back to the garage or to adjacent connected scenes. :contentReference[oaicite:10]{index=10} :contentReference[oaicite:11]{index=11}

---

## Building levels offline, no IDUNA (2026-10-30)

IDUNA's NOCK editor is still the web level editor, but it is not required to author a map any more.
`--edit-map` takes a registry level name or a `.json` path, and starts a **new empty level** when that
path does not exist yet (200x50x200, ground plane on, one brick material, one spawner):

```
./bin/shank_lobby --edit-map var/maps/mymap.json   # new level (or reopen it next time)
./bin/shank_lobby --level    var/maps/mymap.json   # play it (a server needs the same path)
```

You fly as a spectator: **B** places a box on the surface under the crosshair, **X** deletes the box
under it, **M** moves spawner 0 there, **F5** saves the op log, **F6** saves the whole level to
`var/maps/<name>.json`. F6 goes through `level_boxes_save_to_file` (`packages/world/level_boxes.h`),
which writes every field the loader reads -- walls/materials, spawners, buggy spawns, lab stations,
doors and buttons, nav nodes, characters, exits, zones, edu widgets, brick damage, `enclosed`, floor
tint -- so a save/load round-trip cannot silently drop authored objects. A successful F6 also deletes
the matching `.edits.json`: those ops are baked into the saved file, so replaying the log would apply
them twice. `make test-level-save` is the round-trip test, and it also re-saves the checked-in
`var/lab/lab.json`, the story-doors demo and `assets/tyler_levels/*.json` to prove real exports
survive. `make leveltool` builds a headless `./bin/leveltool` (`info` / `map` / `check` / `save` / `new`)
-- `map` prints a top-down ASCII view (`#` walls, `S` spawner, `C` character, `E` exit, `z` zone, `D`
door, `B` button) so a layout can be eyeballed in a terminal -- `--cell <units>` zooms (centred on
`--center x,z`, else spawner 0) and `--min-height <units>` hides short geometry such as a big floor
slab; `make check-levels` round-trips every checked-in export through it, so a map can be validated
without launching the lobby.

Still NOCK-only: glTF import, the widgets/materials screens, and streaming edits into a shared live
session (F5 plus `--edit-live`). Anything the in-game keys do not cover yet can be hand-edited in the
saved JSON.

## Local NOCK: browser + game live editing, levels as code (2026-10-30)

The cloud IDUNA/NOCK tooling is not always reachable, so the map editor also runs locally, with no
network and no database:

```
make local-nock                                   # http://127.0.0.1:8787
SHANKPIT_EDIT_SESSIONS_URL=http://127.0.0.1:8787/api/v1/shankpit-edit-sessions \
  ./bin/shank_lobby --edit-map levels/starter.json --edit-live
```

The browser editor (`tools/nock_editor.html`, served by the tool) and in-game EDIT MAP join the same
live session: an edit in the browser pushes the working level (`PUT .../level` plus a `level` event)
so the game reloads it, and the game's own box / spawner / avatar events show up in the browser.
Saving writes `levels/<name>.json` -- maps are plain files -- and commits them
(`level(<name>): N walls from local NOCK`); nothing pushes automatically.

The server is `tools/local_nock.py`, wire-compatible with the IDUNA handler the client was written
against (`packages/world/edit_sync.h`). `make test-local-nock` covers level save + commit, invalid
level rejection, session create/post/poll, working-draft push/fetch, long-poll wakeup and 404s (12
checks). Two honest gaps: one live room per run, and the event log replays whole rather than honouring
`?since` (the events the client applies are idempotent, so that just refetches the level).

The PARENA port is `tools/local_nock.prn` plus the previously missing dependency
`PARENA/stdlib/net/http_server.prn` (request parsing, case-insensitive header lookup, and response
serialisation). It now emits and runs: `make local-nock-parena` builds it (needs a `../PARENA`
checkout with `.parena-bootstrap`) and serves the same API as the Python runner, with the host
boundary in `tools/local_nock_host.c`/`.h` limited to the arena, env, file/directory access and the
session strings that outlive a request -- routing, JSON, level validation and the git commit are
PARENA.

Getting there needed four real VS0 emitter fixes (2026-10-30, each verified by compiling the
generated C with `-std=c99 -pedantic -Wall -Wextra -Werror`): variadic arithmetic (`(+ a b c)`
left-folds), `set!` on a top-level `(def ...)`, `println`/`print` in the runtime, and `&` on an
already-pointer-typed value (a match-bound `Ok` payload used to emit a real `void **`). `(< a b c)`
still fails by design, with a message that says why: a real chained comparison would have to
evaluate its middle operand twice.

Both runners now pass the same 16 checks (`make test-local-nock`, `make test-local-nock-parena`):
the PARENA one filters events by `?since`, validates sources and kinds, bumps draft revisions and
falls back to the saved level. Sessions are hardened on both -- the id segment must match the live
room (a stale client gets 404 instead of writing into the current one), a level name must be
slug-shaped before any path is built (the traversal defence), bodies over 4 MiB are 413, wrong
methods are 405, unknown sources/kinds are 400, and the event log is capped at 2000 entries with the
oldest dropped. Two limits remain, both named in `mk/local_nock.mk`: the PARENA runner is one room
per process and single-threaded, so `wait` is answered immediately and a client polls again; and it
checks that a level is an object carrying a `walls` array but not each wall's fields, which the
Python runner still does.

### NOCK is an APPS entry, not a terminal to keep open

The lobby's APPS page has a **NOCK** tile (`APP_NOCK`): it starts the server itself and opens
`http://127.0.0.1:8787/` in the system browser, so there is no separate `make local-nock` shell or
`.bat` to find first. In a dev tree it launches `bin/local_nock_parena` (build it with
`make local-nock-parena`) with `--root <this checkout>`, which is what makes saves land in
`levels/` and get committed here; a packaged build looks for `bundled/dw_nock` (`dw_nock.exe`) next
to `bundled/levels` and `bundled/tools/nock_editor.html`. If the port already answers -- an earlier
click, or your own `make local-nock` -- nothing is spawned and the tile just re-opens the tab, so
clicking twice is harmless. The server is spawned detached: it outlives the lobby, so the editor
keeps working after you quit the game. Window is bounded rather than guessed (the tab opens only
after the port answers, up to 2 s). Windows has no bundled binary yet, so the tile says
`NOCK server not found` there instead of pretending it started.

## Android client: shankpit-460 in a WebView (2026-10-31)

The arena shooter ships on Android as **shankpit-460**, the PARENA-compiled
WebAssembly build, inside a WebView. No C engine port: the module has zero imports,
draws its own 640×360 framebuffer, and the Android layer is a WebView plus touch
controls (`android/`, `apps/shankpit-460/`).

- **Matchmaking** keeps the web contract (`/api/v1/shankpit460/play` + `/status`).
  With no backend configured — the only state today — the game drops into the
  **local bot match** and says so, instead of retrying a dead endpoint.
- **Touch controls** are a virtual gamepad: left stick moves, drag aims, and six
  buttons cover fire / jump / crouch / reload / ability / weapon. Raw taps start the
  match, which is also what unlocks audio.
- **Ships in every release**: `release.yml` builds the APK and uploads
  `ShankPit_Android_<build>.apk` next to the desktop zips.

Honest limits: the arena is deathmatch-only (no zombies), there is no map editing
(NOCK is desktop-only), and there are no accounts. `node
android/tools/verify-web-bundle.mjs` is the check — it boots the real wasm through
the real touch layer and proves the offline path.

## Lighting: one environment matrix, resolved in PARENA (2026-10-30)

Founder real-time: "we have cool weather effects but it doesnt work with the ambient lighting like it
always looks too dark everywhere ... we need more of a theory of all the lighting needs to talk to each
other." That was accurate. Weather (`sky_weather.c`), the day/night preset (`retro_lighting.c`), the
pause-menu ambient slider (`ambient_rules.prn`) and the emitters each adjusted the scene alone, and
every one of them could only subtract. A stormy night at slider 0 multiplied down to near-black walls,
because nothing in the engine ever asked "how much light does the world actually get right now?".

There is now one answer per frame. `PARENA/stdlib/shankpit/lighting_rules.prn` (generated into
`packages/simulation/lighting_rules.c`, i32 permille in and out, pure and total like every other mod
here) resolves the atmosphere into six numbers, and every consumer reads them instead of deriving its
own:

| output | what it means | who uses it |
|---|---|---|
| `env-occlusion` | how much sky light the weather actually blocks (capped at 0.78, so a storm at noon is dim daylight, never night) | sun/moon dimming in `env_matrix_apply` |
| `env-light-bleed` | the share the atmosphere **scatters back** | every surface, via `retro_lighting`'s sky bleed |
| `env-ambient-floor` | hard minimum for a surface's ambient term | every surface, via `retro_lighting`'s floor |
| `env-exposure` | low-end compensation for the dimming | diagnostic/lighting state |
| `env-emissive-scale` | 0.75..1.6, inverse of scene brightness | enemy outlines, shield FX |
| `env-emitter-range-scale` / `env-emitter-spread-scale` | reach vs. spread under fog | flashlight beam, its per-face lit cone, the litbox spotlight uniforms |

Three ideas carry the fix. **Scatter instead of only subtract**: the bleed is added to every surface
in the sky's own current colour, so a gloomy sky fills shaded walls rather than just removing the sun --
which is what the old chain was missing entirely. **A floor, not a taste knob**: `env_matrix_apply_surface`
lifts a surface's combined ambient term to the floor with a uniform gain (floor ÷ brightest channel)
rather than clamping each channel, so a wall keeps its hue exactly instead of washing flat, and a
player at slider 0 in a storm still gets readable geometry. **Emitters trade reach for spread**: fog shortens a
beam and the same number widens it, derived from one value so the two can never disagree.

Nothing about this is per-pixel scattering. It is deliberately *not* described as Rayleigh/Mie
anything -- it models the three things that were actually broken (scattering, the low end, and
emitter response), not a physical sky model this renderer cannot support.

**Lightning is a light source, not a screen tint.** A bolt used to only flash
`sky_weather_draw_grade`'s overlay while the world underneath ignored it. `env-flash-boost` turns the
sky's own already-smoothed bolt intensity into extra scattered light in the same bleed channel (up to
0.30), whitened because a bolt is broadband, plus a matching exposure lift -- so a strike visibly
lights the storm deck and the walls beneath it, and stops when the flash decays. No separate state to
track: it is one more term in the same evaluation.

Cost: one CPU evaluation per frame, a pure function of the day/night clock and weather profile both
sides already broadcast in the world-clock packet. No server work, no new wire fields, no per-entity
state -- so every client computes the identical matrix bit for bit (asserted), which is the property
that keeps it cheap under MMO concurrence instead of syncing lighting around.

Verified: `make test-env-matrix` (the PARENA rules' purity/clamps, the clear-weather no-op, the
stormy-night regression through the exact surface function the renderer calls, the publish→surface
wiring, determinism, and the emitter trade-off). `make test-lighting-chain` then sweeps the WHOLE chain — every lighting preset × every weather state ×
slider 0/5/10 × three face directions — and asserts the invariants that must hold for all of them: a
shaded face never gets darker, nothing ends below the floor, no result escapes [0,1] or goes NaN, the
clear-noon-vs-clear-midnight swing survives (the matrix must not flatten the world), and the same
inputs give bit-identical pixels. Supporting that needed one real cleanup: the sun/fog evaluation
moved into a GL-free `packages/render/retro_sky_eval.h`, so `retro_lighting.c` no longer drags
`<SDL2/SDL_opengl.h>` in just to do arithmetic.

Not verified: `retro_sky.c` and the lobby are GL-bound and this sandbox has no SDL2 headers, so
`make test-ambient-boost` (recipe updated for the new link dependency), the `main.c` wiring and the
live look could not be built or seen; and `apps2/`'s duplicate tree was left alone.

## Current visual direction

SHANKPIT is pursuing a deliberately stylized visual identity rather than realism-first rendering.

Key traits in the current renderer include:

- **legacy OpenGL immediate-mode rendering**
- **retro sky and retro lighting systems**
- **procedural texture hooks**
- **distance fog / haze tuning per scene**
- **weather-driven fog** (`packages/render/sky_weather.c`, 2026-09-27): each weather profile's fog in
  `assets/skybox/default.cfg` becomes a real visibility distance (clear noon ~3000 world units, rain ~850,
  storm ~540, plus dawn radiation fog that burns off by mid-morning). The whole world pass -- fixed-function
  geometry and the GOLDENBAND skinned-mesh shader -- fades out by that distance into the same colour the sky
  washes to; the sun shows as a pale disc through thick fog. Verified with headless Xvfb screenshots of the
  default arena and VOXWORLD. Limits: fog is uniform (no ground-hugging height fog, no directional sun glare),
  emissive light panels/glow billboards are not fogged, and the story cave is left on its own fixed haze.
- **weather now reaches the walls, not just the sky dome** (`sky_weather_light_state`, 2026-10-08): a storm
  used to darken only `sky_weather.c`'s own dome/cloud/precip visuals -- `RETRO_LIGHTING_DYNAMIC`'s scene
  ambient/sun/moon/fog (`retro_lighting.c`) never read the weather at all, a real gap named since the
  2026-09-22 engine-merge pass and never closed until now. New `sky_weather_light_state` adjusts the
  already-evaluated `RetroLightingState` in place using the sky's own already-smoothed `cover`/`storm`
  (dims ambient/sun/moon, floored so a storm at noon is dim daylight, not night) and `fog_wash`/
  `visibility`/`fog[3]` (blends world fog toward the sky's real weather-matched colour, tightens draw
  distance) -- the same numbers already driving the dome, reused rather than re-derived. Called once,
  unconditionally, for every mode. Plain host C (matches this file's own existing blend math), not PARENA.
  **Not build-verified**: `sky_weather.h`'s `SkyWeather` struct holds `GLuint` fields, so the file can't
  even be parsed without real SDL2/GL dev headers, which this sandbox doesn't have -- the same pre-existing
  limitation every GL-touching change in this README already carries. Verified instead by a careful,
  field-by-field hand check against both structs' real current definitions.
- **terrain shading from sampled normals**
- **“neon brutalist” block rendering**
- stylized accent elements like hot pink trails and cyan grid motifs
- scene-specific readability tuning for larger outdoor worlds and tighter indoor spaces. :contentReference[oaicite:12]{index=12} :contentReference[oaicite:13]{index=13}

The codebase is intentionally keeping a cheap, centralized “fake baked” atmosphere model so that look-and-feel can evolve quickly without re-architecting the entire game. :contentReference[oaicite:14]{index=14}

---

## Current networking model

SHANKPIT uses a **server-authoritative UDP model**.

The current protocol includes at least these packet types:

- `PACKET_CONNECT`
- `PACKET_USERCMD`
- `PACKET_SNAPSHOT`
- `PACKET_WELCOME`
- `PACKET_DISCONNECT`. :contentReference[oaicite:15]{index=15}

The client currently includes:

- client-side command history
- interpolation state for remote players
- reconciliation correction values
- spawn synchronization logic
- snapshot timing and time offset tracking
- structured network diagnostics and timeout logging. :contentReference[oaicite:16]{index=16}

The lobby client defaults to:

- host: `shankpit.okemily.com` (S459-36 -- the old `s.farthq.com` default silently pointed at a
  stale, different box; replaced with a fresh, never-cached hostname matching BRAWLPIT's own
  `<game>.okemily.com` convention rather than fighting DNS caching on the old record)
- port: `6969`. :contentReference[oaicite:17]{index=17}

This is still an actively evolving stack. The project favors clear packet semantics, aggressive diagnostics, and playable iteration over excessive abstraction.

---

## Team play and objective systems

SHANKPIT now has real team-mode support in both rules and world presentation.

That includes:

- team base markers
- team IDs on net players
- bot identity fields on net players
- team deathmatch variants
- online team deathmatch mode (`TDMO`)
- capture-the-flag state and rendering
- CTF pedestal / base anchors
- dropped / carried / home flag rendering logic. :contentReference[oaicite:18]{index=18} :contentReference[oaicite:19]{index=19}

A dedicated regression test exists for **CTFB carry melee**, confirming that flag carry attack behavior reuses the intended melee envelope and cooldown logic. :contentReference[oaicite:20]{index=20}

---

## Terrain and world systems

The project now includes an actual terrain package rather than relying only on block-map geometry.

Current terrain support includes:

- heightfield allocation and cleanup
- height sampling
- world-space containment checks
- normal sampling
- terrain rendering in the client
- terrain debug overlays
- scene-specific route / spawn / objective anchors over terrain-backed worlds. :contentReference[oaicite:21]{index=21} :contentReference[oaicite:22]{index=22}

This matters because SHANKPIT is no longer just a box-map shooter. It is now supporting larger outdoor spaces, racetrack-style terrain, canyon spaces, island spaces, and other scene-specific layouts. :contentReference[oaicite:23]{index=23}

---

## Bots

There are currently at least two bot-side code paths in the construct:

- a more direct / older bot sender
- a dedicated `apps/bot_client` implementation with a simple “brain” file, target acquisition, reward accumulation, and autosave behavior. :contentReference[oaicite:24]{index=24}

The current bot client can:

- connect to the UDP server
- load or initialize a brain file
- track visible players by scene
- target enemies
- shoot / move / strafe
- randomly jump / crouch
- save updated brain data after hitting a reward threshold. :contentReference[oaicite:25]{index=25}

Bot support is also part of the product strategy for team modes so the game remains playable before a large human player population exists. :contentReference[oaicite:26]{index=26}

---

## Skins

The current skin list is:

- BAT
- MAYRICE
- CYBORG
- PIRATE
- NINJA
- PIMP
- VIKING
- BILL
- GENIE
- WANDERER
- PINK
- GEISHA
- ALPINE. :contentReference[oaicite:27]{index=27}

Skin selection is persisted through:

- `shankpit_skin.cfg`. :contentReference[oaicite:28]{index=28}

The lobby also includes a dedicated **SKINS** entry. :contentReference[oaicite:29]{index=29}

---

## Repo shape

From the current construct, the repo includes work across these kinds of areas:

- `apps/lobby`
- `apps/server`
- `apps/bot_client`
- `apps/tests`
- `packages/common`
- `packages/simulation`
- `packages/render`
- `packages/world`
- `services/game-server`. :contentReference[oaicite:30]{index=30} :contentReference[oaicite:31]{index=31}

This reflects the current direction of the project:

- small native apps
- reusable low-level packages
- simulation and rendering split into packages
- services separated from the core client/server gameplay binaries.
## Bullet holes and destructible brick (2026-10-01)

Two world-reacts-to-your-weapon systems, both in the lobby/server build and both with a pause-menu toggle:

- **Per-gun bullet holes.** Every hitscan shot leaves a Half-Life-style decal on the wall, a different hole per
  gun (magnum / AR / shotgun / sniper). The textures are PARENA programs rendered by NOCK and served from
  IDUNA's `nock_textures` (live fetch, checked-in fallback). `make test-bullet-hole`.
- **Built-in cityscape (restored 2026-10-02).** The procedural `SCENE_CITY` removed by the 2026-02-14 "Update ..."
  commits is back as geometry + a level entry: a 25x25 district grid of towers with alleys/ramps/ruins, an elevated
  highway ring with ramps, curbside hydrants (~1,070 collision/render boxes), soft world edge + kill plane. Pick
  **CITY (BUILT-IN)** (row 0 of the LEVELS menu) or run `shank_lobby --city`. `make test-city` (geometry, spawns, bounds)
  and a real Xvfb screenshot verify it. Honest limits: single-player local deathmatch only -- not in the server
  rotation, no portal from the garage, and the old city alive-sim (huntsman, field agents, dragon heat events) is NOT
  restored (separate card); the ground slab is unlit-dark at night.
- **Third person (fixed 2026-10-02).** The orbit camera put the camera 6 units *in front of* the player looking away
  (z sign flipped), so the character was never visible; it now sits behind. It is also pulled in so it never clips
  into a wall, and shots are aim-bridged (`packages/common/third_person.h`): the eye aims at the point the camera's
  centre ray hits, so hits land on the crosshair instead of parallax-missing. **V** toggles first/third person in a
  local match (`--city --third` starts there). `make test-third-person` + a real Xvfb screenshot verify it. Honest
  limits: the V toggle is local-match only (in a networked match `third_person` comes from the server snapshot);
  the old Feb-2026 shoulder-offset reticle/ADS camera is not restored.
- **Rotated cubes and ramps (not a grid world).** A NOCK cube can carry free rotation (`rot_x/rot_y/rot_z`, Euler degrees, Rotate-mode
  gizmo with 15-degree snap or arbitrary) and a **Ramp** checkbox (still a cube in the data; a real wedge in game). Server and client
  physics (`packages/common/obb.h`, shared) collide the player (four stacked spheres), hitscan/`trace_map` and ground height against
  oriented polytopes; walkable slopes (< 60 degrees) lift the player, steeper faces act as walls; the lobby renderer draws the
  rotation/wedge; leaving a ramp onto a plain cube steps up the small seam lip. `make test-obb` (pure math + real engine paths, ASan/UBSan);
  the live registry level `TUTORIAL_RAMP` (ramp, turned ramp, 45-degree cube, tilted plank) is a worked example and was walked
  through the real engine paths from its live export. Honest limits: never seen in a live GUI/match; rotated
  cubes skip the glass/emissive/specular extra passes and the per-fragment litbox shader and are never brick-fractured; bots,
  zombies, projectiles and buggies still treat a rotated cube as its un-rotated AABB.
- **Destructible brick.** On any level (NOCK/registry level, any mode), a box whose material is `brick`, `concrete`, `wood` or `glass`
  can be shot, blasted and breached (per-kind HP/resistance from PARENA: concrete 120 HP, wood 60, glass 10 and shatters
  on any hit; **glass** also has its own transparent `glass` shader whose cyan RGBA tint is a PARENA function,
  `on-glass-tint` in `brick_rules.prn` -- create a material with shader `glass` in the NOCK materials screen; the render
  pass is verified to compile/link on a real GL context but a glass box in a live level has NOT been screenshotted yet): the box is overlaid with ~3-unit cells, each with HP (PAPERCRAFT's Paper Engine
  decisions -- material resistance, damage tiers, distance falloff -- plus SHANKPIT weapon rules, all PARENA:
  `PARENA/stdlib/papercraft/*` + `stdlib/shankpit/brick_rules.prn`). A destroyed cell becomes a real hole: the
  surviving volume is re-expressed as ordinary collision/render boxes, so movement, hitscan, rockets and
  lighting all see it. Rockets open player-sized holes; blades don't scratch it; floors/slabs, doors and
  other materials (metal, lights) are indestructible. The server is the only authority (`PACKET_BRICK_STATE`,
  idempotent per-cell HP, repeated and refreshed so a late joiner or a lost datagram converges); cracked/gone
  cells throw papercraft-style debris and take their bullet-hole decals with them. Example level in the
  registry: **TUTORIAL_BRICK** (LEVELS menu). `make test-brick` (engine + real-engine integration, ASan/UBSan),
  `make brick-e2e` (real server + real packet clients). Honest limits: custom-level (NOCK) maps only -- the
  hand-built legacy scenes (garage, stadium, ...) are not destructible; melee does not damage brick; one shared
  900-piece geometry budget per level (a damage that would exceed it is refused, never half-applied); damage
  resets with each match.
- **Physical broken brick and falling geometry (2026-10-16).** Broken brick is no longer massless particles: it is rigid
  bodies on GOLDEN BAND's XPBD solver (`packages/goldenband/grb.c`, wrapped by `packages/simulation/brick_bodies.{h,c}`).
  A cell that is shot or blasted out throws two **shards** (0.2 m blocks; glass a thin plate) with `mass = density x volume`
  (brick ~15 kg) that inherit a fraction of the impact -- a bullet's direction at 3% of 300 m/s, a blast's radial push -- then
  drop, bounce, tumble, heap up on the floor and slide/tumble down ramps (oriented boxes and ramps collide through `obb.h`).
  Separately, **geometry that loses every connection to the world falls**: after each change a bounded flood fill
  (`bf_detect_detached`) checks the cells next to what just went; a region that reaches neither the ground nor any
  non-destructible solid (floor, frame, metal) is cut off, removed from the static collision at once and becomes one rigid
  chunk with mass = the sum of its cells -- shoot a circle out of a glass pane and the disc drops, with a seeded sideways drift
  and tumble (strongest for glass) instead of falling dead straight. Level-author geometry deliberately floated in mid-air
  keeps its majority (only the minor fragment of a cut falls). Mass is **configurable per NOCK material**: the Materials screen
  has a Density field (kg/m^3; migration `202610160005_shankpit_materials_density.sql`, exported as `density`; 0 = engine default:
  brick 1900, concrete 2400, wood 600, glass 2500 -- "made up", editable). The server only decides which cells are gone
  (the chunk id rides the `PACKET_BRICK_STATE` pad byte); each client simulates its own bodies, so they are cosmetic and
  not synced between machines. `make test-brick` covers mass, falling, piling, the ramp slide, detachment (ground, frame,
  floating, client mirror) and a glass chunk through the real engine glue. Honest limits: bodies do not collide with
  players or push anything; chunk shape is its bounding box and chunk-vs-body collision does not exist (shards pile on each
  other as cubes, a deliberate fake); chunks over 256 cells stay put; the bodies are cosmetic per client (a late joiner sees
  none of an earlier fall); not yet confirmed visually in a live match (headless tests only); the Windows build compiles the
  same code but is CI-only.

## Broadcast cameras, director and native stream (2026-10-02)

For esports: **F3** (or pause menu -> BROADCAST CAMERAS) puts you behind a camera rig. The *program* camera fills the
screen as a clean world view with a red LIVE tally; the other cameras run live in **multiview** tiles along the bottom
(program red, next-up green; **F5** toggles). A rig is any mix of fixed tripods, follow-cams, orbiters and drones, each run
by a simulated **operator** -- it lags the action by a human reaction time (rookie 700 ms, veteran 120 ms), leads a moving
subject, zooms tighter on distant action and shakes a little -- and an **auto director** that scores every player
(close to an enemy, moving fast, just got a kill, shooting, low health), holds each shot a minimum time and only cuts to a
clearly better one. **F4** hands the cuts to a human (`[` `]` pick the camera, `,` `.` pick who to watch). All of those
decisions are a PARENA mod (`PARENA/stdlib/shankpit/camera_rules.prn`, hand-derived tests, `make regen-camera-rules`).
The rig is a JSON file -- `shankpit_camrig.json`, loaded at startup, saved when broadcast turns off, round-trips exactly,
bad files are rejected -- the scene collection of a native stream. `make test-camera-rig`.

**Native stream (the OBS replacement):** **F2** (or pause menu -> STREAM OUT) pipes the *clean* program feed (no HUD, no
tiles) out as raw video into an encoder: `rtmp://...` goes out as FLV (set `SHANKPIT_STREAM_URL` or put it on the first
line of `shankpit_stream.cfg`), anything else is a file (default `shankpit_stream_out.mp4`). Targets are whitelisted before
they ever touch a shell. **The encoder is ffmpeg -- a labeled STOPGAP** (replacement PARENA H.264 encoder + RTMP muxer are
logged in `EMILY/BACKLOG.md`). **ffmpeg is NOT bundled** (card #618) -- install it onto `PATH` on Windows or Linux first: `docs/FFMPEG_INSTALL.md`.
`SHANKPIT_STREAM_SINK_CMD` swaps in any program that reads raw rgb24 on stdin.
`make test-stream-out` runs headless with no ffmpeg.

Verified live: headless Xvfb screenshots of the real lobby (`--city`) in broadcast mode -- follow cam, orbit cam, three
multiview tiles, manual camera switching -- and a 1280x720 stream of 88 whole frames captured to a raw file through the
real capture path, one frame decoded: clean program view, right way up. Honest limits: **ffmpeg is not installed on this
machine, so the actual encode/RTMP path (the ffmpeg command line) has not been run** -- only its construction and the
capture-and-pipe are tested; the stream carries no audio; it runs at the window's size; the director does not check line of
sight; cameras are not yet editable inside the game (edit `shankpit_camrig.json`, or NOCK later).

## Hammer (weapon 8, key 8) -- the demolition tool (2026-10-02)

A heavy melee tool built for destructible brick. A swing hurts a player in reach (70 damage, no headshot bonus: two
honest hits kill; slow 42-tick swing, no ammo, no reload) **and strikes the wall in front of you** (reach 7.5 units)
through the same map-damage surface hook zombie claws use; PARENA's `brick_rules.prn` counts the hammer x3 against
masonry, so a 6-thick brick wall opens in 2 swings (the magnum needs ~7 bursts; blades still do nothing).
`make test-hammer` (physics: hurt, cooldown, reach, hook) and `make test-brick-world` (real destruction).

**The model comes from NOCK.** Model the hammer in Blender, import it through the Widgets tab's glTF importer, and save it
as the widget **`MODEL_HAMMER`**. IDUNA serves it read-only at `/api/v1/shankpit-models/MODEL_HAMMER` (only `MODEL_*`
widgets are public); the lobby fetches it in a background thread at startup, caches `shankpit_model_MODEL_HAMMER.json`,
and draws its boxes in hand in first AND third person. However it was modelled, the client puts the longest axis along
the hand, the heavier end forward and scales it to fit (`packages/render/held_model.h`, `make test-held-model`). With no
widget (or offline on a first run) a built-in wooden-handle/steel-head hammer is drawn, so it is never invisible.
Honest limits: no dedicated hammer sound (reuses the katana swing), the 1p swing is the shared recoil animation, the
widget model is boxes (one per Blender mesh object, rotated parts become their bounding box), MAX_WEAPONS went 8 -> 9
so a server and client must both be on this build, and none of it has been seen in a live match yet.

## Assault rifle grenade (weapon 3, E / right click / controller LT) (2026-10-30)

The AR's ability is an underbarrel grenade, fired by **E**, by **right click**, or by the
controller's **left trigger** (fire is the right trigger). Right click is a separate input bit
(`BTN_ABILITY_2`) rather than a copy of E, because right click is also the sniper's zoom -- a shared
bit would spend the sniper's storm charges every time someone scoped. Both AR inputs share one
cooldown (260 ticks), so alternating them does not double-fire.

It is a real travelling projectile, tuned by `protocol.h`'s `AR_GRENADE_*` constants: 2.2x the base
4.0 units/tick (~9 units/tick -- fast like a rocket, but you watch it fly), `AR_GRENADE_GRAVITY`
0.22 units/tick² so it **arcs down** instead of flying dead straight, 75 damage with linear falloff
to 40% at the 4.5-unit rim. Detonation goes through the same `explode_splash` path the missile uses,
and the blast now carries the *firing weapon's own id* (`Projectile.weapon`), so it carves
destructible brick with the AR's x2 multiplier from `brick_rules.prn` instead of a rocket's x5: a
grenade opens a hole in a wall, a missile deletes the wall.

Visually it is a real GLSL fireball (`apps/lobby/src/main.c`, same hand-loaded GL 2.0 path the
flashlight beam uses): one camera-facing billboard per detonation with layered value noise over a
sphere normal, a white-hot core cooling to orange then smoke, a shockwave ring racing out ahead of
the flame, a second shell for the first third of a second, a twelve-ember spray, and a fading trail
on the round in flight. Additive blending, so it lights what it lands on. No shader support degrades
to an additive glow quad. It is **not** a port of Bedrock Racers' fire -- BR's flames are JS/WebGL2
particle data in WOTAN, and this renderer is fixed-function GL 1.x -- so the shader was written for
this pipeline.

Limits, named: net clients still render no projectiles at all (pre-existing: projectiles are not on
the wire), so in a networked match the grenade is server-side only until that is built; and the look
of the shader is unverified here (no display/GPU in this sandbox) -- `make test-ar-grenade` covers
the numbers, not the pixels.

## Magnum spray: right click dumps the cylinder (2026-11-01)

The magnum's secondary fire (right click, or the controller's left trigger) is a **held, full-auto
mag dump**: while the bit is down the revolver fan-fires every 4 ticks -- 15 rounds/s, so all eight
rounds are gone in about half a second -- and then the normal full reload starts on its own. No left
mouse is needed; holding both is the same spray. Each round is still the magnum's own 45 damage, so
the trade is the entire magazine and the reload, not a hidden damage nerf.

Accuracy is the other half of the trade. `MAGNUM_SPRAY_SPREAD` (0.16) is the widest player spread in
the roster -- four times the AR's 0.04, past even the shotgun's 0.15 -- so a spray that starts at
point blank is lethal and one pointed at something 200 units away mostly does not land. It is the
same `phys_rand_f()` jitter path every other spread weapon already uses, applied per round.

Right click is `BTN_ABILITY_2`, the bit the AR's grenade already owns, and the two do not collide:
the AR's grenade is an ability on a 260-tick cooldown, the magnum's spray is a firing mode that
spends no ability cooldown and grants no storm charges, so aiming the sniper (also right click)
still costs nothing. `protocol.h`'s `MAGNUM_SPRAY_TICKS` / `MAGNUM_SPRAY_SPREAD` are the knobs.

`make test-magnum-spray` covers it through the real `update_weapons`: the spray fires with no left
mouse, empties the 8-round cylinder in 37 ticks and starts `RELOAD_TIME_FULL` on that same tick, the
stock 25-tick left-click cadence is unchanged, a target at 5 units is hit while one at 200 on the
same line is never hit (where a single aimed shot does reach it), the bit does nothing for the
knife, and the AR's grenade still fires on it. Honest limit: verified headless only -- no window,
no muzzle flash, no audio in this sandbox, so the *feel* of the spray is unproven, and net clients
still see it only as the shooter's `is_shooting` animation.

## Vehicle controls: buggy chase camera + PARENA helicopter flight model (2026-10-02)

- **Buggy is warthog-style.** The camera is a true orbit around the buggy (mouse / right stick swing it, pitch is
  clamped), so the buggy is always framed -- the old camera sat on the wrong side of the car (a flipped z sign), which is
  why it "went behind the camera". The buggy body rotates toward the camera heading (`buggy_chase_steer_intent`),
  while driving, coasting and reversing; a parked buggy doesn't spin chasing the mouse.
- **Helicopter flight model is a PARENA mod** (`PARENA/stdlib/shankpit/heli_rules.prn` -> `packages/simulation/
  heli_rules.c`, `make regen-heli-rules`). Space/Ctrl move a *collective lever* (hover = a lever setting that relaxes back
  to it); W/S pitch and Q/E roll tilt the airframe (it lags the stick) and the *tilted thrust* is what accelerates it, so
  leaning costs lift and a hard bank sags unless you pull collective; A/D pedals command a yaw *rate* that lags. Fixed:
  the chase camera was in front of the helicopter, A/D turned the wrong way and Q/E were swapped. Without the rules
  installed `heli_simulate_step` keeps the old arcade mover.
- Tests: PARENA `make test-heli-rules` (hand-derived numbers), SHANKPIT `make test-heli-flight` (hover, climb, lean-then-go,
  sag, turn direction, buggy chasing the camera, forward/coast/reverse). Honest limits: verified headless only -- not flown
  or driven live; the server needs a rebuild/restart for helicopters to fly the new model; the duck is not changed yet.

## Buggy: walls, placement, PARENA handling, lighting (2026-10-02)

- **Walls are solid.** The buggy used to drive straight through every box (its sim had no collision at all, and one
  tick at top speed is longer than a thin wall). It now sweeps against the level's boxes -- shared by server and
  local play, so multiplayer is authoritative -- scrubs speed on a hit, drives over a curb under 0.6 units, rides on
  box tops (platforms, ramps) and drives under anything higher. A wall blasted open by destructible brick is drivable.
- **Placeable in levels.** A thin wall named `buggy_spawn` / `buggy_spawn_<degrees>` in a NOCK level -- or in a
  placed widget, so a "garage bay" is reusable and rotates with its placement -- exports a `buggy_spawns` entry and
  the buggy spawns on it (NOCK: "+ Add buggy spawn" in the level and widget editors).
- **Programmable.** Handling -- the 4-band transmission curve, top speeds, steering rate/authority, lateral grip -- is
  a PARENA mod (`PARENA/stdlib/shankpit/buggy_rules.prn`, generated into `packages/simulation/buggy_rules.c`,
  `make regen-buggy-rules`). Defaults reproduce the old constants (240-tick run: 759.6 vs 759.8 units); a different
  mod changes the buggy (verified: a 2x top-speed mod). Fixed-point permille, so it is deterministic.
- **Lighting (same sprint).** Map boxes are lit per pixel: flashlights are Gaussian spot lights (no cone edge, the
  beam mesh fades with no hard rim), IPS/HPS fixtures are point lights that light big walls properly. Pause menu
  has an **AMBIENT LIGHT** slider (LEFT/RIGHT, persisted in `shankpit_display.cfg`, mapping is PARENA
  `ambient_rules.prn`).
- Tests: `make test-buggy-wall`, `test-litbox-render` (live GL under Xvfb), `test-ambient-boost`. Honest limits: the
  buggy is verified headless only (no live in-game drive yet), a spawn tile's facing is its name suffix, the
  tile's own `y` is not a floor for buggies beyond the box-top rule above, and NOCK/IDUNA need a rebuild + restart
  to serve the new editor buttons and export.

## ZOMBIES sandbox (2026-10-02)

The main-menu **ZOMBIES** tile (it replaced "FIND CTF") starts a local sandbox on a persistent level -- the
zombies repository's default, else the registry's **nextown**, else `var/zombie/nextown_zombies.json`
(`shank_lobby --zombies` goes straight in). A day/night clock drives a BIG_O-derived population
(`witness_ai_zombies_tick`): citizens wander by day, zombies rise at dusk into a horde at night and burn off at
dawn, The Men clean up witnesses, and **five birds** (BIG_O's avian coalition: they watch the watchers and a
signaling flock's beacon wakes zombies) are there from the first tick. Every mover steers around building walls
and **learns the city roomba-style** (#522): a shared coarse grid fills in from what movers feel (a proximity ring and
whisker fan when blocked, stuck detection when they stop making progress), and A* over that learned map lets a
citizen/bug/cornered zombie back out of a dead-end pocket instead of grinding into its far wall (unit-tested with a
U-shaped trap; only `make test-witness-ai-zombies`-level verified, not played through live yet), while a hunting zombie whose way is walled off **claws the wall** -- brick breaches (other materials stay
indestructible; that is an engine limit, not a ZOMBIES one). Citizens, The Men and zombies use the UAL mannequin
(zombies the UAL-derived `zombie_*` clips) and fall as XPBD rigid-body ragdolls. F1 / exit autosave in ZOMBIES file
the wrecked level into IDUNA's **zombies repository** and make it the next ZOMBIES level; NOCK has a "Set for
ZOMBIES" button and a ZOMBIE Levels list. `make test-witness-ai-zombies`. Honest limits: local play only (the
networked client has no role info, so online zombies render with default skins); ragdolls spawn from the rest pose
(the living clip pose isn't captured); needs the IDUNA deploy for the zombies repository; rendering of the new
NPCs was exercised headless for crashes but not eyeballed in a screenshot yet.

ZOMBIES zombies **wander and get hungry** (2026-10-04) rather than always hunting: a hero within sense range
(grows with the zombie's own mood/hunger/aggression alertness) is locked on and chased/remembered for a short
window, a far one is eventually found by hunger scent, and they still spawn 72-130 units out preferring spots
**walled off by a building** so they come round a corner instead of popping into view.

ZOMBIES zombies also **think with the OG train-mode neural net** (2026-11-03) -- the shipped
`shank_brain_horde.pth` checkpoint (`packages/simulation/brain_weights.h`, 8 -> 256 -> 128 -> 4) runs once per
zombie per tick in `packages/simulation/zombie_brain.c`, and `PARENA/stdlib/shankpit/zombie_brain_rules.prn`
decides how each CLASS reads that intent: a BRUTE believes a hero 100 units away is 1200 permille away (so it
commits late and hard) and may only weave 0.35 deg, where a RUNNER reads 900 and weaves 1.5 deg, and the net's
own shoot channel commits a lunge at 300 permille on a BRUTE but needs 700 on a SHAMBLER -- so the same net
charges from AGITATED on one body and hesitates on another. This was MEASURED, not assumed: fed raw the net
walks a zombie **backwards** (fwd -0.8, 86 units of retreat in 600 ticks, in its absolute frame, its own frame,
and its own frame with true yaw -- it was trained to kite a rifle, which a zombie has no affordance for), so the
target bearing still decides *where* and the net decides *how*: the weave, the jink, the 0.55..1.0 commitment and
when to lunge. After that mapping every class closes 100 units to 0.00 in 600 ticks. Enabled in ZOMBIES only and
switchable off with `SHANKPIT_ZOMBIE_BRAIN=0`, which leaves the hand-tuned chase exactly as it was.
`make test-zombie-brain` (76 checks) and `make test-witness-ai-zombies` (ASan/UBSan) both green. Honest limits:
nothing here learns -- it is a forward pass over the shipped weights, no training path; the checkpoint's
absolute-coordinate inputs make it map-specific, and the real fix is a checkpoint trained on horde observations;
and no renderer on this box means the weave has never been watched, only measured.

Population events are now **REFLUX**-evented (2026-10-08) -- `REFLUX_ACTION_ZOMBIE_SPAWNED`/`_HARVESTED`/
`_MOOD_ESCALATED`/`_WITNESS_ESCALATED`/`_GIANT_BUG_ATE_ZOMBIE`/`_MEN_DISPATCHED`/`_MEN_RESOLVED`
(`packages/reflux/reflux_runtime.h`) dispatch at the real moment a zombie spawns/dies/goes loud, a citizen
crosses into SILENCING/ENGAGE, a Giant Zombie Bug eats a zombie, or The Men dispatch to/resolve a hunt, and the
bird flock alerts on the citizen-escalation one too (closing the avian coalition's 3rd "observing the observer"
channel). **This population now has a real subscriber** (2026-10-08, same-day continuation):
`packages/simulation/zombies_hud_bridge.c` polls the log, asks a small PARENA rules module
(`zombies_awareness_rules.prn`) which of those events deserve surfacing, and feeds a ported BIG_O/NORTHSTAR.md
§35 compass+intensity readout (`packages/common/awareness_compass.h`) -- a bottom-left `! ZOMBIE (E) 90%`-style
HUD line for 3 real seconds, drawn by `draw_zombies_awareness_hud` in `apps/lobby/src/main.c`. The bridge's own
logic is build+test-verified (`zombies_hud_bridge_test.c`, run manually, no Makefile target -- same precedent
`world_alert_bridge_test.c` already set); the HUD draw call itself is **not** build-verified in this sandbox
(no SDL2 dev headers here, and `make lobby` has a separate, pre-existing, unrelated blocker -- a missing
EDITOR.GAME sibling checkout). `make server` and `make test-witness-ai-zombies` (ASan/UBSan) are both clean.

ZOMBIES/SURVIVAL also now have a real **pheromone command tool** (2026-10-08, later the same day --
BIG_O/NORTHSTAR.md §10's own "tactical pheromone arrays... paint a target"): hold **G** to throw a marker
along your own facing (rate-limited, no spam), and a zombie that doesn't already have you locked on will
steer toward it instead of wandering -- `packages/common/pheromone.h`'s own pure targeting primitives
(ported in an earlier pass with no live consumer at all) finally have one. A zombie that already has you
locked on keeps chasing you instead -- `zombies_pheromone_should_steer_to_marker`
(`PARENA/stdlib/shankpit/zombies_pheromone_rules.prn`) is the real decision, matching this engine's own
"a hunt doesn't get cheaply interrupted" instinct. Works the same in real networked multiplayer and in the
local sandbox: `BTN_PHEROMONE` (`packages/common/protocol.h`) travels the same `UserCmd` path every other
button does (`shankpit_apply_usercmd_inputs`, `packages/common/net_sim.h`), read once per tick in
`packages/simulation/local_game.h` for whichever player pressed it. A real, live-found correctness bug
was caught and fixed in the same pass: the melee-damage check originally kept firing off the *current*
target distance even when that distance belonged to a marker, not the hero -- a zombie merely standing
near a marker could not actually hit you, but the code would have let it; fixed with a dedicated
`targeting_hero` flag. `make server` and `make test-witness-ai-zombies` (ASan/UBSan, 3 new assertions
covering cooldown, marker-recruits-a-wandering-zombie, and hero-beats-marker-when-already-locked) are both
clean. Same honest limit as BIG_O's own v0: no projectile arc, no acoustic pingers. (Checked 2026-10-08,
later the same day: citizens/The Men DO react to a commanded zombie once it escalates to HUNTING/
FRENZIED -- the flee check and the witness/dispatch chain both key on mood alone, with no regard for
whether a hero lock or a marker drove the escalation, so this needed no new code, only checking.) The
client-side G-key
binding itself is pattern-matched against already-compiling sibling code but, like the HUD line above, not
build-verified against a real SDL2/GL build in this sandbox.

## SURVIVAL (2026-10-02)

The **SURVIVAL** tile (`shank_lobby --survival`) is local wave defence on the built-in **SCENE_CITY**. Wave *n*
is 4+3n always-hunting zombies (at most 5+n on the field), spawned hidden behind buildings near you; clear the
field and, after a 6 s rest, the next wave starts. The HUD shows the wave and zombies left; there is no
day/night, citizens or birds (that is the ZOMBIES sandbox). Logic: `witness_ai_survival_tick`
(`make test-witness-ai-zombies` drives it to wave 3). Honest limits: offline only, no score/leaderboard, no
dedicated game-over screen beyond the HUD line, tuning numbers are first guesses.

**Guns are ground items (card #487).** You start with the knife and flashlight only; 12 guns (colour-coded boxes
with a light beam) lie in a ring around the spawn, and a killed zombie has a 35% chance to leave one (magnum 30 /
AR 25 / shotgun 20 / katana 10 / sniper 10 / missile 5). Walk over one to take it: it is equipped with a full
magazine, and number keys only select weapons you own. Logic: `packages/simulation/gun_items.c`
(`gun_items_test.c`, hand-derived). Limits: offline only -- not server-authoritative, so the Fortnite-style
competitive multiplayer survival map this card describes is still to do; ground is assumed flat at y = 0; no
pickup sound or on-screen prompt.

## BIG_O LAB level (2026-10-02)

`shank_lobby --lab` (or SURVIVAL -> MODES -> LAB) loads the **LAB** level: INTERRIOR_1 cloned and dressed with seven lab station
widgets created programmatically through IDUNA's `LevelStore`/`WidgetStore` API (`IDUNA/cmd/shankpit-lab-builder`, run on a DB
copy; the result is checked in as `var/lab/lab.json`, and a registry level named LAB wins if one is published). Tiles named
`lab_<kind>` export as `lab_stations`; walk up and press **E**: SPLICE bench (sample -> clone), CENTRIFUGE (2 samples -> 1 of the
next type), PCR (amplify), CLONE VAT (decant a clone, +25 hp), SAMPLE FRIDGE (read-out), CONSOLE (opens the phone's LAB app), WAR
TERMINAL (deploys your whole clone roster into the shadow war, see below). Every rule is PARENA
(`PARENA/stdlib/big_o/lab_station_rules.prn` -> `packages/simulation/lab_station_rules.c`; `make test-lab-station`,
`regen-lab-station`, `check-lab-station`). The BIG_O phone (P) and the Orb (backquote) work inside the lab; ZOMBIES kills feed the
same samples. Status: headless-verified (renders, E on the splice bench splices); **not published to the live registry** (needs an
IDUNA deploy + admin write), and the station art is blocky placeholder boxes.

## Basement shadow war (2026-10-08, "continue full game")

The basement's **async shadow war** (BIG_O/NORTHSTAR.md §1/§4: "splice, breed, deploy, command via pheromones", ending in "an
algorithmic shadow war") is live as a real, deterministic battle sim, closing the one BIG_O system
`docs2/specs/BIGO_ENGINE_MERGE_NORTHSTAR.md` §3 and this engine's own gap ledger (`packages/simulation/witness_ai.h`) both still
named as entirely unbuilt. Walk up to the LAB's new **WAR TERMINAL** and press **E**: your whole clone roster (base x trait,
`packages/common/phone.h`'s own `Phone.clones[]`/`clone_traits[]`) deploys against a same-size bot "rival cell" army, both sides
pick a command (ADVANCE/HOLD/SCATTER) and the battle resolves instantly over up to 20 deterministic ticks -- a win, loss or draw,
an Elo rating (starts at 1200, floored at 100) that actually moves, and every clone sent is expended either way. Every rule --
unit power (BRUTE > HOUND > SCAVENGER at every trait; a non-obvious one: at equal power, HOLD actually blunts ADVANCE, not the
other way around), command multipliers, per-tick damage, the deploy gate -- is PARENA (`PARENA/stdlib/big_o/shadow_war.prn` ->
`packages/simulation/shadow_war_rules.c`; `make test-shadow-war`, `regen-shadow-war`, `check-shadow-war`). Elo itself stays plain
host math (`packages/simulation/shadow_war_host.c`) for the same reason `lab_sim.c`'s own curves did: it needs real
exponentiation, not PARENA's integer-only scalar model. Dispatches `REFLUX_ACTION_SHADOW_WAR_RESOLVED` (no subscriber yet, same
"reserved for later" shape most new REFLUX actions in this engine start with).

**Verified, not just compiled:** `make test-shadow-war` (determinism, mirror-army symmetry, the HOLD-blunts-ADVANCE property, a
strict-superset army never doing worse, a malformed/out-of-range army never crashing, Elo sanity including the floor, bot-policy
determinism) and `make test-lab-station` (the real WAR TERMINAL path through a live `Phone`, end to end) are both ASan/UBSan
clean, and both PARENA regens are byte-identical on a second run. `make server` is unaffected (the lab/war system is client-local
only, same as the rest of the phone/lab). **Real, honest, deliberately NOT built here:** this is bot-only -- real two-human-crew
async PvP needs cross-session persistence (an IDUNA-backed match queue/Elo ladder), which is separate, scoped follow-up work, not
this pass's job; "quality" as a third unit dimension (named in `BIG_O/docs/SHIP_PLAN.md`'s own sketch of this system) has no real
source anywhere in the lab yet, so it isn't modeled, only base x trait; a battle is two aggregate power pools draining each
other, not individual units dying one at a time or a positional/grid fight; and the WAR TERMINAL's own client-side widget (placed
in `var/lab/lab.json` directly, since this sandbox has no reliable path to the live NOCK registry) is pattern-matched against
already-compiling sibling code, not build-verified through a real SDL2/GL `make lobby` (same pre-existing, unrelated missing-
EDITOR.GAME-checkout blocker named throughout this README).

## Architect's Orb -- EduVM scripting terminal (2026-10-02)

A tiny scripting language you write in-game that drives physical machines, ported from the GoblinFoxDragon/SHANKPIT lineage
(card #494). Available in every local mode **except the multiplayer QUEUE**.

- Press **`** (backquote) to open the Orb. **F7** compile, **F8** run, **F9** clear the slot, **F10** reset the machines, **TAB** next
  slot (8 slots: Architect Trial, Orb Scan, Legacy Motion, Bubble Sort, empty...), **`** or ESC to close. Movement is frozen while it is open.
- Scripts are `let`/`if`/`while`, integer arrays (`let a = array(5); a[0] = 3;`) and builtins (`open_gate()`, `raise_bridge()`,
  `stabilize_portal(n)`, `scan_portal()`, `move_crate()`, `print(x)` ...). A glowing orb on a plinth and a small machine bay (crate, gate, bridge,
  portal) appear a few metres ahead of where the match started and react to the scripts. Solving the Architect Trial (stability >= 100, portal
  open, bridge raised, gate open) prints ARCHITECT TRIAL COMPLETE.
- Layout: the VM is `packages/education/` (C, vendored from GFD). `eduvm_mod.c` is **generated** from `PARENA/stdlib/shankpit/eduvm.prn`
  (`make regen-eduvm` / `make check-eduvm`), the PARENA mod bindings: compile/run a slot, read the machine state, the trial rule. `make test-eduvm` runs the mod over the real VM.
- Verified headless (Xvfb): opened the Orb in SURVIVAL, ran the Architect Trial (gate/bridge/portal all set, stability 125), ran the bubble-sort slot.
- Fixed on the way: the GFD parser mis-rewound after a bare identifier, so any script whose statements began with a builtin call (not `let`) failed to compile
  ("unknown variable"). Only `let`-led scripts ever worked upstream. GFD's copy still has the bug.
- **Saveable snippets (card #495):** **F5** saves every non-empty slot to `shankpit_orb_slots.json` (next to the executable); it reloads the
  next time the Orb first opens. A background thread also fetches the snippets NOCK authors attach to the widget `ORB`
  (`GET https://okemily.com/api/v1/nock-edu-snippets?widget=ORB`, IDUNA's `nock_edu_snippets`, cached as `shankpit_edu_snippets_ORB.json`)
  and drops them into empty slots 5-8. Parse/serialize/placement are unit-tested (`make test-eduvm`). **Untested live:** the IDUNA endpoint exists in
  code and tests but is not deployed yet, and NOCK has no UI tab for it, so remote snippets are created over the admin API only.
- Limits: single-player only; the machine bay is fixed props, and snippets attach to the one `ORB` widget (per-placed-widget attachment is not built);
  the Orb is also an ORB app on the BIG_O phone (press P in MODE_STORY or MODE_ZOMBIES, ENTER on ORB; headless-verified in ZOMBIES).

### Programmable level widgets (2026-10-15, card #660)

A NOCK level can carry an `edu_widgets` array: named machines (gate, bridge, portal, crate, or a pressure-plate **sensor**) each bound
to one numeric EduVM register. Scripts in the Orb drive the level: `set_state("gate_1", 1)`, `set_reg(2, 1)`, `read_sensor(1)`,
`read_state("gate_1")` (an unwired register reads 0; scripts can never write a plate, only the host does, from a player standing on it).
A widget with `box_index` moves that wall out of the way when its state is 1, exactly like a story door (physics and render). Stand next
to a non-plate widget and press **E** to open the Orb; levels with widgets get a ready-made "Gate Sequencer" `while` loop in slot 8.
Try it: `examples/programmable/TUTORIAL_PROGRAMMABLE.json` (plate + three gates; see its README).
- Layout: schema in `packages/world/level_boxes.h` (`LevelEduWidget`), VM side in `packages/education/edu_bindings.c`, host wiring in
  `apps/lobby/src/main.c` (`lobby_edu_widgets_*`). `make test-edu-widgets` (VM builtins, sequencer loop, plate read-only, schema parser).
- **Verified:** VM builtins and loader headless; the lobby wiring (bind, plate sensing, door-style box moves, E-radius, world reset)
  ran in a stubbed harness against the real EduVM. **Not verified:** the actual lobby has not been built or run here (no SDL2/GLU), so
  drawing and the E key are untested live. Not built: NOCK editor UI / IDUNA store support for `edu_widgets` (levels are authored in JSON
  by hand today), networked play (the Orb is local-only), destructible-brick barriers in the tutorial.

## Menu: SETTINGS, CUSTOMIZE, baked levels (2026-10-02)

The last main-menu tile is **CUSTOMIZE** (SKINS, SPRAYS); a **SETTINGS** button under the APPS toggle opens
live-level downloading, bullet holes, brick debris and fullscreen (persisted in `shankpit_display.cfg`).
Every NOCK level is now **baked into the binary** (`packages/world/baked_levels_gen.h`, regenerate with
`make bake-levels` from a live IDUNA and commit the diff); the client uses those by default and never touches the
network for levels. **LIVE LEVELS: ON** prefers the network and falls back to the baked copy. A level added in NOCK
after the last bake is invisible with the setting OFF -- by design. The game server keeps resolving live
(`SHANKPIT_BAKED_LEVELS=1` makes it baked-only). `make test-baked-levels`. Not yet: the baked table has the
registry's levels at bake time only (the ZOMBIES default is baked, later zombies snapshots are not), and level
"bridge" can't be baked because IDUNA itself refuses to export it (121 boxes after flattening, max 100).

QUEUE clients now learn the level the server is on (`PACKET_QUEUE_LEVEL`, 1 Hz) and reload geometry when it
changes at a round boundary (`make test-queue-level`); previously a client kept the first level it fetched.

## Enemy outlines, shield effects, shield packs (2026-10-02)

- **Enemy outline (#538):** zombies, The Men, giant bugs and (in multiplayer rounds) other players you are not teamed
  with get a glowing inverted-hull outline from a GLSL shader -- yellow normally, pulsing red while they are in
  combat (shooting, just hit, zombie mid-attack; held 1.5 s so it does not flicker). Skinned mannequin bodies only: the
  old box-body skins have no outline. Because the queue mannequin itself is yellow, the yellow outline reads best
  against dark ground.
- **Shield hit effect (#539):** when a player's shield drops, a hex-lattice energy bubble (fresnel rim, ripple ring and a
  white-hot spot on the side the nearest other player is on) flashes around them; you get a blue screen-edge flash
  and a zap when it is you. Detection is client-side from the replicated shield value.
- **Shield break (#540):** shield reaching zero bursts the bubble (cracking lattice, expanding fade), throws ~56 blue
  shards, and plays a synthesized snare + crash cymbal (`audio_play_shield`, no sample files).
- **Shield packs (#541):** in QUEUE, a dying player has a 25% chance to drop a pack; walking onto one refills your
  shield to full (only if it is below full). A shield nobody refills also comes back on its own now: four
  seconds of nothing after the last hit, then a one-second tick worth 1, 2, 3 ... 10 points (rule in
  `packages/sim/shield_rules.prn`, ticked by `packages/simulation/shield_regen.h`). The server alone rolls and grants
  (`packages/simulation/shield_packs.h`, `make test-shield-packs`) and sends the whole set as `PACKET_SHIELD_PACKS`.
  **Needs the new server binary** -- an old server sends no packs and clients simply show none.
- Verified: shield + outline shaders compile and render under Xvfb/llvmpipe (screenshots of the hex bubble and of the
  real mannequin with yellow/red outline); the pack rules and wire format are unit-tested. Not yet seen in a live
  queue match (no GPU session, no running bot pool here) -- the live feel (bubble size, hit direction guess, sound
  levels) is untuned.

## Controls (Player + Vehicles)

### Core movement & combat
- `W / S`: move forward/back.
- `A / D`: strafe left/right on foot.
- `Mouse`: look (yaw/pitch).
- `Left Mouse`: fire weapon.
- `Right Mouse`: ADS/zoom (sniper narrows FOV).
- `Space`: jump.
- `Left Ctrl`: crouch.
- `R`: reload (on foot).
- `1..6`: weapon select.
- `F`: use/interact (portal use, enter/exit vehicles).
- `E`: ability 1.

if you are crouching when you land a jump you get a speed boost while you slide

### Helicopter controls
- `F`: enter/exit helicopter when in range.
- `W / S`: helicopter forward/back thrust.
- `A / D`: helicopter yaw (turn left/right).
- `Space`: ascend.
- `Left Ctrl`: descend.
- `E`: strafe left.
- `Q`: strafe right.

### Ground vehicle controls
- `F`: toggle enter/exit ground vehicle pads where available (ex: garage pads).
- `W / A / S / D`: drive/steer with the standard movement axes.

### Debug / client toggles
- `F6`: terrain wireframe debug.
- `F7`: terrain normals debug.
- `F8`: vehicle style toggle.
- `F9`: vehicle underglow toggle.
- `F10`: vehicle worklights toggle.
- `F11`: scene points debug.
- `F12`: VS0 art direction toggle.
- `Esc`: in a match, opens the pause menu (RESUME / BULLET HOLES / BRICK DEBRIS / FULLSCREEN / QUIT TO LOBBY;
  the three toggles persist in `shankpit_display.cfg`); in the lobby, backs out of overlays.

## System Architecture

### High-Level Components

```
┌─────────────────────────────────────────────────────────────┐
│                        SHANKPIT                              │
├─────────────────────────────────────────────────────────────┤
│  apps/lobby/          │ Main game client + rendering        │
│  packages/common/     │ Shared protocol & physics           │
│  packages/simulation/ │ Local game simulation               │
│  apps/training/       │ Headless sim for ML training        │
└─────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Main Application (`apps/lobby/src/main.c`)

**Purpose**: Game client with rendering, input handling, and state management

**Key Features**:
- Three application states: `STATE_LOBBY`, `STATE_GAME_LOCAL`, `STATE_GAME_NET`
- SDL2 window management and OpenGL rendering
- Network initialization (UDP sockets)
- Input processing (keyboard, mouse)

**Rendering Pipeline**:
- 3D scene rendering with perspective projection
- HUD overlay with orthographic projection
- Dynamic FOV adjustment (zoom for sniper)
- Crosshair and hit markers

**Visual Elements**:
```
Scene Hierarchy:
├── Grid (cyan lines for spatial reference)
├── Map Geometry (gray boxes with magenta outlines)
├── Players (red boxes with colored heads)
│   ├── Body (3.8 unit height)
│   ├── Head (0.8 unit cube, color-coded by weapon)
│   └── Weapon (3rd person view)
└── First-Person Weapon (with animations)
```

### 2. Protocol Layer (`packages/common/protocol.h`)

**Purpose**: Data structures and constants shared across all components

**Key Definitions**:

```c
// System Limits
#define MAX_CLIENTS 70
#define MAX_WEAPONS 5
#define MAX_PROJECTILES 1024

// Weapon IDs
WPN_KNIFE (0), WPN_MAGNUM (1), WPN_AR (2), 
WPN_SHOTGUN (3), WPN_SNIPER (4)

// Timing Constants
#define RELOAD_TIME 60 frames (~1 second)
#define SHIELD_REGEN_DELAY 180 frames (~3 seconds)
```

**Weapon Statistics**:

| Weapon   | Damage | ROF | Count | Spread | Ammo |
|----------|--------|-----|-------|--------|------|
| Knife    | 200    | 20  | 1     | 0.0    | ∞    |
| Magnum   | 45     | 25  | 1     | 0.0    | 6    |
| AR       | 20     | 6   | 1     | 0.04   | 30   |
| Shotgun  | 64     | 50  | 8     | 0.15   | 8    |
| Sniper   | 101    | 70  | 1     | 0.0    | 5    |

**Data Structures**:

```c
PlayerState {
    // Identity & Status
    id, active, is_bot
    
    // Transform
    x, y, z, vx, vy, vz, yaw, pitch, on_ground
    
    // Input State
    in_fwd, in_strafe, in_jump, in_shoot, in_reload, crouching
    
    // Combat
    current_weapon, ammo[5], reload_timer, attack_cooldown
    health, shield, shield_regen_timer
    
    // Feedback
    kills, hit_feedback, recoil_anim
    
    // AI/ML
    accumulated_reward
}

ServerState {
    players[MAX_CLIENTS]
    projectiles[MAX_PROJECTILES]
    server_tick
}
```

### 3. Physics Engine (`packages/common/physics.h`)

**Purpose**: Movement, collision detection, and combat mechanics

**Movement System**:

```
Movement Parameters (Tuned for "Turbo" feel):
├── GRAVITY: 0.018 units/frame²
├── JUMP_FORCE: 0.61 units/frame
├── MAX_SPEED: 0.75 units/frame
├── FRICTION: 0.42 (ground only)
├── ACCEL: 1.618 (golden ratio!)
└── MAX_AIR_SPEED: 0.2 units/frame
```

**Movement Pipeline**:
1. **Friction Application** (ground only)
   - Reduces velocity based on current speed
   - Uses `STOP_SPEED` threshold for standing still

2. **Acceleration**
   - Calculates wish direction from input
   - Applies acceleration toward wish speed
   - Respects max speed limits

3. **Gravity**
   - Constant downward force when airborne

4. **Collision Resolution**
   - AABB collision with map geometry
   - Separates players from walls/floors
   - Sets `on_ground` flag for landing

**Map Structure**:

The map consists of 70+ box primitives (`Box {x, y, z, w, h, d}`):

```
Map Zones:
├── Floor (900×300 base)
├── Central Ziggurat (3-tier pyramid)
├── East/West Spires (tall pillars with platforms)
├── Spine (long walls dividing the map)
├── Ruins (scattered box obstacles)
├── Parkour Platforms (floating jumps)
├── Cover Positions
├── Alpha Base (complex multi-level structure)
├── Omega Base (mirrored complex)
└── Boundary Walls (prevent out-of-bounds)
```

**Combat System**:

**Hit Detection**:
```
Ray-Capsule Intersection Algorithm:
1. Cast ray from shooter position + eye height
2. Check sphere intersection with target HEAD
   - Position: target.y + 4.5
   - Radius: 0.8 units
3. If miss, check sphere intersection with BODY
   - Position: target.y + 2.0
   - Radius: 2.7 units
4. Return: 0=Miss, 1=Body, 2=Headshot
```

**Damage Calculation**:
```python
if hit_type == HEADSHOT and target.shield <= 0:
    damage = base_damage × 3  # Triple damage
    
# Shield absorbs first
if target.shield > 0:
    shield_damage = min(damage, target.shield)
    target.shield -= shield_damage
    damage -= shield_damage

# Remaining damage to health
target.health -= damage
```

**Shield Mechanics**:
- **Capacity**: 100 points
- **Regen Delay**: 4 seconds after the last hit; any hit inside the window restarts it
- **Regen Rate**: one tick per second after the delay, worth 1, 2, 3 ... 10 points (capped), so the longer a
  body is left alone the more each tick gives back. The curve is `packages/sim/shield_rules.prn`;
  `packages/simulation/shield_regen.h` ticks it per entity, exposes the countdown a HUD draws
  (`shield_regen_counter_ms` / `_tenths`, `shield_regen_bar_permille`, `shield_regen_next_amount`),
  and lets a host install a per-body curve (`shield_regen_set_amount_hook`) -- the same hook shape the
  job stats use -- which is how a character mod's own tick value reaches the C host. `make
  test-shield-regen` proves the host half headless against the real `PlayerState`.
- **Damage Absorption**: Shields take all damage before health

**Weapon-Specific Logic**:

- **Knife**: Melee range check (100 units²) before hit detection
- **Shotgun**: Fires 8 pellets with 0.15 spread
- **AR**: Continuous fire with 0.04 spread
- **Sniper**: No spread, high damage, low ROF

### 3b. Rigid Body Physics (GOLDEN BAND `grb`, 2026-09-27)

Player movement above is still the tuned arcade controller -- unchanged. Alongside it the repo now
vendors GOLDEN BAND's formal rigid body engine (`packages/goldenband/grb.{h,c}`: 6-DOF bodies with
quaternion orientation and real inertia, XPBD hinge/ball joints with limits, effort-limited
servos, ground contact with Coulomb friction), plus its datasheet-robot loader (`grobot`) and RL
reward compiler (`grl`) -- see the GOLDENBAND repo's README for those.

**Rigid-body ragdoll** (`packages/simulation/rigid_ragdoll.{h,c}`): the multiplayer mannequin
skeleton as 17 rigid capsules with real human segment masses (Winter/Dempster fractions of a
70 kg body), ball joints with swing/twist limits and one-way knee/elbow hinges. It replaces the
point-mass `tools/ragdoll_spike` (which could only ever lie poker-straight -- no orientation
state). A 2.5 m/s chest shove falls, crumples (knees/elbows flexed, none hyperextended) and
settles in ~2 s with no ground penetration; ~0.5 ms per ragdoll per 64 Hz tick.

```bash
make test-physics                  # headless: falls, settles, stays in limits, bit-identical rerun (also in CI)
make rigid-ragdoll && ./bin/rigid_ragdoll [push_m_per_s] [seconds]
# -> tools/rigid_ragdoll/output/ragdoll_fall.gband + getup_oracle.gband (reversed fall; a get-up
#    imitation target), with real .qx/.qy/.qz/.qw channels for all 65 joints -- plays through
#    the existing gseq/skeleton path unchanged
```

**Honest status:** a tested library + tool, **not yet wired into live gameplay** (no ragdoll on
death in the lobby/server yet, no network replication of ragdoll bodies). Ground-plane contact
only -- limbs don't collide with each other or with level walls. The new Bazel targets
(`//packages/goldenband:grb_physics`, `//packages/simulation:rigid_ragdoll`) are untested
(Bazel couldn't be fetched in the authoring sandbox); the Makefile path is the tested one.

### 3c. Structural layer over destructible brick (2026-10-30)

Brick cells answer "did this shot break this cell?". `packages/simulation/structure.h` answers the
next question up: **is this piece still joined to the ground, and can the thing under it carry it?**

- **Joints at load.** Boxes whose volumes intersect are joined, not merely interpenetrating. A piece
  sunk into a concrete master is *anchored*; two crossing pieces of equal class are *pegged* (the
  virtual peg — timber is strong in shear, weak in tension). PARENA decides the strength
  (`struct_joint_strength`), which is what makes a **glass pane leaning on a brick wall not a joint at
  all** (150 permille, under the 300 threshold): it is resting, so it falls when the wall goes.
- **Support flood.** Grounded pieces spread support outward along joints. A piece that loses its last
  path to the ground is disconnected. Joints hold in both directions — a roof is held by the pillar
  as much as the pillar is held by the foundation — so only the load cascade follows gravity.
- **Load cascade.** Mass (volume × density) accumulates downward, each support passes its load to
  whatever would bear it, and each is compared with its own compression capacity (kg/m² × footprint,
  15% safety factor). A support that fails stops passing support and the check repeats (≤3 rounds), so
  a shot-out pillar drops the roof onto the next one instead of leaving it in the air.

```
[ roof ] ──peg── [ pillar ] ──anchor── [ foundation ] ──► ground
```

`structure_remove_node(g, i)` then `structure_evaluate(g)` is the whole "this piece is gone, what is
now falling?" step: pure integer/float arithmetic on a fixed graph (≤512 nodes, ≤4096 joints, indexed
only at load and after a breach — never per tick, never per entity on the wire), so the server and
every client compute identical answers.

Rules live in `PARENA/stdlib/shankpit/structure_rules.prn` (density, tensile, compression, shear,
damping, fracture kind, joint strength, load comparison), regenerated with
`scripts/gen_structure_rules.sh`. `make test-structure` runs the graph headlessly (ASan+UBSan, no GL,
no display).

**Wired into the game.** `brick_world.h` builds the graph when a level loads (one node per destructible
parent, anchored if it sits on the world floor or touches any non-destructible solid) and re-evaluates
it after a breach, inside `brick_world_flush` — so it costs nothing per tick. A piece the graph can no
longer justify is dropped whole through the new `bf_detach_parent`, becoming rigid chunks on the
existing debris path. A piece too big to be one body falls in **pieces** along its longest axis (a
300-cell deck becomes two slabs) rather than not at all, and the slab height is chosen so a part stays
under `BF_CHUNK_CELLS` even with every cell intact. All or nothing still applies: if the chunk queue
cannot take every part, nothing is detached — a half-fallen wall is worse than a standing one. Piece
mass follows the damage, so a wall carved down to a stub weighs what is left of it — and so does its
**strength**: capacity is scaled by the fraction of the piece still there, because a pier missing 90%
of its material carrying exactly as much as an intact one until its very last cell goes is the most
obvious way a structural model can look wrong. Linear in what is left, which is defensible for a
column and explicitly *not* a stress solver (a hollowed-out tube really does keep more capacity than
this says).

Two rules keep it from demolishing levels on its own:

- **Authored floating decoration is baselined, not collapsed.** A sign hanging over a doorway has no
  path to the ground at load, so it is marked handled on load and left alone. The baseline is taken on
  the support flood *before* any load verdict — reading it afterwards would baseline the overloaded
  support too, and a building the rules say cannot stand would quietly stand forever.
- **An overloaded support does come down at load.** If the rules say a piece cannot hold what is on
  it, that is a verdict about geometry you can see, and the cascade runs before the first frame.

```
3 x 20 x 3 wood post        18 x 6 x 18 brick deck
12.6M kg column capacity    3,693 tonnes over a 9 m^2 footprint
        \                          /
         +------  cannot hold -----+      -> both fall as rigid chunks (no damage involved)
the same deck on a concrete post: holds
```

Verified in `make test-brick-world`: the wood-post level loses all 7 post cells and all 72 deck cells at
load, both pieces stop being solid immediately, and two chunks are queued; a 288-cell deck (more than
one chunk) comes down as two slabs of at most 256 cells each, so size is not an escape hatch; the same
deck on a concrete post survives untouched with zero failures; the wall-only level keeps the layer
switched off. `make server` also links with the new rules file in `SERVER_SRC`.

**Not modelled, deliberately:** shear flow inside a piece, bending/buckling shape (a pillar holds its
column load or the node fails — it does not bow), fatigue, per-cell stress inside one box, and collapse
*animation* (a dropped piece is one rigid body, not a tumbling pile of brick). Those need a solver this
engine does not have, and saying so is cheaper than pretending.

### 4. Local Game Simulation (`packages/simulation/local_game.h`)

**Purpose**: Single-player/bot match logic (referenced but not shown in files)

**Expected Functionality**:
- `local_init_match(int bot_count)`: Initialize match with bots
- `local_update(...)`: Step simulation forward
- `local_state`: Global `ServerState` instance
- `USE_NEURAL_NET`: Flag to enable ML-powered bots

**Game Modes** (based on lobby inputs):
- **D Key**: Demo mode (1 bot)
- **B Key**: Bot battle (31 bots, no AI)
- **S Key**: Smart mode (31 neural network bots)
- **N Key**: Network multiplayer

*(This list predates most of the real `GameMode` enum in `packages/common/protocol.h` and is kept
here as historical UI-binding documentation, not a current mode inventory -- see that enum for the
real, current list, which now runs through `MODE_TYLER=109`: TYLER VALHANNA's cold open
(`docs2/specs/TYLER_VALHANNA_MODE_NORTHSTAR.md`), a scripted-sequence + third-person demo for the
S536 BIG_O engine-merge track, reachable via `--tyler` on the dedicated server.)*

### 5. Training Interface (`apps/training/headless.c`)

**Purpose**: Headless simulation for reinforcement learning

**Python C API**:
```c
void sim_init(int bots)
void sim_step(float fwd, float strafe, float yaw, 
              float pitch, int shoot, int jump)
ServerState* sim_get_state()
```

**Training Loop**:
```
1. Initialize environment with bots
2. For each timestep:
   a. Get game state
   b. Neural network computes action
   c. Step simulation
   d. Calculate reward from state change
   e. Update network weights
```

## Rendering System

### Camera System

**First-Person View**:
```c
Eye Height: 
- Standing: 4.0 units
- Crouching: 2.5 units

Camera Transform:
1. Translate to -player.position
2. Rotate by -yaw (Y-axis)
3. Rotate by -pitch (X-axis)
4. Offset by eye height
```

**FOV System**:
- Default: 75°
- Sniper ADS: 20° (interpolated smoothly)
- Affects sensitivity: zoom mode uses 0.05, normal uses 0.15

### Weapon Rendering

**First-Person Weapon**:
```
Animations:
├── Recoil Kick: Backward + upward motion
├── Reload Dip: Sine wave during reload
├── Movement Bob: Speed-based sine oscillation
└── Position: Offset right for normal, centered for zoom
```

**Third-Person View**:
- Body: 1.2×3.8×1.2 box
- Head: 0.8×0.8×0.8 cube (positioned at y+2.0)
- Weapon: Attached to body, rotates with pitch

**Color Coding**:
- Players match their equipped weapon color
- Dead players: dark red (0.2, 0, 0)

### HUD System

**Elements**:
1. **Crosshair**:
   - Normal: Small cross (green)
   - Sniper ADS: Full-screen crosshair (green)

2. **Hit Marker**:
   - Body hit: Cyan circle (10 frames)
   - Headshot: Magenta circle (20 frames)

3. **Health Bar** (bottom-left):
   - Dark red background (50×20px)
   - Bright red fill (width = health × 2)

4. **Shield Bar** (above health):
   - Dark blue background
   - Light blue fill (width = shield × 2)

5. **Ammo Display** (bottom-right):
   - Yellow vertical bars
   - Each bar represents 1 ammo
   - Knife shows 99 bars

## Network Architecture

**Protocol**: UDP (unreliable, connectionless)

**Server Address**:
- Primary: `shankpit.okemily.com:6969` (S459-36; the port here was also stale -- 6969 is the real,
  current port everywhere else in this doc and in code)
- Fallback: `127.0.0.1:6969`

**Socket Configuration**:
- Non-blocking mode (`O_NONBLOCK` / `FIONBIO`)
- Platform-specific initialization (Winsock on Windows)

**Expected Packet Types** (not implemented in shown code):
- Client → Server: Input state
- Server → Client: World state snapshot

## Input System

**Keyboard Bindings**:
```
Movement:
  W/A/S/D - Forward/Left/Back/Right
  Space - Jump
  LCtrl - Crouch

Combat:
  LMB - Shoot
  RMB - Aim/Zoom
  R - Reload
  1-5 - Weapon select

Menu:
  Escape - Return to lobby
  D/B/S/N - Start game modes (lobby only)
```

**Mouse**:
- Relative motion mode (captured in-game)
- Sensitivity scaling based on FOV
- Yaw/Pitch clamping (pitch: -89° to +89°)

## Performance Characteristics

**Frame Rate**: 60 FPS target (16ms delay)

**Simulation Rate**: Same as frame rate (no separate tick)

**Memory Footprint** (approximate):
- PlayerState: ~200 bytes × 70 = 14KB
- Projectiles: ~40 bytes × 1024 = 40KB
- Map geometry: ~24 bytes × 70 = 1.7KB
- Total state: ~56KB

**Rendering Complexity**:
- Draw calls: ~200 (map) + 70 (players) + HUD
- No texture loading (pure geometry)
- Immediate mode OpenGL (glBegin/glEnd)

## AI/ML Integration

**Neural Network Interface**:
- **Observation Space**: Full `ServerState` (56KB)
- **Action Space**: 6D continuous (fwd, strafe, yaw, pitch, shoot, jump)
- **Reward Signal**: `accumulated_reward` field in `PlayerState`

**Training Pipeline**:
```
Python (PyTorch/TF) ←→ C Extension ←→ Headless Sim
                    ↑               ↑
                    ctypes/cffi     game logic
```

## Future Extensions

**Potential Improvements**:
1. Projectile system (currently hitscan-only)
2. Network multiplayer implementation
3. Server-side state authority
4. Lag compensation techniques
5. Texture mapping and materials
6. Sound effects and music
7. Particle systems for effects
8. Spectator mode
9. Killcam/replay system
10. Leaderboard and stats tracking

## Dependencies

**External Libraries**:
- SDL2: Window management, input, timing
- OpenGL: 3D rendering (legacy immediate mode)
- GLU: Utility functions (gluPerspective, gluOrtho2D)
- Platform-specific: Winsock2 (Windows) / BSD sockets (Unix)

**Build Requirements**:
- C compiler (C99+)
- SDL2 development libraries
- OpenGL headers

## Code Style Notes

**Conventions**:
- Snake_case for functions and variables
- UPPER_CASE for constants and macros
- Minimal comments (code is documentation)
- Single-file compilation units
- Global state (e.g., `local_state`)

**Performance Optimizations**:
- Stack allocation for temporaries
- No dynamic memory allocation in hot paths
- Inline hit detection in combat loop
- Minimal function call overhead

## Destructible loot chests (survival, card #528, 2026-10-02)

Survival mode now scatters chests across the city road ring. Shoot, stab or blast one and it wears down (darkening toward red);
when it breaks it drops a weapon pickup where it stood. Three tiers -- wooden, reinforced, rare (gold, with a light beam). HP,
tier rarity, the per-tier drop table and per-weapon damage scaling are a PARENA mod (`PARENA/stdlib/shankpit/chest_rules.prn`,
generated into `packages/simulation/chest_rules.c`; `make regen-chest-rules` / `check-chest-rules`). Pool + geometry:
`packages/simulation/chests.c`. Tests: `make test-chests` (headless, hand-derived) and `make test-chest-rules` in PARENA.
**Honest limits:** headless-tested only, never seen in a live match; client-local (not server-authoritative) like gun pickups;
NOCK placement of chests, WOTAN rare-drop integration and EDUSCRIPT scripting are NOT built yet.

## GFD phone chat (2026-10-16)

The phone's GFD app is a live chat terminal on IDUNA's cross-game chat bus. Sign in first (phone > IDUNA), then type in GFD and press Enter: every SHANKPIT player's GFD phone sees the line under your IDUNA display name, and so does everyone in the GFD MUD via its `global` (`gc`) command, whose messages show up on the phones as `[GFD] name: ...`. Polling is every 3s, so expect a few seconds of latency. Code: `packages/gfd_chat/gfd_chat.h` (parsing unit-tested). Status: verified with two clients and a simulated MUD against a fake chat-bus server; not yet against the live IDUNA, which needs the new `shankpit` channel deployed. Windows builds have a no-op stub (chat only). The name is your IDUNA display name, not a GFD character name.


### In-phone browser on Windows (2026-10-16)
Windows builds previously linked a stub, so the phone's PELLEGRINO app said "embed surface unavailable". `scripts/build_pgb_windows.sh` now cross-compiles the real embed (mbedTLS + HTTP/3) with mingw and ships `cacert.pem` beside the exe; if that build fails it falls back to the stub. **Untested:** no mingw/Windows machine here — verified only that Linux still builds; CI builds it.

## DEADWEIGHT — the card game, played by the citizens (2026-11-03)

DEADWEIGHT's whole rule set is PARENA (`PARENA/stdlib/deadweight/card_rules.prn`: 105 cards, three
kinds in a triangle, energy, credits, hull, and a packed effect engine), and it now runs inside
SHANKPIT. `packages/simulation/dw_engine.*` is the shim: that engine plus the citizens' policy
(`stdlib/deadweight/citizen_policy.prn`) behind one flat integer ABI, native by default and swappable
for a wasm module — `dw_engine_attach_wasm` validates the table and `dw_engine_selftest` asks the
live backend for the numbers it owns. `dw_match.*` is the sequencer (20 hull, 2 energy, 3 credits, a
4-card hand, the triangle, armour, burn/regen, both fx phases for EVERY channel the catalogue carries (COPY, DISABLE, BLIND, SWAP,
REDRAW and DISCARD included), bankruptcy, a 100-round draw and `dw_ladder.*` is the citizens' ladder.

Citizens challenge each other, citizens challenge you, you challenge citizens, and you can challenge
another player — all four through one call, `dw_ladder_challenge`. They play without you: every few
seconds a citizen opens a game of its own. And they **craft their own decks from their win rate and
the current meta**: a citizen losing to the top deck rebuilds toward the answer to it, a citizen that
keeps winning keeps its identity, and both re-craft after every match, because the deck is a function
of the record and the meta rather than a stored preference.

The player's end is the phone: **P > DEADWEIGHT** shows your record, the population's meta, a
challenge waiting on you as its own PLAY / DECLINE row, and the roster — every citizen you could ask,
with their record — plus what happened in your last match, read from your own seat. A waiting
challenge also marks the tile on the home grid. `packages/simulation/dw_play.*` is the only layer
between the ladder and that screen, and SELECT never resolves a match: it names a choice, and the
host plays it through the real sequencer.

**Accept a challenge and the duel is read, not resolved.** The match is set up, then played one round
per world tick (1.2 s), with the page showing ROUND / HULL / ENERGY as it goes; when the last round
lands the result is recorded through the same recorder an instant match uses. Under the score is the
last few rounds, each side's card shown as `OPS T2 P6` — kind, tier and power, because the catalogue
holds no name strings at all, and deriving them from the engine beats a second catalogue that drifts.
Your deck is a **choice of playstyle** — CRAFTER, TRADER, DUELIST, ENDGAME or DRIFTER — and the
sixteen cards are then built by the same function that builds a citizen's, from your record and the
current meta (so it answers the meta the way the population does). The live deck is counted by kind on
the page, and it cannot be swapped while a duel is playing.

Watching is scheduling, not a second game — `make test-deadweight` plays one challenge both ways and asserts the winner, the
round count, both hulls, the meta, every record and every re-crafted deck are identical. Citizens'
own matches still resolve instantly: nobody is watching those.

**Every member carries a RATING**, which is the number a leaderboard climbs and now the same number in
both worlds: `packages/simulation/dw_rating.c` is emitted from `PARENA/stdlib/deadweight/rating.prn`
— the file NOOBTOPIA emits to Luau — by `make regen-dw-engine`. Beating somebody 200 stronger pays 20,
beating somebody 200 weaker pays 12, a draw moves both by a tenth of the gap (clamped ±4), nothing
drops below 100, and `gain(w, l) == loss(l, w)` holds for every pair: it is zero-sum because
`rating-loss` takes its term from the WINNER's side. One delta is applied per match, in
`dw_ladder_record` — the single recorder an instant match and a watched duel both go through — so two
ways of playing one game cannot price it differently. The save carries it (format 2), and a format-1
file written before the rating existed still LOADS, with every member starting at 1000 rather than the
whole file being refused: a ladder from yesterday's binary is a real ladder. **The published
snapshot carries it too** (`dw_net` schema 2 adds `rating` to every record, straight off the
ladder rather than recalculated), so WOTAN's Citizens page ranks the same population by the
same number the game ranks it by — the tie-break is rating, then wins, then pid, and it lives
in the page's one client module instead of being shipped twice in the JSON.

Records survive a restart when a world names a file: `SHANKPIT_LADDER_FILE=var/deadweight/ladder.db`
makes every finished match autosave the members (pid, person-or-citizen, path, aggression, wins,
games), the meta tally, the results and the hero, and a boot loads them back. The **decks are not
saved** — a deck is a function of the record and the meta, so loading re-crafts every one, which both
restores it exactly and proves the function is deterministic. Off by default, like the published
snapshot.

The sequencer is the one piece still hand-written per world (NOOBTOPIA carries its own Luau port for
Roblox), so it is pinned by DATA: `PARENA/stdlib/deadweight/match_parity.txt` holds five whole matches
— seed, both seats' deck-crafting inputs, aggression, and every round's cards, hulls, energy, credits
and damage — and **both** `make test-dw-parity` here and NOOBTOPIA's
`lua tests/deadweight_match_test.lua` assert the same rows. Writing it found a real bug in this file:
the policy chose its card BEFORE the round dealt, so it read the previous hand, and on a fresh match
four zero-filled slots — round 1 played card 0, which neither seat held. A round is now BEGIN (clock,
economy, deal, DISABLE) and FINISH, with the policy choosing in between.

The **ladder** is pinned the same way, and its fixture carries the SCRIPT as well as the answers:
`PARENA/stdlib/deadweight/ladder_parity.txt` holds a sequence of ladder operations (challenge in all
four directions, accept, decline, tick, remove) plus what they left behind, and both
`make test-dw-ladder-parity` here and NOOBTOPIA's `lua tests/deadweight_ladder_test.lua` execute that
script out of the file. `accept`/`decline` address "the challenge opened most recently", so neither
side has to name an internal slot. On the Roblox side the ladder feeds a **per-server leaderboard**
ranked by `PARENA/stdlib/deadweight/rating.prn` — one number, defined once, that players and citizens
climb; it resets with the server, so SHANKPIT's own ladder stays the durable one.

Status, honestly: **the game plays headlessly, the citizens play it, and a match you accept is read
round by round on the phone's own scoreboard — but nobody has yet seen it happen on a screen.** There
is no card art (the board shows numbers, not cards), no wasm module is actually
attached (SHANKPIT has no interpreter in-tree; the seam is real, the module is not), records live in
a file only when `SHANKPIT_LADDER_FILE` names one (otherwise they are RAM-only), and decks come from
the game's core nine cards — a player picks a playstyle, not individual cards, which needs the
catalogue walk still blocked on the emitter's cross-module call typing. Every
next step, in order, is in `docs/DEADWEIGHT_IN_SHANKPIT_NEXT_STEPS.md` — including the previous
step's (the citizens' generated bodies) outstanding work. Run `make test-deadweight`,
`make test-dw-parity` and `make test-dw-ladder-parity` for the whole thing headless,
`make regen-dw-engine` after editing the PARENA rules, or `make regen-dw-parity` /
`make regen-dw-ladder-parity` after changing the sequencer / the ladder.

### The internet of SHANKPIT (the shim, 2026-11-03)

The ladder also has an outside world. `packages/simulation/dw_net.*` serialises it to one JSON file
(schema 1: population, the meta and its tallied share, every seat's record and the sixteen cards it
built for itself, open challenges, results by direction, a ticker of finished matches) with
write-then-rename, and `tools/shankpit_netd.c` is a dependency-free HTTP daemon that serves that file
for WOTAN's `/shankpit-citizens.html`. Neither knows anything about the other beyond the file, which
is why either can be restarted or tested alone, and why the game never opens a port.

```bash
make test-deadweight    # plays matches AND writes var/deadweight/net.json
make netd               # builds bin/shankpit_netd
make net-demo           # serves that snapshot on :8791 so the page has something real to read
SHANKPIT_NET_SNAPSHOT=var/deadweight/net.json ./bin/server   # a live world publishing itself
```

Publishing is **off unless `SHANKPIT_NET_SNAPSHOT` names a file**. Routes: `GET
/api/v1/shankpit-net/deadweight` and `/health`, with a real ETag/304 and CORS. Status, honestly: the
snapshot, the daemon and the WOTAN page are all real and tested end to end, but **the daemon is not
deployed** (WOTAN's nginx proxies `/api/v1/shankpit-net/` to `127.0.0.1:8791`, which is the box that
runs the world, not a cluster pod), nothing in the game draws the feed yet, and a finished match is
not yet reported to IDUNA as a rated match.

## Clean-room clients: GCC, Clang/LLVM, WASM core (2026-11-05)

The clean-room game (`apps/game`, `packages/game`, `packages/reflux`) now builds the SAME PARENA
level-module registry into several client families. "Pure LLM" in the request is read as the LLVM
toolchain: Clang plus LTO, linked with `lld`, kept separate from the GCC build. Nothing is forked:
all targets compile `build/game_rules.c` from the same `.prn` sources and link the same
`level_mods_host.c` / `reflux_runtime.c`.

### Target matrix

| target | command | artifact | modules it admits |
|---|---|---|---|
| GCC native | `make game` | `bin/shank_game` | all 5 (core sim, level, weather render, weather audio, sky) |
| Clang native | `make game-clang` | `bin/shank_game_clang` | all 5 |
| LLVM-LTO native | `make game-llvm` | `bin/shank_game_llvm` (clang + `-flto` + `lld`) | all 5 |
| Pure WASM client core | `make wasm` | `bin/shank_core.wasm` (22 KB, zero imports) + `apps/game/wasm_client.html` | core sim + level only (mask 3); canvas/input host |
| Native core (no networking) | `make core` | `bin/shank_core` | all 5 level modules; net none |
| Clang rules suite | `make game-test-clang` | `bin/shank_game_test_clang` | all 5 |
| WASM boot check | `make wasm-smoke` | Node boots the real module | core sim + level only |

The WASM module needs no libc, no SDL and no imports, and carries its own `sin`/`cos`/`atan2`. It
exports the registry/state API plus a playable ABI: `wasm_play_boot` registers a level and puts a
body on the floor, `wasm_set_input`/`wasm_turn` feed the stick and look, `wasm_frame` spends elapsed
milliseconds on the frame rules' fixed 16 ms ticks and draws through `rp_*` and the 460 palette, and
`wasm_fb_ptr` exposes a 1024x576 RGBA framebuffer. Movement still calls the same `loop_move_x`/
`loop_move_z` rules as the native host. `apps/game/wasm_client.html` is the canvas/keyboard shell:

```sh
make wasm
python3 -m http.server 8080
# open http://localhost:8080/apps/game/wasm_client.html
```

`make wasm-smoke` boots the real module, checks the clamped registry/state and trig, then renders a
frame, asserts the framebuffer has real colours, moves forward and after a turn, and checks the
six-tick stall cap. Audio, networking and the per-level weather/sky module bits are still ahead, so
`wasm_boot` continues to advertise mask 3.

The native core (`make core`, `bin/shank_core`) is the same registry with all five level modules but
no SDL and no socket code. It is the "core without the networking mod" half of the ladder below.

### One registry, per-build capabilities

A module id and its default/mandatory policy live in `packages/game/level_mods.prn`; the target
does not re-implement any of that. Every client compiles `apps/game/build_profile.c` with one
`SHANKPIT_BUILD_TARGET` from `game.mk` and applies that profile before registering anything:

- `build_profile_apply()` sets the target's capability mask from `packages/game/build_profiles.prn`,
- `build_profile_name()` / `build_profile_net()` report the same row (native, llvm, wasm-core,
  native-core; none / host-C / PARENA),
- `--capabilities MASK` remains only a test/debug override on the native client and core runner,
  e.g. `3` = core sim + level.

The table is the one place that also records whether a target links a network transport at all
(`bp-target-net`: none / host-C / PARENA).

Opening masks and incoming authoritative state are intersected with that mask, and enabling an
unported module is refused before a REFLUX row is dispatched. Mandatory modules are always kept.
The client also advertises its mask and the level it watches once the first snapshot proves the server
is live (packet 6). The server keeps four address-keyed peers, assigns each a body slot from 8-11 and a 64-bit
per-peer token in a welcome packet, and clamps/filters that peer's level rows through
`net-interest-match`, leaving its own registry untouched. Both sides therefore clamp independently.
`make game-capped-client-smoke` proves the mask half across a real socket: a surface server (31) and
a core-only client (3) meet, the server reports `peer_caps=3`, and the client stays at 3.
`make game-level-interest-smoke` registers four levels on the server and has the client watch level
2; the server must send exactly one row (`level_rows=1`) carrying mask 11.
`make game-multi-peer-smoke` runs two clients at once with different masks and watched levels: the
server reports `peers=2`, slots 8 and 9, caps 31 and 3, and one independently clamped row for each.

Packet 5 now carries the welcome token. A request from an unknown address is dropped rather than
registering a peer, a token mismatch is refused, and accepted requests are limited to one per peer
per 500 ms. The server chooses who may ask with `--mod-policy`:

- `owner` (default) - the earliest peer still sending traffic; a disconnected owner goes stale after
  the same 2 s window the peer table uses, so the session does not lock to a dead slot;
- `peer` - any active peer with its own token;
- `off` - no remote requests (the operator's local `--set-mod` still works).

`make game-level-request-smoke` proves the default owner path and `make game-mod-authority-smoke`
starts a peer-policy server, sends one request with a deliberately wrong token and one with the
issued token, and requires `mod_requests=1 mod_refused=1` plus the accepted mask change.

### Networking: host-C now, transport mod later

The founder's example is the right shape, with one distinction worth keeping explicit: weather, sky and
audio are PER-LEVEL level-module bits; networking is a BUILD capability. A level does not disable
networking with a mask bit, and a server does not ship a transport inside a level update.

Today:

- `apps/game/net_transport.h` is the seam: `open/close/addr/send/recv` take only `NetAddr`, so no
  socket header reaches `host.c`. `apps/game/net_transport_udp.c` is the host-C implementation the
  full native/LLVM clients link. `build_profiles.prn` reports that as `bp-net-host-c`.
- `bin/shank_core` and `shank_core.wasm` link no transport implementation at all (`bp-net-none`) while
  still running the same PARENA rules and registry. `core-smoke` also checks `nm -u shank_core` for
  socket symbols, so a hidden dependency would be a red build.
- `make core-smoke` runs the native core at a moon mask (3), a capped surface mask (3), and a surface
  shed (27, revision 2).

Porting ladder:

1. **Host-C baked**: the full client is the core plus the host-C UDP transport. No longer the whole
   story: the socket API now sits behind the seam below.
2. **Transport seam (done)**: `host.c` owns protocol behaviour and speaks only `NetAddr` + send/recv;
   the UDP implementation is a swappable file the full client links and the core does not.
3. **PARENA/REFLUX transport mod**: packet scheduling, interest/delta policy and level-mod state deltas
   move into rules; the edge keeps only byte I/O. `net_rules.prn` already owns packet ids and sizes.
4. **FFI-free edge**: a native LLVM transport and a WASM WebSocket/import edge, so the LLVM build can
   ship with no C FFI and add or omit the networking mod like any other module. Until then, calling
   the current build "FFI-free" would be false.

What this buys: the core can be tested and shipped with no network; the transport can change per
target; a server can evolve level mods over whatever transport that target has.

### Next steps, in order

| # | step | current state | done when |
|---|---|---|---|
| 1 | Capability handshake | client advertises caps + level (packet 6); the server keeps four address-keyed peers, assigns slots 8-11 and a token via welcome, and clamps/filters each independently; the client clamps locally too | packet 5 now uses the per-peer token; play commands and caps still use the source address, so the whole play socket does not share one identity check yet |
| 2 | WASM render/input/audio | browser canvas/input landed: fixed-step movement, arena renderer and RGBA framebuffer; audio and the weather/sky level modules are still unported | audio output and per-level render/sky module parity, so the browser client matches native's admitted modules |
| 3 | Per-target profiles from one source | every client applies/reports `build_profiles.prn` through one `build_profile` module; a target is still chosen by one `SHANKPIT_BUILD_TARGET` define | the profile is emitted by the build system itself, so adding a target needs no second hand edit |
| 4 | Mod-request authority | packet 5 requires the per-peer welcome token, enforces a 500 ms per-peer cooldown, and obeys an explicit `--mod-policy off|owner|peer` (default owner); unknown senders do not register | remote play commands/caps still carry no token, so the remaining identity work is across the rest of the play socket |
| 5 | Durable stream identity | every JSONL line carries a persisted `stream` id that continues after the file's last line across process restarts; replay validates the whole contiguous sequence before applying anything and refuses gaps, duplicates or old unnumbered lines | done for the level-mod JSONL stream; replay schema versioning is step 8 |
| 6 | Level unregister/transition | the table only grows (8 slots); no way to remove a level | `level_mods_unregister` frees a slot, emits an event, and clients drop/transition without keeping a stale mask |
| 7 | Manifest write-back | evolved masks are memory-only; replay is the only recovery path | an operator can persist the live mask/revision back to `levels.json` atomically, with the same validator as load |
| 8 | Replay schema versioning | line parser trusts event names and skips unknowns | the stream carries a schema version; unknown versions/rows are refused rather than silently dropped |
| 9 | Unified core packaging | WASM core now ships in every release; native clients ship separately | one core archive/build feeds native and browser shells, with a shared version string and no duplicated host glue |
| 10 | Cross-target parity | GCC/Clang/LTO/WASM each have their own checks | one scenario runs through all targets and asserts identical masks, revisions and event counts |
| 11 | Networking transport | transport seam exists (`net_transport.h`, host-C UDP impl); native/WASM cores link none; scheduling is still host C | PARENA-owned scheduling, then FFI-free LLVM/WASM edges; core builds and ships without it |

Ordering rationale: 1 and 3 make module porting safe before more ports land; 2 is the first
user-visible WASM client; 4-8 harden the server/stream path the founder asked to evolve live; 9-10
are the payoff (one core, several shells) and are only meaningful after the ports exist.

### Verification for this slice

- `make game-test` - 283 checks (registry, capabilities, profiles, token/policy rules, replay, relay, catalog, durable event streams).
- `make game-test-clang` - the same 283 under Clang.
- `make game-clang` / `make game-llvm` then `--headless 5` - both boot, 31 modules on level 1.
- `make wasm` / `make wasm-smoke` - zero-import module, wasm-core profile, caps 3, no transport, state 31 clamped to 3, trig checked, plus a rendered 1024x576 frame and fixed-step forward/turn movement.
- `make core` / `make core-smoke` - native core with no networking: moon 3, capped 3, shed 27/rev 2,
  and no socket symbols in `nm -u`.
- `make game-net-smoke` / relay / request / capped - the same wire behaviour through the seam.
- `make game-capped-client-smoke` - surface server 31, core client 3 across UDP, server logs `peer_caps=3`.
- `make game-level-interest-smoke` - four registered levels, one interested peer, exactly one row sent.
- `make game-multi-peer-smoke` - two clients, independent caps 31/3 and levels 1/2, slots 8/9, two rows.
- `make game-level-request-smoke` - the owner policy accepts the client's tokened request and reports `mod_requests=1 mod_refused=0`.
- `make game-mod-authority-smoke` - a wrong token is refused and a correct one is accepted across two real clients (`mod_requests=1 mod_refused=1`).
- `make game-stream-smoke` - two separate host processes append a contiguous `stream` 1..N sequence to one JSONL file, and replay accepts it.
- `make game-level-relay-smoke`, `make game-smoke` - unchanged.

Known limits: the WASM client core has no allocator and no audio or networking; its weather/sky
module bits remain off; the trig is accurate for the rules' range but is not a full libm; the profile table is applied in every build but a target is still selected by one compile-time define; the transport seam exists but only one implementation (host-C UDP) does, packet scheduling is
still host C, and no target is FFI-free yet; LLVM-LTO's size drop (90 KB to 69 KB) is a build result, not a measured runtime speedup. Packet 5 is token-authenticated, but caps and play commands still trust the UDP source address, and the 64-bit token is an opaque capability generated from the clock/address, not a cryptographic credential or an account identity.
