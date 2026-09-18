#!/bin/sh
# Build and run the host-side symbolic-engine check. Needs a host gcc; on
# this Windows setup the Ruby devkit's MSYS2 one works:
#   PATH="/c/Ruby34-x64/msys64/ucrt64/bin:$PATH" tools/symtest/build.sh
# Run from the repo root. Exit status is non-zero on any leak.
set -e
cd "$(dirname "$0")"
SRC=../../src
OUT=./out
mkdir -p "$OUT"
CF="-std=c11 -Wall -Wextra -O1 -I$SRC"
COUNT="-Dmalloc=h_malloc -Dfree=h_free -include stdlib.h"
gcc $CF $COUNT -c "$SRC/expr.c"     -o "$OUT/expr.o"
gcc $CF $COUNT -c "$SRC/symbolic.c" -o "$OUT/symbolic.o"
gcc $CF        -c "$SRC/fmt.c"      -o "$OUT/fmt.o"
gcc $CF        -c symtest.c         -o "$OUT/symtest.o"
gcc -o "$OUT/symtest.exe" "$OUT/symtest.o" "$OUT/expr.o" "$OUT/symbolic.o" "$OUT/fmt.o"
"$OUT/symtest.exe"
