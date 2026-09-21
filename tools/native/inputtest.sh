#!/bin/sh
# Build and run the headless keyboard/mouse translation test.
#
# Links the REAL src/platform/sl_input.c against stub SDL and stub game
# queries (tools/native/inputtest.c), so what is asserted is the shipping
# translation and not a second copy of it. No window, no pointer, no ROM.
set -e
cd "$(dirname "$0")/../.."
OUT=build/native
mkdir -p $OUT
gcc -m32 -O0 -g -Wall -Wno-unused-parameter \
    -o $OUT/inputtest tools/native/inputtest.c src/platform/sl_input.c \
    src/platform/sl_action.c \
    $(pkg-config --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)
exec $OUT/inputtest
