#!/usr/bin/env bash
# scripts/build_brawlpit_level.sh -- rebuild assets/levels/brawlpit/ from source: the BRAWLPIT level module
# (PARENA -> wasm32) and its art (WOTAN), T0, docs/T0_LEVEL_WASM_CONTRACT.md.
#
#   scripts/build_brawlpit_level.sh           rebuild in place (make brawlpit-level)
#   scripts/build_brawlpit_level.sh --check   rebuild to a temp dir; fail if anything committed differs
#                                             (make check-brawlpit-level)
#
# Founder real-time 2026-11-08: "Boot the SHANKPIT world plus BRAWLPIT as WASM, BRAWLPIT in the foreground",
# then "implement as always mods first parena first mods driven development".
#
# The module is COMMITTED, like the PARENA-generated *_rules.c files: CI and the release zips have no LLVM
# wasm toolchain, so what ships is a dev box's build output, and this script is how a dev box makes it.
# Source of truth is the level's manifest in the sibling PARENA checkout (sources, exports, the imports it
# may use); PARENA's scripts/build_level_wasm.sh does the build and proves the surface. The build is
# reproducible (same sources -> same bytes), so --check is a real stale check, not a timestamp test.
#
# Art: sprites.png (512x256, 8 portrait tiles) and duck.png (640x340 atlas) are WOTAN's, the same files the
# web build serves. The shell decodes them at load (packages/world/png_decode) and writes the RGBA into the
# module's asset slots; without them BRAWLPIT draws its own shape fallbacks.
#
# Needs: ../PARENA (or $PARENA_DIR), the LLVM 18 toolchain (MONOREPO BOOTSTRAP.md), and for the art
# ../WOTAN (or $WOTAN_DIR). Missing pieces skip loudly in --check mode (exit 0), fail in build mode.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
PARENA_DIR="$(cd "${PARENA_DIR:-../PARENA}" 2>/dev/null && pwd || true)"
WOTAN_DIR="$(cd "${WOTAN_DIR:-../WOTAN}" 2>/dev/null && pwd || true)"
DEST="$ROOT/assets/levels/brawlpit"
MODE="build"
[ "${1:-}" = "--check" ] && MODE="check"

skip_or_fail() {
  if [ "$MODE" = "check" ]; then echo "SKIP check-brawlpit-level: $1"; exit 0; fi
  echo "build_brawlpit_level: $1" >&2; exit 1
}
[ -n "$PARENA_DIR" ] && [ -f "$PARENA_DIR/stdlib/brawlpit/level.manifest" ] || skip_or_fail "no PARENA checkout (set PARENA_DIR)"
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
[ -x "$TC/usr/lib/llvm-18/bin/wasm-ld" ] || skip_or_fail "no LLVM 18 wasm toolchain at $TC (MONOREPO BOOTSTRAP.md)"

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
"$PARENA_DIR/scripts/build_level_wasm.sh" "$PARENA_DIR/stdlib/brawlpit/level.manifest" "$W/brawlpit_level.wasm"
ART=()
if [ -n "$WOTAN_DIR" ] && [ -f "$WOTAN_DIR/BRAWLPIT/sprites.png" ] && [ -f "$WOTAN_DIR/BRAWLPIT/duck.png" ]; then
  cp "$WOTAN_DIR/BRAWLPIT/sprites.png" "$WOTAN_DIR/BRAWLPIT/duck.png" "$W/"
  ART=(sprites.png duck.png)
else
  echo "build_brawlpit_level: no WOTAN checkout; the art is left as committed (set WOTAN_DIR)" >&2
fi

if [ "$MODE" = "check" ]; then
  stale=0
  for f in brawlpit_level.wasm "${ART[@]}"; do
    if [ ! -f "$DEST/$f" ]; then
      echo "MISSING: assets/levels/brawlpit/$f -- run make brawlpit-level and commit"; stale=1
    elif ! cmp -s "$W/$f" "$DEST/$f"; then
      echo "STALE: assets/levels/brawlpit/$f differs from a fresh build -- run make brawlpit-level and commit"; stale=1
    fi
  done
  [ $stale -eq 0 ] && echo "check-brawlpit-level: assets/levels/brawlpit is current (module + ${#ART[@]} art files)"
  exit $stale
fi

mkdir -p "$DEST"
for f in brawlpit_level.wasm "${ART[@]}"; do
  cp "$W/$f" "$DEST/$f.tmp" && chmod 644 "$DEST/$f.tmp" && mv "$DEST/$f.tmp" "$DEST/$f"
done
rev="$(git -C "$PARENA_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
[ -n "$(git -C "$PARENA_DIR" status --porcelain -- stdlib scripts 2>/dev/null)" ] && rev="$rev+dirty"
echo "built assets/levels/brawlpit/brawlpit_level.wasm ($(wc -c < "$DEST/brawlpit_level.wasm") bytes) from PARENA $rev"
