# SHANKPIT Master Affordance Library

**Build 0186 (repo-verified)** · supersedes Build 0185 (draft). Canon comes from the operator's scripts;
the engine column in every table was checked against this repository on 2026-11-03, which the previous
builds could not do. Where the drafts and the code disagree, the code is quoted and the draft is marked.

## 0. How this document works

- It is the source of truth for **physical assets, lighting, audio, mechanics and data schemas** that a
  TYLER script needs from SHANKPIT. It is written so an agent that cannot read the repository can still
  build the right thing.
- The operator pastes a script. The maintainer returns **three deltas** — props, lighting, audio — and
  appends one change-log line. Nothing in the canon sections is paraphrased or invented.
- Tags: **[script]** a line or stage direction supports it. **[inferred]** extrapolated, not stated.
  **[stated]** the operator said it directly. **[ai-authored]** from an AI-authored continuation, canon
  unresolved. **[draft-0183]** from the earlier draft and **unverified**. **[verified]** checked in this
  repository on 2026-11-03, with the path quoted.
- **Prop IDs stay unassigned** until the operator approves a block (suggested: 100 and up). The proposals
  below are names, not ids.

## 1. What the engine actually has (verified)

This section is the part the earlier builds could not check. Read it before writing any code.

| Claimed in Builds 0183–0185 | Status | Reality in this repo |
|---|---|---|
| `packages/world/level_props.h`, `LevelPropInstance`, `PropTypeId` | **DOES NOT EXIST** | never present; no `PROP_OFFICE_TABLE` / `LevelPropInstance` anywhere in `packages/` or `apps/` |
| `packages/world/level_boxes.h` | **EXISTS** | the real level schema: `LevelBox` (x/y/z centre, w/h/d extents, r/g/b, `material_idx`), `LevelSpawner`, `LevelBrickCell`, `LevelSubtractBox` |
| `packages/common/obb.h` | **EXISTS** | OBB maths; `LevelBox` carries `rot_x`/`rot_y`/`rot_z` (Euler degrees, three.js XYZ order, matching NOCK) plus a `ramp` flag -- parsed, saved, collided (`phys_set_custom_level_orient` -> `orient_from_euler_deg`, with all-zero boxes kept on the AABB fast path) and rendered (the lobby hands the renderer all three arrays), pinned by `oriented_boxes_test` and `level_save_test`; a "table on its side" is authorable today |
| `packages/common/fluid_state.h` | **DOES NOT EXIST** | no fluid system of any kind; a sink is geometry only |
| `packages/render/retro_lighting.c`, `packages/render/env_matrix.c`, `packages/simulation/lighting_rules.c` | **EXIST** | lighting is **global** (env matrix built from PARENA rules), not per-prop lamps |
| `packages/simulation/lab_station_rules.c`, `widget_rules.c`, `brick_bodies.c`, `packages/reflux/reflux_runtime.c` | **EXIST** | all generated from `PARENA/stdlib/shankpit/*.prn` |
| `--edit-map`, `tools/local_nock.{c,h,prn,py}`, `tools/nock_editor.html` | **EXIST** | offline authoring is real: `packages/world/edit_map.h`, `--edit-map var/maps/<name>.json`, a local NOCK host and its browser editor |
| `make check-levels` | **EXISTS** | `mk/leveltool.mk`: round-trips `var/lab/lab.json`, `examples/story-doors/demo_level/level.json`, `var/zombie/nextown_zombies.json`, `assets/tyler_levels/*.json` through `bin/leveltool` |
| In-world interactive widgets | **EXIST AND RUN** | `edu_widgets[]` in the level JSON, loaded by `packages/simulation/widget_runtime.h`, driven by `widget_circuit.h`; see `docs/WIDGET_AFFORDANCES.md` |
| The `edu_widgets` schema in the drafts (`type`, `bound_register`, `scale`, `density`) | **WRONG** | the loader reads `entity_id`, `machine_type` (1..8), `memory_address`, `x/y/z/radius`, `input_pin`, `gate_pin`, `delay_ticks`, `box_index` |
| TYLER repository | **NOT PRESENT in this checkout** | canon is reproduced from the operator's text only; it could not be checked against the TYLER repo |

