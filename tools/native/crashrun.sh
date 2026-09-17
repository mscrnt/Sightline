#!/bin/sh
# crashrun - play normally, but capture everything if it crashes.
#
# A bare run gives the shim's own SIGSEGV line and a list of return addresses,
# which is enough to name the functions and no more. That was enough to locate
# the subcalcmatrices fault and not enough to explain it, because the value
# that mattered was in a struct nobody could see afterwards.
#
# This runs the same game under gdb. gdb sits out of the way until the fault,
# then dumps the backtrace WITH LOCALS, the registers, and the model/anim state
# at the crash point - and keeps the process alive so nothing is lost.
#
#   tools/native/crashrun.sh                 facility (stage 34)
#   tools/native/crashrun.sh 29              streets
#
# Output: /tmp/crash-<stage>.log, plus whatever the game prints. Play to the
# spot that crashed; quit normally if it does not.
set -u
cd "$(dirname "$0")/../.."
STAGE="${1:-34}"
OUT="/tmp/crash-$STAGE.log"

[ -x build/native/skeleton ] || tools/native/build.sh || exit 1
command -v gdb >/dev/null || { echo "crashrun: gdb not installed" >&2; exit 1; }

cat > /tmp/crashrun.gdb <<'GDB'
set confirm off
set pagination off
set print pretty on
set logging file /tmp/crashrun-inner.log
set logging overwrite on
set logging enabled on
run
echo \n=== FAULTED ===\n
info registers eip eax ebx ecx edx esi edi ebp esp
echo \n--- backtrace with locals ---\n
bt full
echo \n--- frame 0 ---\n
frame 0
info locals
info args
echo \n--- caller ---\n
frame 1
info locals
info args
echo \n--- THE MODEL, whole ---\n
p *modelptr
echo \n--- raw memory around it (anim2 is at +84 = +0x54) ---\n
x/48xw modelptr
echo \n--- is anim2 in the animation table? ---\n
p (void *)modelptr->anim
p (void *)modelptr->anim2
echo \n=== END ===\n
GDB

echo "crashrun: stage $STAGE. Play to the crash; quit normally if it does not happen."
echo "crashrun: writing $OUT"
SL_ROM="${SL_ROM:-baserom.u.z64}" SL_BOOT_LEVEL="$STAGE" SL_WINDOW=1 \
  gdb -q -batch -x /tmp/crashrun.gdb ./build/native/skeleton > "$OUT" 2>&1

if grep -q 'FAULTED' "$OUT"; then
    echo "crashrun: CRASHED - full state in $OUT"
else
    echo "crashrun: no crash this run ($OUT)"
fi
