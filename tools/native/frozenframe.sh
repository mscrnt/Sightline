#!/bin/sh
# Render the SAME frame of a deterministic replay N times and require every
# capture to be byte-identical.
#
# The flicker in B-045 was only ever visible to a human playing the game, so
# every candidate fix cost the owner a playthrough and nine of them were
# refuted that way. Replay is deterministic: same input, same frame, same
# state. If two runs of one binary disagree on one frame, that is a renderer
# state leak, and it is a test failure rather than something someone has to
# notice.
#
#   tools/native/frozenframe.sh <run-dir> <frame> [runs] [env...]
#
# SL_FRAMES COUNTS AT TWICE THE RENDERED-FRAME RATE, so reaching rendered
# frame N needs SL_FRAMES ~ 2N. The first version passed FRAME+2 and therefore
# never got past the opening view: it compared three renders of frame 0, which
# are trivially identical, and reported STABLE. It even returned the same hash
# after a fix that provably changes the image - which is what gave it away.
#
# A gate that cannot fail is worse than no gate. Every capture is checked
# against the requested frame below.
#
# ROM-derived pixels: output lives under /tmp and is never committed.
set -u
RUN="${1:?usage: frozenframe.sh <run-dir> <frame> [runs]}"
FRAME="${2:?}"
N="${3:-3}"
OUT=/tmp/frozenframe
rm -rf $OUT; mkdir -p $OUT
cd "$(dirname "$0")/../.." || exit 1

i=1
while [ "$i" -le "$N" ]; do
    xvfb-run -a --server-args="-screen 0 640x480x24" \
      env SL_ROM=baserom.u.z64 SL_BOOT_LEVEL=34 SL_WINDOW=1 SL_WINDOW_SIZE=640x480 \
          SL_INPUT="$RUN/input" SL_VIS="$RUN/input.vis" SL_RUN=0 \
          SL_SHOT="$OUT/r$i-" SL_SHOT_EVERY="$FRAME" \
          SL_FRAMES="$((FRAME * 2 + 40))" SL_HANG_SECONDS=1500 \
          ${SL_FOG:+SL_FOG=$SL_FOG} ${SL_FOG_VIZ:+SL_FOG_VIZ=$SL_FOG_VIZ} \
          ./build/native/skeleton > "$OUT/r$i.log" 2>&1
    i=$((i + 1))
done

echo "frame $FRAME, $N runs of one binary:"
FAIL=0
for i in $(seq 1 "$N"); do
    F=$(ls "$OUT/r$i-"*.ppm 2>/dev/null | tail -1)
    [ -n "$F" ] || { echo "  run $i: NO FRAME CAPTURED"; FAIL=1; continue; }
    case "$(basename "$F")" in
        *-00000.ppm) echo "  run $i: only frame 0 captured - the run never"\
                          " reached frame $FRAME"; FAIL=1; continue ;;
    esac
    echo "  run $i: $(md5sum "$F" | cut -c1-16)  $(basename "$F")"
done
U=$(for i in $(seq 1 "$N"); do
        F=$(ls "$OUT/r$i-"*.ppm 2>/dev/null | tail -1)
        [ -n "$F" ] && md5sum "$F" | cut -d' ' -f1
    done | sort -u | wc -l)
if [ "$U" = "1" ] && [ "$FAIL" = "0" ]; then
    echo "  STABLE - every run byte-identical"
else
    echo "  NONDETERMINISTIC - $U distinct images across $N runs"
    exit 1
fi