### 1.1 The one schema to author against

A level is walls (`LevelBox`) plus `edu_widgets`. A widget's real JSON:

```json
{ "entity_id": "lever_a", "machine_type": 6, "memory_address": 10,
  "x": 0, "y": 0, "z": 0, "radius": 3,
  "input_pin": -1, "gate_pin": -1, "delay_ticks": 0, "box_index": -1 }
```

| `machine_type` | kind |
|---|---|
| 1–4 | MACHINE (motor / lift / gate / terminal) — follows its input, may raise the box named by `box_index` |
| 5 | PLATE — pressure plate, written by the **host** from bodies standing on it; a script cannot forge one |
| 6 | LEVER — F toggles it; 0 or 15; no wiring needed |
| 7 | DELAY — relay, both edges, `delay_ticks` |
| 8 | TRANSISTOR — AND: passes input only while `gate_pin` is powered |

Pins must point at **lower** indices; a mis-wire degrades to "that switch does nothing". Power is one
integer **0..15**. A door is a real `LevelBox` toggled open/closed, either by a door script
(`DoorRuntime`, `script_url`) or as `MACHINE(2)` with `box_index`. Full recipes: `docs/WIDGET_AFFORDANCES.md`.

**Do not** create `level_props.h` or a `LevelPropInstance`. If props need HP/density, that is a material
extension on `LevelBox` (materials already carry `friction`, shader names and per-material destruction),
not a parallel struct that the loader will ignore.

## 2. Canon baseline (as supplied)

### 2.1 Four-layer architecture

| Layer | Name | Summary |
|---|---|---|
| 4 | Emily OS | The substrate. Runs the universe, enforces physics. Speaks in concise modal tiles (max 3 lines, 12 words/line). Shows as environmental room tightening or "UI spacing". [NO FURTHER COMMENT] |
| 3 | The Documentary (TYLER) | Jiangshi Syndicate binding ritual: tries to pin a migrant to a single point via handheld footage and confessional interviews. |
| 2 | Tides of Paradox | Faction cold wars, teen protagonists, the love triangle, the search for Sir Francis Bacon's Chrono-Cube. |
| 1 | The Custody Trial | Repository level where Eli and Mara touched infrastructure code. Rule: "Do not confuse momentum with permission. Clean builds first." |

### 2.2 Laws of Temporal Movement

1. **Absolute Boundary of Immutability:** Tyler cannot complete a self-defining sentence. Fragments and rewrites only ("What I am is—" is blocked).
2. **Two-Second Lens Gaze:** he never locks eyes with the lens for more than 2 seconds. A 2-second look is active resistance.
3. **Stolas Resonance Jamming:** he hums in 7/8 and 11/8 (Stolas Frequency 7.83 Hz), disrupting Jiangshi and Kuroshio resonance arrays.
4. **OBB Superposition Artifact:** the coin is an uncollapsed object showing changing dates; it anchors him to multiple timeline coordinates.
5. **Social Web Retention:** he keeps small open obligations. A guest who leaves owing nothing vanishes into temporal displacement; a minor debt is a reservation to return.

### 2.3 Recept Frequency Matrix (Goetia calibration)

| Hz | Name / body | Function |
|---|---|---|
| 0.33 | Bael / Sun | Strategic concealment, invisibility |
| 0.51 | Agares / Mercury | Transit dilation; inverted during Mercury Retrograde (fast paths stall) |
| 1.618 | Amon / Moon | Faction reconciliation, treaty negotiation |
| 2.22 | Beleth / Venus | Love triangle detonation, proximity loops |
| 4.44 | Zagan / Saturn | Transmutation, asset-density conversions |
| 7.83 | Stolas / Jupiter | Timeline sight, structural navigation; Tyler's signature |
| 11.11 | Vassago / Node | Eastwind Owl precognition, footnote cataloguing |

### 2.4 Compilation watchlist

- **The Subscriber:** identity unknown; dead drops arrive before instructions are issued.
- **Tuesdays are unmeasured:** no entries, no instruments read data, Tyler's hum vanishes from audio channels.
- **Order of Service:** entry/departure strictly Black, Green, Red, Yellow, White.
- **Names as seed deeds:** a name said is a gift; a name written is a deed. True names are withheld in data displays.

