#!/usr/bin/env bash
# scripts/level_smoke_test.sh -- `make level-smoke`: T0 end to end, in the real lobby binary, under Xvfb, driven by
# real X key events (XTEST, so SDL sees exactly what a keyboard sends). Founder real-time 2026-11-08: "Boot the
# SHANKPIT world plus BRAWLPIT as WASM, BRAWLPIT in the foreground. Shift+M toggles to the world view and pauses
# BRAWLPIT." The run, and what each step must produce:
#   boot                 [level] BRAWLPIT LOADING -> FG; LEVEL_LOADED (1, 1, 1); the screen is not black
#   Shift+M              FG -> BG; LEVEL_HIDDEN; the world differs from the level; the yellow resume/close hint
#   Shift+M              BG -> FG; LEVEL_SHOWN
#   1, Enter, Enter      BP_MATCH_STARTED (1100, mode 0 VS CPU, 2 fighters) -- keys reach the level as DOM codes
#   Shift+M              FG -> BG again (BRAWLPIT pauses its match: proven headless by make test-level-host)
#   Shift+X              BG -> NONE; LEVEL_CLOSED (1, 0, 0); the cyan "play" hint
#   Shift+M              NONE -> LOADING -> FG; LEVEL_LOADED (1, 1, 2): a fresh BRAWLPIT, generation 2
# and the lobby is still running at the end. The shell's log lines (one per transition; every lifecycle and
# BRAWLPIT REFLUX row with SHANKPIT_LEVEL_TRACE=1) are the assertions; the screenshots in $OUT are the evidence.
#
# Needs: the lobby built (make lobby), Xvfb, python3 with python-xlib (pip install --user python-xlib) and Pillow.
# Missing tools skip loudly (exit 0); a missing binary fails.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${SHANKPIT_LOBBY_BIN:-./bin/shank_lobby}"
OUT="${1:-${TMPDIR:-/tmp}/shankpit-level-smoke}"
DISPLAY_NUM="${SHANKPIT_LEVEL_SMOKE_DISPLAY:-:95}"
DRIVE="python3 -P scripts/level_smoke_drive.py"
mkdir -p "$OUT"
# the no-sudo dev prefix from MONOREPO BOOTSTRAP.md (libGLU etc.), when this box uses one
if [ -f "$HOME/.local/opt/sysdev/env.sh" ]; then . "$HOME/.local/opt/sysdev/env.sh"; fi

[ -x "$BIN" ] || { echo "level-smoke: no lobby binary at $BIN -- run make lobby first" >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "SKIP level-smoke: Xvfb not installed"; exit 0; }
python3 -P -c "import Xlib.ext.xtest, PIL" 2>/dev/null || { echo "SKIP level-smoke: needs python-xlib and Pillow (pip install --user python-xlib pillow)"; exit 0; }

Xvfb "$DISPLAY_NUM" -screen 0 1280x720x24 >/dev/null 2>&1 &
XVFB_PID=$!
LOBBY_PID=""
cleanup() { [ -n "$LOBBY_PID" ] && kill "$LOBBY_PID" 2>/dev/null; kill "$XVFB_PID" 2>/dev/null; true; }
trap cleanup EXIT
sleep 1
export DISPLAY="$DISPLAY_NUM"

LOG="$OUT/lobby.log"
SHANKPIT_LEVEL_TRACE=1 "$BIN" > "$LOG" 2>&1 &
LOBBY_PID=$!

