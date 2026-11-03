# The macro/targeting wire — status and next steps

Written 2026-11-03, at the end of the turn that made the wire real end to end. Every "done" below is
backed by a command that was actually run on the committed tree; every "not done" says what is missing
and where the seam is, so the next thread starts from the real state instead of re-deriving it.

Read `docs/WOW_WIRE.md` first for the frames themselves and `docs/UNITS.md` for the unit layer.

## What landed

| Unit | Real state | Verified by |
|---|---|---|
| Frames + decoders (`packages/protocol/wow_wire.h`) | SET/CLEAR target, CAST, DEFINE/INVOKE macro, AUTH, ACK; every byte layout asserted | `make test-wow-wire` |
| Retry + ACK (`wow_client.h`, `wow_server.h`) | bounded pending table, byte-identical re-sends (a fresh sequence would be rejected as stale), idempotent server re-ACK, `WOW_CLIENT_PENDING` 4, `wow_retry_max` 2/4/5 by preset | `make test-wow-client`, `make test-wow-e2e` (loss phases) |
| Identity (`wow_ticket.h`) | 36-byte IDUNA ticket, MAC-first constant-time verify, expiry after MAC | `make test-wow-ticket`, `make test-hmac` |
| Cast range as a rule | `wow_cast_range_yd` (40) in PARENA, converted in C once via `units_author_length_m(..., 5)` (36.576 m), squared-distance compare | `make check-wow-wire-rules`, `make test-units` |
| Give-up is visible | an abandoned frame pushes `WOW: the server never acknowledged a <kind> …` into the chat pane from `WowClient.last_abandoned_kind` | `make lobby` |
| End to end over UDP | real `bin/shank_server`, real ticket, target intent + re-send + retire, cast — ACKs read off the socket | `make test-wow-e2e` (also in CI) |

The E2E found one real bug the moment it ran: a CONNECT sent before the server finishes binding is lost,
and every later frame is then dropped as an unknown sender — indistinguishable from a broken wire. The
test now retries CONNECT until WELCOME, exactly like the lobby's reconnect path.

## Honest headline

**The wire is proven against a real server and a real socket; it has never been driven by a real
player.** There is no display on this box, and no IDUNA-issued ticket, so:

- nothing has been *seen* — the crosshair plate, the range feedback and the give-up chat line are
  compiled, not observed;
- the ticket in the E2E is minted locally with the shared secret, not obtained through SSO;
- the GoblinFoxDragon MUD side has no code at all (see lane 4).

## Next steps, in the order worth doing them

### 1. ~~Prove retry under loss~~ -- done 2026-11-03
The E2E now runs three phases against three differently-behaved private servers: `0` (every ACK arrives),
`2` (the server swallows the first two CAST ACKs via `--drop-cast-acks 2`, so only the client's real
re-send can recover), and `99` (every CAST ACK is lost, so the client must abandon exactly once).
Asserted: phase 2 re-sends at least once and still retires the frame; phase 3 sets
`WowClient.last_abandoned_kind` to `WOW_OP_CAST`, empties the pending table, and never reports an ACK.
The flag is test-only and defaults to 0, so a production server's ACK path is untouched.

*Done when (met):* `make test-wow-e2e` passes all three phases with the ACK drops actually happening.

### 2. ~~Make the range per weapon~~ -- done 2026-11-03
`wow_cast_range_yd_for(WPN_*)` owns the table in yards: knife/hammer 2, spray can 8, flashlight 15,
shotgun 20, magnum 25, rifle 55, missile 90, sniper 120; an id the table does not know falls back to the
generic 40. The server converts the chosen value once with `units_author_length_m` and compares squared
distances. The vertical question was answered by measuring in 3D instead of adding an artificial band: a
target 12 m up and 10 m away is 15.6 m from the muzzle and in range for a rifle, while one straight
overhead at 40 m is out. Asserted by `make test-wow-wire` (the table, its ordering, and the fallback, by
`WPN_*` name) and kept honest by `make check-wow-wire-rules`.

*Done when (met):* two weapons disagree, elevation is a consequence of the measurement rather than an
afterthought, and both are assertions instead of intentions.

### 3. ~~Fuzz the decoders~~ -- done 2026-11-03
`test-wow-wire` now ends with a deterministic randomized sweep: 20,000 buffers of length 0..96 through
`wow_ack_decode`, `wow_cast_decode`, `wow_invoke_decode`, `wow_targets_apply` and `wow_server_rx`, each
read from an exactly-sized heap buffer (so an overread is an ASan report rather than a lucky pass) with
encoder caps deliberately smaller than the frames. Then 2,000 single-bit mutations of a valid cast: when a
mutated frame still decodes, the decoded fields must equal the mutated bytes -- the decoder may refuse, it
may not invent. The ASan/UBSan half of the target runs all of it, and the seed is fixed, so any failure
reproduces. Honest scope: this is a randomized sweep, not a coverage-guided fuzzer with a persisted corpus.

*Done when (met):* the sweep runs in CI under the sanitizers and every decoder refuses instead of coercing.

### 4. The GoblinFoxDragon adapter — still blocked, deliberately unwritten
The seam is already named and stable: the MUD tick calls `wow_server_take_cast()` and installs
`WowServer.unit_truth`. **No Go toolchain exists on this box** (`go version` → not found), and shipping
uncompiled Go is not acceptable, so this stays blocked rather than faked.
*Done when:* `go build ./...` runs in this repo's CI lane, a MUD tick drains one cast, and a cast's
`[harm]` evaluates against the MUD's own unit truth.

