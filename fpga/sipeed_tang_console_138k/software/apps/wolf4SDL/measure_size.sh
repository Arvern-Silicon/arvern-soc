#!/bin/bash
# Compile every Wolf4SDL source for the aRVern core (rv32imc, -Os, xPacks GCC)
# against the SDL2 headers and the stubs in ./stub, without linking, and print
# the code / data sizes per object file and the largest static arrays.
# Usage: ./measure_size.sh [SDL2 include dir]
cd "$(dirname "$0")"
SDL_INC=${1:-/opt/homebrew/include/SDL2}
CC=riscv-none-elf-gcc
CFLAGS="-c -Os -march=rv32imc_zicsr -mabi=ilp32 -std=c99 -D_POSIX_C_SOURCE=200809L \
        -ffunction-sections -fdata-sections -w -Istub -I$SDL_INC -IWolf4SDL"
SRCS=$(make -s -C Wolf4SDL -n -p 2>/dev/null | sed -n 's/^SRCS := //p' | head -1)
rm -rf obj && mkdir obj
for f in $SRCS; do
    o=obj/$(basename "$f" .c).o
    $CC $CFLAGS "Wolf4SDL/$f" -o "$o" 2> "${o%.o}.err" || echo "FAIL $f (see ${o%.o}.err)"
done
riscv-none-elf-size -t obj/*.o | sort -k1 -n
echo
echo "Largest static objects (bytes, type, symbol, file):"
for o in obj/*.o; do riscv-none-elf-nm -S "$o" | sed "s|$| $(basename "$o")|"; done | python3 -c "
import sys
r = []
for l in sys.stdin:
    p = l.split()
    if len(p) == 5 and p[2] in 'bBdDcCrR':
        r.append((int(p[1], 16), p[2], p[3], p[4]))
for x in sorted(r)[-22:]:
    print(*x)
"
