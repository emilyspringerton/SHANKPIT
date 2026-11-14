#!/usr/bin/env bash
# scripts/build_phone_levels.sh -- rebuild the WOTAN games the SHANKPHONE plays natively, as T0 level modules
# (docs/T0_LEVEL_WASM_CONTRACT.md) on the phone's own wasm3 host instead of the Chromium stopgap (EMILY/BACKLOG.md S635-4):
#
#   assets/levels/shankpit460/shankpit460_level.wasm      <- PARENA stdlib/shankpit460/level.manifest
#   assets/levels/bedrockracers/bedrockracers_level.wasm  <- PARENA stdlib/bedrockracers/web/level.manifest
#
#   scripts/build_phone_levels.sh           rebuild in place (make phone-levels)
#   scripts/build_phone_levels.sh --check   rebuild to a temp dir; fail if a committed module differs (make check-phone-levels)
#
# Committed for the same reason as BRAWLPIT's (scripts/build_brawlpit_level.sh): CI has no LLVM wasm toolchain. Each is the
# WOTAN web engine unchanged plus a level adapter (sp_level.prn / br_level.prn). No art: everything is in the module.
# Needs ../PARENA (or $PARENA_DIR) and LLVM 18 + lld-18 (LLVM_TOOLCHAIN_ROOT, e.g. / for the distro packages). A missing
# piece skips loudly in --check mode (exit 0) and fails in build mode.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
PARENA_DIR="$(cd "${PARENA_DIR:-../PARENA}" 2>/dev/null && pwd || true)"
MODE="build"; [ "${1:-}" = "--check" ] && MODE="check"
LEVELS=(
  "stdlib/shankpit460/level.manifest assets/levels/shankpit460/shankpit460_level.wasm"
  "stdlib/bedrockracers/web/level.manifest assets/levels/bedrockracers/bedrockracers_level.wasm"
)
if [ -z "$PARENA_DIR" ]; then
  [ "$MODE" = check ] && { echo "check-phone-levels: SKIP (no ../PARENA)"; exit 0; }
  echo "build_phone_levels: needs ../PARENA (or PARENA_DIR)" >&2; exit 1
fi
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
stale=0
for entry in "${LEVELS[@]}"; do
  read -r manifest dest <<< "$entry"
  out="$W/$(basename "$dest")"
  if ! (cd "$PARENA_DIR" && bash scripts/build_level_wasm.sh "$manifest" "$out"); then
    [ "$MODE" = check ] && { echo "check-phone-levels: SKIP (the PARENA level build did not run: toolchain?)"; exit 0; }
    exit 1
  fi
  if [ "$MODE" = check ]; then
    cmp -s "$out" "$ROOT/$dest" || { echo "check-phone-levels: $dest is stale -- run scripts/build_phone_levels.sh" >&2; stale=1; }
  else
    mkdir -p "$(dirname "$ROOT/$dest")"; cp "$out" "$ROOT/$dest"
    echo "built $dest ($(wc -c < "$ROOT/$dest") bytes, PARENA $(git -C "$PARENA_DIR" rev-parse --short HEAD))"
  fi
done
[ "$MODE" = check ] && { [ $stale = 0 ] && echo "check-phone-levels: up to date"; exit $stale; }
exit 0
