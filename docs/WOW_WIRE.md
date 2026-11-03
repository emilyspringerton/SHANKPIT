# The macro/mod wire: multiple targets, casts, and macros as bytecode

Text is a fine authoring format and a terrible wire format. On a LAN the difference does not matter; on
a 1 kbit radio it is the whole budget. So the client compiles the macro **once**
(`packages/ui/wow_macro.h`), ships the compiled form, and then invokes it by id.

Every number here is PARENA: `PARENA/stdlib/shankpit/wow_wire.prn` → `packages/protocol/wow_wire_rules.c`
(regenerated and byte-checked by `make check-wow-wire-rules`). The byte layouts are fixed and asserted by
`make test-wow-wire`.

## Frames

| Op | Bytes | Layout | When |
|---|---|---|---|
| `SET_TARGET` / `CLEAR_TARGET` | 7 | `ver, op, seq_lo, seq_hi, slot, entity_lo, entity_hi` | only when the value **changes** |
| `CAST` | 6 | `ver, op, weapon, token, seq_lo, seq_hi` | one pull of the trigger |
| `DEFINE_MACRO` | 5 + Σ(6 + len(arg)) | `ver, op, id, cmd, clause_count`, then per clause `require_lo, require_hi, forbid_lo, forbid_hi, token, arg_len, arg…` | once per macro, at registration |
| `AUTH` | 38 | `ver, op, player_id[16], expires_at(LE u32), mac[16]` | once, at connect |
| `INVOKE_MACRO` | 5 | `ver, op, id, seq_lo, seq_hi` | every activation |
| `ACK` | 5 | `ver, op, seq_lo, seq_hi, kind` | answering any intent |

The two-clause example `cast [harm,nodead,@mouseover] magnum; [@player] knife` is **28 bytes** on the
wire versus 58 bytes of text, and costs **5 bytes** per press afterwards. A `/cast` of the same thing
costs **6 bytes** instead of 37 — and the server never parses a spell name, because the weapon id is the
engine's own `WPN_*` number.

## Multiple targets

`WowTargets` is a four-slot table **per client**: `target`, `focus`, and two slots a mod can name. Each
slot holds an entity id and an explicit presence bit, so "no target" is different from "entity 0".

- The sender emits an intent only when `wow_targets_set`/`clear` reports a change. Re-aiming at the same
  mob sends **nothing**.
- Every intent carries a per-client 16-bit sequence; `wow_targets_apply` accepts only a **strictly
  newer** sequence (wrap-safe via signed difference) and drops stale or duplicated datagrams. A
  reordered packet cannot resurrect a target you already left.
- Malformed input (wrong version, unknown op, slot out of range, entity 0 on a SET, sequence 0, short
  frame) is refused, never coerced.

`wow_target_resend_ms(preset)` bounds how often the server may re-send target state: 500 ms by default,
1000 ms on a slow link, 250 ms on a fast one. `wow_cast_queue(preset)` bounds outstanding casts (2 / 1 /
3) — a queue deeper than the link can drain is latency with extra steps.

## Macros as bytecode, and equivalence

`DEFINE_MACRO` is the compiled `WowMacroLine`: command, clause count, and per clause the two condition
masks, the unit token and the argument. The test the matters is the round-trip one — compile text →
encode → decode → **compare every mask, token and argument** — so the wire cannot quietly disagree with
the compiler. Trailing bytes after a definition are a refusal, not something to ignore.

`INVOKE_MACRO` is how a mod's macro is activated in 5 bytes. The macro id is 1..255, `0` is "no macro".

## The mod surface

`packages/ui/wow_mod.h` is deliberately a **table, not a VM**:

- `wow_mod_register_macro(name, id, text)` compiles at registration; text the compiler rejects is
  refused (`0`), a duplicate name **or** duplicate id is refused (`-1`).
- `wow_mod_register_spell(alias, weapon)` only accepts weapons the engine actually has — the alias
  cannot invent an id.
- `/cast` resolves a mod alias first, then the engine's own generated weapon names.

A mod cannot shadow another mod's macro, cannot register something the compiler rejects, and cannot add
a weapon. That is the entire capability boundary, which is why it fits in a header.

## The server end (PACKET_WOW_WIRE 21)

`apps/server/src/main.c` routes `PACKET_WOW_WIRE` through `wow_server_rx()`
(`packages/protocol/wow_server.h`) and sends back the ACK it produces. The envelope is a `NetHeader`,
a length byte and up to 64 bytes of wire frame — one frame per datagram, no fragmentation.

