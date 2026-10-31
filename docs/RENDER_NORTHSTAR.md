# RENDER — materials, light styles, and the Source 2 gap

**Status:** the *vocabulary* is implemented as rules (`PARENA/stdlib/shankpit/material_rules.prn`);
the renderer still draws it with the existing pipeline. This document answers the two questions
that came with the spec dump — what materials/shaders a Doom/Quake/Half-Life/GTA story needs, and
what is missing next to Source 2 — and it corrects the pasted spec where that spec does not fit this
engine.

## 1. What SHANKPIT actually renders with

Read from the source, not assumed:

- **Fixed-function GL 1.x** for world geometry (`glBegin`/`glTranslate`/`glRectf`), plus **one
  hand-loaded GL 2.0 shader path** (`packages/render/gl_shader.h`) used by the flashlight beam and
  the fireball. Programs are loaded through `SDL_GL_GetProcAddress`, because the build links GL 1.1.
- **Lighting** is `lighting_rules.prn` → `env_matrix` (occlusion, sky bleed, ambient floor,
  exposure, emissive scale) applied to fixed-function per-face shading, plus **HPS/IPS point
  fixtures** with per-fixture range and lit cones.
- **Textures** are proc-generated (`proc_tex.c`) and material shaders referenced by *name* from the
  level JSON (`material_shaders.h`); levels carry material, specular, shininess, friction, density.
- No VAO, no `#version 330`, no `sampler3D`, no deferred G-buffer, no normal maps, no shadow maps.

That is the constraint every proposal below has to survive.

## 2. What a Doom/Quake/Half-Life/GTA story needs, in this engine's language

Implemented in `material_rules.prn` (pure functions of the tick, so the server and every client
animate identically):

| Need | How it is expressed | What the renderer must add |
|---|---|---|
| Doom palette cycling / hazard colour | `palette-256(index)` with wrap, plus material kind 3 (LUT) | A 256-entry LUT upload and an indexed fragment path — the one place a real shader is required |
| Quake texture swim | material kind 1 scroll offsets (`mat-scroll-u/v-milli`) | UV offset per face; the *warp* itself is the existing affine mapping, no shader needed to make it worse |
| Half-Life terminals, cameras, bomb timers | material kind 2 frame-loop (`mat-frame-index(fps, frames, tick)`) | A sprite-sheet UV window; four floats per frame |
| Unstable fluorescents, strobes, dying bulbs | 8 light styles (`light-style-level-permille`) | Multiply the fixture's own HPS/IPS intensity — one integer, already per-frame |
| Emissive faces that react to room darkness | `mat-emissive-milli(style, tick, room_light)` | Reuse `env_matrix`'s existing emissive scale |
| Toxic ooze / canals / water | kind 1 with a vertical component | Alpha-blended quad, additive pass at the shoreline |
| Low-health / submerged / acid / shield tint | `tint-*-milli(...)` | One full-screen colour quad — this engine already draws those for weather grading |
| Dark corners with no light source | `vlm-ambient-floor-milli()` (120‰) | Apply as a floor under per-face shading |

## 3. What is missing next to Source 2 (and what is realistic here)

The pasted spec's list is correct about *what* Source 2 has; it is wrong about *what this engine can
compile or run*. Ranked by value per unit of risk:

1. **Baked probe grid (the honest half of `M_combined_light_probe_volume`).** A 32×16×32 RGB grid per
   level, baked at level load or by the level tool, sampled per vertex or per face — *not* a
   `sampler3D` in a GLSL 330 shader (unavailable). Cost: a few integers per lookup; exactly what
   `vlm-axis-index-milli` now defines. This is the single biggest visual win: corners, basement
   interiors and streetlight pools without a real light at either end.
2. **PBR, cheaply.** Albedo + roughness + metal as *numbers on the material* (material JSON already
   has specular/shininess), and a per-face specular term in fixed-function — no normal map, no
   multi-texture pass. Calling that PBR would be a lie; calling it "PBR-shaped" is honest.
3. **Volumetric fog as distance-graded slabs.** `sky_weather.c` already fades geometry; adding three
   height slabs with different density gives light shafts through windows for almost no cost. No
   froxel grid.
4. **Palette LUT (Doom).** A real fragment shader, small, and the only item here that genuinely needs
   one. Worth it because hazard colour cycling is the cheapest atmosphere in the business.
5. **Frame-loop and scroll materials.** No new shader if the existing texture path takes a UV offset
   per draw; these cheap wins are the ones terminal/portal/signage storytelling is made of.

**Not realistic, and named as such:** GPU mesh shaders (requires GL 4.6 hardware and a different
build entirely), hardware ray-traced lightmaps (VRAD3 assumes a DXR/Vulkan toolchain), parallax
occlusion mapping (needs a height map + tangent space + a shader per surface, and the fixed-function
path has no tangent basis), a deferred G-buffer (the pipeline is immediate mode; converting it is a
rewrite, not an upgrade).

## 4. Corrections to the pasted engineering spec

- `#version 330 core`, `glGenVertexArrays`, `glVertexAttribIPointer`, `sampler3D` / `glTexImage3D`:
  none of these exist in this build's GL surface. The shader path loads **GL 2.0** entry points
  through SDL on top of GL 1.1 headers; a 3.3 program would fail to compile on the target.
- The vertex struct's `uint32_t material_id` **interleaved per vertex** is the wrong shape here:
  materials are per-face in `map_geo`, and the renderer batches by material. A per-vertex id buys
  nothing and costs 4 bytes per vertex.
- "Bit-identical visual timing across the cluster" is true only of the **CPU-side parameters** — the
  tick, the style level, the UV offset, the tint alpha. GPU rasterisation is not bit-identical across
  vendors, and claiming otherwise would be wrong. That is exactly why the parameters live in PARENA
  and the pixels do not.
- The sample fragment shader is not PBR: `pow(1-roughness,4)*metallic*NdotV` has no light integral,
  no Fresnel, no distribution term, and it ignores the sampled ambient colour's direction. It is a
  plausible-looking placeholder, not a shading model.
- A 32×16×32 `float`-but-actually-`uint8` `VoxelLightVolume` stored **inside the level JSON** is
  49 KB of base64 per volume before any encoding; the level format would need a sidecar buffer, not
  the JSON the editor round-trips today.

## 5. Build order

1. Probe grid baked at load + sampled per face (rules already exist).
2. Per-face UV offset for scroll/frame-loop materials (rules already exist) — terminals, ooze,
   signage.
3. Light styles wired to the existing HPS/IPS intensity (one integer per fixture).
4. Screen tints (already have a full-screen quad path from weather grading).
5. Palette LUT shader — the first genuinely new fragment program.
6. Height-slab fog, then a real specular term per face.

Everything in this document that is *already* implemented is the rules layer, and it is verified
headless by `make test-material-rules`. Nothing here claims a visual result that has not been seen:
no GPU, no display in this environment.
