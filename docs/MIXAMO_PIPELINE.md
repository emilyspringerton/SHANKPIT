# Mixamo → GOLDENBAND → SHANKPIT: the asset pipeline, and its licensing boundary

Status: import → binding → a selectable skin (yours, and an NPC's) playing **all thirteen** clips is
built and tested. The IDUNA NOCK endpoint is not (see the end, where the gap is named rather than implied).

## The boundary first, because it decides the design

Mixamo assets are free for commercial use, may be modified, may be baked into a compiled game, and
need no attribution. Two terms shape this repo:

- **No standalone redistribution of the raw files.** Selling or handing out the editable `.fbx`
  (including via an asset store) is not allowed. A public repo that carries a folder of raw Mixamo
  exports is the shape the terms forbid.
- **No bulk download to train models.** Not something this pipeline does.

So nothing here ingests or stores a raw Mixamo export. The repo ships **`.gband` clips** — baked,
resampled, channel-hashed motion data produced by GOLDENBAND's importer — plus the skeleton/mesh
assets they were made against. `tools/local_nock.py` and its editor never write an `.fbx` into the
tree, and a local staging folder for raw downloads is a personal, gitignored concern, not a
repo path.

## The three steps

```
Mixamo .fbx/.glb  →  (GOLDENBAND / IDUNA importer)  →  <clip>.gband + <clip>.gband.json
                                                            │
                          tools/mixamo_clipset.py --auto ───┘
                                                            ▼
                         assets/goldenband/clipsets/<set>/anim_<state>.gband (+ .json)
                                                            │
                            gseq_clip_load ── anim_clipset.h ─┘  → the tick's pose
```

1. **Convert** (outside this repo's format work): GOLDENBAND's `gbtool import --gltf`, or IDUNA's
   server-side `internal/nock/gltf_convert.go`, turns a Blender/Mixamo export into a `.gband` binary
   plus its manifest. Channels are `<joint>.tx/ty/tz` or `<joint>.qx/qy/qz/qw`, sampled at a uniform
   tick rate.
2. **Bind** (`tools/mixamo_clipset.py`): map those clips onto the thirteen gameplay states and write
   them under the contract names. This is the step that fails loudly — wrong magic, wrong length,
   manifest rates that disagree with the binary, a **content hash that no longer matches the data**,
   an **unterminated or path-shaped zone of text in the id**, or **clips from two different
   skeletons** are all refused.
3. **Play** (`packages/simulation/anim_clipset.h`): load the thirteen, loop postures, hold one-shot
   endings, crossfade per `anim_rules.prn`, and fall back to a clip that exists when the bundle has
   holes.

## Doing it from the editor

`python3 tools/local_nock.py --port 8787`, open `http://127.0.0.1:8787`, and use **Character clips**:

- `GET /api/clipsets` lists the usable clips (binary **and** manifest — a binary alone is
  channel-blind and cannot be sampled by joint) and the sets already in the tree.
- The panel gives one dropdown per contract state; **auto-map by name** fills them from stems like
  `UAL1_Standard_Walk_Loop` or `mixamo_run`.
- **Import + commit** posts to `POST /api/clipsets`, which runs the same binder and then commits
  `assets/goldenband/clipsets/<name>/` with a message naming the set. Nothing is pushed: that stays a
  human step, exactly like a level save.
- Refusals come back as the binder's own sentences ("no clip for: anim_run, anim_ability",
  "content_hash mismatch…"), not a generic failure, because the artist is the one who has to act.

## From an imported set to a skin you can actually wear

The binder producing a valid bundle is half the chain; the other half is the engine offering it. Three
places close it, and none of them keeps a second list of skins:

- **Discovery** (`packages/simulation/skin_registry.h`): the lobby scans `assets/goldenband/clipsets/`
  at startup. Every directory with a `skin.json` naming its mesh, an `anim_idle` and an `anim_walk` is
  offered. A directory without a `skin.json` is refused — a clip is channel data and channels only
  mean something against one skeleton, so a set that does not name its rig cannot be drawn.
- **A row in the SKINS menu**: the built-in cosmetics come first, then one row per imported set.
  Choosing one sets the draw path to `SKIN_MANNEQUIN` (the skin that knows how to draw a general
  GOLDENBAND rig) and records which set, so the player's body is that rig. The choice is saved in
  `shankpit_skin.cfg` as two numbers — built-in id and imported index — which is why old one-number
  configs still load.
- **An NPC body** (`AI_KIT_IMPORTED_BASE` in `packages/simulation/story_ai.h`): an authored
  `LevelCharacter.kit` of `AI_KIT_IMPORTED_BASE + index` gives that NPC the imported skin. The host
  prints the number for every set it loads (`... = kit N -- authored LevelCharacter.kit M`), so a
  designer reads the index off the startup line instead of guessing. Up to ten imported sets are
  addressable; a value past what this build loaded falls through to the ordinary role/round-robin
  cascade, never to an invisible NPC.

The mount point is worth stating because it is where the first attempt broke: a set lives at
`<assets root>/clipsets/<name>/` while the `.gskel`/`.gmesh` it was made against sit **one level up**,
so the loader resolves the mesh directory by walking up out of the set and checks `skin.json`'s own
`"assets"` field against that path rather than appending it.

What an imported skin draws today: **all thirteen**. The kit loads every contract clip
(`gband_skel_npc_load_clipset` → `load_state_clips`), and `gband_skel_npc_draw_state` samples the one the
body is actually in — `anim_state_for_player` decides (run, crouch-walk, jump, in-air, fire, reload,
swing, ability, climb), so the four-clip idle/walk/greet/dance path is now only what the built-in
characters use. A one-shot holds its last tick because the loop flag comes from each clip's own manifest
(the binder fills it from `anim_rules.prn`), not from a copy of the table in the renderer.

`make test-anim-states` proves it on the committed Mike set, without a GPU: the two contract name tables
agree, 13/13 states load, run samples different vertices than idle, fire holds instead of restarting, an
unloaded state falls back to idle instead of drawing nothing, and a kit with no states resolves to -1 so
the caller keeps the old path.

## The IDUNA side — named, not built

IDUNA already converts glTF → GOLDENBAND server-side (`internal/nock/gltf_convert.go`, the
`/admin/nock/api/shankpit-widgets/import-gltf` route and the Animations tab). What it lacks is the
**binding** step and a persisted bundle path. The handler it needs is small:

- `POST /admin/nock/api/shankpit-clipsets` — body `{name, map}` (or `{name, auto:true}`), reuse the
  assets' own manifest/hash validation, write the bundle to the animation store, return the same
  `{bound, missing, warnings}` shape the local endpoint returns.
- No git in the cloud path: IDUNA's cluster cannot commit a checkout, so a cloud import updates the
  store and serves the bundle; committing stays local NOCK's job.

This was **not** written in this pass: there is no Go toolchain on the machine this was developed on,
so a handler plus tests would have been unverified code in a large Go service. The contract above is
what it needs to implement; the local endpoint is the reference behaviour.
