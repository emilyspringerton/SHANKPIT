# The macro/targeting wire — status and next steps

Written 2026-11-03, at the end of the turn that made the wire real end to end. Every "done" below is
backed by a command that was actually run on the committed tree; every "not done" says what is missing
and where the seam is, so the next thread starts from the real state instead of re-deriving it.

Read `docs/WOW_WIRE.md` first for the frames themselves and `docs/UNITS.md` for the unit layer.

## What landed

| Unit | Real state | Verified by |
|---|---|---|
| Frames + decoders (`packages/protocol/wow_wire.h`) | SET/CLEAR target, CAST, DEFINE/INVOKE macro, AUTH, ACK; every byte layout asserted | `make test-wow-wire` |
| Retry + ACK (`wow_client.h`, `wow_server.h`) | bounded pending table, byte-identical re-sends (a fresh sequence would be rejected as stale), idempotent server re-ACK, `WOW_CLIENT_PENDING` 4, `wow_retry_max` 2/4/5 by preset | `make test-wow-client` |
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

### 1. Prove retry under loss, not just under duplication
Today the re-ACK is proven against a real server, but **no ACK is ever dropped**, so the resend path is
only exercised by calling `wow_client_retry` directly. Add loss to the E2E: either a `--drop-acks N`
flag on the test server (cheapest: count ACK sends, drop the first N) or a 30-line UDP relay in front of
it that swallows every other ACK. Then assert the frame retires within `wow_retry_max` and the chat line
appears when it does not.
*Done when:* the test drops 2 of 3 ACKs and still retires the frame; and with all ACKs dropped it reports
the give-up exactly once, after `wow_retry_max` sends.

### 2. Make the range per weapon, and give it a vertical tolerance
`wow_cast_range_yd` is one global number: a knife and a rifle currently share 40 yd. Move the table into
PARENA beside the weapon ids (`WPN_*` already come from the generated rules), keep the yard unit, and add
a vertical band so a shot at a target on a roof parapet is judged the same way a player expects. The
conversion stays in one place (`units_author_length_m`) — do not add a second one.
*Done when:* a test shows 25 yd refused at 30 and 55 yd accepted at 50 for two different weapons, and a
target 12 m up at 10 m horizontal is still in range for the rifle.

### 3. Fuzz the decoders
Malformed input is asserted today by hand-picked cases (bad version, unknown op, slot out of range,
sequence 0, trailing bytes). That proves the cases someone thought of. `wow_wire_decode`,
`wow_targets_apply` and the macro decoder are pure functions over a byte buffer — ideal for a fuzz
target that asserts only "never coerces, never reads past `len`".
*Done when:* a fuzz target runs under ASan/UBSan for a fixed budget in CI with no findings, and the same
harness replays the corpus from `make test-wow-wire`.

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

### 8. Port hygiene in the E2E
`test-wow-e2e` defaults to a fixed port (6979). A dev server already on that port makes the test look
like a wire failure. Let the test bind an ephemeral port and tell the child which one, or fail fast with
a message that names the collision.
*Done when:* `make test-wow-e2e` passes with a dev server already listening on the default port.

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