fail=0
pass() { echo "PASS: $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# wait until PATTERN has appeared at least COUNT times in the log (fixed-string match)
wait_log() {  # $1 pattern, $2 count, $3 timeout seconds
  local i n
  for i in $(seq 1 $(( $3 * 4 ))); do
    n=$(grep -cF -- "$1" "$LOG" 2>/dev/null || true)
    [ "${n:-0}" -ge "$2" ] && return 0
    kill -0 "$LOBBY_PID" 2>/dev/null || return 1
    sleep 0.25
  done
  return 1
}
expect() {  # $1 pattern, $2 count, $3 timeout, $4 description
  if wait_log "$1" "$2" "$3"; then pass "$4"; else bad "$4 (no '$1' x$2 in $LOG)"; fi
}

expect "[level] BRAWLPIT LOADING -> FG" 1 90 "boot: BRAWLPIT loads in the foreground"
expect "[level] reflux 1000 (1, 1, 1)" 1 5 "LEVEL_LOADED (level 1, api 1, generation 1)"
sleep 2
$DRIVE move 640 700
$DRIVE shot "$OUT/1_boot.png"

$DRIVE key shift+m
expect "[level] BRAWLPIT FG -> BG" 1 10 "Shift+M: the world comes to the front"
expect "[level] reflux 1002 (1, 0, 0)" 1 5 "LEVEL_HIDDEN (1)"
sleep 1.5
$DRIVE shot "$OUT/2_world.png"

$DRIVE key shift+m
expect "[level] BRAWLPIT BG -> FG" 1 10 "Shift+M again: BRAWLPIT comes back"
expect "[level] reflux 1001 (1, 0, 0)" 2 5 "LEVEL_SHOWN (1)"
sleep 1

$DRIVE key 1
sleep 1
$DRIVE key Return
sleep 1
$DRIVE key Return
expect "[level] reflux 1100 (0, " 1 15 "1, Enter, Enter: a VS CPU match starts (BP_MATCH_STARTED)"
sleep 1.5
$DRIVE shot "$OUT/3_match.png"

$DRIVE key shift+m
expect "[level] BRAWLPIT FG -> BG" 2 10 "Shift+M mid-match: the world comes to the front"
$DRIVE key shift+x
expect "[level] BRAWLPIT BG -> NONE" 1 10 "Shift+X in the world view closes BRAWLPIT"
expect "[level] reflux 1003 (1, 0, 0)" 1 5 "LEVEL_CLOSED (1, reason 0 = the player)"
sleep 1.5
$DRIVE shot "$OUT/4_closed.png"

$DRIVE key shift+m
expect "[level] BRAWLPIT LOADING -> FG" 2 60 "Shift+M with nothing loaded: a fresh BRAWLPIT"
expect "[level] reflux 1000 (1, 1, 2)" 1 5 "LEVEL_LOADED generation 2"
sleep 2
$DRIVE shot "$OUT/5_reloaded.png"

if kill -0 "$LOBBY_PID" 2>/dev/null; then pass "the lobby is still running"; else bad "the lobby exited (see $LOG)"; fi

shots=("$OUT"/1_boot.png "$OUT"/2_world.png "$OUT"/3_match.png "$OUT"/4_closed.png "$OUT"/5_reloaded.png)
if $DRIVE nonblack "${shots[@]}"; then pass "no screenshot is black"; else bad "a screenshot is black"; fi
if $DRIVE differ "$OUT/1_boot.png" "$OUT/2_world.png" >/dev/null; then pass "the world view is not the level view"; else bad "Shift+M changed nothing on screen"; fi
if $DRIVE banner "$OUT/2_world.png" 1.0,0.85,0.2 >/dev/null; then pass "the resume/close hint is drawn over the world"; else bad "no resume/close hint in $OUT/2_world.png"; fi
if $DRIVE banner "$OUT/4_closed.png" 0.6,0.9,1.0 >/dev/null; then pass "the play hint is drawn once BRAWLPIT is closed"; else bad "no play hint in $OUT/4_closed.png"; fi
if $DRIVE banner "$OUT/1_boot.png" 1.0,0.85,0.2 >/dev/null; then bad "a hint is drawn over BRAWLPIT in the foreground"; else pass "nothing is drawn over BRAWLPIT in the foreground"; fi

echo "screenshots and log: $OUT"
[ $fail -eq 0 ] && echo "level-smoke: ALL PASS" || { echo "level-smoke: FAILED"; exit 1; }
