#!/usr/bin/env bash
# ux_screenshot_test.sh -- real, live UX regression test for shank_lobby's own screens.
#
# Founder real-time, 2026-09-28: "shankpit levels is down its just a blank screen can we add
# some ux screenshot testing." This boots the real lobby binary under a real headless Xvfb X
# server, sends a real synthetic Enter keypress via the X server's own XTEST extension (python3's
# Xlib -- xdotool isn't installed in every sandbox this runs in, XTEST needs nothing extra) to
# open the LEVELS overlay (lobby_selection defaults to 0 = LOBBY_LEVEL_SELECT, so Enter alone
# opens it, no mouse coordinates needed), screenshots both the boot screen and the LEVELS
# overlay, and fails if either is suspiciously close to solid black -- the real, live symptom
# already documented elsewhere in this repo (CHANGELOG: "SDL_CreateWindow was returning NULL
# unchecked... causing every GL call to silently no-op... zero crash, zero visible output").
#
# This is a real brightness sanity check, not a pixel-perfect screenshot diff -- that's a real,
# heavier follow-up (see NORTHSTAR), named honestly rather than half-built here. It catches the
# class of bug that actually happened before (a window that never renders anything) and the class
# reported this session (a screen that goes blank), not subtle visual regressions.
set -euo pipefail
cd "$(dirname "$0")/.."

BIN="${SHANKPIT_LOBBY_BIN:-./bin/shank_lobby}"
OUT_DIR="${1:-/tmp/shankpit-ux-screenshots}"
DISPLAY_NUM="${SHANKPIT_UX_TEST_DISPLAY:-:97}"
MIN_MEAN_BRIGHTNESS="0.02" # ImageMagick fx:mean is 0..1; pure black is 0.0

mkdir -p "$OUT_DIR"

if [ ! -x "$BIN" ]; then
  echo "no lobby binary at $BIN -- run 'make lobby' first" >&2
  exit 1
fi
if ! command -v Xvfb >/dev/null; then
  echo "Xvfb not installed -- cannot run headless UX screenshot tests" >&2
  exit 1
fi
if ! command -v import >/dev/null; then
  echo "ImageMagick's 'import' not installed -- cannot capture screenshots" >&2
  exit 1
fi
if ! python3 -c "import Xlib.ext.xtest" 2>/dev/null; then
  echo "python3-xlib not installed -- cannot send synthetic input (pip install python-xlib)" >&2
  exit 1
fi

Xvfb "$DISPLAY_NUM" -screen 0 1280x720x24 &
XVFB_PID=$!
trap 'kill "$XVFB_PID" 2>/dev/null || true; kill "${LOBBY_PID:-}" 2>/dev/null || true' EXIT
sleep 1

DISPLAY="$DISPLAY_NUM" "$BIN" > "$OUT_DIR/lobby.log" 2>&1 &
LOBBY_PID=$!
sleep 3

# The lobby loads its GOLDENBAND/NPC/shader assets after the window opens, and the first frames are
# pure black until that finishes (CI, 2026-10-08: boot frame black at 3s, LEVELS overlay rendered
# fine after input). So poll for the first real frame up to a cap rather than sampling once at a
# fixed time. A client that never renders still fails -- the cap is a wait, not a pass.
BOOT_TIMEOUT="${SHANKPIT_UX_BOOT_TIMEOUT:-20}"
boot_ok=0
for _ in $(seq 1 "$BOOT_TIMEOUT"); do
  DISPLAY="$DISPLAY_NUM" import -window root "$OUT_DIR/01_boot.png"
  bm=$(identify -format "%[fx:mean]" "$OUT_DIR/01_boot.png" 2>/dev/null || echo "0")
  if awk -v m="$bm" -v min="$MIN_MEAN_BRIGHTNESS" 'BEGIN { exit !(m+0 >= min+0) }'; then
    boot_ok=1
    echo "boot: first non-black frame after ~${_}s"
    break
  fi
  sleep 1
done
[ "$boot_ok" -eq 1 ] || echo "boot: still black after ${BOOT_TIMEOUT}s"

# Real synthetic Enter keypress via XTEST -- lobby_selection defaults to 0 (LOBBY_LEVEL_SELECT is
# deliberately the first tile), so this opens the real LEVELS overlay with no mouse coordinates.
DISPLAY="$DISPLAY_NUM" python3 - << PYEOF
from Xlib import display, X, XK
from Xlib.ext import xtest
d = display.Display("$DISPLAY_NUM")
kc = d.keysym_to_keycode(XK.XK_Return)
xtest.fake_input(d, X.KeyPress, kc)
d.sync()
xtest.fake_input(d, X.KeyRelease, kc)
d.sync()
d.close()
PYEOF
sleep 2
DISPLAY="$DISPLAY_NUM" import -window root "$OUT_DIR/02_levels_overlay.png"

kill "$LOBBY_PID" 2>/dev/null || true
wait "$LOBBY_PID" 2>/dev/null || true
sleep 0.3
kill "$XVFB_PID" 2>/dev/null || true
trap - EXIT

fail=0
for shot in "$OUT_DIR/01_boot.png" "$OUT_DIR/02_levels_overlay.png"; do
  mean=$(identify -format "%[fx:mean]" "$shot" 2>/dev/null || echo "0")
  awk -v m="$mean" -v min="$MIN_MEAN_BRIGHTNESS" 'BEGIN { exit !(m+0 < min+0) }' && {
    echo "FAIL: $shot is suspiciously close to solid black (mean brightness $mean, want >= $MIN_MEAN_BRIGHTNESS) -- this is what a genuinely blank screen looks like"
    fail=1
  } || echo "OK: $shot (mean brightness $mean)"
done

if [ "$fail" -ne 0 ]; then
  echo "ux_screenshot_test: FAIL -- see $OUT_DIR for the actual captured screenshots and $OUT_DIR/lobby.log for stderr/stdout"
  exit 1
fi
echo "ux_screenshot_test: PASS -- boot screen and LEVELS overlay both rendered real content. Screenshots in $OUT_DIR"