### 2.5 Provisional canon (Episode 4, [ai-authored], provenance unresolved)

The Academy, Handlers and the Handler ring; "present, not assigned"; the Ledger and the Owl's name for it,
the Standing Debt; the Stillness Council; the Eastwind Owls; "Green case"; the Owl's two prior cases; Emily OS
speaking three full lines; the honesty budget. **The operator has also said to ignore the Episode 4 script
for build purposes** — both instructions are recorded here rather than silently resolved (open question 4).

## 3. Affordance register

Status vocabulary: **PLACE NOW** = authorable today with `LevelBox` + `edu_widgets`. **WIDGET** = needs a
circuit wired, all machine types exist. **NEW SYSTEM** = nothing in the repo does this. **NOT AN ASSET** =
it is a lighting/UI/audio state, not geometry.

### 3.1 Pilot trailer — start here [script]

| Prop | Evidence | Proposed token | Nearest real engine path [verified] | Status |
|---|---|---|---|---|
| Table, on its side | 0:00 | PROP_OFFICE_TABLE | `LevelBox` with `rot_x`/`rot_z` | PLACE NOW -- tip it with a rotation; only *animated* rotation would be new |
| Kitchen sink | 0:40 | PROP_BREED_SINK | `LevelBox` + material | PLACE NOW as geometry; **no fluid** → tap/used state is NEW |
| Door | 0:55 | PROP_DOOR | door `LevelBox` + `DoorRuntime`/`script_url`, or `MACHINE(2)` + `box_index` | **PLACE NOW / WIDGET** |
| Coin | 1:10 | — | no held-item schema; only weapons are held | **NEW SYSTEM** (item + focus readout); date flicker is UI |
| Security camera | 0:25 | — | no camera prop; 1901 stamp is a capture look | **NEW** prop; stamp is UI/capture, not geometry |
| Interview lamp | 0:10 | — | lighting is global (`env_matrix`), no per-lamp light | **NEW** per-light source, or fake with emissive material |
| Lav mic | 0:00 | — | `packages/audio/audio_chain.c` exists; no prop-triggered audio | **NEW SYSTEM** (proximity audio on a prop) |
| Half-shifted furniture | 0:00 | — | `LevelBox` set | **PLACE NOW** |
| Dim hallway | 0:00 | — | `LevelBox` corridor + global lighting | **PLACE NOW** (lighting as global state) |

### 3.2 Episode 4 "Undocumented" [script] [ai-authored]

| Prop | Evidence | Proposed token | Engine path [verified] | Status |
|---|---|---|---|---|
| Vinyl booth | Act I | PROP_DINER_BOOTH | `LevelBox` | PLACE NOW (two-sided seating is dressing, not a mechanic) |
| Booth table | Act I | PROP_DINER_TABLE | `LevelBox`; item-on-surface is not a system | PLACE NOW; **holding an item is NEW** |
| Counter | Act II, V | PROP_DINER_COUNTER | `LevelBox` | PLACE NOW |
| Counter stool | Act II | PROP_DINER_STOOL | `LevelBox` with `rot_y` | PLACE NOW -- a fixed swivel angle is authorable; a stool that actually *turns* needs a runtime re-orient (NEW) |
| Coffee cup | Acts I–II | PROP_COFFEE_CUP | none | NEW SYSTEM (held/fillable item) |
| Coffee pot + burner | Act I | PROP_COFFEE_POT | `LevelBox` | PLACE NOW as dressing |
| Sugar dispenser | Act I | PROP_SUGAR_DISPENSER | none | NEW SYSTEM (pour interaction) |
| Diner door + bell | Acts I, IV | PROP_DOOR (bell variant) | door as above; audio one-shots exist, no prop trigger | WIDGET + NEW (bell trigger) |
| Diner window | Act VI | PROP_DINER_WINDOW | `LevelBox` (transparent material is a shader choice) | PLACE NOW |
| Radio (off-screen) | Act V | PROP_RADIO | audio chain can play, no positional prop source | NOT AN ASSET (audio bed) / NEW if positional |
| Handler ring | Act I | PROP_HANDLER_RING | none | NEW SYSTEM (held item, state worn/unworn) |
| UNDOCUMENTED card | Act V | PROP_UNDOC_CARD | no prop heat state; `edu_widgets` can hold an on/off register, not temperature or text | **NEW SYSTEM** (heat + text-on-prop) |
| Academy facade | Act VI | PROP_ACADEMY_FACADE | `LevelBox` exterior set | PLACE NOW |

