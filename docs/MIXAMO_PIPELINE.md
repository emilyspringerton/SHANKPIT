# Mixamo → GOLDENBAND → SHANKPIT: the asset pipeline, and its licensing boundary

Status: the binding and loading halves are built and tested; the IDUNA NOCK endpoint is not (see the
end, where the gap is named rather than implied).

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