Per connected client the server keeps its **own** four-slot target table, up to 8 macro definitions by
id, and a cast queue bounded by `wow_cast_queue(preset)` (2 by default, 1 on a slow link, 3 on a fast
one). `wow_server_take_cast()` is the FIFO the simulation drains; `[harm]` is evaluated against
`WowServer.unit_truth`, which the simulation installs, and with no hook a target is "exists, unknown
otherwise" so `[harm]` does not hold.

**Identity is IDUNA's.** The `AUTH` frame carries a **36-byte player ticket** -- never the 4 KB SSO JWT, which stays in the client. IDUNA mints it (`IDUNA/internal/http/handlers/shankpit_ticket.go`) as `player_id` (16 raw UUID bytes) `|| expires_at` (4 bytes LE unix seconds) `|| HMAC-SHA256(SHANKPIT_TICKET_SECRET, those 20 bytes)[:16]`, with a 5-minute TTL, and returns it hex-encoded; the wire carries the **raw** bytes, so the frame is 38 bytes and the server never parses hex. Verification is local (`packages/protocol/wow_ticket.h` over `packages/common/hmac_sha256.h`, RFC 4231 vectors in `make test-hmac`): constant-time MAC compare first, then the expiry check, then the canonical UUID text becomes the client's bound identity. The server installs the verifier from `SHANKPIT_TICKET_SECRET` at startup and otherwise keeps its **fail-closed** default -- no secret, no verifier, every op refused.

A queued cast **fires**: `process_user_cmd` drains `wow_server_take_cast()` right after
`shankpit_apply_usercmd_inputs`, equips the weapon and sets `in_shoot` for that one command. Folding it
into the usercmd is the point — the shot is ordered against that client's own movement exactly like a
mouse click, and it cannot fire from the receive path, outside the simulation.

Honest gaps: a cast whose token is `target` or `focus` **does** aim at that entity -- the server resolves the slot in the per-client target table to a simulation slot and points the caster's yaw/pitch at its eye with `tp_yaw_pitch_to_point` (the inverse of the math the third-person camera already used) -- but only when that slot is a live player in the caster's own scene and not the caster; otherwise the shot leaves along the player's own aim rather than a crosshair that teleports. Other tokens and server-side aim assist are not built. The IDUNA verifier **is** installed: the server verifies the 36-byte ticket locally with the shared `SHANKPIT_TICKET_SECRET` (no HTTP client, no crypto library) and refuses every wire op when that secret is unset. What is not proven is a ticket from a real IDUNA deployment -- none exists in this environment, so the evidence is IDUNA's own format plus RFC 4231 vectors, not a live SSO handshake. `WOW_SERVER_CLIENTS` is
8 while `MAX_CLIENTS` is larger, so slots beyond 8 are ignored with a one-time log. And the cast → fire
path is compiled, not driven: there is still no client in this environment to send a cast to a running
server.

## The client half: one sender, and when it stays quiet

`packages/protocol/wow_client.h` is what turns the client's own actions into frames, and its whole job is
deciding **not** to send:

- `wow_client_target(&c, slot, entity, out, cap)` returns 7 bytes only when this client's mirror of the
  slot changes, and `0` otherwise -- holding a target costs no air at all. `entity < 0` clears the slot,
  so `/cleartarget` and `/clearfocus` are the same path as `/target` and `/focus`.
- `wow_client_cast` always returns 6 bytes: a cast is an event, not a state, and suppressing a repeat
  press would drop a shot. The sequence advances with PARENA's `wow_seq_next` (never 0, wraps at 16 bits),
  which is exactly the "strictly newer or drop" rule the receiver enforces.
- `wow_client_auth` copies the 36-byte ticket verbatim into the 38-byte AUTH frame. It does not mint,
  retry or refresh -- that is IDUNA's business and the caller's.

The lobby is the first caller (`apps/lobby/src/main.c`): `net_send_wow_frame` wraps one frame in a
`PACKET_WOW_WIRE` datagram beside the other fire-and-forget sends; `wow_net_begin_session()` resets the
mirror and sends AUTH on each connect attempt; `wow_net_sync_targets()` runs once per input pass and
sends target/focus only when they moved; and `/cast` in a net match sends a CAST intent instead of arming
the local trigger, because the server's cast queue is what fires in a net match.

