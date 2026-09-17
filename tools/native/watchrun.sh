#!/bin/sh
# watchrun - catch whatever writes the garbage, at the instant it writes it.
#
# The crash itself only proves the value is already wrong. A hardware
# watchpoint stops the process ON THE STORE, so the backtrace names the culprit
# directly instead of by inference.
#
# This is possible because the allocation is DETERMINISTIC: the crashing guard
# model landed at 0xc89a9fd8 in two separate runs, and its anim2 field is at
# +0x54. If a future run reports a different model address, pass it:
#
#   tools/native/watchrun.sh                    use the known address
#   tools/native/watchrun.sh 0xc89a9fd8         same, explicit
#
# Play to the same spot in the scientist section. Output: /tmp/watch-34.log
set -u
cd "$(dirname "$0")/../.."
MODEL="${1:-0xc89a9fd8}"
OUT="/tmp/watch-34.log"

[ -x build/native/skeleton ] || tools/native/build.sh || exit 1

cat > /tmp/watchrun.gdb <<GDB
set confirm off
set pagination off
set print pretty on

# Arm the watchpoint only once the address is real. chrUpdateAnim runs every
# tick for every character, so the first hit is well after the heap is up.
break chrUpdateAnim
commands
silent
delete breakpoints
# A *hardware* watchpoint on the 4 bytes of Model.anim2 (+0x54). It stops on
# the STORE, so \$_siginfo is irrelevant and the backtrace is the writer's.
watch -l *(unsigned int *)($MODEL + 0x54)
continue
end

run

echo \\n=== WRITE TO anim2 CAUGHT ===\\n
echo --- who wrote it ---\\n
bt
echo \\n--- with locals ---\\n
bt full
echo \\n--- registers ---\\n
info registers eip eax ebx ecx edx esi edi
echo \\n=== END ===\\n
GDB

echo "watchrun: watching Model.anim2 at $MODEL+0x54"
echo "watchrun: play to the scientist section. Writing $OUT"
SL_ROM="${SL_ROM:-baserom.u.z64}" SL_BOOT_LEVEL=34 SL_WINDOW=1 \
  gdb -q -batch -x /tmp/watchrun.gdb ./build/native/skeleton > "$OUT" 2>&1

if grep -q 'WRITE TO anim2 CAUGHT' "$OUT"; then
    echo "watchrun: CAUGHT IT - see $OUT"
else
    echo "watchrun: watchpoint never fired ($OUT)"
    echo "watchrun: if the crash still happened, the model moved - check the"
    echo "          address in the log and re-run with it as an argument."
fi
