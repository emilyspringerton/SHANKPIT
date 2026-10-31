/* anim_engine_constant.c -- the engine constants the animation rules are checked against.
 *
 * Only this one translation unit includes physics.h, and it is built WITHOUT -Werror: physics.h is a
 * whole-engine header with its own unused file-scope statics and unused parameters, and silencing
 * those would mean either weakening this repo's -Werror on code that is not ours or editing a header
 * every other subsystem depends on. A two-line probe is the smaller, honest choice.
 *
 * The point of exporting it is that the test asserts anim_eye_height_mm(0) == EYE_HEIGHT * 1000, so a
 * change to the camera height cannot silently leave the pose at the old height (the spec's own 4.0 /
 * 2.5 numbers are from a different build; this engine's is 2.59).
 */
#include "physics.h"

int anim_engine_eye_height_mm(void) { return (int)(EYE_HEIGHT * 1000.0f); }