The ticket itself comes from IDUNA (`POST /api/v1/shankpit/ticket`, 72 hex chars). The client has no HTTP
client, so it takes that text from `/ticket <72 hex>` or `SHANKPIT_TICKET` and keeps the 36 bytes. That is
a paste-in bridge, named as such: it is not an SSO login, and a missing or expired ticket leaves the
client exactly where the server puts it -- unauthenticated and refused.

## Retry and ACK: when the receipt is the packet that got lost

The server always ACKed what it accepted, but nothing on the client noticed the ACK, so a dropped datagram
was simply a dropped action. `wow_client.h` now keeps a **bounded** table of frames it has sent and not had
acknowledged (`WOW_CLIENT_PENDING` = 4: AUTH, two targets, one cast) and resends the **same bytes** when the
receipt does not arrive.

- `wow_client_track(&c, frame, len, now_ms)` remembers a just-sent frame. Only frames the server ACKs are
tracked; an ACK is not.
- `wow_client_ack(&c, frame, len)` retires the pending frame whose `kind` and `seq` the ACK names, so a late
  ACK cannot retire a newer frame that reused the slot.
- `wow_client_retry(&c, now_ms, out, cap)` returns the oldest due frame unchanged, `0` when none is due, or
  `-2` when that frame exhausted its retries and was dropped. The period is PARENA's `wow_ack_ms(preset)` and
  the cap is `wow_retry_max(preset)` (2 / 4 / 5 by preset), so a slow link waits longer instead of flooding
  itself, and a dead server cannot turn a 7-byte intent into an endless loop.
- **The resend is byte-identical.** Rebuilding it with a fresh sequence would be dropped by the receiver's
  "strictly newer" rule -- the exact failure retransmission exists to avoid.

That only works if the receiver is idempotent, so the server re-ACKs a frame it already applied:
`wow_targets_apply` answers **1** for "this is the sequence I already have" (re-ACK it) and **0** only for a
genuinely older sequence (a reordered datagram, still dropped), and `wow_server_queue_cast` answers 2 for an
already-accepted cast. A client whose first ACK was lost therefore recovers on the first resend, instead of
being left holding a target the server took.

The lobby is wired end to end: `wow_net_send_tracked()` sends and tracks, `wow_net_poll_retries()` runs once
per input pass, and a `PACKET_WOW_WIRE` received from the server is decoded as an ACK and retires the pending
frame. An abandoned frame is logged (`WOW_GIVEUP`) rather than silently forgotten.

## Cast range: a rule in yards, enforced in metres

A cast aimed at `[@target]` / `[@focus]` only turns the player's aim when the target is within
`wow_cast_range_yd` (**40 yards**) of them. The rule lives in PARENA because it is a game rule, and it is
written in yards because that is the unit it is reasoned about in; `apps/server/src/main.c` converts it once
through `units_author_length_m(..., 5)` and compares squared distances, so no C file restates 36.576. Past
range the shot still fires, along the player's own aim, and the refusal is logged -- a client cannot swing
the server's crosshair across the map by naming a distant entity.

## End to end, over a real socket

`make test-wow-e2e` is the difference between "the state machine agrees with itself" and "a server, a
socket and a ticket agree". It forks the real `bin/shank_server` on a private port (`--beacon-port 0`,
`SHANKPIT_BAKED_LEVELS=1`, so it is headless and never advertises), retries CONNECT until WELCOME (a single
CONNECT races the server's own startup), shows a ticket signed with another secret is never ACKed, then
drives a real IDUNA-shaped ticket, a target intent and its re-send, and a cast through loopback UDP.

## GoblinFoxDragon / MUD: what is real today

`GoblinFoxDragon` is now cloned at `MONOREPO/GoblinFoxDragon`. Its MUD is Go under `apps2/mud`
(line-oriented sessions over SSH/WebSocket: `ssh_listener.go`, `ws_listener.go`), with party state in
`server/party/party.go` and the product northstar in `docs2/MMO_NORTHSTAR.md`.

**No Go toolchain exists in this environment**, so no Go-side adapter was written or compiled — shipping
untested Go into a fork and calling it "glued" would be a lie. What exists here is the contract the
adapter has to satisfy: fixed frames, per-client target slots, ack sequencing, and macro ids. The adapter
is the next slice, and it is a Go package in `apps2/mud` that:

1. binds a second, binary listener (UDP) next to the line-based one,
2. maps a client's target slots onto its MUD session's own target state,
3. turns `CAST` into the MUD's existing attack/spell path rather than a new one,
4. ACKs with the same sequence it received.

Until that exists, nothing in this file has been on a radio or in a MUD session. It is a byte contract
with tests, which is the part that can be verified headlessly.
