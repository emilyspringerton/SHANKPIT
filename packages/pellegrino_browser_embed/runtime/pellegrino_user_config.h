/* PELLEGRINO's mbedTLS tweaks, applied on top of mbedtls' default mbedtls_config.h through
 * MBEDTLS_USER_CONFIG_FILE (set by third_party/mbedtls/mbedtls.BUILD and scripts/package_windows.sh).
 * Everything else -- TLS 1.2 + 1.3, the usual ECDHE/AEAD suites, X.509, full chain + hostname
 * verification -- is the library's default. */
#ifndef PELLEGRINO_MBEDTLS_USER_CONFIG_H
#define PELLEGRINO_MBEDTLS_USER_CONFIG_H

/* NO MALLOC: mbedTLS allocates out of one static buffer that the PARENA runtime hands to
 * mbedtls_memory_buffer_alloc_init() (PARENA_TLS_STATIC_HEAP). Once that runs, libc's calloc/free are
 * never reached by the TLS stack. */
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C
/* exposes the allocator's high-water mark, which PELLEGRINO logs (PARENA_TLS_HEAP_STATS) */
#define MBEDTLS_MEMORY_DEBUG

/* a browser is only ever a TLS client */
#undef MBEDTLS_SSL_SRV_C

#endif
