#!/bin/sh
# Build and run the input calibration tool.
#
# Standalone on purpose: no ROM, no window, no game. SDL's gamepad layer needs
# none of those, so this can run in a terminal beside a running demo without
# competing for the display.
set -e
cd "$(dirname "$0")/../.."

OUT=build/native
mkdir -p $OUT

PKGI=/usr/lib/i386-linux-gnu/pkgconfig
CFLAGS="$(PKG_CONFIG_PATH=$PKGI pkg-config --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)"
LIBS="$(PKG_CONFIG_PATH=$PKGI pkg-config --libs sdl2 2>/dev/null || echo -lSDL2)"

# -m32 to match the SDL the rest of the native build links against.
gcc -m32 -w $CFLAGS tools/native/inputcal.c -o $OUT/inputcal $LIBS

exec $OUT/inputcal "$@"
