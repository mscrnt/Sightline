#!/usr/bin/env python3
"""Generate a modern-FPS keyboard+mouse mapping for recording sessions.

GoldenEye has no native mouse support, so this leans on the shape of its own
control schemes. Set the in-game control style to **1.2 Solitaire**, where the
analog stick LOOKS and the C-buttons MOVE. Point the mouse at the stick and WASD
at the C-buttons and you have conventional FPS controls.

Any other style will feel wrong - 1.1 Honey (the default) puts turning on the
stick and strafing on the C-buttons, which fights mouse-look.

Recording only. Replay reads a recorded stream and needs no input device.

usage: fps_config.py <config-dir> [--sensitivity X,Y]
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# SDL keysym codes as used by mupen64plus-input-sdl.
K = {"w": 119, "a": 97, "s": 115, "d": 100, "e": 101, "r": 114, "f": 102,
     "q": 113, "space": 32, "return": 13, "escape": 27, "tab": 9,
     "lshift": 304, "lctrl": 306, "1": 49, "2": 50}

# Dual-stick pad mapping for GoldenEye control style 1.2 Solitaire.
#
# The N64 pad has ONE stick, so a modern pad needs the split the community
# settled on: right stick drives the N64 analog stick (look), and the left
# stick is mapped onto the C-buttons (move). The C-buttons are digital, so the
# left stick is bound as axis DIRECTIONS rather than as an analog axis.
#
# DualSense layout, MEASURED with nothing pressed:
#   0 left stick X    1 left stick Y
#   2 right stick X   5 right stick Y
#   3 L2, 4 R2 - BOTH rest at -32768, so pressed is the POSITIVE direction.
#     Binding a trigger as axis(N-) makes it permanently active at rest, which
#     shows up in game as the aim crosshair stuck on screen.
#   D-pad is a HAT (1 hat, 15 buttons), not buttons - hence hat(0 Up) etc.
PAD_MAPPING = {
    "X Axis":     "axis(2-,2+)",       # right stick X -> turn
    "Y Axis":     "axis(5-,5+)",       # right stick Y -> aim
    "C Button U": "axis(1-)",          # left stick up    -> forward
    "C Button D": "axis(1+)",          # left stick down  -> back
    "C Button L": "axis(0-)",          # left stick left  -> strafe left
    "C Button R": "axis(0+)",          # left stick right -> strafe right
    "Z Trig":     "axis(4+)",          # R2 pressed -> fire
    "R Trig":     "axis(3+)",          # L2 pressed -> aim mode
    "A Button":   "button(0)",         # cross
    "B Button":   "button(2)",         # square
    "L Trig":     "button(4)",         # L1
    "Start":      "button(9)",         # options
    "DPad U":     "hat(0 Up)",
    "DPad D":     "hat(0 Down)",
    "DPad L":     "hat(0 Left)",
    "DPad R":     "hat(0 Right)",
}

PAD_SETTINGS = {
    "mode": "0",            # manual: autoconfig would undo the stick split
    "device": "0",
    "name": '"Sony Interactive Entertainment DualSense Wireless Controller"',
    "plugged": "True",
    "plugin": "2",
    "mouse": "False",
    "AnalogDeadzone": '"3000,3000"',
    "AnalogPeak": '"32000,32000"',
}

# GoldenEye control style 1.2 Solitaire: stick looks, C-buttons move.
MAPPING = {
    # movement -> C buttons
    "C Button U": f'key({K["w"]})',
    "C Button D": f'key({K["s"]})',
    "C Button L": f'key({K["a"]})',
    "C Button R": f'key({K["d"]})',
    # weapons and actions
    "Z Trig":     "mouse(1)",          # left click - fire
    "R Trig":     "mouse(3)",          # right click - aim mode
    "B Button":   f'key({K["r"]})',    # reload / context action
    "A Button":   f'key({K["e"]})',    # use / activate
    "L Trig":     f'key({K["lshift"]})',
    "Start":      f'key({K["return"]})',
    # weapon cycling on the d-pad, reachable without leaving WASD
    "DPad U":     f'key({K["1"]})',
    "DPad D":     f'key({K["2"]})',
    "DPad L":     f'key({K["q"]})',
    "DPad R":     f'key({K["f"]})',
    "Mempak switch": f'key({K["tab"]})',
    "Rumblepak switch": f'key({K["escape"]})',
}

# Keyboard/mouse fallback mapping. With a gamepad attached the SDL plugin
# autoconfigures it from InputAutoCfg.ini, which is what we want - GoldenEye
# expects an analog stick, and a real pad needs no translation layer.
SETTINGS = {
    "mode": "0",              # fully manual: do not let auto-config overwrite us
    "device": "-1",
    "plugged": "True",
    "plugin": "2",
    "mouse": "True",          # enables relative mouse motion on the analog axes
}


def ensure_defaults(cfgdir: Path, rom: Path, datadir: str) -> Path:
    """Let mupen64plus write a default config we can patch."""
    cfg = cfgdir / "mupen64plus.cfg"
    if cfg.is_file():
        return cfg
    cfgdir.mkdir(parents=True, exist_ok=True)
    # mupen64plus writes the config during startup and then runs the game
    # forever, so a timeout here is the expected outcome, not a failure.
    try:
        subprocess.run(
            ["mupen64plus", "--configdir", str(cfgdir), "--datadir", datadir,
             "--gfx", "dummy", "--audio", "dummy", "--input",
             "mupen64plus-input-sdl", "--rsp", "mupen64plus-rsp-hle", str(rom)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=15, check=False)
    except subprocess.TimeoutExpired:
        pass
    if not cfg.is_file():
        sys.exit(f"mupen64plus did not produce {cfg}")
    return cfg


def patch(cfg: Path, sensitivity: str, gamepad: bool = False) -> None:
    text = cfg.read_text()
    start = text.index("[Input-SDL-Control1]")
    nxt = text.find("\n[", start + 1)
    end = len(text) if nxt == -1 else nxt
    section = text[start:end]

    def set_key(sec: str, key: str, value: str) -> str:
        pat = re.compile(rf'^({re.escape(key)}\s*=\s*).*$', re.M)
        if pat.search(sec):
            return pat.sub(lambda m: m.group(1) + value, sec, count=1)
        return sec.rstrip("\n") + f"\n{key} = {value}\n"

    settings = PAD_SETTINGS if gamepad else SETTINGS
    mapping = PAD_MAPPING if gamepad else MAPPING
    for k, v in settings.items():
        section = set_key(section, k, v)
    if not gamepad:
        section = set_key(section, "MouseSensitivity", f'"{sensitivity}"')
    for k, v in mapping.items():
        section = set_key(section, k, f'"{v}"')
    if not gamepad:
        # Mouse drives the analog axes; unbind so keys cannot fight it.
        section = set_key(section, "X Axis", '""')
        section = set_key(section, "Y Axis", '""')

    cfg.write_text(text[:start] + section + text[end:])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("configdir")
    ap.add_argument("--rom", default="build/u/ge007.u.z64")
    ap.add_argument("--datadir", default="/usr/share/games/mupen64plus")
    # Positive. A negative value appears to disable mouse motion in this
    # plugin rather than invert it. If look feels inverted, check GoldenEye's
    # own aim-invert setting first - and confirm the control style is 1.2
    # Solitaire, because in 1.1 the stick drives move/turn and every axis
    # reads as "backwards".
    ap.add_argument("--sensitivity", default="2.00,2.00")
    ap.add_argument("--gamepad", action="store_true",
                    help="dual-stick pad profile instead of keyboard+mouse")
    args = ap.parse_args()

    cfg = ensure_defaults(Path(args.configdir), Path(args.rom), args.datadir)
    patch(cfg, args.sensitivity, gamepad=args.gamepad)
    print(f"mapping written to {cfg}")
    if args.gamepad:
        print("  right stick  look        (set control style to 1.2 Solitaire)")
        print("  left stick   move/strafe")
        print("  R2 / L2      fire / aim")
        print("  cross        use")
        print("  square       reload")
    else:
        print("  mouse        look        (set control style to 1.2 Solitaire)")
        print("  WASD         move/strafe")
        print("  LMB / RMB    fire / aim")
    return 0


if __name__ == "__main__":
    sys.exit(main())
