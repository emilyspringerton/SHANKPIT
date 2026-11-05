#ifndef SHANKPIT_WASM_MATH_H
#define SHANKPIT_WASM_MATH_H

/* The clean-room core rules call exactly these three. Their freestanding implementations live in
 * apps/game/wasm_core.c so the module needs no host imports. */
double sin(double x);
double cos(double x);
double atan2(double y, double x);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif
