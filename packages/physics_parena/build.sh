#!/bin/sh
# Build + run the PARENA physics port suites. Usage: ./build.sh  (PARENA=/path/to/PARENA to override)
set -e
D="$(cd "$(dirname "$0")" && pwd)"
P=${PARENA:-$D/../../../PARENA}; S=$D/..
O=${OUT:-${TMPDIR:-/tmp}/physics_parena_build}; mkdir -p "$O"; cd "$D"
BOOT=$P/.parena-bootstrap; [ -x "$BOOT" ] || BOOT=$P/parena
$BOOT build $P/stdlib/shankpit/physics.prn -o $O/physics_gen.c
$BOOT build $P/stdlib/shankpit/movement.prn -o $O/movement_gen.c
F="-std=c11 -Wall -Wextra -Werror -pedantic -DPARENA_NO_GRAPHICS -D_DEFAULT_SOURCE -I$P/runtime -I$D"
for f in physics_host test_physics; do gcc $F -c $f.c -o $O/$f.o; done
for f in physics_gen movement_gen; do gcc $F -c $O/$f.c -o $O/$f.o; done
gcc $F -DPARENA_NO_MALLOC -c $P/runtime/parena_runtime.c -o $O/rt.o
gcc -o $O/test_physics $O/test_physics.o $O/physics_host.o $O/physics_gen.o $O/movement_gen.o $O/rt.o -lm
$O/test_physics
DEFS="-Daccelerate=orig_accelerate -Dapply_friction=orig_apply_friction -Dreflect_vector=orig_reflect_vector -Dresolve_collision=orig_resolve_collision -Dcheck_hit_location=orig_check_hit_location -Dtrace_map=orig_trace_map"
gcc -std=gnu11 -O1 -w -I$S/common -I$S -c $S/world/terrain.c -o $O/terrain.o
gcc -std=gnu11 -O1 -w $DEFS -DPARENA_NO_GRAPHICS -I$S/common -I$S -I$D -c test_diff.c -o $O/test_diff.o
gcc -o $O/test_diff $O/test_diff.o $O/physics_host.o $O/physics_gen.o $O/movement_gen.o $O/rt.o $O/terrain.o -lm
$O/test_diff
nm -u $O/physics_host.o $O/physics_gen.o $O/movement_gen.o $O/rt.o | grep -E ' (malloc|calloc|realloc|free)$' && echo "HEAP SYMBOLS FOUND" || echo "no heap symbols in port objects"
