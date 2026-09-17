# Mesh Editing in NOCK — NORTHSTAR

Registered as `SHANKPIT-MESH-NORTH`. Scoping doc for `EMILY/BACKLOG.md` SECTION 488.

## Founder real-time direction

"can we iterate the widget builder? i think this is a decent place to build our little mini
version of blender... 4 modes object mode, edit mode (select face, select vertex, select edge)...
we really need the move tool added to the level editor too... move scale extrude... V S E."

Asked directly whether to build this against the current box-per-wall model (practical, ships
now, "extrude" = duplicate a face into a new connected box) or pursue true arbitrary-mesh editing
(vertex/edge dragging into non-rectangular shapes) — chose the latter as the real destination, then
immediately redirected: **"build box extrude first."** This doc reflects both: Phase 1 is real,
shippable, and forward-compatible; Phases 2+ are the honestly-large native rewrite, scoped but not
started.

## The real constraint

SHANKPIT's native collision (`packages/common/physics.h`) represents every wall as an axis-aligned
box: center (x,y,z) + full extents (sx,sy,sz). Every consumer of that data — collision resolution,
the wire/export format (`Wall` in `IDUNA/internal/shankpit/level_store.go`, byte-for-byte matching
`packages/world/level_boxes.h`'s own `LevelBox`), rendering — assumes this shape. True vertex/edge
editing (dragging one corner independently) produces a shape this representation cannot hold at
all, let alone collide against correctly.

## Phase 1 (this pass) — box-based mesh tools, real and shippable today

Ships against the CURRENT box model, no native engine changes, no wire-format changes. Forward-
compatible: the gizmo, selection model, and keybinding scheme carry over unchanged once Phase 2+
lands a real mesh representation underneath.

- **Object mode** (default) — select/move/scale a whole wall, exactly as today, now via a real
  `TransformControls` gizmo (three.js's own standard translate/scale gizmo, colored X/Y/Z arrows)
  instead of billboard-plane dragging.
- **Edit mode, Face select only** — vertex/edge select modes are real, named, NOT built this pass
  (the founder's own "if you have to cut a feature im not sure if id miss that" applied to edges;
  extended here to vertices for the identical reason — both hit the same box-shape wall).
  - Click a face to select it; Shift-click adds/removes from the selection.
  - Extrude (`Alt+E`) duplicates the selected face into a NEW box, flush against the original,
    thickness 0 until scaled/moved — the real "select face, extrude, scale, extrude again"
    hallway-building workflow the founder described, implemented as connected-box-chaining rather
    than true topology extrusion. Functionally equivalent for this use case, not a compromise on
    the actual workflow.
  - Move (`Alt+V`) / Scale (`Alt+S`) apply the gizmo to the current face selection.
- **Mode switching**: `Tab` toggles Object ↔ Edit. `Alt+1`/`Alt+2`/`Alt+3` are reserved for
  Vertex/Edge/Face select-mode (Blender's own real convention) — only `Alt+3` (Face) does anything
  in Phase 1; `Alt+1`/`Alt+2` are inert until Phase 2.
- **Alt-modified hotkeys throughout** (founder: "hotkeys can be alt driven if need a modifier") —
  deliberate: `E` alone already means "fly camera up" (S487), so every mesh-tool hotkey uses
  `Alt+` to never collide with camera flight or anything future.

## Phase 2+ (real, scoped, NOT started) — arbitrary mesh editing

1. **New native mesh-collision representation** (`packages/common/physics.h`) — the real, hard
   problem. Candidate: restrict to convex polyhedra (tractable collision via GJK/SAT, still covers
   the real majority of level-building needs) rather than fully general meshes (concave collision
   is a much harder, slower problem this game's own tick budget likely can't afford). A real,
   separate design decision, not assumed here.
2. **New wire/export format** — `Wall{x,y,z,sx,sy,sz}` can't express arbitrary vertex positions;
   needs a real vertex list + face index format, versioned so existing levels' box-shaped walls
   still parse (a box is a trivial special case of a convex mesh).
3. **Migration** — every existing level (`TRAINING_GROUND`, `FORTRESS`, etc.) is 100% box-shaped
   today; the new format must round-trip them losslessly, not require hand re-authoring.
4. **NOCK editor**: real Vertex/Edge select modes, vertex-drag, edge-loop-adjacent operations
   (bevel, inset — real Blender primitives, not assumed needed, a later design pass).
5. **PARENA/GOLDENBAND implications** — a widget or level object composed from arbitrary meshes
   still needs to compose correctly through `flattenObjects` (materials, doors, etc.) — real,
   checked-not-assumed follow-up once the base representation exists.

## Ramps (named, real, NOT scoped in detail yet)

Founder real-time: "i need ramps and shit too." A ramp is a sloped/rotated box — outside the
CURRENT wall model (`Wall` has no rotation, only center+extents) but a much smaller lift than full
arbitrary-mesh editing: adding a rotation (or a pair of triangular end-caps + an angled top face)
is a real, scoped-able Phase 1.5, not a Phase 2 dependency. Not designed in detail here — flagging
it as real and wanted, likely worth scoping properly once box extrude itself ships and the
founder's actually building floorplans with it.

No timeline committed on Phase 2+ — that's the honestly-scoped destination, not a sprint plan.
Phase 1 (box extrude) is the real, immediate priority: "i need box extrude to make floorplans
quickly ship that first... i need box extrude to quickly sketch out rooms or interiors of
buildings."
