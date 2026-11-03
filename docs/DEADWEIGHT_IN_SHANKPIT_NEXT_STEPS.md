# DEADWEIGHT in SHANKPIT — status and next steps

Written 2026-11-03, at the end of the turn that put DEADWEIGHT inside SHANKPIT. Two units of work
landed back to back and both still have known, nameable futures, so this file records BOTH: the next
steps for the **citizen avatars** step (the one immediately before this), and the next steps for
**DEADWEIGHT** itself. It exists so the next thread starts from the real state instead of re-deriving
it — every "done" below is backed by a command that was actually run, and every "not done" says what
is missing and where the seam is.

## What landed

| Unit | Real state | Verified by |
|---|---|---|
| Citizen bodies + wardrobes (`stdlib/forgedinface/citizen.prn`, `packages/simulation/citizen_avatar.*`) | a citizen's proportions and clothes are a total function of its own persona (seed, slot, path, 5 traits); 256/256 distinct in a 256-citizen sweep | `make test-citizen-avatar` ALL PASS |
| Citizens drawn in that body (`apps/lobby/src/main.c`) | each citizen draws in its generated coat colour and at its generated height; every non-citizen path untouched | `gcc -c apps/lobby/src/main.c` → 0 errors (GLU prototypes stubbed: this box has no `libglu` headers) |
| DEADWEIGHT engine in SHANKPIT (`dw_engine.*`, `dw_rules.c`, `dw_citizen_rules.c`) | PARENA's 105-card rule set and the citizen policy, emitted to C behind one flat integer ABI, with a switching/validating backend seam | `make test-deadweight` ALL PASS (backend swap, selftest, base line, triangle, fx decode) |
| The sequencer (`dw_match.*`) | 20 hull / 2 energy / 3 credits, 4-card hands, the triangle, armour, burn/regen, both fx phases for EVERY channel the catalogue carries (including the six hand-lifecycle ones), bankruptcy, 100-round draw | scripted rounds with hand-derived hull/energy/credit numbers; a deterministic replay compared byte-for-byte |
| The internet-of-SHANKPIT shim (`dw_net.*`, `tools/shankpit_netd.c`, WOTAN's `/shankpit-citizens.html`) | the ladder published as schema-1 JSON by the game, served by a dependency-free HTTP daemon with ETag/304, and read by a real WOTAN page | `make test-deadweight` (export + file) and WOTAN's `scripts/shankpit-net-test.js` (53 checks against the real daemon) |
| The citizens' ladder (`dw_ladder.*`, wired into `witness_ai.c`) | all four challenge directions through one entry point, records, meta tally, decks that re-craft on the record + meta | all four kinds classified; records and meta move; a losing citizen rebuilds to the answer, a winning one keeps its identity |

Honest headline: **the game plays headlessly and the citizens play it. Nobody has watched a match on
screen, and the player cannot yet click "accept".**

---

## A. Next steps for the PREVIOUS step — the citizen avatars

1. **Draw the per-part proportions, not just overall height.** Today `draw_player_skin_mannequin`
   multiplies one uniform scale by `height_mm / 1710`, so a heavy crafter and a lean duelist of the
   same height look identical. The data is already there (`bulk/shoulder/arm/leg_permille` and
   `citizen_avatar_scale(av, part)`) and unused. Two routes: (a) find/extend a per-bone scale hook in
   `packages/goldenband/gband_skel_npc.c` — `gband_skel_npc_set_scale` is whole-body only today; (b)
   swap the citizen's skinned kit for a box body (`draw_ronin_shell_articulated`) whose limb
   dimensions already come from `RONIN_*` constants and can be multiplied per part. (b) is the
   cheaper proof and can be gated to `MODE_ZOMBIES`.
   *Done when:* two citizens of equal height but different `bulk_permille` are measurably different
   widths in a frame captured by the harness in item 6.
2. **Faces.** `stdlib/forgedinface/rig.prn` already has morph targets (`morph/new`,
   `morph/add-delta!`, `morph/add-weight!`), and the avatar has no face inputs at all. Next slice:
   derive 3-4 morph weights (jaw squareness, brow, cheek, mouth width) from the same traits —
   `ambition` and `bravery` are the obvious drivers — and add them to `CitizenAvatar`. Rendering a
   face on a 5-kit skinned mannequin is a separate question; box bodies can at least get a faceplate
   colour/emblem from the accent.
3. **Real body meshes instead of a borrowed kit.** `stdlib/forgedlevel/mesh.prn` already emits
   face-culled quads into an arena; nothing has ever applied it to a body. Emitting a 20-bone box
   body sized by the avatar's permilles is the honest "FORGEDINFACE mesh" milestone, and it is what
   makes item 1 tractable without touching the skeletal pipeline.
4. **Zombie bodies.** `packages/simulation/zombie_taxonomy.h` gives every zombie `(class, level)` and
   players already read it. The same generator should produce a zombie silhouette (BRUTE wide, RUNNER
   thin, ALPHA tall) — `citizen.prn` is 90% the right shape, needing a `zombie` entry point rather
   than a fork.
5. **Per-citizen animation variation.** A DUEL citizen and an IDLE one walk at different speeds
   already (`citizen_life.c`), but they animate identically. Expose `citizen_life`'s path speed to the
   draw path and scale the anim clock, not the pose.
6. **A headless visual harness — the biggest gap in the whole thread.** There is no GL driver here,
   so "verified" has meant "compiled". `stdlib/forgedinface/render.prn` already rasterises quads into
   an arena with a z-buffer and `make test-forgedinface` validates it. Next: write the frame out as a
   PPM/PNG from a test, render a citizen body (and a DEADWEIGHT board) and LOOK at it. That converts
   every future avatar change from "compiles" to "seen".
7. **Fix the lobby link blocker (pre-existing, not ours).** `make -B lobby` stops on a missing
   `../EDITOR.GAME/gen/editor_stdlib_gen.c`, so the lobby cannot be produced on a clean box even
   though `main.c` compiles. Either commit that artifact, generate it in-tree, or make the rule
   optional. Until then no avatar change can be *run* here.
8. **Fresh-box build recipe.** `libglu` headers are absent on this image while SDL2/GL are present;
   either add the package to the box recipe or vendor the four GLU prototypes main.c needs. Also add
   the `docs/` + `scripts/gen_*.sh` conventions to the bootstrap so `regen-*` works from a new box.
9. **Citizens' appearance persistence.** A citizen's body is derived from its `CitizenLife.rng`, which
   is seeded with `now_ms` at spawn — so the same *slot* returns as a different person after a scene
   reload. If a player should recognise "the same citizen" across a session, seed `citizen_life_roll`
   from the slot alone when persistence matters (a design decision, not a bug fix).
10. **Emily Way bookkeeping (blocked, not skipped).** No Apple, no kanban card, and no `GOLDEN_DOCS`
    resync for any of this: the `emily` CLI has no session, IDUNA is unreachable from this box, and
    `GOLDEN_DOCS` is not cloned here. The `EMILY/context/golden-docs-index.md` rows ARE updated.

---

## B. Next steps for DEADWEIGHT in SHANKPIT

1. ~~The six unsequenced fx channels~~ — **DONE** (same day, `dw_match.c`). COPY, DISABLE, BLIND,
   SWAP, REDRAW and DISCARD are sequenced; `DwSeat` gained `disabled_next` and `blind_rounds`, DISABLE
   applies after the deal (a hand you cannot fully use is the point), and `unsupported_fx` is now only
   a GUARD for a channel that does not exist yet. One real finding from writing the test: the
   catalogue's ONLY COPY carrier is Hostile Takeover (36), which also carries CANCEL on its first word
   — so in practice COPY copies a cancelled declaration, i.e. a pass, and the card is a stalemate.
   That ordering is deliberate (cancels resolve before copies) and is now asserted rather than
   assumed. Remaining work in this area: no catalogue card carries DISCARD, so that path is
   implemented but untested by a real card — worth a card of its own.
2. ~~The player's end of the ladder — a UI~~ — **DONE (same day, `dw_play.*` + `BP_APP_DEADWEIGHT`).**
   `dw_play.{h,c}` is the one layer between the ladder's pids/member indices and a screen's rows:
   `dw_play_pending` / `dw_play_opponent_for` / `dw_play_answer` (accept or decline) /
   `dw_play_roster` / `dw_play_challenge`. The phone app shows your record, the meta, the waiting
   challenge as its own PLAY / DECLINE row, and the roster with each citizen's record; a pending
   challenge also marks the tile on the home grid. `DwLadder` gained `DwLadderLast`, so a result is
   the MATCH's own record (kind, both pids in the challenge's order, winner, rounds, both hulls) and
   `dw_play_answer` translates it to the reader's seat once. SELECT only names a choice; the host
   re-derives the roster before acting, so a challenge cannot land on the wrong person. Tested in
   `make test-deadweight` (see the note on seat translation below — a hull-ORDER assertion would have
   been wrong, because bankruptcy can lose a match with more hull left). **Still missing here:** a
   BOARD to watch a match on — a match still resolves instantly, so the phone reports the result
   rather than the play-by-play (that is B6, a round clock), and the page has never been LOOKED at
   (no GL driver, and `make -B lobby` still cannot link — A7).
   *Done when:* a citizen's challenge can be answered from inside the game on the founder's machine.
