#!/usr/bin/env bash
# Builds the real in-phone PELLEGRINO browser for Windows (mingw) into bin/pgb_win/*.o.
# Soft-fails: if anything is missing/breaks, exits 1 and removes the dir so callers fall back to
# packages/pellegrino_browser_embed/phone_browser_stub.c. Also stages cacert.pem (Windows has no
# system trust store; the engine looks for cacert.pem next to the exe) at bin/pgb_win/cacert.pem.
# Usage: scripts/build_pgb_windows.sh [SDL2_MINGW_PREFIX]   env: PELLEGRINO_DIR (default ../PELLEGRINO)
set -uo pipefail
cd "$(dirname "$0")/.."
SDLW="${1:-${SDL2_MINGW_PREFIX:-./sdl2_mingw}}"
PG="${PELLEGRINO_DIR:-../PELLEGRINO}"
[ -d "$PG" ] || { [ -d PELLEGRINO ] && PG=PELLEGRINO; }  # CI checks it out inside the repo
OUT=bin/pgb_win
CC="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
D=packages/pellegrino_browser_embed
fail() { echo "[pgb-win] $* -- falling back to stub" >&2; rm -rf "$OUT"; exit 1; }
[ -d "$PG" ] || fail "no PELLEGRINO checkout at $PG"
command -v "$CC" >/dev/null || fail "no $CC"
rm -rf "$OUT"; mkdir -p "$OUT/mbed"
MB="$(cd "$PG" && bash scripts/fetch_mbedtls.sh 2>/dev/null)" || fail "mbedTLS fetch failed"
case "$MB" in /*) ;; *) MB="$PG/$MB" ;; esac
CA="$(cd "$PG" && bash scripts/fetch_cacert.sh 2>/dev/null)" || fail "cacert fetch failed"
case "$CA" in /*) ;; *) CA="$PG/$CA" ;; esac
DEFS=(-DPARENA_WITH_TLS -DPARENA_TLS_STATIC_HEAP -DPARENA_TLS_HEAP_STATS -DPARENA_WITH_QUIC_CRYPTO "-DMBEDTLS_USER_CONFIG_FILE=\"pellegrino_user_config.h\"")
for f in "$MB"/library/*.c; do
  "$CC" -std=gnu99 -O2 -w "${DEFS[@]}" -I"$D/runtime" -I"$MB/include" -I"$MB/library" -c "$f" -o "$OUT/mbed/$(basename "$f" .c).o" || fail "mbedTLS $f"
done
"$CC" -std=gnu99 -O2 -w -c "$D/runtime/aead/monocypher.c" -o "$OUT/monocypher.o" || fail "monocypher"
"$CC" -std=gnu99 -O2 -w "${DEFS[@]}" -I"$D" -I"$D/runtime" -I"$MB/include" -I"$SDLW/include" -I"$SDLW/include/SDL2" \
  -c "$D/phone_browser_embed.c" -o "$OUT/phone_browser_embed.o" || fail "phone_browser_embed.c"
cp "$CA" "$OUT/cacert.pem" || fail "cacert copy"
echo "[pgb-win] ok: $(ls "$OUT"/mbed | wc -l) mbedTLS objs + embed in $OUT"
