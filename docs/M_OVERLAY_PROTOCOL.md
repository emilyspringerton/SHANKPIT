# M Overlay Protocol v0.1

This document describes the first in-game implementation of the **M Overlay** in SHANKPIT (SDL2 + legacy OpenGL fixed pipeline).

## Overlay modes

`OverlayMode` is a single enum used by all forks:

- `OVERLAY_OFF`
- `OVERLAY_SCAN`
- `OVERLAY_COMBAT`
- `OVERLAY_AI`
- `OVERLAY_EDU`
- `OVERLAY_VM`
- `OVERLAY_DEBUG`

### Input

- Press **M** to cycle:
  - `OFF -> SCAN -> COMBAT -> AI -> EDU -> VM -> DEBUG -> OFF`
- Current mode is shown in a compact HUD label (`M OVERLAY: <MODE>`).

## Entity provider contract

Each overlay-capable object publishes presentation facts through lightweight structs:

- `OverlayInfo`
  - Name/type labels
  - Which sub-elements are allowed (state/prompt/health/weakpoint/debug)
  - Priority and max distance
  - Optional lore/prompt/state strings
- `AIOverlayInfo`
  - AI-readable state (`IDLE`, `PATROL`, `HUNTING`, `COMBAT`, `FLANKING`, `SCRIPTED`, `PUPPET`, etc.)
  - Alertness/aggression/confidence
  - Target metadata and squad order

Renderer code must only consume these facts and must not own gameplay truth.

## Frame lifecycle

The overlay system runs per frame:

1. `overlay_begin_frame()`
2. Entity/story systems call `overlay_submit_entity()`
3. `overlay_filter_items()` applies mode + visibility + distance checks
4. `overlay_render()` draws:
   - world-space glyphs/rings/brackets
   - screen-space labels

## Distance/readability rules (v0.1)

- **Close (~0-12m):** name/state labels may render
- **Medium (~12-25m):** glyph-only markers preferred
- **Far (~25-60m):** only high-importance objective markers
- **Very far (>60m):** hidden unless objective-critical

The implementation intentionally keeps stable screen label sizing and avoids long-distance label spam.

## Current providers wired in v0.1

Initial data providers include:

- Rift Hound
- Shambler Trooper
- Gore Brute (breach anchor priority)
- Story boss / boss corpse state
- Portal-spew breach VM label (`PORTAL SPEW DETECTED`)
- Telecrystal / travel portal ring
- EDU annotation stub node in story voxworld

## Adding a new entity

To add a new overlay-capable entity:

1. Build `OverlayInfo` + optional `AIOverlayInfo` near that entity logic.
2. Call `overlay_submit_entity(...)` each frame while active.
3. Set `importance` and `max_visible_distance` for readability.
4. Reuse existing mode logic (`SCAN/COMBAT/AI/EDU/VM/DEBUG`) instead of hardcoding renderer branches.

This keeps the system data-driven and fork-safe for story, education, combat, puppet, and future co-op use.
