#!/usr/bin/env python3
"""The cartridge half of an intro-camera comparison: read the shot, and CHOOSE it.

Two jobs, both of which a round comparing a SEEDED cinematic against the ROM
needs, and neither of which existed before B-100.

  read   Per video frame, out of RDRAM at symbols resolved from the matching
         build's link map: the camera mode and world position, the projection
         (fovy, aspect, znear, zfar), and optionally every object prop's world
         position. These are exactly the quantities the native probe in
         src/native/sl_game_query.c prints under SL_CAM_DBG, in the same units,
         so the two sides can be joined ON VALUE rather than on frame index.

  pick   Make the cartridge choose a NAMED intro camera, without modifying the
         ROM. bondview_r.c:357 picks with `randomGetNext() % count` and boss.c
         seeds the RNG from osGetCount, so the pick is a function of WHEN the
         level loads. Prepending k neutral records to a recorded input stream
         shifts the load by k frames and re-rolls it. The only thing that
         changes is the controller stream, which is an input.

WHY `pick` MATTERS. Natively the shot is selectable with SL_RNG_SEED, so a
round can look at whichever seeded camera the bug lives on. On the cartridge it
was not selectable at all, which meant the seeded shots - the whole of B-099
and B-100 - had no ROM oracle and every comparison silently answered a
different camera. Measured 2026-09-08: shift 2 on the `dam` stream lands on
Dam's truck camera at (17608.08, -59.79, 17408.57), the one B-100 is about.

A TRAP THIS FILE EXISTS SO NOBODY REPEATS. The recorded streams hold buttons
continuously, so ANY live record inside the intro can deliver a skip edge and
cut the shot short. --cut zeroes the stream from a record index onward, and it
must be set EARLIER than the intro rather than merely before the moment of
interest: on `dam`, cutting at 3602 left the truck camera up for 98 video
frames, and cutting at 3560 left the same camera up for 525. A shot that ends
early looks exactly like a shot the port gets wrong.

    tools/native/introcam.py pick --input dam --cut 3560 \
                                  --target 17608.08,-59.79,17408.57
    tools/native/introcam.py read --input dam --cut 3560 --shift 2 \
                                  --first 3400 --out /tmp/rom-cam.txt

Output is ROM-derived: it goes outside the repository and is never committed.
A libretro core may only be loaded once per process, so `pick` re-execs.
"""
import argparse
import math
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))

# VideoSettings (src/fr.h): u8 mode, s8[3], s16 x, s16 y, then the four floats.
VS_FOVY, VS_ASPECT, VS_ZNEAR, VS_ZFAR = 8, 12, 16, 20
# struct player (src/game/bondview.h): s32 cameramode @0, coord3d pos @0x04.
PL_MODE, PL_POS = 0x00, 0x04
# PropRecord (src/bondtypes.h): u8 type @0, union ptr @4, coord3d pos @8,
# prev @0x24. chrprop.c links head->tail, so walking g_ActivePropsTail through
# ->prev is the whole list; walking ->next off the TAIL reaches one element and
# looks like a healthy short list, which is how a first attempt at this
# concluded that no prop is ever near the camera.
PR_TYPE, PR_PTR, PR_POS, PR_PREV = 0x00, 0x04, 0x08, 0x24
OB_OBJ = 0x04                    # PropDefHeaderRecord is 4 bytes, then s16 obj
PROP_TYPE_OBJ = 1


def resolve_stream(name):
    """A stream NAME under tools/trace/inputs, or a path to an .input file."""
    p = pathlib.Path(name)
    if "/" in name or "\\" in name or name.endswith(".input"):
        return p
    return ROOT / "tools/trace/inputs" / (name + ".input")