### 5. A real player, on a machine with a display
The only honest way to close the visual gap. Needs: `SHANKPIT_TICKET_SECRET` shared with IDUNA, an
IDUNA-issued ticket in the lobby's login path, and a GL-capable box. Then check the crosshair target
plate, the 40 yd range feedback and the give-up chat line.
*Done when:* a screenshot exists showing a cast fired at a real target through a real login. Not
claimable from here — say so rather than substituting "it compiles".

### 6. ~~Stale-claim sweep in PARENA's stdlib~~ -- done 2026-11-03
The headers that justified an integer ABI with "VS0 has no F32 params" now say what is true: the boundary
is integer **by design**, and F32 params exist in the emitter as of 2026-11-03. Touched:
`brawlpit/commander_mod.prn`, `ecowar/card_effect_mod.prn`, `racer/bike_gear_mod.prn`,
`redgarden/abraham_fireball_mod.prn`, seven `papercraft/*_mod.prn` headers, and
`docs/TTS_VITS_PORT_PLAN.md` (whose `emit.c` reading predates the F32/I64 rows). No constants or signatures
changed -- the fixed point is still the right call where exactness matters.

*Done when (met):* the grep now returns only the two hoverboard comments that explain a deliberate integer
choice, not a missing feature.

### 7. ~~A second consumer for the unit layer: geometry authoring~~ -- done 2026-11-03
The procedural dungeon's gate is now a dimension a designer states in feet: `zone-door-height-ft` (**7 ft**)
sits in `PARENA/stdlib/shankpit/zone_rules.prn` beside the rest of the zone layout, and
`packages/simulation/proc_dungeon.h` converts it exactly once with `units_author_length_m(..., 4)` before
building the gate box. 2.1336 m appears nowhere in C. Two things make this a real consumer rather than a
decoration: the gate used to span the full wall height (18-30 world units), so the dungeon geometry visibly
changed, and the test now asserts the gate box's height equals the authored feet value, that 7 ft converts
to 2.1336 m, and that the gate is shorter than the wall. `test-proc-dungeon` also keeps the PARENA rule and
the header's compiled-in default in agreement sample for sample, and the lobby had to link
`packages/common/units_rules.c` -- which is how you can tell the call is really there.

*Done when (met):* changing the authored feet value changes one box, with a single conversion site.

### 8. ~~Port hygiene in the E2E~~ -- done 2026-11-03
`make test-wow-e2e` now defaults all three phases to `E2E_PORT=0`, which makes the test bind an ephemeral
UDP port, ask the OS for its number, close it and hand that port to the child; an explicit port still
overrides. A dev server on 6979 can no longer make the wire look broken. If the child loses the tiny
bind/close race, the CONNECT retry times out and the failure names the port it tried.

*Done when (met):* `make test-wow-e2e` passes with a dev server already listening on 6979.

### 9. ~~Bytes on a 1 kbit link~~ -- measured and trimmed 2026-11-03
The envelope no longer carries the 64-byte frame buffer: the length byte already frames the datagram and
the server validates `header + 1 + len <= size`, so both senders (lobby and E2E) now send exactly
`header + 1 + len`. A cast went from **80 bytes to 19**, the round trip from 98 to **37**, and a
slow-preset give-up from 240 to **57**. At 125 B/s that is **296 ms** per successful cast (was 784 ms) and
**456 ms** for a give-up (was 1920 ms). `wow_wire_test` pins the new figures, and the E2E now proves the
server accepts the short datagram for real -- auth frames arrive at 51 bytes and casts at 19.

The first attempt at this patch aborted because the lobby's `sendto` line is byte-identical in three
functions; the fix is anchored on the unique `memcpy` + send pair, which is worth knowing before the next
trim.

*Done when (met):* measured before and after, with the after pinned by the test.

## Verified together (2026-11-03)

One pass, after lanes 1-3 and 6-9 landed, run rather than remembered: `check-wow-wire-rules`,
`test-wow-wire` (plain and ASan/UBSan), `test-wow-client`, `test-wow-macros`, `test-wow-ticket`,
`test-units`, `check-units`, `test-proc-dungeon`, `test-zone-sync`, `check-levels`, `test-leveltool`,
`make lobby`, `make server`, `test-wow-e2e` (all three phases on ephemeral ports), and PARENA's
`make test` (374/374). Every one exited 0 on this tree.

Lanes 4 and 5 are blocked, and the same pass re-checked instead of assuming: no `go` binary, no `DISPLAY`,
zero `/dev/dri` entries, no IDUNA-issued ticket. Nothing above claims anything about them.

## Re-verified 2026-11-03 (wire integration)

The pass below was re-run after the client/server integration fixes in `7b904f2` (retry poll off the
snapshot gate), `9cf42b8` (that change's rationale corrected -- `wow_net_wire_ready()` requires
`STATE_GAME_NET`, so there is no menu AUTH path) and `9274712` (refused wire ops are logged instead of
discarded). All fifteen targets exit 0, `test-wow-e2e` ran **three times** and produced 3/3 phases each
time (the ports are ephemeral, so a collision would have flaked rather than failed), and PARENA is
374 passed / 0 failed.

What this does and does not cover: the E2E drives the real server over UDP, so retry, ACK reprieve and
give-up are proven on the wire. The lobby's own call sites are compile-verified only -- no `DISPLAY` and
no IDUNA ticket here, so nothing has yet watched a player take a shot.
