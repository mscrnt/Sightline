#!/bin/sh
# Launch the native build as something you can play.
#
# Everything here is a default, not a policy: each value is overridable from
# the environment, and the flags it sets are the ones a player wants rather
# than the ones trace replay wants (a window, real-time pacing, no frame
# limit, live keyboard and mouse).
#
# No assets are read from the repo. The level data, models and textures all
# come out of your own ROM at runtime, which is why SL_ROM is required and
# why the ROM is never committed.
set -e
cd "$(dirname "$0")/../.."

ROM="${SL_ROM:-baserom.u.z64}"
LEVEL="${SL_LEVEL:-facility}"
DIFFICULTY="${SL_DIFFICULTY:-1}"
SIZE="${SL_WINDOW_SIZE:-960x720}"

if [ ! -f "$ROM" ]; then
    echo "sightline: no ROM at '$ROM'." >&2
    echo "  Point SL_ROM at your own GoldenEye (U) copy. Nothing ROM-derived" >&2
    echo "  ships in this repository, so there is no fallback to fall back to." >&2
    exit 1
fi

# name -> stage number through the ONE resolver, so the demo, the health gate
# and the determinism corpus cannot drift apart. Two copies of this lookup is
# how health.sh came to pass a name the binary never reads.
STAGE=$(${PY:-.venv/bin/python3} tools/native/levelstage.py "$LEVEL" 2>/dev/null || true)
if [ -z "$STAGE" ]; then
    echo "sightline: unknown level '$LEVEL'." >&2
    echo "  Known names are the keys in tools/trace/sltrace/levelboot.py" >&2
    echo "  (facility, dam, runway, surface, bunker1, silo, archives, ...)." >&2
    exit 1
fi

[ -x build/native/skeleton ] || tools/native/build.sh

# The cartridge save. Without one the game starts from defaults every launch
# and nothing changed in the options survives. The native cold-start style is
# 1.2 Solitaire (see sl_style_load); an existing <save>.style sidecar wins over
# it, so the player's own choice is never overridden. Lives outside the repo:
# it is player data,
# and rule 2 keeps ROM-derived bytes out of the tree regardless.
SAVE="${SL_SAVE:-${XDG_DATA_HOME:-$HOME/.local/share}/sightline/eeprom.bin}"
mkdir -p "$(dirname "$SAVE")" 2>/dev/null || true

cat <<EOF
Sightline - $LEVEL (stage $STAGE), difficulty $DIFFICULTY

  mouse         CLICK IN THE WINDOW to capture the pointer
  move          W A S D  /  left stick
  look          mouse    /  right stick
  fire          left mouse, R1 or R2  aim   right mouse, Q, L1 or L2
  use / reload  E or Space, pad A or B
  next weapon   R, pad X
  watch / pause Tab or Esc, pad Start
  d-pad         arrow keys
  menus         W A S D or the arrows move, the wheel steps up and down,
                Enter selects, Esc backs out
  quit          close the window (Alt+F4)

THE POINTER IS NOT TAKEN UNTIL YOU CLICK IN THE WINDOW. That first click
only captures - it does not fire. Opening a menu gives the pointer back
and closing it takes it again with no second click; alt-tabbing away
releases it for good until you click in again. SL_MOUSE=0 disables
capture entirely and leaves the keyboard working.

Esc does NOT quit. Tab and Esc are the only keys that open the watch -
Enter is confirm and nothing else. Closing the window is the way out.

KEYBOARD AND MOUSE feel the same whichever control style is selected. They
do not go through the N64 stick for movement: WASD and the mouse drive the
game's own movement channels directly, so mouse look is analog on both
axes under 1.1 Honey exactly as it is under 1.2 Solitaire.

THE GAMEPAD is unchanged and still follows the control style set in the
in-game options, with all eight switching live. Each 1.x style is a
compromise the N64 pad forced - one analog stick has to choose. 1.2
Solitaire puts looking on the stick and walking on the C buttons; 1.1
Honey does the reverse and has no free pitch axis at all, so looking up
and down is digital except while aiming. For modern twin-stick on a pad
pick 2.2 Galore: it is the only style that gives analog look on BOTH axes
at once, because it reads a second controller, which the native build
synthesises.

Press nothing at the start. The level opens on its own intro camera, then
hands you first person facing the airlock door. Tab opens Bond's watch,
it is not needed to begin.

  SL_LEVEL=dam SL_DIFFICULTY=2 $0     other level / difficulty
  SL_MOUSE_SENS=10                    faster look (default 6)
  SL_MOUSE_INVERT=1                   invert pitch
  SL_MOUSE=0                          never capture the pointer
  SL_MOUSE_WARP=1                     warp-based relative mode (diagnostic only)
  SL_MOUSE_DX_SIGN=1                  un-invert mouse X (not needed normally)
  SL_MOUSE_DY_SIGN=1                  un-invert mouse Y (not needed normally)
  SL_LOOK_INVERT=1                    invert pitch on every device
  SL_FPS=30                           pace to something other than 60

EOF

exec env \
    SL_ROM="$ROM" \
    SL_BOOT_LEVEL="$STAGE" \
    SL_BOOT_DIFFICULTY="$DIFFICULTY" \
    SL_WINDOW=1 \
    SL_WINDOW_SIZE="$SIZE" \
    SL_EEPROM_RW="$SAVE" \
    ./build/native/skeleton "$@"
