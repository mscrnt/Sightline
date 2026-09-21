#!/bin/sh
# Build and run the stage 1-3 ACMD replay harness against a census capture.
#   tools/native/acmd_census.py --input facility --frames 900 \
#       --out /tmp/sl-acmd3 --capture /tmp/sl-acmd3/zerovoice.bin
#   tools/native/acmdreplay.sh /tmp/sl-acmd3/zerovoice.bin
set -e
cd "$(dirname "$0")/../.."
OUT=build/native
mkdir -p $OUT
# The RESAMPLE / ENVMIXER tables are no longer a generated header: sl_acmd.c
# takes them at run time through sl_acmd_set_ucode_tables (src/platform/
# sl_ucode.c derives them from the ROM). This harness does not yet supply
# them, so sl_acmd_exec reports SL_ACMD_ERR_UNGROUNDED until it does
# (docs/backlog.md, the v0.2.0 release round).
# Same recipe build.sh uses for src/platform: host headers only, -m32.
gcc -m32 -g -Wall -Wextra -Werror=implicit-function-declaration \
    -msse2 -mfpmath=sse -I$OUT \
    tools/native/acmdreplay.c src/platform/sl_acmd.c \
    -lm -o $OUT/acmdreplay
exec $OUT/acmdreplay "${1:-/tmp/sl-acmd3/zerovoice.bin}"
