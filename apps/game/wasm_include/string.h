#ifndef SHANKPIT_WASM_STRING_H
#define SHANKPIT_WASM_STRING_H

/* wasm32 has no libc headers. Only the two routines the linked host files actually use are declared;
 * their definitions live in apps/game/wasm_core.c. */
#include <stddef.h>

void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);

#endif
