# WoW-compatible macros, unit tokens, and the add-on surface

This is the part of the WoW macro/add-on model that SHANKPIT can actually run today, and an explicit
list of what it cannot. The layers are separated so that a scripting binding, when one exists, is a thin
wrapper rather than a rewrite.

```
  "/cast [harm,nodead,@mouseover] Blaster; [@player] Mend"
                  |
            wow_macro_parse()          packages/ui/wow_macro.h
                  |  text -> WowMacroLine { cmd, clause[4] { require_bits, forbid_bits, token, arg } }
                  v
            wow_macro_resolve()        first clause whose masks hold for the unit behind its token
                  |
                  v
            wow_macro_execute()        packages/ui/wow_api.h -> WowHostApi vtable -> the engine
```

## What compiles

| Syntax | Meaning here |
|---|---|
| `/cast`, `/use`, `/target`, `/focus`, `/cleartarget`, `/clearfocus`, `/startattack` | the command; a leading `/` is optional |
| `[harm]`, `[help]`, `[dead]`, `[stealth]`, `[combat]`, `[exists]` | a REQUIRE bit |
| `[nodead]`, `[noharm]`, `[noexists]`, ... | the same bit as a FORBID bit — `nodead` is "not dead", not a second word |
| `[harm,nodead]`, `[harm][nodead]` | identical: bracket groups accumulate with AND |
| `[@player]`, `[@target]`, `[@focus]`, `[@mouseover]`, `[@arena1]`, `[@arena2]`, `[@party1]`, `[@none]` | the unit token the clause acts on |
| `a; b` | clauses, tried in order, first match wins |

`[help]`/`[harm]` only hold for a unit that EXISTS — a missing target is not "helpable". A clause with no
`[@token]` acts on the engine's current target, which is why `/cast [help] Mend; [harm] Zap` reads the
way a WoW player expects. `[@none]` is a real token meaning "no unit", so `[noexists]` is expressible
without a second code path.

Evaluation is two mask tests: `(bits & require) == require && (bits & forbid) == 0`. The cost of a macro
is the parse (once, when bound) and then a few ANDs per activation. Nothing is looked up by string at
cast time, which is the only reason this is worth compiling at all.

### Rejection, not reinterpretation

`wow_macro_parse` returns a specific code: `-1` unknown command, `-2` unclosed bracket / unknown
condition / unknown token, `-3` more than 4 clauses, `-4` argument too long (48), `-5` a required
argument missing, `-6` line longer than 192 bytes, `0` blank. A macro the player wrote does not silently
become a different macro.

## The host contract (`packages/ui/wow_api.h`)

`WowHostApi` is the engine side: `unit_exists`, `unit_dead`, `unit_hostile`, `unit_health`,
`unit_health_max`, `cast_spell`, `use_action`, `set_target`, `clear_target`, `set_focus`, `clear_focus`.
A NULL pointer means "this engine does not offer that", and the macro reports a refusal rather than
crashing. `wow_host_unit` adapts the vtable to the macro evaluator.

`wow_api_globals[]` declares the global surface an add-on calls, with arity, arguments, results and what
backs each one today:

- **host** — `CastSpellByName`, `UseInventoryItem`, `TargetUnit`, `ClearTarget`, `FocusUnit`, `ClearFocus`,
  `UseAction`, `UnitExists`, `UnitHealth`, `UnitHealthMax`, `UnitPower`, `UnitPowerMax`,
  `UnitCanAttack`, `UnitCanAssist`, `UnitIsDeadOrGhost`
- **frame** — `CreateFrame`, `SetAttribute`, `RegisterEvent`, `SetScript` (declared, not implemented)
- **todo** — `UnitName`, `UnitClass`, `UnitGUID`, `GetSpellCooldown`, `IsUsableSpell`, `UnitCastingInfo`,
  `UnitChannelInfo`, the action bar queries

The table is a contract, and it is tested: names are unique, arity is sane, no invented globals.

## Add-on metadata (`packages/ui/addon_toc.h`)

`## Key: value` directives, `#` comments, then bare file lines loaded in order. Unknown `##` keys are
ignored (a newer client's key must not stop an add-on loading). Lists that overflow are an error, not a
truncation, because truncating a dependency list changes load order — the one thing this file exists to
get right. `addon_toc_order` is a depth-first walk over a fixed table: `-1` a required dependency is not
installed, `-2` a dependency cycle, with `*bad` naming the add-on responsible. No `exit()`, no partial
load, no alphabetical fallback.

## What is NOT here

- **No Lua VM.** Lua is not vendored in this repository and adding it is a dependency decision, not a
  detail. Everything above is the layer a binding wraps; nothing above pretends to be the interpreter.
- **No XML layout parsing, no `CreateFrame` implementation, no frame metatable, no anchors.**
- **No event bus** (`PLAYER_TARGET_CHANGED`, `UNIT_HEALTH`, `UNIT_SPELLCAST_*`).
- **No combat log framing** (`COMBAT_LOG_EVENT_UNFILTERED` and its parameter tuples).
- **No secure templates** and therefore no protected/combat-lock model. That model exists in WoW to stop
  add-ons automating targeting; it is a security boundary, and a half-built one is worse than none.
## Wired into the lobby (local matches)

`apps/lobby/src/main.c` now dispatches `cast`, `use`, `target`, `focus`, `cleartarget`, `clearfocus` and
`startattack` through `wow_macro_parse` + `wow_macro_execute`, so the same text works typed in chat with
a `/`, in `shankpit_macros.cfg`, and on a bound key (binds are checked before the built-in keys):

```
bind F6 cast [harm,nodead,@mouseover] magnum; [@player] knife
```

- **`[@mouseover]`** is the actor under the crosshair: the same eye ray and the same `check_hit_location`
  the guns use, nearest hostile first.
- **`[@player]`** is slot 0 while `app_state == STATE_GAME_LOCAL`.
- **`/cast magnum`** equips that weapon and pulls the trigger for the next input pass — the engine's
  "spell" is a gun. The name table is **generated** from `packages/common/protocol.h`
  (`scripts/gen_wow_weapons.py` → `packages/ui/wow_weapons.h`), so a weapon cannot exist in the engine and
  be misspelled in a macro; `make check-wow-weapons` fails CI on drift. Unknown names are refused.
- **`/use 7`** = the number keys' own slot mapping (1..6 → weapons 0..5, 7 flashlight, 8 hammer,
  9 spraycan), one generated table, not a second copy.
- **Target/focus** are this client's pointers (`g_wow_target`, `g_wow_focus`), set and cleared by the
  commands; `[harm]` is the engine's own `phys_is_friendly` rule, so FFA targets read hostile and team
  targets do not.

Named limits, not hidden ones: **local matches only** — in a net match a unit query answers "no unit"
rather than guessing somebody else's actor slot, and the attempt says so in chat. `[stealth]` never
holds because the engine does not track stealth. Target/focus are not on the wire. `/cast` fires; it
does not queue a cast through a server-authoritative ability buffer (there is no such buffer).

Stated plainly: the macro language, the unit-token model, the host vtable, and the add-on loader are
real and tested headless (`make test-wow-macros`). The Lua runtime, the UI toolkit and the event/combat
log surfaces are contract only.
