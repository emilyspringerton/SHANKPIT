/* packages/wasm_host/wasm_surface.h -- the C prototypes of the PARENA wasm surface reader.
 *
 * The definitions are generated: PARENA/stdlib/wasm/surface.prn -> wasm_surface_gen.c
 * (scripts/gen_wasm_surface.sh, `make regen-wasm-surface`; never edit the generated file). PARENA emits
 * bodies, not a header, so the caller declares what it uses -- the same convention
 * the packages/simulation rules headers follow. Semantics, signature packing and the -1/-2 conventions are
 * documented at the top of surface.prn and in docs/T0_LEVEL_WASM_CONTRACT.md.
 */
#ifndef SHANKPIT_WASM_SURFACE_H
#define SHANKPIT_WASM_SURFACE_H

#include "../world/parena_runtime.h" /* Bytes; pinned: the lobby -I order would find packages/simulation's copy first */

int ws_valid_(Bytes d);
int ws_import_count(Bytes d);
int ws_import_kind(Bytes d, int i);
int ws_import_module_off(Bytes d, int i);
int ws_import_module_len(Bytes d, int i);
int ws_import_field_off(Bytes d, int i);
int ws_import_field_len(Bytes d, int i);
int ws_import_sig(Bytes d, int i);
int ws_export_count(Bytes d);
int ws_export_kind(Bytes d, int i);
int ws_export_name_off(Bytes d, int i);
int ws_export_name_len(Bytes d, int i);
int ws_export_sig(Bytes d, int i);
int ws_sig_char(int sig, int k);

#endif /* SHANKPIT_WASM_SURFACE_H */