def build_stream(src_input, out_dir, shift, cut):
    """Write a shifted, intro-input-suppressed copy of a stream, plus its save."""
    src = src_input.read_bytes()
    body = bytearray(b"\x00\x00\x00\x00" * shift + src)
    if cut is not None:
        for i in range(cut + shift, len(body) // 4):
            body[i * 4:i * 4 + 4] = b"\x00\x00\x00\x00"
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = "%s-s%d-c%s" % (src_input.stem, shift, cut)
    inp = out_dir / (stem + ".input")
    inp.write_bytes(bytes(body))
    eep = src_input.with_suffix(".eeprom")
    if eep.is_file():
        shutil.copyfile(eep, out_dir / (stem + ".eeprom"))
    return inp


def sample(inp, frames, first, every, want_props):
    """Run the cartridge and return [(frame, mode, pos, proj, [(obj, pos)])]."""
    from sltrace.emu_libretro import LibretroEmulator
    from sltrace.state import Memory
    from sltrace.symbols import SymbolTable

    syms = SymbolTable.from_map(ROOT / "build/u/ge007.u.map")
    a_player = syms.addr("g_CurrentPlayer")
    a_vi = syms.addr("g_ViBackData")
    a_tail = syms.addr("g_ActivePropsTail")

    eep = inp.with_suffix(".eeprom")
    emu = LibretroEmulator(str(ROOT / "baserom.u.z64"), input_stream=str(inp),
                           eeprom=str(eep) if eep.is_file() else None)
    rows = []

    def on_tick(index, fc, ram):
        if index < first or (index - first) % every:
            return
        m = Memory(ram)
        p, v = m.u32(a_player), m.u32(a_vi)
        if not (m.valid_ptr(p) and m.valid_ptr(v)):
            return
        try:
            mode = m.s32(p + PL_MODE)
            pos = tuple(round(m.f32(p + PL_POS + i * 4), 2) for i in range(3))
            proj = (m.f32(v + VS_FOVY), m.f32(v + VS_ASPECT),
                    m.f32(v + VS_ZNEAR), m.f32(v + VS_ZFAR))
        except ValueError:
            return
        # Boot leaves a valid-looking pointer over memory that is not a player
        # yet; the VALUES, not the pointer, are what say so.
        if any(abs(c) > 1e6 for c in pos):
            return
        props = []
        if want_props:
            pr, n = m.u32(a_tail), 0
            while m.valid_ptr(pr) and n < 4096:
                try:
                    if m.u8(pr + PR_TYPE) == PROP_TYPE_OBJ:
                        o = m.u32(pr + PR_PTR)
                        q = tuple(round(m.f32(pr + PR_POS + i * 4), 2)
                                  for i in range(3))
                        if all(abs(c) < 1e6 for c in q):
                            props.append((m.s16(o + OB_OBJ)
                                          if m.valid_ptr(o) else -1, q))
                    pr = m.u32(pr + PR_PREV)
                except ValueError:
                    break
                n += 1
        rows.append((index, mode, pos, proj, props))

    stats = emu.run(on_tick, lambda ram: 0, max_ticks=frames)
    emu.close()
    return rows, stats


def cmd_read(args):
    inp = resolve_stream(args.input)
    if args.shift or args.cut is not None:
        inp = build_stream(inp, pathlib.Path(args.workdir), args.shift, args.cut)
    rows, stats = sample(inp, args.frames, args.first, args.every, args.props)
    out = open(args.out, "w") if args.out else sys.stdout
    seen = set()
    for f, mode, pos, proj, props in rows:
        out.write("rom_cam: f%d mode=%d pos=%.2f,%.2f,%.2f near=%.2f far=%.2f "
                  "fovy=%.3f aspect=%.5f\n"
                  % (f, mode, pos[0], pos[1], pos[2], proj[2], proj[3],
                     proj[0], proj[1]))
        seen.add((round(proj[2], 3), round(proj[3], 3)))
        for oid, q in props:
            out.write("rom_cam:   f%d prop obj=%d pos=%.2f,%.2f,%.2f dist=%.2f\n"
                      % (f, oid, q[0], q[1], q[2], math.dist(q, pos)))
    if args.out:
        out.close()
    print("introcam: %d frames, %d rows -> %s"
          % (stats["frames"], len(rows), args.out or "stdout"), file=sys.stderr)
    # An instrument that cannot vary is not measuring anything: name what it saw.
    print("introcam: distinct (near,far) pairs: %s" % sorted(seen),
          file=sys.stderr)
    return 0


def first_cinematic(path, target):
    """The first cinematic camera position in a `read` dump, and whether it hits."""
    first, hit = None, False
    for line in pathlib.Path(path).read_text().splitlines():
        if " mode=1 " not in line:
            continue
        p = tuple(float(v) for v in line.split("pos=")[1].split(" ")[0].split(","))
        if first is None:
            first = p
        if math.dist(p, target) < 1.0:
            hit = True
            break
    return first, hit


def cmd_pick(args):
    """Re-exec ourselves per shift: a libretro core loads once per process."""
    target = tuple(float(v) for v in args.target.split(","))
    src = resolve_stream(args.input)
    work = pathlib.Path(args.workdir)
    for k in range(args.max_shift + 1):
        inp = build_stream(src, work, k, args.cut)
        dump = work / ("pick%d.txt" % k)
        r = subprocess.run([sys.executable, __file__, "read",
                            "--input", str(inp), "--frames", str(args.frames),
                            "--first", str(args.first), "--every", "8",
                            "--out", str(dump)],
                           cwd=str(ROOT), capture_output=True, text=True)
        if r.returncode != 0:
            print("introcam: shift %d failed:\n%s" % (k, r.stderr),
                  file=sys.stderr)
            return 1
        first, hit = first_cinematic(dump, target)
        print("shift=%-3d first cinematic camera %s%s"
              % (k, first, "   <== TARGET" if hit else ""))
        if hit:
            print("introcam: use %s" % inp)
            return 0
    print("introcam: target not reached within %d shifts" % args.max_shift)
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--input", required=True,
                        help="stream NAME under tools/trace/inputs, or a path")
    common.add_argument("--frames", type=int, default=4600)
    common.add_argument("--first", type=int, default=3400)
    common.add_argument("--cut", type=int, default=None,
                        help="zero the stream from this record onward, so no "
                             "held button can skip an intro screen. Set it "
                             "EARLIER than the intro - see the docstring.")
    common.add_argument("--workdir", default=None,
                        help="where derived streams go; ROM-derived, so it "
                             "defaults outside the repository")

    r = sub.add_parser("read", parents=[common])
    r.add_argument("--every", type=int, default=1)
    r.add_argument("--shift", type=int, default=0)
    r.add_argument("--props", action="store_true",
                   help="also dump every object prop's world position")
    r.add_argument("--out", default=None)
    r.set_defaults(fn=cmd_read)

    p = sub.add_parser("pick", parents=[common])
    p.add_argument("--target", required=True, metavar="X,Y,Z",
                   help="the camera world position to land on, as measured "
                        "natively under SL_CAM_DBG")
    p.add_argument("--max-shift", type=int, default=12)
    p.set_defaults(fn=cmd_pick)

    args = ap.parse_args()
    if args.workdir is None:
        import tempfile
        args.workdir = str(pathlib.Path(tempfile.gettempdir())
                           / "sightline-introcam")
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
