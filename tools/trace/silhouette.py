#!/usr/bin/env python3
"""The orientation metric: how WIDE the lit silhouette is against how TALL.

WHAT THIS MEASURES, AND WHY IT IS NOT A SCREENSHOT COMPARISON
-------------------------------------------------------------
The Nintendo logo spins about Y. Seen face-on it is at its widest; seen
edge-on it is at its narrowest. Its HEIGHT is very nearly unchanged by that
rotation, because rotating about Y does not move anything vertically.

So the ratio

    aspect = (horizontal span of lit pixels) / (vertical span of lit pixels)

tracks the logo's ORIENTATION and cancels almost everything else. It is
invariant to scale, so the logo's zoom-in ramp does not contaminate it; and
invariant to resolution, so a 640x480 emulator frame and a 640x480 (or any
other size) GL back buffer produce comparable numbers.

That last property is the point. The two sides of this comparison are a
libretro core's XRGB8888 callback and a PPM written by glReadPixels. They can
never share a decoder - but they MUST share the decision logic, or "the ROM is
face-on and the native build is a quarter turn off" becomes a claim about two
different measurements. So the threshold, the sampling grid, the bounding box
and the ratio all live HERE, once, and each side supplies only an ``is_lit``
that knows its own pixel format.

WHAT THE NAMES MEAN
-------------------
``span_x`` is literally ``x1 - x0`` in PIXELS: the distance between the extreme
lit samples, not a width with a pixel added for the far edge, and not a
fraction of the frame. ``aspect`` is literally ``span_x / span_y`` in those
same pixel units.

This pedantry is earned. A previous instrument in this repository printed
``end - start`` and labelled it "total", and the wrong number was carried into
the next round as a measured fact. A metric whose name does not say exactly
what it computes is the same fault waiting to happen.

WHAT IT CANNOT DO
-----------------
``aspect`` is symmetric about the face-on axis: it cannot tell 30 degrees from
330 degrees, nor face-on-front from face-on-back. It is a PHASE metric, read
against a sequence, not an absolute heading. Read the curve, not one sample.

Usage, native side (PPMs written by SL_SHOT):

    python tools/trace/silhouette.py <dir-or-file>...
"""

from __future__ import annotations

import sys
from pathlib import Path

#: A sample counts as lit when any channel is ABOVE this. Shared with
#: bootprobe.frame_stats so "visible" means one thing across the oracle.
BLACK_LEVEL = 16

#: Sample every Nth column and row. On a 640x480 frame that is 80x60 = 4800
#: samples, which locates each silhouette edge to within 8 pixels - far finer
#: than the difference between face-on and edge-on, and cheap enough to run
#: over a thousand frames. BOTH sides must use this same grid or their spans
#: are not comparable.
SAMPLE_STRIDE = 8


def lit_bbox(width, height, is_lit, stride=SAMPLE_STRIDE):
    """Bounding box of the lit samples, and the orientation ratio.

    ``is_lit(x, y) -> bool`` decodes one pixel in whatever format the caller
    has. Everything that could make two callers disagree is decided here.

    Returns a dict. ``lit`` is ALWAYS present and is a count, so a frame that
    sampled nothing reports 0 explicitly rather than arriving as an absent
    field that reads like "nothing interesting".
    """
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    lit = 0
    samples = 0
    for y in range(0, height, stride):
        for x in range(0, width, stride):
            samples += 1
            if not is_lit(x, y):
                continue
            lit += 1
            if x < x0:
                x0 = x
            if x > x1:
                x1 = x
            if y < y0:
                y0 = y
            if y > y1:
                y1 = y

    if lit == 0:
        # -1, not 0: a zero span and an ABSENT span are different findings and
        # a black frame must not read as "an infinitely thin logo".
        return {"lit": 0, "samples": samples, "x0": -1, "x1": -1,
                "y0": -1, "y1": -1, "span_x": -1, "span_y": -1,
                "aspect": -1.0}

    span_x = x1 - x0
    span_y = y1 - y0
    return {"lit": lit, "samples": samples, "x0": x0, "x1": x1,
            "y0": y0, "y1": y1, "span_x": span_x, "span_y": span_y,
            "aspect": (span_x / span_y) if span_y > 0 else -1.0}


def read_ppm(path):
    """(width, height, rowbytes) from a binary P6 PPM. maxval must be 255.

    Written for the files SL_SHOT emits (sl_gfx_sdl.c), which are plain P6 at
    255 with no comments - but comments and split headers are handled anyway,
    because a parser that assumes its own writer's layout stops being a check
    on that writer.
    """
    data = Path(path).read_bytes()
    fields = []
    i = 0
    while len(fields) < 4:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while i < len(data) and data[i:i + 1] not in (b"\n", b"\r"):
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    i += 1                                    # single whitespace before pixels

    if fields[0] != b"P6":
        raise ValueError(f"{path}: not a binary PPM (magic {fields[0]!r})")
    width, height, maxval = (int(fields[1]), int(fields[2]), int(fields[3]))
    if maxval != 255:
        raise ValueError(f"{path}: maxval {maxval}, expected 255")
    need = width * height * 3
    px = data[i:i + need]
    if len(px) != need:
        raise ValueError(f"{path}: {len(px)} pixel bytes, expected {need}")
    return width, height, px


def ppm_metrics(path):
    """lit_bbox for one PPM, plus the mean and lit fraction bootprobe reports.

    The same three numbers the ROM side records, computed by the same
    ``lit_bbox``, so a row from either side can be read against the other.
    """
    width, height, px = read_ppm(path)

    def is_lit(x, y):
        o = (y * width + x) * 3
        return px[o] > BLACK_LEVEL or px[o + 1] > BLACK_LEVEL \
            or px[o + 2] > BLACK_LEVEL

    bb = lit_bbox(width, height, is_lit)

    total = 0
    n = 0
    for y in range(0, height, SAMPLE_STRIDE):
        base = y * width * 3
        for x in range(0, width, SAMPLE_STRIDE):
            o = base + x * 3
            total += px[o] + px[o + 1] + px[o + 2]
            n += 1
    bb["width"] = width
    bb["height"] = height
    bb["mean_luma"] = (total / (n * 3)) if n else 0.0
    bb["nonblack"] = (bb["lit"] / bb["samples"]) if bb["samples"] else 0.0
    return bb


def main(argv):
    paths = []
    for a in argv:
        p = Path(a)
        if p.is_dir():
            paths.extend(sorted(p.glob("*.ppm")))
        else:
            paths.append(p)
    if not paths:
        print("0 PPM files given - nothing measured")
        return 1

    print(f"{len(paths)} PPM file(s)")
    print(f"{'file':<28} {'lit':>6} {'nonblk':>8} {'mean':>8} "
          f"{'x0':>5} {'x1':>5} {'spanx':>6} {'spany':>6} {'aspect':>8}")
    for p in paths:
        m = ppm_metrics(p)
        print(f"{p.name:<28} {m['lit']:>6} {m['nonblack']:>8.5f} "
              f"{m['mean_luma']:>8.4f} {m['x0']:>5} {m['x1']:>5} "
              f"{m['span_x']:>6} {m['span_y']:>6} {m['aspect']:>8.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
