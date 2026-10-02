/* Compiles the generated eduvm_mod.c with the host prototypes in scope (the generated file calls
 * eduvm_host_* with no declaration of its own). */
#include "eduvm_host.h"
#include "eduvm_mod.c"
