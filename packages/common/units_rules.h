#ifndef SHANKPIT_UNITS_RULES_H
#define SHANKPIT_UNITS_RULES_H

/* units_rules.h -- the unit affordance layer's C contract.
 *
 * The numbers and the arithmetic live in PARENA (stdlib/shankpit/units.prn) and are generated into
 * units_rules.c by scripts/gen_units.sh (`make regen-units`, `make check-units` fails on drift). This
 * header only declares what that file defines.
 *
 * Canonical units: length MILLIMETRE, mass MILLIGRAM, volume CUBIC METRE. Imperial factors are the
 * exact by-definition ones (25.4 mm/in, 453592.37 mg/lb, 16.387064 mL/cu in, 3.785411784 L/US gal),
 * so a metric -> imperial -> metric round trip does not accumulate invented error.
 *
 * Codes are stable ints and cross a mod/wire boundary as plain numbers:
 *   family 0 length: 0 mm, 1 cm, 2 m, 3 in, 4 ft, 5 yd
 *   family 1 mass:   0 g,  1 kg, 2 oz, 3 lb, 4 short ton (2000 lb)
 *   family 2 volume: 0 ml, 1 L, 2 m3, 3 cu in, 4 cu ft, 5 US gallon
 * Append only; never renumber.
 *
 * HONEST: the emitter this repo has cannot take F32 or I64, so these are doubles (F64) rather than the
 * F32 the original sketch used. Nothing in the engine calls this layer yet -- it is generated, tested
 * and linked only by `make test-units`. */

int    units_code_valid(int family, int code);
double units_length_mm(double v, int unit);
double units_length_mm_to(double mm, int unit);
double units_length_m(double v, int unit);
double units_mass_mg(double v, int unit);
double units_mass_mg_to(double mg, int unit);
double units_mass_kg(double v, int unit);
double units_volume_m3(double v, int unit);
double units_volume_m3_to(double m3, int unit);
double units_convert(double v, int family, int from, int to);
double units_box_volume_m3(double w, double h, double d, int unit);
double units_box_mass_kg(double volume_m3, double density_kg_m3);
double units_snap(double x, int profile);

/* Authoring entry points: F32 in, F32 out, exact F64 arithmetic underneath. New level/editor/brick
 * work should use these rather than re-deriving a factor; the double->float narrowing happens once, at
 * the end, on an already-correct number. */
float  units_author_length_mm(float v, int unit);
float  units_author_length_m(float v, int unit);
float  units_author_box_m3(float w, float h, float d, int unit);
float  units_author_mass_kg(float v, int unit);
float  units_author_snap(float x, int profile);

#endif /* SHANKPIT_UNITS_RULES_H */
