#!/bin/sh
# Boot every campaign level natively and report what happens.
#
# Why this exists: Facility crashed on a gas-releasing object that Archives
# does not have. "It boots" had only ever been established for the levels
# somebody happened to try, and the levels somebody happened to try were the
# ones that worked. A per-level failure is invisible to a harness pointed at
# one level, and twenty levels is too many to check by hand.
#
# This is a DETERMINISTIC SMOKE GATE, not a correctness gate. It answers
# "does the sim come up"; trace-verify answers "does state match". 20/20 boots
# is not a statement about game correctness, and should never be read as one.
# Exit status is the count of levels that did not reach a running sim, so CI
# can use it if wanted.
#
# SL_LEVELS overrides the list ("name:stage name:stage ...").
# SL_SWEEP_FRAMES overrides how far each level runs (default 400).
# SL_RNG_SEED overrides the pinned seed below.
# SL_SWEEP_RANDOM=1 unpins it entirely - host-clock seed per level, which is
#   stochastic coverage rather than a gate. Use `make bootsweep-random`.
#   Unset or 0 means pinned, matching the src/ switches where only an exact
#   "0" disables.
set -u
cd "$(dirname "$0")/../.."

ROM="${SL_ROM:-baserom.u.z64}"
FRAMES="${SL_SWEEP_FRAMES:-400}"
OUT="${SL_SWEEP_OUT:-build/bootsweep}"

# RNG seeding. This sweep asks the same question on every invocation, so it
# pins the seed rather than sampling a different RNG state each run - a
# regression check that varies run to run cannot be bisected, and a failure
# that appears once in twenty runs reads as noise.
#
# The value is the historical native default: before B-053 the shim's
# osGetCount() returned a constant at seed time, randomSetSeed stored
# (s64)(s32)x + 1, and every launch began at this state. Pinning it here keeps
# the exact RNG chain this sweep has always run on, so results stay comparable
# across the B-053 boundary.
#
# Interactive play, replay and trace determinism are untouched by this - the
# seed is supplied by the harness, not by any change to policy.
case "${SL_SWEEP_RANDOM:-}" in
    ""|0) SEED="${SL_RNG_SEED:-00000000000f4241}" ;;
    *)    SEED="" ;;
esac

if [ ! -f "$ROM" ]; then
    echo "bootsweep: no ROM at '$ROM' - set SL_ROM." >&2
    exit 1
fi
[ -x build/native/skeleton ] || tools/native/build.sh

mkdir -p "$OUT"
RESULT="$OUT/results.txt"
: > "$RESULT"

# Campaign order, matching tools/trace/sweep.py's CAMPAIGN list. Stage numbers
# come from tools/trace/sltrace/levelboot.py - the same table the trace harness
# and the demo launcher boot with, so the three cannot drift apart.
LEVELS="${SL_LEVELS:-dam:33 facility:34 runway:35 surface:36 bunker1:9 silo:20
frigate:26 surface2:43 bunker2:27 statue:22 archives:24 streets:29 depot:30
train:25 jungle:37 control:23 caverns:39 cradle:41 aztec:28 egypt:32}"

fail=0
for entry in $LEVELS; do
    name=${entry%%:*}
    stage=${entry##*:}
    log="$OUT/$name.log"
    env ${SEED:+SL_RNG_SEED=$SEED} \
        SL_ROM="$ROM" SL_BOOT_LEVEL="$stage" SL_BOOT_DIFFICULTY=1 \
        SL_FRAMES="$FRAMES" SL_HANG_SECONDS=180 \
        ./build/native/skeleton > "$log" 2>&1
    rc=$?
    case $rc in
        0) verdict="OK" ;;
        4) verdict="HANG" ;;
        5) verdict="SEGV" ;;
        *) verdict="EXIT$rc" ;;
    esac
    [ "$rc" = "0" ] || fail=$((fail + 1))
    # The faulting PC, so a failure names a place rather than just a level.
    # Resolve it with: nm build/native/skeleton | sort, nearest preceding symbol.
    pc=$(grep -m1 "^  pc 0x" "$log" 2>/dev/null | awk '{print $2}')
    # The seed this level actually ran on, so a randomized failure names the
    # state that produced it and can be replayed with SL_RNG_SEED=<value>.
    seed=$(grep -m1 "RNG state" "$log" 2>/dev/null | awk '{print $5}')
    printf '%-10s stage=%-3s %-6s seed=%s %s\n' \
        "$name" "$stage" "$verdict" "${seed:-?}" "$pc" | tee -a "$RESULT"
done

echo "bootsweep: $fail level(s) did not reach a running sim; logs in $OUT" | tee -a "$RESULT"
if [ -z "$SEED" ] && [ "$fail" != "0" ]; then
    echo "bootsweep: seeds were per-level host clock; reproduce one with" \
         "SL_RNG_SEED=<seed from the line above>" | tee -a "$RESULT"
fi
exit $fail