3. **Decks from the full catalog.** `cp_deck_card` uses the base line (ids 0-8) because kind k's cards
   are contiguous there; the other 96 cards are interleaved by id, so picking them needs the catalog
   walk that hit the emitter's `void *` gap (see B10). Until then a citizen's deck is the game's core
   nine, which is balanced but shallow.
4. **Actually run the wasm backend.** `dw_engine_attach_wasm` is a real seam with a real validator, but
   nothing attaches a module today: there is no wasm interpreter in-tree. Next: build
   `card_rules.prn` + `citizen_policy.prn` to `wasm32-unknown-unknown` with
   `PARENA/scripts/build_d2_wasm.sh`'s proven toolchain, then either embed wasm3 (small, C99,
   zero-dependency) or expose the table from the browser/Android host that already runs D2's wasm.
   `dw_engine_selftest()` is the acceptance test — it asks the live backend the numbers it owns.
5. ~~Real card choice for the player~~ — **DONE, at playstyle level (same day, `dw_play_set_path`).**
   Five playstyles (CRAFTER / TRADER / DUELIST / ENDGAME / DRIFTER), and the sixteen cards are built by
   the same `cp_deck_card` a citizen's are — from the player's record and the population's meta — so a
   player's deck answers the meta the way the population does. The phone shows the five rows, marks the
   live one, and counts the built deck by kind; the swap is refused mid-duel (`-5`).
   **Still open:** card-by-card choice. That needs the deck walk over all 105 cards, which is B3, which
   is blocked on the emitter gap in B10.
