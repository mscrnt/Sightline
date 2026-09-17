#!/bin/sh
# probe - drive the game and photograph it, unattended.
#
# The owner's idea, and the right one: "create a harness that makes you
# invincible so you can move around and take screenshots. Probably just need to
# load in and take screenshots and measurements."
#
# Every renderer bug in this project was found by looking at a frame, and every
# one of them was invisible to telemetry that read perfectly healthy. This makes
# looking cheap and repeatable: boot a level, survive it, drive Bond through a
# short scripted routine, and leave a directory of PNGs plus the stderr log.
#
#   tools/native/probe.sh                     facility, default routine
#   tools/native/probe.sh streets             another level
#   tools/native/probe.sh facility 60         capture every 60 frames
#   SL_PROBE_ROUTINE=look tools/native/probe.sh
#
# Routines:
#   walk   (default) forward, turn, forward, look around, fire
#   look   stand still and sweep the view - best for inspecting one spot
#   fire   stand still and shoot repeatedly - for muzzle flash and impact marks
#   still  no input at all - the level's own intro camera
#
# Invincibility is SL_TOUGH, which already existed (player.c:497 reads it into
# actual_health at spawn). Nothing new was invented for it, and it is a runtime
# knob that defaults to Rare's value.
#
# Output is ROM-derived pixels: it goes under /tmp and is never committed.
set -u
cd "$(dirname "$0")/../.."

LEVEL="${1:-facility}"
EVERY="${2:-45}"
ROUTINE="${SL_PROBE_ROUTINE:-walk}"
OUT="${SL_PROBE_OUT:-/tmp/probe-$LEVEL}"
SIZE="${SL_WINDOW_SIZE:-640x480}"
ROM="${SL_ROM:-baserom.u.z64}"

[ -f "$ROM" ] || { echo "probe: no ROM at '$ROM'" >&2; exit 1; }
[ -x build/native/skeleton ] || tools/native/build.sh

STAGE=$(.venv/bin/python3 - "$LEVEL" <<'PY' 2>/dev/null || true
import sys, pathlib, re
src = pathlib.Path("tools/trace/sltrace/levelboot.py").read_text()
m = re.search(r'"%s"\s*:\s*(\d+)' % re.escape(sys.argv[1]), src)
print(m.group(1) if m else "")
PY
)
[ -n "$STAGE" ] || { echo "probe: unknown level '$LEVEL'" >&2; exit 1; }

rm -rf "$OUT"; mkdir -p "$OUT"
SESSION="probe$$"
tmux kill-session -t "$SESSION" 2>/dev/null

# SL_TOUGH is the invincibility. SL_HANG_SECONDS bounds a hang so an unattended
# run cannot wedge; the routine below is the only thing that ends it normally.
tmux new-session -d -s "$SESSION" \
  "SL_ROM=$ROM SL_BOOT_LEVEL=$STAGE SL_BOOT_DIFFICULTY=1 \
   SL_WINDOW=1 SL_WINDOW_SIZE=$SIZE \
   SL_TOUGH=${SL_TOUGH:-1000} \
   SL_SHOT=$OUT/f SL_SHOT_EVERY=$EVERY \
   SL_HANG_SECONDS=400 \
   ./build/native/skeleton > $OUT/log 2>&1"

# Wait for the window rather than sleeping a guessed interval.
WID=""
i=0
while [ $i -lt 60 ]; do
    WID=$(xdotool search --name "Sightline" 2>/dev/null | head -1)
    [ -n "$WID" ] && break
    i=$((i + 1)); sleep 1
done
if [ -z "$WID" ]; then
    echo "probe: no window appeared - see $OUT/log" >&2
    tmux kill-session -t "$SESSION" 2>/dev/null
    exit 1
fi
# Let the intro camera hand over to first person before driving.
sleep 14
xdotool windowactivate "$WID" 2>/dev/null
sleep 1

turn() { i=0; while [ $i -lt "$2" ]; do xdotool mousemove_relative -- "$1" 0; sleep 0.05; i=$((i+1)); done; }
pitch() { i=0; while [ $i -lt "$2" ]; do xdotool mousemove_relative -- 0 "$1"; sleep 0.05; i=$((i+1)); done; }

case "$ROUTINE" in
  still) sleep 25 ;;
  look)  turn 25 24; sleep 1; turn -25 48; sleep 1; turn 25 24
         pitch -12 10; sleep 1; pitch 12 20; sleep 1; pitch -12 10; sleep 2 ;;
  fire)  i=0; while [ $i -lt 10 ]; do xdotool mousedown 1; sleep 0.5; xdotool mouseup 1; sleep 0.6; i=$((i+1)); done ;;
  # Bond spawns FACING the airlock door at point-blank range, so any routine
  # that inspects the view must turn away first or every frame is one wall
  # texture. Cost an inconclusive crosshair test before this existed.
  aim)   turn 25 24; sleep 1
         xdotool keydown w; sleep 1.2; xdotool keyup w; sleep 1
         echo "  (holding aim)"
         xdotool mousedown 3; sleep 6; xdotool mouseup 3; sleep 2 ;;
  *)     turn 25 20; sleep 1
         xdotool keydown w; sleep 2.5; xdotool keyup w; sleep 1
         turn -25 30; sleep 1
         xdotool keydown w; sleep 2; xdotool keyup w; sleep 1
         pitch -10 8; sleep 1; pitch 10 16; sleep 1
         xdotool mousedown 1; sleep 1.5; xdotool mouseup 1; sleep 2 ;;
esac

tmux kill-session -t "$SESSION" 2>/dev/null
sleep 1
for p in $(ps -eo pid,args | grep "native/skeleton" | grep -v grep | awk '{print $1}'); do
    kill "$p" 2>/dev/null
done

n=0
for f in "$OUT"/*.ppm; do
    [ -e "$f" ] || continue
    convert "$f" "${f%.ppm}.png" 2>/dev/null && n=$((n + 1))
done

uniq=$(md5sum "$OUT"/*.ppm 2>/dev/null | awk '{print $1}' | sort -u | wc -l)
echo "probe: $LEVEL (stage $STAGE), routine=$ROUTINE -> $OUT"
echo "  frames: $n   distinct: $uniq"
[ "$n" -gt 1 ] && [ "$uniq" -le 1 ] && \
    echo "  WARNING: every frame identical - stale surface, not a render"

# Rank by structure so the interesting frame is easy to find.
echo "  busiest frames:"
for f in "$OUT"/*.png; do
    [ -e "$f" ] || continue
    sd=$(convert "$f" -format "%[fx:standard_deviation]" info: 2>/dev/null)
    echo "    $sd $f"
done | sort -rn | head -4

echo "  faults: $(grep -c 'SIGSEGV\|HANG' "$OUT/log" 2>/dev/null)"
