# Solo / Non-Squad Enemy AI — NORTHSTAR

Founder real-time direction, 2026-09-17, direct follow-up to the now-closed S461 squad-based
auto-pilot AI (routed via `emily observe`, curated into `EMILY/BACKLOG.md` S462): when building
non-squad enemies (wild monsters, zombies, lone predators), the auto-pilot shifts focus from
tactical communication onto territorial boundaries, distinct sensory profiles, and physiological
drives. Three archetypes (relentless pursuer, ambush predator, territorial beast), asymmetric
sensory profiles (sound-only echo-locator, scent/blood-trail tracker), steering overrides
(sine-wave flank-weaving, wander jitter), and a utility-based Hunger/Fear/Fatigue drive system.

## What's built (S462, this pass)

Three new `AIRole` values in `story_ai.c`/`.h`, deliberately never passed to
`story_ai_form_squad` (stay `squad_id == -1`, the same default every AI already spawns with — no
new guard needed):

- **`AI_ROLE_RELENTLESS_PURSUER`** ("Zombie") — `ai_combat_pursuer` has no `preferred_range`
  kiting/backing-off branch at all, unlike every other role's combat function: it always closes.
  Courage `1.0` also clears S461-01's flee gate (`courage < 0.7`) outright — this archetype does
  not retreat, ever, which is the whole point.
- **`AI_ROLE_TERRITORIAL_BEAST`** — real radius-anchor + leash retreat. `home_x/y/z` (set to the
  AI's own real spawn position for every role, but only load-bearing when `leash_radius > 0`) +
  `leash_radius` (this role's own default, `120.0`). New `AI_MODE_LEASH_RETURN` + `ai_run_leash_
  return`, following the founder's own `TerritorialAutoPilot::Update` pseudocode closely: checked
  *unconditionally* in the mode-decision loop (before combat/flee, only `AI_MODE_SCRIPTED`'s lock
  takes priority), forces retreat regardless of current state, real stun immunity while returning
  using the actual existing `stunned_until_ms`/`stun_immune_until_ms` fields
  (`packages/common/protocol.h`) rather than inventing a new flag, exits only on real arrival
  (`dist < 2.0`) — matching "Arrived back home" exactly, not on the radius re-check alone.
- **`AI_ROLE_BLIND_STALKER`** ("Ambush Predator" + "Sightless Echo-Locator", combined — the
  founder's own doc treats these as one real behavioral thread, both centering on charging off
  non-visual detection). `vision_range = 8.0` means `ai_gather_perception`'s existing
  `dist <= vision_range` check almost never passes — this role is functionally blind and detects
  almost entirely through the *already-existing* `hearing_range` path. **Zero new perception code
  needed for this part** — it's a real, working example of how much of the founder's "asymmetric
  sensory profiles" ask the existing per-role stat-tuning system already covers for free.
- **Real sine-wave weaving** (`ai_weave_strafe`) for all three new roles' combat functions,
  replacing the square-wave strafe alternation every other role uses — the founder's own
  `offset = LeftVector * sin(Time.time * frequency) * amplitude` collapses to a plain sine on
  `in_strafe` in SHANKPIT's 2D top-down movement (no separate perpendicular-vector math needed,
  same simplification `ai_run_search`'s existing orbit math already makes). Phase-shifted by
  `player_id` so multiple instances don't weave in lockstep.
- One real spawn of each in `story_ai_seed_voxworld_encounter`, placed away from the two S461-03
  squads (a lone monster sharing a squad's engagement space would undercut the point of a
  non-coordinated archetype).

`make server` and `make lobby` both build clean.

## What's real, honestly not built yet

- **Scent/blood-trail breadcrumb tracker.** Needs a real new subsystem: a central array the
  player manager drops points into (gated on a real "is bleeding or sprinting" signal, which
  doesn't exist as a named concept anywhere in `protocol.h` today — `is_shooting`/`in_shoot` exist,
  a "recently took damage" or "sprint" flag does not), plus a new AI behavior that paths to the
  oldest nearby breadcrumb instead of a perception-based target. Nothing built toward this.
- **Utility-based Hunger/Fear/Fatigue drives.** A real architectural alternative to the existing
  `AIMode` FSM for solo roles — continuously-ticking drive variables selecting behavior each tick
  instead of discrete mode transitions. This is a bigger change than "add a mode": every existing
  per-role combat/patrol/flee function assumes the FSM shape. A real, honest fork-in-the-road, not
  attempted here — the existing FSM already gives these three roles real, distinct behavior
  without it. If pursued, the natural integration point is `humanness.c`'s `HumannessState` (it
  already has `energy`/`fatigue` fields ticking per-instance every frame) rather than a wholly
  separate drive system — worth checking first whether `fatigue` already means enough of what
  "Fatigue" here means before adding a new field.
- **True wander jitter for idle solo enemies.** All three new roles spawn with no authored patrol
  route today, so they fall into `ai_run_patrol`'s existing generic fallback (`patrol_count <= 0`
  → a fixed weak forward+strafe+turn, not real jitter). `ai_run_search`'s own cos/sin orbit-around-
  a-point is the real, already-proven pattern to reuse for genuine "sniffing/scouting" wander — not
  wired to the PATROL fallback here, since that fallback is shared by every role and changing it
  changes behavior for the whole roster, not just these three. A dedicated small patrol loop per
  spawn (same convention every other role already gets) would also work and needs no new code at
  all — either approach is real, straightforward follow-up, not attempted in this pass.
- **Territorial Beast's `leash_radius` is a single hardcoded default (`120.0`)**, not tuned per
  spawn or per scene size — fine for one demo spawn in `SCENE_VOXWORLD`, a real parameter to
  expose once more than one is authored.

## Related

- `EMILY/BACKLOG.md` S462 — this phase's own backlog entry.
- `docs2/specs/AI_WAYPOINT_NAV_NORTHSTAR.md` (S461-01) — `ai_move_towards`'s arrival steering,
  reused directly by `ai_run_leash_return`.
- `docs2/specs/AI_SCRIPTED_ANIMATION_NORTHSTAR.md` (S461-04) — `AI_MODE_SCRIPTED`'s lock is the
  one thing that still takes priority over the leash override.
- `packages/simulation/humanness.c`/`.h` — named above as the real, existing per-instance
  continuous-state precedent for any future drive system.
