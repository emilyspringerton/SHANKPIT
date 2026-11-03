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

## Honest limits

- **F64, not F32.** The PARENA emitter in this repo accepts `Unit/I32/Bool/F64/String/Bytes` and rejects
  `F32` with `parena: defn: unsupported return type symbol 'F32'`. The original sketch's F32 signatures
  and `(fn ...)`/`(defenum Name Variant)` forms are not this language; this layer is written in the syntax
  the compiler actually accepts (nested `if` over I32 codes, because the C emitter's `case` output does
  not compile for I32 either).
- **Mass, not weight.** "Ton" is the short ton (2000 lb). There is no pound-force, no gravity-dependent
  weight, and no pressure/stress unit here.
- **Nothing in the engine calls this yet.** It is generated, linked and tested standalone
  (`make test-units`, `make check-units`); wiring it into level authoring, the brick solver or the renderer
  is a separate change.