6. ~~Pacing and watchers~~ — **DONE (same day, `DwWatch` + `dw_match_step_policy`).**
   `dw_ladder_accept_watched` sets a duel up, `dw_ladder_watch_step` plays one round when it is due
   (called from `witness_ai`'s tick), and the finish goes through `dw_ladder_record` — the same
   recorder an instant match uses, extracted for exactly this reason. Accepting a challenge on the
   phone starts a watched duel (1.2 s a round) and the page shows ROUND / HULL / ENERGY while it
   plays. The test plays one challenge both ways and asserts identical winner, rounds, hulls, meta,
   records and decks, so watching cannot drift from playing.
   **Still open here:** citizens' own matches resolve instantly (deliberate — nobody watches those),
   no replay, and no "thinking" pause per citizen beyond the fixed round interval. The per-round log
   landed the same day (last 6 rounds, each seat's card as kind/tier/power — the catalogue carries no
   name strings, so a name would mean inventing a second catalogue).
7. ~~Records that survive a restart~~ — **DONE (same day, `dw_ladder_save/load`).** The ladder is
   written as line-oriented text (members, meta tally, results, hero) behind `SHANKPIT_LADDER_FILE`,
   autosaved at `dw_ladder_accept` — the one place a record moves — with write-then-rename.
   **Decks are deliberately not saved:** loading restores the meta FIRST and then re-crafts every
   deck, so the same sixteen cards come back and the crafting function is proven deterministic by the
   test rather than assumed. Challenges are not persisted (their TTL did not run), and a malformed
   line refuses the whole file. Records are keyed by PID, so **a citizen now keeps its record when
   its body respawns** in the same slot — that was the open design question in this item, answered by
   the key rather than by a decision; revisit if that is not the wanted behaviour.
8. **Network.** Player-vs-player is local today: challenges and results are in-process only. Needs a
   packet type (a new `PACKET_CHALLENGE`/`PACKET_DW_STATE`), authoritative resolution on the server,
   and a client board. The determinism (one seed, one deck pair) is what makes this cheap — send the
   seed, replay the match.
9. ~~Per-card golden vectors~~ — **DONE (2026-11-03), and it paid for itself immediately.**
   `PARENA/stdlib/deadweight/match_parity.txt` holds five whole matches (seed, each seat's
   deck-crafting inputs, aggression, every round's cards, hulls, energy, credits and damage), written
   and read by `packages/simulation/dw_match_parity_test.c` — `make test-dw-parity` asserts it,
   `make regen-dw-parity` regenerates it — and asserted by NOOBTOPIA's
   `tests/deadweight_match_test.lua` against its Luau port of the sequencer. **Two bugs fell out of the
   first comparison, one in each world:** the emitter's `if` lowering mangled Bool ladders (fixed in
   PARENA), and this file's policy chose each card BEFORE the round dealt, so round 1 played card 0,
   which neither seat held (see the CHANGELOG entry). A third thing it exposed is worth keeping in
   mind: `make test-deadweight` had passed over that bug because every scripted round names its own
   cards — the fixture is the only test here that asserts WHICH card a policy plays.
   **Still open:** the fixture pins the C and the Luau against each other, not against
   `DEADWEIGHT/core/match.c` upstream (that file is not in this tree), so effect ORDER is pinned to
   this sequencer's own answers plus a hand-checked row or two.
10. **Fix the emitter gap that shaped this design.** `parena build` types an imported call's result as
    `void *`, so `(let [k (card-kind id)] ...)` does not compile in C. That is why
    `citizen_policy.prn` is import-free and takes facts as arguments. Fixing the emitter (propagate
    the callee's declared return type across module boundaries) would let B3 be written the obvious
    way. It is a contained compiler change and it affects every future module that wants to use
    another module's numbers.
11. **Balance telemetry.** `dw_ladder` already tallies kind plays and per-kind results. Surface it
    (HUD line, WOTAN leaderboard): meta share over time, win rate by deck kind, and which cards are
    never played — the last one is how a dead card gets found.
12. **Other populations as players.** Zombies, The Men and the hero's NPCs all have pids and can be
    added to the ladder with `dw_ladder_add_citizen`; the only reason they are not is taste (does a
    SHAMBLER play cards?). Worth a design decision, not a code change.
13. **Emily Way bookkeeping** — same blocked state as A10.

14. ~~The ladder has no outside world~~ — **DONE (same day, the internet-of-SHANKPIT shim).**
    `dw_net.*` exports schema 1 and publishes it to one file (off unless `SHANKPIT_NET_SNAPSHOT` is
    set), `tools/shankpit_netd.c` serves it with a real ETag/304 and CORS, and WOTAN's
    `/shankpit-citizens.html` reads it through `js/shankpit-net.js`. Remaining in that area is
    deployment and the direction of travel, below.
15. **Deploy the daemon.** `WOTAN/ops/nginx-wotan.conf` proxies `/api/v1/shankpit-net/` to
    `127.0.0.1:8791`, which is right for a box and wrong for the cluster: WOTAN runs as a static
    nginx pod, so the upstream has to be the address of whatever host runs a world (a Service, or
    the game host by IP). Needs a real deployment decision and a systemd unit beside
    `dw-ws-bridge.service`.
16. **Results to IDUNA.** A finished match is a record only inside this process. Reporting it as a
    rated result needs a `shankpit.match.write`-gated POST from the daemon (it is the only long-lived
    process that sees them all), and IDUNA needs a DEADWEIGHT-ladder table to hold it — the same
    shape as `game_player_stats`, not a new subsystem. Until then "records that survive a restart"
    (item 7) and "rated" are both still open.
17. **Draw the feed.** Nothing in the game reads `dw_net_export`: a phone tab or a HUD line ("2
    citizens are playing, Adele challenges you") is the in-world half, and it is the same gap as
    item 2 seen from the other side.
18. **One file is the contract, so watch it.** The export is a pure function and its shape is pinned
    by WOTAN's test; any key change is a schema bump in both repos at once. Worth remembering before
    adding a field casually.

---

## Where the seams are (for whoever picks this up)

| Want to change… | Touch |
|---|---|
| a card's numbers / a new card | `PARENA/stdlib/deadweight/card_rules.prn` then `make regen-dw-engine` |
| how a citizen picks or builds | `PARENA/stdlib/deadweight/citizen_policy.prn` then `make regen-dw-engine` |
| the ORDER effects resolve in | `packages/simulation/dw_match.c` (this is the sequencer; DEADWEIGHT's own `core/match.c` is not in this tree) |
| who plays whom, records, meta | `packages/simulation/dw_ladder.*` |
| the engine backend (native vs wasm) | `packages/simulation/dw_engine.*` |
| a citizen's body | `PARENA/stdlib/forgedinface/citizen.prn` then `make regen-citizen-avatar` |
| where a citizen is drawn | `draw_player_skin_mannequin` in `apps/lobby/src/main.c` |

## Verification actually run on 2026-11-03

```
make test-deadweight            -> deadweight: ALL PASS (hand-lifecycle channels covered)
make test-citizen-avatar        -> citizen_avatar: ALL PASS
make test-witness-ai-zombies    -> ALL PASS
make -B server                  -> links (one pre-existing unused-variable warning)
gcc -std=gnu99 -O1 -c apps/lobby/src/main.c  -> 0 errors (GLU prototypes stubbed)
```

Not run, and why: no GL driver (nothing rendered), `make -B lobby` (missing sibling artifact, A7), no
emulator/device, and no IDUNA/emily session (no Apple, no kanban).

---

## C. Landed since this report (same day) — the rating, and the Roblox surface

This section exists because two of the "not done" items above are now done, and a stale report is
worse than no report.

1. **The C ladder carries a rating.** `PARENA/stdlib/deadweight/rating.prn` is emitted to
   `packages/simulation/dw_rating.c` by `make regen-dw-engine` (beside `dw_rules.c` and
   `dw_citizen_rules.c`), `DwLadderMember` carries `rating`, and `dw_ladder_record` applies ONE
   zero-sum delta per match — the same recorder a watched duel and an instant match share, so a game
   cannot be priced two ways. The save format is 2 and still LOADS a v1 file. The ladder parity fixture
   prints the rating in every member line, so both worlds pin the pricing.
   *Not done:* nothing inside the game RANKS on it yet (the phone's roster still lists records), and
   `dw_net.c`'s published snapshot does not carry the field, so WOTAN cannot show it.
2. **The Roblox surface exists** — in NOOBTOPIA, not here: `src/deadweight_room.lua` (seats, offers,
   one prompt, `frame()`), `src/deadweight_ui.lua` (tabletop + `ProximityPrompt` + a server-written
   panel) and `src/deadweight_server.lua` (one ladder per server). It has **never been run** (no
   Studio). A real bug came out of writing it: `memberForPid` answers -1 for any negative pid — the C's
   own "-1 means nobody" — so the server's negative citizen pids were unreachable bodies. Citizens now
   mint positive pids from a documented base.
3. **Still the biggest gap on this list:** nobody has LOOKED at any of it. No GL driver here, no
   Roblox Studio, and `make -B lobby` still cannot link (pre-existing, missing
   `../EDITOR.GAME/gen/editor_stdlib_gen.c`). Item A6 (a headless frame dump through
   `stdlib/forgedinface/render.prn`) is the cheapest way to change that, and it is now the one that
   unblocks the most.
