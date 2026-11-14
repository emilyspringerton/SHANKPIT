#!/usr/bin/env bash
# scripts/build_shankpit460_level.sh -- rebuild assets/levels/shankpit460/shankpit460_level.wasm from source: shankpit-460
# (PARENA -> wasm32) as a T0 level (docs/T0_LEVEL_WASM_CONTRACT.md), so the SHANKPHONE plays it natively on the phone's
# own wasm3 host instead of the Chromium stopgap (EMILY/BACKLOG.md S635-4).
#
#   scripts/build_shankpit460_level.sh           rebuild in place
#   scripts/build_shankpit460_level.sh --check   rebuild to a temp dir; fail if the committed module differs
#
# Committed for the same reason as BRAWLPIT's (scripts/build_brawlpit_level.sh): CI has no LLVM wasm toolchain. Source
# of truth: PARENA's stdlib/shankpit460/level.manifest (the very sp_*.prn the WOTAN web build ships, plus sp_level.prn).
# No art: the font, maps and people are all in the module.
#
# Needs ../PARENA (or $PARENA_DIR) and LLVM 18 + lld-18 (LLVM_TOOLCHAIN_ROOT, e.g. / for the distro packages).
# A missing piece skips loudly in --check mode (exit 0) and fails in build mode.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
PARENA_DIR="$(cd "${PARENA_DIR:-../PARENA}" 2>/dev/null && pwd || true)"
DEST="$ROOT/assets/levels/shankpit460/shankpit460_level.wasm"
MODE="build"; [ "${1:-}" = "--check" ] && MODE="check"
if [ -z "$PARENA_DIR" ] || [ ! -f "$PARENA_DIR/stdlib/shankpit460/level.manifest" ]; then
  [ "$MODE" = check ] && { echo "check-shankpit460-level: SKIP (no ../PARENA)"; exit 0; }
  echo "build_shankpit460_level: needs ../PARENA (or PARENA_DIR)" >&2; exit 1
fi
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
if ! (cd "$PARENA_DIR" && bash scripts/build_level_wasm.sh stdlib/shankpit460/level.manifest "$W/out.wasm"); then
  [ "$MODE" = check ] && { echo "check-shankpit460-level: SKIP (the PARENA level build did not run: toolchain?)"; exit 0; }
  exit 1
fi
if [ "$MODE" = check ]; then
  cmp -s "$W/out.wasm" "$DEST" || { echo "check-shankpit460-level: $DEST is stale -- run scripts/build_shankpit460_level.sh" >&2; exit 1; }
  echo "check-shankpit460-level: up to date"
else
  mkdir -p "$(dirname "$DEST")"; cp "$W/out.wasm" "$DEST"
  echo "built $DEST ($(wc -c < "$DEST") bytes, PARENA $(git -C "$PARENA_DIR" rev-parse --short HEAD))"
fi
