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

### 6. Stale-claim sweep in PARENA's stdlib (comment-only, cheap)
The compiler accepts F32, I64, `fn`, bare `defenum` and `case` as of `PARENA@aa0b8ec`, but several
stdlib headers still justify their integer ABI with "VS0 has no F32 params yet". The BIG_O and SHANKPIT
hoverboard files are already corrected; these still carry the old wording:

- `PARENA/stdlib/brawlpit/commander_mod.prn:17`
- `PARENA/stdlib/ecowar/card_effect_mod.prn:15`
- `PARENA/stdlib/racer/bike_gear_mod.prn:17,19`
- `PARENA/stdlib/redgarden/abraham_fireball_mod.prn:33`
- `PARENA/stdlib/papercraft/interact_falloff_mod.prn:6`
- `PARENA/stdlib/papercraft/pickup_mod.prn:10`
- `PARENA/stdlib/papercraft/explosion_mod.prn:11`
- `PARENA/stdlib/papercraft/level_mod.prn:8`
- `PARENA/stdlib/papercraft/stat_effects_mod.prn:9`
- `PARENA/stdlib/papercraft/slide_jump_mod.prn:12`
- `PARENA/stdlib/papercraft/talent_mod.prn:11`
- `PARENA/stdlib/papercraft/paper_fragment_mod.prn:9`
- `PARENA/docs/TTS_VITS_PORT_PLAN.md:32` (describes `emit.c`'s type table, which now has an F32 row)

Keep the fixed point where it is genuinely the right call (exactness, per-tick determinism) — the point
is to stop citing a limitation that no longer exists, not to convert the constants.
*Done when:* `grep -rn "no F32\|F32 params\|F32 is missing" PARENA/stdlib` returns only comments that
explain a deliberate integer choice.

### 7. A second consumer for the unit layer: geometry authoring
The cast range is the first real consumer; the level boxes, the brick solver and the editor still author
in raw metres. Pick one dimension a designer would state in feet (a doorway, a jump span), put it in
PARENA in feet, convert through `units_author_length_m` and pin it with a test.
*Done when:* changing one authored foot value visibly changes the geometry, with no second conversion
site in C.

### 8. ~~Port hygiene in the E2E~~ -- done 2026-11-03
`make test-wow-e2e` now defaults all three phases to `E2E_PORT=0`, which makes the test bind an ephemeral
UDP port, ask the OS for its number, close it and hand that port to the child; an explicit port still
overrides. A dev server on 6979 can no longer make the wire look broken. If the child loses the tiny
bind/close race, the CONNECT retry times out and the failure names the port it tried.

*Done when (met):* `make test-wow-e2e` passes with a dev server already listening on 6979.

### 9. Bytes on a 1 kbit link
A 6-byte CAST currently travels in an 80-byte envelope (`NetHeader` 10 + length byte + the padded frame
buffer). That is the right trade for a LAN and the wrong one for the radio this format exists for. Measure
first: bytes per cast on the slow preset, then trim (pack the header, drop the padding) or compress — LZ4
is this monorepo's default codec, but at 6 bytes of payload a hand-packed envelope will beat it.
*Done when:* there is a measured bytes-per-cast number for the slow preset, before and after.

## Known-good commands

```bash
cd SHANKPIT
make test-wow-e2e        # real UDP, real server, real ticket shape
make test-wow-wire       # byte layouts
make test-wow-client     # retry + ACK state machine
make test-wow-ticket     # MAC/expiry
make test-units check-units
make check-wow-wire-rules
make lobby server
```

PARENA: `make test` (374 assertions), `make test-units`, `make check-units`.

## What would make this report wrong

If a real IDUNA ticket lands (lane 5), lanes 1-2 become the only real risk left on the wire, and lane 4
becomes the largest remaining unit of work. If the Go toolchain appears, lane 4 is a day of adapter code
against a seam that already has a test harness on the C side.
