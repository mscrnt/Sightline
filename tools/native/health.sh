#!/bin/sh
# Native survival gate at the CANONICAL fixture.
#
# Not overridable. `make native-health` always means the same run: facility,
# 900 frames, a pinned RNG seed. The frame count is not a parameter because the
# whole point is that it matches the cartridge ACMD capture's 900 - treating the
# parity duration and the survival duration as different numbers is how a build
# that faults at frame 501 got landed on a 60-frame check. For ad-hoc runs,
# invoke build/native/skeleton directly with your own environment.
#
# Failure is taken from the PROCESS, not from reassuring text: exit status,
# frames reached and crash count must all be right. A timeout or a signal fails
# even if a "survived" line was printed before it.
set -u
cd "$(dirname "$0")/../.."

LEVEL=facility
FRAMES=900
SEED=00000000697ec8c8          # pinned: deterministic run, not the host clock
LOG=$(mktemp)

# THE LEVEL HAS TO BE A STAGE NUMBER, and this is the whole reason the gate is
# worth re-reading.
#
# It used to pass SL_LEVEL=facility. **Nothing in the binary reads SL_LEVEL** -
# tree-wide over src/ and tools/trace/ for .c, .h and .py that name appears in
# no source file, only in this script and play.sh. What the skeleton actually
# reads is SL_BOOT_LEVEL, a stage NUMBER (sl_main.c:164, sl_ultra_shim.c:1490).
# So the canonical fixture booted the DEFAULT stage, which is LEVELID_TITLE = 90
# (bondconstants.h:1726): every "facility 900/900" in this repo's history is a
# measurement of the TITLE SCREEN.
#
# It passed as a survival gate because the title screen survives 900 frames
# perfectly well, and an inert environment variable produces no error at all.
# That is the failure mode to design against, so the resolution below FAILS
# LOUDLY instead of falling back to a default.
STAGE=$(${PY:-.venv/bin/python3} tools/native/levelstage.py "$LEVEL" 2>/dev/null || true)
if [ -z "$STAGE" ]; then
    echo "native-health: cannot resolve level '$LEVEL' to a stage number." >&2
    echo "  Names are the keys in tools/trace/sltrace/levelboot.py. Refusing to" >&2
    echo "  run, because booting the default stage would silently gate the title" >&2
    echo "  screen instead - which is exactly what SL_LEVEL used to do." >&2
    exit 1
fi

# env deliberately scrubbed so an exported SL_FRAMES cannot shorten the gate,
# or an exported SL_BOOT_LEVEL redirect it to a different stage
env -u SL_FRAMES -u SL_LEVEL -u SL_RNG_SEED -u SL_ACMD_OUT -u SL_BOOT_LEVEL \
    -u SL_BOOT_DIFFICULTY \
    SL_BOOT_LEVEL="$STAGE" SL_BOOT_DIFFICULTY=1 \
    SL_FRAMES="$FRAMES" SL_RNG_SEED="$SEED" \
    timeout 600 build/native/skeleton >"$LOG" 2>&1
STATUS=$?

# The gate must PROVE it booted the stage it names. A survival count says
# nothing about WHICH level survived, which is how this went unnoticed.
if ! grep -q "direct boot level $STAGE" "$LOG"; then
    echo "native-health: FAIL - the run never reported 'direct boot level $STAGE'"
    echo "native-health: log kept at $LOG"
    exit 1
fi

REACHED=$(grep -oE 'survived [0-9]+ pumped frames' "$LOG" | grep -oE '[0-9]+' | tail -1)
[ -n "$REACHED" ] || REACHED=0
# grep -c PRINTS 0 and EXITS 1 when there are no matches, so `|| echo 0`
# appends a second zero and the comparison silently fails. It always prints a
# count; take it and drop the fallback.
CRASHES=$(grep -c 'CRASH' "$LOG" 2>/dev/null)
[ -n "$CRASHES" ] || CRASHES=0

printf 'native-health: level=%s(stage %s) fixture=canonical seed=%s requested=%s reached=%s exit=%s crashes=%s\n' \
       "$LEVEL" "$STAGE" "$SEED" "$FRAMES" "$REACHED" "$STATUS" "$CRASHES"

FAIL=0
[ "$STATUS" = "0" ]        || { echo "native-health: FAIL exit status $STATUS"; FAIL=1; }
[ "$REACHED" = "$FRAMES" ] || { echo "native-health: FAIL reached $REACHED of $FRAMES"; FAIL=1; }
[ "$CRASHES" = "0" ]       || { echo "native-health: FAIL $CRASHES crash report(s)"; FAIL=1; }
if [ "$FAIL" != "0" ]; then echo "native-health: log kept at $LOG"; exit 1; fi
rm -f "$LOG"
echo "native-health: OK"
