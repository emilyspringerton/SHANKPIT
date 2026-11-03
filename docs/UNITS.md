# The unit affordance layer

Lengths, masses and volumes, metric and imperial, converted through **one canonical unit per family**
rather than a matrix of pairwise factors. The arithmetic is PARENA (`PARENA/stdlib/shankpit/units.prn`)
and is generated into `packages/common/units_rules.c`; the contract is `packages/common/units_rules.h`.

## Canonical units and why these

| family | canonical | why |
|---|---|---|
| length | millimetre | every imperial length is exact in mm (25.4 / 304.8 / 914.4) |
| mass | milligram | 1 lb = 453592.37 mg and 1 oz = 28349.523125 mg exactly |
| volume | cubic metre | 1 cu in = 16.387064 mL, 1 cu ft = 28.316846592 L, 1 US gal = 3.785411784 L |

A conversion is always `to canonical -> from canonical`, so adding a unit adds one factor, not N factors,
and a metric -> imperial -> metric round trip cannot drift by anything but the double rounding of the
canonical value itself (`make test-units` asserts the round trip on every code).

## Codes

Codes are plain ints, stable, and cross a mod or wire boundary as numbers -- never as names:

```
family 0 length: 0 mm  1 cm  2 m   3 in  4 ft  5 yd
family 1 mass:   0 g   1 kg  2 oz  3 lb  4 short ton (2000 lb)
family 2 volume: 0 ml  1 L   2 m3  3 cu in  4 cu ft  5 US gallon
```

`units_code_valid(family, code)` is the check every other entry point applies before converting; a bad
code is `0.0`, not a guess. Renumbering a code is a breaking change; append instead.

## Compound affordances

- `units_box_volume_m3(w, h, d, unit)` -- author in inches, feet, cm or yards and get m3, with all three
  dimensions converted through the same canonical mm.
- `units_box_mass_kg(volume_m3, density_kg_m3)` -- volume times density, so a material profile only has to
  state density.
- `units_snap(x, profile)` -- structural grid: 1 CMU block 0.4 m, 2 tile 0.3 m, 3 stud/16 in 0.4064 m,
  4 brick/8 in 0.2032 m, 0 none. The imperial steps are the exact metric equivalents, which is what lets a
  16-inch wall meet a metric wall on the same grid.

## Authoring: `units_author_*`, F32 in and out

New level, brick and editor work should call the `units_author_*` entry points (`length_mm`, `length_m`,
`box_m3`, `mass_kg`, `snap`). They take and return **F32** because the geometry they feed
(`packages/world/*.h`) is F32: `units_author_length_m(12, 4)` is 12 feet in metres, as a float, with no
`0.3048` written at the call site. They are deliberately thin -- the factor arithmetic stays in the F64 core
below -- so an authoring path cannot grow its own rounded copy of 25.4, and the only error introduced is one
narrowing at the very end, on an already-correct number.

## Why the core is F64

1 cu ft is 28.316846592 L, which is more significant digits than a float holds (24-bit mantissa, ~7 decimal
digits). A ft3 -> L -> ft3 round trip performed in F32 drifts by roughly 1e-7 relative per pass; in F64 the
only rounding is the final narrowing at an authoring call. `make test-units` asserts both the exact F64
factors and the F32 entry points.

## Honest limits

- **Mass, not weight.** "Ton" is the short ton (2000 lb). There is no pound-force, no gravity-dependent
  weight, and no pressure/stress unit here.
- **Two engine paths consume it: the server's cast range and the dungeon gate.** `PARENA/stdlib/shankpit/wow_wire.prn` states
  the rule in **yards** (`wow_cast_range_yd_for`, per weapon (2 yd knife to 120 yd sniper), the unit a designer says out loud) and
  `apps/server/src/main.c` converts the chosen value once with `units_author_length_m(..., 5)` before comparing squared
  distances, so no metre value is restated in C. Beyond that range a `[@target]` cast fires along the
  player's own aim instead of swinging the crosshair. Level/brick/editor authoring is still unconsumed;
  `units_author_*` is its entry point. The second consumer is a geometry dimension a designer states
  in feet: `zone-door-height-ft` (7 ft) in `PARENA/stdlib/shankpit/zone_rules.prn`, converted once in
  `packages/simulation/proc_dungeon.h` with `units_author_length_m(..., 4)`, so 2.1336 m is never
  restated and the dungeon's gate box is exactly the authored height.
- **The syntax this layer was first written around now compiles.** `fn`, `F32`, `I64`, bare
  `(defenum E A B)` variants and `case` were all refused by the C emitter when this file was written; the
  same session fixed them in PARENA (`src/emit.c` + the shared `ast_sugar.h` pass), and the layer now uses
  `fn` and `case` directly. The checked-in `units_rules.c` is generated from that source, not hand-edited.
