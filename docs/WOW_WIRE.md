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

**Identity is IDUNA's.** The `AUTH` frame carries a 48-byte **player ticket**, never the 4 KB SSO JWT —
the token stays in the client. Verification is a `WowAuthFn`; the server starts with `require_auth = 1`
and **no verifier installed**, so every wire op is refused (fail closed) until the IDUNA call is wired.
That is deliberate: a server that silently accepts unauthenticated casts is worse than one that does
nothing until SSO is connected. The test suite covers both the refusal and a verified flow (fake
verifier), including that an unauthenticated *other* client cannot set a target.

A queued cast **fires**: `process_user_cmd` drains `wow_server_take_cast()` right after
`shankpit_apply_usercmd_inputs`, equips the weapon and sets `in_shoot` for that one command. Folding it
into the usercmd is the point — the shot is ordered against that client's own movement exactly like a
mouse click, and it cannot fire from the receive path, outside the simulation.

Honest gaps: a cast whose token is `target` or `focus` **does** aim at that entity -- the server resolves the slot in the per-client target table to a simulation slot and points the caster's yaw/pitch at its eye with `tp_yaw_pitch_to_point` (the inverse of the math the third-person camera already used) -- but only when that slot is a live player in the caster's own scene and not the caster; otherwise the shot leaves along the player's own aim rather than a crosshair that teleports. Other tokens and server-side aim assist are not built. The IDUNA verifier is not
installed (the server has no HTTP client), so authentication still fails closed. `WOW_SERVER_CLIENTS` is
8 while `MAX_CLIENTS` is larger, so slots beyond 8 are ignored with a one-time log. And the cast → fire
path is compiled, not driven: there is still no client in this environment to send a cast to a running
server.

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
