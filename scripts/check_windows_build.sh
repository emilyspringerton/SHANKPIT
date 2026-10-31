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
# would, and reports the first errors per file. No real link, so no SDL2 .a/.dll and no EDITOR.GAME EDITOR
# objects are needed -- only the SDL2 mingw HEADERS.
#
# Since 2026-10-31 it also audits the LINK symbols from those objects (`nm`), because compiling clean is
# only half of what the release job does and two link failures reached master past this script:
#   - a duplicate global definition (`s_abs`, defined by two generated PARENA modules that are both in
#     LOBBY_SRC) which the linker reports as `multiple definition of 's_abs'`;
#   - an undefined symbol whose only definition was in a file missing from LOBBY_SRC
#     (`mat_reflection_strength_permille`, in packages/simulation/material_rules.c).
# Both are visible without any libraries: duplicates from the objects themselves, and unresolved symbols
# after subtracting what the objects and the mingw import libraries define. Symbols the release job links
# from its own separately-built objects (EDITOR.GAME's `editor_widget_*`, PELLEGRINO's `phone_browser_*`)
# are allowlisted, as are SDL/GL/Win32/CRT names.
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

# ---- link-symbol audit -----------------------------------------------------------------------------
nm_bin="${NM:-x86_64-w64-mingw32-nm}"
if ! command -v "$nm_bin" >/dev/null 2>&1; then
    echo "check_windows_build: $nm_bin not found -- link-symbol audit skipped (install mingw-w64 binutils)." >&2
    echo "check_windows_build OK ($total sources compile for x86_64-w64-mingw32; symbols NOT audited)"
    exit 0
fi

# Objects are flat names in $tmp (see the loop above), so this glob is the whole build.
objs=$(ls "$tmp"/*.o)
"$nm_bin" --defined-only -g $objs 2>/dev/null | awk 'NF >= 3 && $2 ~ /^[TDBRCGWV]$/ { print $3 }' > "$tmp/def_all"
sort -u "$tmp/def_all" > "$tmp/def"
uniq -d "$tmp/def_all" > "$tmp/dups"
"$nm_bin" -u $objs 2>/dev/null | awk 'NF >= 2 { print $2 }' | sort -u > "$tmp/undef"

# What the mingw import libraries provide (msvcrt + mingwex is where the math/CRT names actually live;
# SDL/GL are prefix-allowlisted because their .a files are not in this checkout).
for lib in libmsvcrt.a libmingwex.a libmoldname.a libmingw32.a libgcc.a libgcc_eh.a \
           libkernel32.a libuser32.a libgdi32.a libwinmm.a libws2_32.a libopengl32.a libglu32.a \
           libadvapi32.a libshell32.a libcomdlg32.a libole32.a libimm32.a libversion.a \
           libsetupapi.a libdwmapi.a libbcrypt.a libuxtheme.a libcomctl32.a libshlwapi.a libcrypt32.a; do
    p="$("$cc" -print-file-name="$lib")"
    if [ -f "$p" ]; then
        "$nm_bin" --defined-only "$p" 2>/dev/null | awk 'NF >= 3 { print $3 }'
    fi
done | sort -u > "$tmp/sysdef"

comm -23 "$tmp/undef" "$tmp/def" | comm -23 - "$tmp/sysdef" \
    | grep -vE '^(SDL_|__|_|gl[A-Z]|glu[A-Z]|editor_widget_|phone_browser_)' > "$tmp/unresolved" || true

dups=$(wc -l < "$tmp/dups" | tr -d ' ')
unresolved=$(wc -l < "$tmp/unresolved" | tr -d ' ')
if [ "$dups" -gt 0 ]; then
    echo "check_windows_build: $dups symbol(s) defined more than once -- the linker would fail with 'multiple definition':" >&2
    head -20 "$tmp/dups" | sed 's/^/    /' >&2
fi
if [ "$unresolved" -gt 0 ]; then
    echo "check_windows_build: $unresolved symbol(s) no object or system library defines -- 'undefined reference' at link:" >&2
    head -20 "$tmp/unresolved" | sed 's/^/    /' >&2
fi
if [ "$dups" -gt 0 ] || [ "$unresolved" -gt 0 ]; then
    echo "check_windows_build: link-symbol audit FAILED (a duplicate generated name, or a source list missing a file)" >&2
    exit 1
fi
echo "check_windows_build OK ($total sources compile; link symbols clean: no duplicate definitions, no unresolved project symbols)"
