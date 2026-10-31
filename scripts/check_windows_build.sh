#!/usr/bin/env bash
# check_windows_build.sh -- compile every Windows-client source under mingw-w64, without linking.
#
# Why this exists: the Windows client is built only in CI (release.yml), so a break that is purely
# compile-time can sit on master unseen until a release job runs. The one that motivated this script was
# a lobby-local helper (`env_emissive_scale`) defined after its first use and shadowing PARENA's generated
# `env_emissive_scale(int)`; it failed only the mingw job because nobody had ever compiled the lobby on
# Linux either (no SDL2 headers in the sandbox).
#
# It compiles each file in LOBBY_SRC on its own, exactly like the real build's one-shot gcc invocation
# would, and reports the first errors per file. No link, so no SDL2 .a/.dll and no EDITOR.GAME EDITOR objects
# are needed -- only the SDL2 mingw HEADERS.
#
# Usage:
#   scripts/check_windows_build.sh
#   SDL2_MINGW_PREFIX=/path/to/sdl2_mingw scripts/check_windows_build.sh
# (CI gets the headers from the same SDL2 2.30.10 / SDL2_ttf 2.22.0 mingw kits release.yml downloads.)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

cc="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
if ! command -v "$cc" >/dev/null 2>&1; then
    echo "check_windows_build: $cc not found -- install mingw-w64 (or set MINGW_CC). Skipping." >&2
    exit 0
fi

sdl="${SDL2_MINGW_PREFIX:-/tmp/sdl2_mingw}"
if [ ! -d "$sdl/include/SDL2" ]; then
    echo "check_windows_build: no SDL2 headers at $sdl/include/SDL2" >&2
    echo "  fetch the mingw dev kit (same one CI uses) or set SDL2_MINGW_PREFIX:" >&2
    echo "    curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-devel-2.30.10-mingw.tar.gz" >&2
    echo "    tar xzf SDL2-devel-2.30.10-mingw.tar.gz && mv SDL2-2.30.10/x86_64-w64-mingw32 /tmp/sdl2_mingw" >&2
    exit 1
fi

src="$(make -s print-lobby-src)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

total=0
fail=0
for f in $src; do
    total=$((total + 1))
    log="$tmp/$(echo "$f" | tr '/' '_').log"
    if ! "$cc" -std=gnu99 -w -c "$f" -o "$tmp/$(echo "$f" | tr '/' '_').o" \
        -I. -I"$sdl/include" \
        -Ipackages/common -Ipackages/simulation -Ipackages/render -Ipackages/reflux \
        -Ipackages/world -Ipackages/audio -Ipackages/goldenband >"$log" 2>&1; then
        echo "FAIL $f"
        grep -m5 "error:" "$log" | sed 's/^/    /'
        fail=$((fail + 1))
    fi
done

if [ "$fail" -gt 0 ]; then
    echo "check_windows_build: $fail of $total source(s) failed to compile for Windows" >&2
    exit 1
fi
echo "check_windows_build OK ($total sources compile for x86_64-w64-mingw32)"
