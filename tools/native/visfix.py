#!/usr/bin/env python3
"""Stamp and phase-correct a natively-recorded .vis sidecar.

A live capture recorded before the recorder was corrected writes its VI count
AFTER the pump's two advances for the frame, while replay consumes the target
BEFORE the game's sample consume - one frame of phase error.  Replaying one at
the default lag diverges: a one-unit bounding-box difference amplifies into a
different room within a thousand frames.

Recordings made after the fix carry the "SVI1" stamp and need nothing.  This
converts the ones made before it, so every recording replays with no special
setting.  The transform is exactly reversible (drop the stamp, add 2 back).

DO NOT run this on the emulator traces from nativediff.py - those are produced
at a different phase and are already correct.  It only touches unstamped files
and refuses anything already stamped.

    tools/native/visfix.py ~/.sightline/runs/*/input.vis
"""
import struct, sys, pathlib

MAGIC = 0x53564931


def convert(path: pathlib.Path) -> str:
    raw = path.read_bytes()
    if len(raw) < 4:
        return "too short - skipped"
    if struct.unpack(">I", raw[:4])[0] == MAGIC:
        return "already stamped - skipped"
    if len(raw) % 4:
        return "not a whole number of records - skipped"
    vals = struct.unpack(">%dI" % (len(raw) // 4), raw)
    out = struct.pack(">I", MAGIC) + b"".join(
        struct.pack(">I", v - 2 if v > 2 else 0) for v in vals)
    path.write_bytes(out)
    return "converted %d frames" % len(vals)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        raise SystemExit(2)
    for a in sys.argv[1:]:
        p = pathlib.Path(a)
        print("%-58s %s" % (a, convert(p) if p.is_file() else "missing"))