### 3.3 Inferred and unassigned

The Build 0185 inferred list stands (interview chair, hand towel, glass of water, pocket draw, phone
exclusion, off-screen cook, fidget object). Of these, only the **chair** is placeable today (a `LevelBox`,
optionally `MACHINE` if it must move); the towel, glass and coin-style fidget objects all need the held-item
system. The draft's unassigned props (fridge, thermos vault, transit bench, vending machine) remain
unassigned: **none is used by any script**, and all four are `LevelBox` geometry today, not mechanics.

## 4. Mechanics: what is real, what is prose

The PARENA modules pasted in Builds 0183/0185 (`prop_interaction.prn`, `office_furniture.prn`) are **not in
this repository and do not compile as pasted** — `evaluate_widget-interaction` mixes a hyphen and an
underscore, `set-env-occlusion` does not exist, and the shapes are not the dialect this repo's `defn` forms
use. Treat them as design notes, not code.

The real pattern in this repo, which any prop logic must follow: a rule lives in
`PARENA/stdlib/shankpit/<name>.prn`, is generated into a checked-in `packages/simulation/<name>_rules.c`,
and is byte-checked by a `check-*-rules` target. Widget behaviour itself is **EduVM**, not a new module:
the host writes widget registers (`edu_widget_host_set`), a script reads them (`read_sensor`), and a script
**cannot** forge a sensor.

Consequences for the register above: HP/density, temperature and "is this prop powered" have **no schema**.
The nearest real mechanisms are material-based destruction (`brick_bodies.c`, `PACKET_BRICK_STATE`), the
0..15 widget power line, and the EduVM register. Anything else is a proposal.

## 5. Lighting, audio and UI (as supplied, engine status added)

| ID | Source | Status in engine [verified] |
|---|---|---|
| LGT-001 | Kitchen neon bleed | not implementable as a prop today; global lighting only |
| LGT-002 | Dim hallway ambient | global lighting / env matrix: real |
| LGT-003 | Interview warm lamp | needs per-light source: NEW |
| LGT-004 | Security-cam capture look | post-process/capture, no prop |
| LGT-005 | Handheld/lav capture look | capture look, not a world object |
| LGT-006 | Room temperature on entry | global ambient parameter: real as state, not per-prop |
| LGT-007 | Black cut | render state (zero light), not geometry |
| LGT-008 | On-screen log tags | Emily OS UI layer; not in SHANKPIT today |
| LGT-009 | Night baseline | global lighting: real |
| LGT-010 | Coin flip highlight | needs the held-item system first |
| LGT-011 | Door-open spill | could merge with LGT-006; door exists, spill needs a light |
| LGT-012 | Afternoon sun on brick | global lighting: real |
| LGT-013 | Card reveal under warmth | needs a heat + text-on-prop system: NEW |

Audio: AUD-001 bell and AUD-004 lav crackle have no prop-trigger path; AUD-002 hum/radio and AUD-003 cook
call are ambient beds the audio chain can play. UI: only the pilot's tags are supplied; none is a prop.

## 6. Open questions (superseding Build 0185 §8–9)

1. **Schema:** the repo's `edu_widgets` is implemented -- it loads, saves, drives a circuit, mirrors into
   EduVM, and is exercised by `test-widget-runtime` and `check-levels`; the drafts' `LevelPropInstance` and
   `level_props.h` exist in no file. **Recommendation:** keep `edu_widgets` and stop the `level_props.h`
   track -- its `type` / `bound_register` / `density` keys would be a parallel struct the loader ignores.
