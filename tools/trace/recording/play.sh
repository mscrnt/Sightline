#!/bin/bash
# Launch a playable GoldenEye session with modern FPS controls.
#
#   play.sh                      just play (tune controls, learn a level)
#   play.sh --record FILE        play and capture the input stream
#
# Recording writes controller state per read via tools/trace/slinput. That stream
# can then be replayed headlessly to produce a trace - no controller involved.
#
# IN-GAME: set the control style to 1.2 Solitaire, or mouse-look will fight the
# default scheme. Pause -> Control Style -> 1.2 Solitaire.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# Separate profiles: a pad needs the stick split, keyboard needs mouse-look.
if ls /dev/input/js* >/dev/null 2>&1 && [ -r "$(ls /dev/input/js* 2>/dev/null | head -1)" ]; then
    HAVE_PAD=1
else
    HAVE_PAD=0
fi
if [ "$HAVE_PAD" = "1" ]; then
    CFGDIR="${SL_PLAY_CONFIG:-$HOME/.config/sightline-play-pad}"
    # Our mouse-look would overwrite the stick axes; disable it.
    export SL_MOUSE_LOOK=0
else
    CFGDIR="${SL_PLAY_CONFIG:-$HOME/.config/sightline-play}"
fi
ROM="${SL_ROM:-$REPO/build/u/ge007.u.z64}"
PY="${PYTHON:-$REPO/.venv/bin/python3}"
RECORD=""

while [ $# -gt 0 ]; do
    case "$1" in
        --record) RECORD="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

[ -f "$ROM" ] || { echo "ROM not found: $ROM (run 'make matching')" >&2; exit 1; }

# Report the input device situation rather than silently falling back to
# keyboard, which is what made this confusing to diagnose the first time.
if ls /dev/input/js* >/dev/null 2>&1; then
    if [ -r "$(ls /dev/input/js* | head -1)" ]; then
        echo "gamepad: detected"
    else
        echo "gamepad: present but NOT READABLE - run:" >&2
        echo "  sudo tools/trace/recording/attach_pad.sh <busid>" >&2
    fi
else
    echo "gamepad: none attached - keyboard/mouse only" >&2
    echo "  attach with: sudo tools/trace/recording/attach_pad.sh <busid>" >&2
fi

# Generate the FPS mapping once; edit the cfg afterwards to tune sensitivity.
if [ ! -f "$CFGDIR/mupen64plus.cfg" ]; then
    GP=""
    [ "$HAVE_PAD" = "1" ] && GP="--gamepad"
    "$PY" "$REPO/tools/trace/recording/fps_config.py" "$CFGDIR" --rom "$ROM" $GP
fi

# Always go through slinput: it chains to the SDL plugin and applies axis
# inversion (mouse look is mirrored on both axes against GoldenEye's analog
# aim, and a negative MouseSensitivity disables motion rather than inverting).
# Recording is then just "also write down what it returned".
make -C "$REPO/tools/trace/slinput" >/dev/null
INPUT_PLUGIN="$REPO/tools/trace/slinput/slinput.so"
if [ -n "$RECORD" ]; then
    export SL_INPUT_RECORD="$RECORD"
    echo "recording input to $RECORD"
fi

echo "controls: mouse=look  WASD=move  LMB=fire  RMB=aim  E=use  R=reload  Enter=start"
echo "in-game:  set Control Style to 1.2 Solitaire"

exec mupen64plus \
    --configdir "$CFGDIR" \
    --datadir /usr/share/games/mupen64plus \
    --resolution 960x720 \
    --gfx mupen64plus-video-glide64mk2 \
    --audio mupen64plus-audio-sdl \
    --input "$INPUT_PLUGIN" \
    --rsp mupen64plus-rsp-hle \
    "$ROM"
