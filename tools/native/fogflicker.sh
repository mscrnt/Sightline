#!/bin/sh
# Measure fog's frame-to-frame stability, and REFUSE TO REPORT unless the run
# actually reached the gas.
#
# Fog's contribution is the pixel difference between the same frame rendered
# with SL_FOG on and off. Camera motion cancels because both runs replay the
# same deterministic input. Stable fog varies a few percent between adjacent
# frames; the flicker the owner sees swings by multiples.
#
# THE PRECONDITION IS THE POINT. Four separate measurements in this
# investigation were taken on frames where the gas had never triggered, and
# each produced a confident wrong conclusion:
#   - SL_FRAMES does NOT count rendered frames. Measured 21000 -> 5285
#     rendered, about 4x, and an earlier run implied 2x. Do not assume a ratio.
#   - SL_SHOT_FIRST silently does not apply, so captures start at frame 0.
#   - Slicing the FIRST N captures analyses the opening of the level.
# So this reads <run>/events, finds the record where far-fog first falls, and
# will not print a verdict if the capture set does not reach it.
#
#   tools/native/fogflicker.sh <run-dir> [frames] [samples]
set -u
RUN="${1:?usage: fogflicker.sh <run-dir> [frames] [samples]}"
FRAMES="${2:-60000}"
N="${3:-12}"
cd "$(dirname "$0")/../.." || exit 1

TRIG=$(awk '/far-fog 5000.0 ->/{print $1; exit}' "$RUN/events" 2>/dev/null)
[ -n "$TRIG" ] || { echo "fogflicker: no gas trigger in $RUN/events" >&2; exit 2; }
echo "  gas triggers at record $TRIG"

for MODE in 0 1; do
    D=/tmp/fogflicker-$MODE; rm -rf $D; mkdir -p $D
    xvfb-run -a --server-args="-screen 0 320x240x24" \
      env SL_ROM=baserom.u.z64 SL_BOOT_LEVEL=34 SL_WINDOW=1 SL_WINDOW_SIZE=320x240 \
          SL_FOG=$MODE SL_INPUT="$RUN/input" SL_VIS="$RUN/input.vis" SL_RUN=0 \
          SL_FRAMES="$FRAMES" SL_SHOT=$D/f SL_SHOT_EVERY=1 \
          SL_HANG_SECONDS=2400 ./build/native/skeleton > $D/log 2>&1
done

python3 - "$TRIG" "$N" <<'PY'
import glob, sys
trig, n = int(sys.argv[1]), int(sys.argv[2])
def rd(p):
    d=open(p,'rb').read(); i=0; t=[]
    while len(t)<4:
        while d[i:i+1].isspace(): i+=1
        st=i
        while not d[i:i+1].isspace(): i+=1
        t.append(d[st:i])
    return d[i+1:]
off=sorted(glob.glob('/tmp/fogflicker-0/*.ppm'))
on =sorted(glob.glob('/tmp/fogflicker-1/*.ppm'))
have=min(len(off),len(on))
# captures are one per rendered frame; the trigger record is ~2 records/frame
need = trig // 2
print(f"  captured {have} frames; gas needs frame ~{need}")
if have < need + n:
    print("  REFUSING TO REPORT - the run never reached the gas."
          f" Re-run with more frames (this one used the given limit).")
    raise SystemExit(3)
vals=[]
for a,b in list(zip(off,on))[need:need+n]:
    A,B=rd(a),rd(b); m=min(len(A),len(B))
    vals.append(sum(1 for i in range(0,m,3) if A[i:i+3]!=B[i:i+3]))
print("  fog contribution, consecutive GASSED frames:")
print("   " + ", ".join(str(v) for v in vals))
mean=sum(vals)//max(len(vals),1)
sw=[abs(vals[i]-vals[i-1]) for i in range(1,len(vals))] or [0]
print(f"  mean={mean}  max adjacent swing={max(sw)}"
      f" ({100*max(sw)//max(mean,1)}% of mean)")
print("  STABLE" if max(sw) <= mean//3 else "  FLICKERING")
PY
