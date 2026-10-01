#!/usr/bin/env bash
# gen_tyler_voice_mod.sh -- regenerate packages/simulation/tyler_voice_mod.c from PARENA.
#
# MODE_TYLER's voice decisions (founder real-time, 2026-10-01: "VALHANNA voices via TTS") live in
# PARENA: PARENA/stdlib/tyler/voice_mod.prn -- beat-hold sizing around a clip, plus the playback
# decisions tyler_coldopen.c and the audio callback call (line-state, seek, duck target/step). The
# host C owns clocks, files and the mixer; every decision is made by the generated code.
# Usage: scripts/gen_tyler_voice_mod.sh   (needs a sibling ../PARENA checkout with a built ./parena)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
[ -x "$parena_dir/parena" ] || (cd "$parena_dir" && make build >/dev/null)
"$parena_dir/parena" build "$parena_dir/stdlib/tyler/voice_mod.prn" -o "$root/packages/simulation/tyler_voice_mod.c"
echo "regenerated packages/simulation/tyler_voice_mod.c"