2. **HP/density on props:** nothing reads them today -- prop damage would ride `brick_bodies` /
   `PACKET_BRICK_STATE`, and density is already per-`LevelBox` material. **Recommendation:** add material
   fields only when a consumer exists; extending them is a save/load and network change, not a detail.
3. **Networked props:** the widget circuit is single-player authority and there is no widget packet; a prop a
   remote player can use is new wire work.
4. **Episode 4 provenance:** labelled [ai-authored] at Build 0134, and the operator separately said to ignore
   that script. Canon, provisional, or dropped?
5. **Rotation:** corrected 2026-11-03 -- `LevelBox` does carry `rot_x`/`rot_y`/`rot_z` (Euler degrees, matching NOCK), parsed, saved, collided and rendered, so "on its side" and a fixed swivel angle are authorable today. The real gap is *animated* rotation: a box is oriented once at level load and never re-oriented after.
   **Recommendation:** no schema or wire work; add a runtime re-orient hook only if a prop must move while the level runs.
6. **Meshes:** a widget is a box; there are no prop models. Is box-representation acceptable for the trailer,
   or does the library wait on art?
7. The Build 0185 canon flags (Tuesday vs unmeasured, Law 2 cap, Order of Service, "AH SHOCK", 1901 lighting,
   sink/table faction bindings) remain open and unchanged — none can be answered from this repo.
8. **TYLER repo** is not in this checkout, so nothing in Section 2 was verified against it.

## 7. Change log

| Build | Change |
|---|---|
| 0183 | Integrated draft: schema, four props with faction bindings, PARENA runtime, canon baseline |
| 0184 (draft) | Trailer extraction; fridge/vault/bench unassigned; chair/towel/glass inferred; lighting register LGT-001..011 |
| 0185 (draft) | Episode 4 extraction (13 props), bell variant, LGT-012/013, audio register AUD-001..004, provisional canon, flags 8–14 |
| **0186** | **Repo verification:** `level_props.h`, `LevelPropInstance`, `fluid_state.h` do not exist; the real widget schema and machine types are quoted; `--edit-map`, local NOCK, `make check-levels`, `widget_runtime`/`widget_circuit` verified; every prop row now carries a real engine path and a status; the pasted PARENA modules marked as non-compiling design notes; questions narrowed to decisions the repo cannot make |

## Audited 2026-11-03 (normal maps, faked mountains, climbable heightfields)

A pasted design pass proposed faking mountains with normal maps on low-poly shells, then upgrading them to
climbable Skyrim-style terrain by sampling a heightfield inside an OBB. Checked against the repo:

- **Real and already shipped:** rotated boxes (`LevelBox.rot_x/rot_y/rot_z`, Euler degrees, matching NOCK;
  parsed, saved, collided, rendered, tested -- see the table above); ramp wedges; procedural textures
  (`packages/render/proc_tex.c`); per-vertex normals (`a_normal` in `packages/render/gl_shader.c`);
  generated global lighting (`packages/simulation/lighting_rules.c`, PARENA output, hands off);
  weather/fog (`packages/render/sky_weather.c`); terrain height sampling (`terrain_sample_height`, used
  throughout `packages/common/physics.h`); authoring via `--edit-map`, the local NOCK host, and
  `make bake-levels`.
- **Not in the repo, and would be new systems:** tangent-space normal-map import and sampling (the texture
  path is fixed-function `GL_TEXTURE_2D` enable/disable in `retro_material.c`, and `a_normal` is per-vertex,
  never read from a texture); any `GEOM_BACKGROUND_ONLY` collision-skip flag; a `TEXTURE_SAMPLED_OBB`
  collider; a `nock_textures` service. Collision height comes from the global terrain, not from a texture
  sampled per box.
- **Wrong premise:** a normal map cannot "feed directly into `retro_lighting.c`". That file is generated from
  PARENA and produces a global environment matrix; there is no per-pixel lighting stage to feed.

A map builder arriving from Unreal/Godot would therefore find the geometry affordances (rotated boxes,
ramps, spawners with yaw, prefab widgets) already present, and the material affordances (normal maps,
material instances, per-pixel shading) absent. The cheap next step is not a normal-map pipeline but
procedural rock texture variety on the existing rotated-box geometry.
