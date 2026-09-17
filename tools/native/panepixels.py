#!/usr/bin/env python3
"""Characterise a region of a captured frame. Pixels only - it names no effect.

B-051 asks what the cartridge DRAWS on a tinted-glass pane that we draw as a
flat fill. "Glare", "tint", "reflection" and "the room behind is just dark" all
predict different NUMBERS, so the numbers come first and the name comes after.

Per region it reports:
  mean R,G,B and mean luminance          - is it brighter or darker
  p05 / p50 / p95 luminance              - is there a bright part at all
  luminance stddev                       - is there ANY variation
  local deviation (3x3)                  - is the variation STRUCTURE or noise
  edge energy                            - are there shapes with borders
  distinct colours                       - a flat fill has very few

Regions are given in pixels as x0,y0,x1,y1 (x1/y1 exclusive, top-left origin,
matching the PPMs that romshot.py, panescope.py and sl_gfx_sdl.c all write).

    tools/native/panepixels.py --stats shot.ppm \\
        --region pane=250,180,390,300 --region wall=60,180,200,300

    tools/native/panepixels.py --png shot.ppm shot.png       # to look at it
    tools/native/panepixels.py --grid shot.ppm grid.png      # with a ruler on it

No ROM-derived pixels are written anywhere but /tmp (project rule 2).
"""

from __future__ import annotations

import argparse
import math
import pathlib
import struct
import sys
import zlib


def read_ppm(path) -> tuple[int, int, bytes]:
    """P6 only - that is what every capture path in this repo writes."""
    raw = pathlib.Path(path).read_bytes()
    if not raw.startswith(b"P6"):
        raise ValueError(f"{path}: not a P6 PPM")
    # header: P6 <ws> W <ws> H <ws> MAX <single ws> data
    fields, i = [], 2
    while len(fields) < 3:
        while i < len(raw) and raw[i:i + 1].isspace():
            i += 1
        if raw[i:i + 1] == b"#":
            while i < len(raw) and raw[i] != 0x0A:
                i += 1
            continue
        j = i
        while j < len(raw) and not raw[j:j + 1].isspace():
            j += 1
        fields.append(int(raw[i:j]))
        i = j
    i += 1
    w, h, mx = fields
    if mx != 255:
        raise ValueError(f"{path}: maxval {mx}, only 255 is handled")
    return w, h, raw[i:i + w * h * 3]


def write_png(path, w: int, h: int, data: bytes) -> None:
    """Minimal RGB8 PNG, so a capture can actually be LOOKED at."""
    rows = b"".join(b"\x00" + data[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(rows, 6))
           + chunk(b"IEND", b""))
    pathlib.Path(path).write_bytes(png)


def lum(r: int, g: int, b: int) -> float:
    """Rec.601 luma. The choice matters less than using ONE of them
    everywhere, so every number in a comparison is the same quantity."""
    return 0.299 * r + 0.587 * g + 0.114 * b


def analyse(w: int, h: int, px: bytes, rect) -> dict:
    x0, y0, x1, y1 = rect
    x0 = max(0, x0); y0 = max(0, y0); x1 = min(w, x1); y1 = min(h, y1)
    if x1 <= x0 or y1 <= y0:
        raise ValueError(f"empty region {rect} in a {w}x{h} image")
    rw, rh = x1 - x0, y1 - y0
    L = [[0.0] * rw for _ in range(rh)]
    sr = sg = sb = 0
    colours = set()
    for yy in range(rh):
        base = ((y0 + yy) * w + x0) * 3
        row = px[base:base + rw * 3]
        Lr = L[yy]
        for xx in range(rw):
            r, g, b = row[xx * 3], row[xx * 3 + 1], row[xx * 3 + 2]
            sr += r; sg += g; sb += b
            Lr[xx] = lum(r, g, b)
            colours.add((r, g, b))
    n = rw * rh
    flat = [v for r in L for v in r]
    flat_sorted = sorted(flat)
    mean = sum(flat) / n
    var = sum((v - mean) ** 2 for v in flat) / n

    # Local deviation: |pixel - mean of its 3x3|. This is the number that
    # separates STRUCTURE from a smooth gradient. A flat fill gives ~0; a
    # smooth ramp across the region gives a large stddev but a tiny local
    # deviation; a texture or visible shapes give both.
    ld = 0.0
    ld_n = 0
    edge = 0.0
    for yy in range(1, rh - 1):
        for xx in range(1, rw - 1):
            s = 0.0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    s += L[yy + dy][xx + dx]
            ld += abs(L[yy][xx] - s / 9.0)
            ld_n += 1
            gx = L[yy][xx + 1] - L[yy][xx - 1]
            gy = L[yy + 1][xx] - L[yy - 1][xx]
            edge += math.sqrt(gx * gx + gy * gy)
    # Block statistics: the mean of each 8x8 tile, then the spread of those
    # means. This is the number that survives the N64's 16-bit dither.
    #
    # It matters here. RGBA5551 with dithering makes neighbouring cartridge
    # pixels differ by a few levels EVERYWHERE, which inflates local_dev and
    # the distinct-colour count on the cartridge side of any comparison
    # against a 32-bit native framebuffer. Reading either of those as
    # "the cartridge has more detail" would be reading the dither. block_std
    # cannot be produced by dither: averaging 64 dithered pixels removes it.
    B = 8
    blocks = []
    for by in range(0, rh - B + 1, B):
        for bx in range(0, rw - B + 1, B):
            t = 0.0
            for yy in range(by, by + B):
                Lr = L[yy]
                for xx in range(bx, bx + B):
                    t += Lr[xx]
            blocks.append(t / (B * B))
    if blocks:
        bm = sum(blocks) / len(blocks)
        bstd = math.sqrt(sum((v - bm) ** 2 for v in blocks) / len(blocks))
        brange = max(blocks) - min(blocks)
    else:
        bstd = brange = 0.0
    return {
        "rect": (x0, y0, x1, y1),
        "block_std": bstd,
        "block_range": brange,
        "blocks": len(blocks),
        "px": n,
        "mean_rgb": (sr / n, sg / n, sb / n),
        "mean_lum": mean,
        "p05": flat_sorted[int(0.05 * (n - 1))],
        "p50": flat_sorted[int(0.50 * (n - 1))],
        "p95": flat_sorted[int(0.95 * (n - 1))],
        "min": flat_sorted[0],
        "max": flat_sorted[-1],
        "std": math.sqrt(var),
        "local_dev": (ld / ld_n) if ld_n else 0.0,
        "edge": (edge / ld_n) if ld_n else 0.0,
        "colours": len(colours),
    }


def fmt(name: str, a: dict) -> str:
    r, g, b = a["mean_rgb"]
    x0, y0, x1, y1 = a["rect"]
    return (f"{name:<22s} {x0:4d},{y0:4d}-{x1:4d},{y1:4d} "
            f"rgb {r:6.1f},{g:6.1f},{b:6.1f}  L {a['mean_lum']:6.2f}  "
            f"p05/50/95 {a['p05']:6.1f}/{a['p50']:6.1f}/{a['p95']:6.1f}  "
            f"std {a['std']:6.2f}  blkstd {a['block_std']:6.2f}  "
            f"blkrange {a['block_range']:6.1f}  locdev {a['local_dev']:6.3f}  "
            f"edge {a['edge']:6.3f}  colours {a['colours']:5d}")


def grid_overlay(w: int, h: int, px: bytes, step: int = 64) -> bytes:
    """A ruler burned into a copy, so a region can be named from an image
    rather than from a guess about where the pane is."""
    out = bytearray(px)
    for x in range(0, w, step):
        for y in range(h):
            i = (y * w + x) * 3
            out[i] = 255; out[i + 1] = 0; out[i + 2] = 0
    for y in range(0, h, step):
        for x in range(w):
            i = (y * w + x) * 3
            out[i] = 255; out[i + 1] = 0; out[i + 2] = 0
    return bytes(out)


def crop_lum(w: int, h: int, px: bytes, rect) -> tuple[int, int, list]:
    x0, y0, x1, y1 = rect
    x0 = max(0, x0); y0 = max(0, y0); x1 = min(w, x1); y1 = min(h, y1)
    rw, rh = x1 - x0, y1 - y0
    if rw <= 0 or rh <= 0:
        raise ValueError(f"empty region {rect} in {w}x{h}")
    L = []
    for yy in range(rh):
        base = ((y0 + yy) * w + x0) * 3
        row = px[base:base + rw * 3]
        L.append([lum(row[i * 3], row[i * 3 + 1], row[i * 3 + 2])
                  for i in range(rw)])
    return rw, rh, L


def resample(rw: int, rh: int, L, gw: int, gh: int,
             cap: int = 1 << 30) -> list:
    """Area-average onto a gw x gh grid.

    ASPECT IS PRESERVED BY THE CALLER, deliberately. An earlier proxy test
    resampled a 122x83 pane onto a SQUARE grid and then transposed it, which
    distorts in a way no real generator swap does - the distortion, not the
    axis assignment, could have produced the correlation. Nothing here
    transposes anything; both sides go through the identical grid, so the
    resampling is common-mode and cancels.

    `cap` bounds how many samples are taken per grid cell in each axis. It is
    a SPEED control for the pairing search, which resamples thousands of
    frames; it must be left unbounded wherever a number is being reported,
    because a capped cell is a point sample of a dithered image and its value
    moves with the cap."""
    out = []
    for gy in range(gh):
        y0 = gy * rh // gh
        y1 = max(y0 + 1, (gy + 1) * rh // gh)
        ys = range(y0, y1, max(1, (y1 - y0) // cap))
        row = []
        for gx in range(gw):
            x0 = gx * rw // gw
            x1 = max(x0 + 1, (gx + 1) * rw // gw)
            xs = range(x0, x1, max(1, (x1 - x0) // cap))
            s = 0.0
            n = 0
            for y in ys:
                Ly = L[y]
                for x in xs:
                    s += Ly[x]
                    n += 1
            row.append(s / n)
        out.append(row)
    return out


def pearson(a, b) -> float:
    fa = [v for r in a for v in r]
    fb = [v for r in b for v in r]
    n = len(fa)
    ma = sum(fa) / n
    mb = sum(fb) / n
    sa = sum((v - ma) ** 2 for v in fa)
    sb = sum((v - mb) ** 2 for v in fb)
    if sa <= 0.0 or sb <= 0.0:
        return float("nan")     # one side is FLAT - r is undefined, not 0
    cov = sum((fa[i] - ma) * (fb[i] - mb) for i in range(n))
    return cov / math.sqrt(sa * sb)


def diffbox(pa, pb) -> dict:
    wa, ha, a = read_ppm(pa)
    wb, hb, b = read_ppm(pb)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"size mismatch {wa}x{ha} vs {wb}x{hb}")
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    n = 0
    for y in range(ha):
        base = y * wa * 3
        ra = a[base:base + wa * 3]
        rb = b[base:base + wa * 3]
        if ra == rb:
            continue
        for x in range(wa):
            i = x * 3
            if ra[i:i + 3] != rb[i:i + 3]:
                n += 1
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
    return {"w": wa, "h": ha, "n": n,
            "rect": (x0, y0, x1 + 1, y1 + 1) if n else None}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--diffbox", nargs=2, default=None, metavar=("A", "B"),
                    help="count differing pixels and print their bounding "
                         "box. This is how a surface is LOCATED: two arms "
                         "that differ only in one draw's vertex colour differ "
                         "on exactly that draw's pixels, so the box is "
                         "measured rather than eyeballed off a screenshot.")
    ap.add_argument("--containment", nargs=2, default=None, metavar=("A", "B"),
                    help="with --exclude: report differing pixels INSIDE and "
                         "OUTSIDE the excluded rect. Outside must be 0 for a "
                         "change to be contained.")
    ap.add_argument("--exclude", default="",
                    help="x0,y0,x1,y1 for --containment")
    ap.add_argument("--corr", nargs=2, default=None, metavar=("A", "B"),
                    help="structural correlation between a region of A and a "
                         "region of B, at several grid resolutions")
    ap.add_argument("--rect-a", default="", help="x0,y0,x1,y1 in A")
    ap.add_argument("--rect-b", default="", help="x0,y0,x1,y1 in B")
    ap.add_argument("--cols", default="6,8,12,16,24",
                    help="grid COLUMN counts for --corr; rows follow the "
                         "region's own aspect so nothing is distorted")
    ap.add_argument("--pair", nargs="+", default=None,
                    help="REF.ppm CAND.ppm... - rank candidate frames by how "
                         "well they match the reference OUTSIDE --rect-a. "
                         "This is how a cartridge frame is matched to a "
                         "native frame WITHOUT assuming they are the same "
                         "frame number: the two runs are different ROM "
                         "images and diverge, so the pairing has to be "
                         "MEASURED on geometry both sides agree about, and "
                         "the surface under investigation is excluded so it "
                         "cannot select its own answer. If the best match is "
                         "poor, there is no matched pair and no comparison "
                         "should be reported.")
    ap.add_argument("--top", type=int, default=8)
    ap.add_argument("--cap", type=int, default=3,
                    help="samples per grid cell per axis for --pair. A SPEED control for the search only - the pairing it selects is then confirmed with an uncapped --corr, because a capped cell is a point sample of a dithered image.")
    ap.add_argument("--stats", nargs="+", default=[],
                    help="PPM files to characterise")
    ap.add_argument("--region", action="append", default=[],
                    help="name=x0,y0,x1,y1 (repeatable)")
    ap.add_argument("--whole", action="store_true",
                    help="also report the whole frame - the global tonal "
                         "question, which needs no region at all")
    ap.add_argument("--png", nargs=2, default=None, metavar=("PPM", "PNG"))
    ap.add_argument("--grid", nargs=2, default=None, metavar=("PPM", "PNG"))
    ap.add_argument("--grid-step", type=int, default=64)
    ap.add_argument("--scale", type=int, default=1,
                    help="integer downscale for --png/--grid")
    ap.add_argument("--zoom", type=int, default=1,
                    help="integer nearest-neighbour UPSCALE for --png, so a "
                         "small region can be looked at without resampling "
                         "away the thing being looked for")
    ap.add_argument("--crop", default="",
                    help="x0,y0,x1,y1 to cut out before --png/--grid")
    args = ap.parse_args()

    if args.diffbox:
        d = diffbox(*args.diffbox)
        print(f"panepixels: {args.diffbox[0]} vs {args.diffbox[1]} "
              f"({d['w']}x{d['h']})")
        if d["n"] == 0:
            print("  IDENTICAL - 0 differing pixels. If an arm was expected "
                  "to change something, it did not run.")
            return 1
        x0, y0, x1, y1 = d["rect"]
        print(f"  differing pixels: {d['n']}   bbox {x0},{y0},{x1},{y1} "
              f"({x1-x0}x{y1-y0})")
        return 0

    if args.containment:
        d_all = diffbox(*args.containment)
        if not args.exclude:
            print("panepixels: --containment needs --exclude", file=sys.stderr)
            return 2
        ex = tuple(int(v) for v in args.exclude.split(","))
        wa, ha, a = read_ppm(args.containment[0])
        _, _, b = read_ppm(args.containment[1])
        inside = outside = 0
        for y in range(ha):
            base = y * wa * 3
            ra = a[base:base + wa * 3]
            rb = b[base:base + wa * 3]
            if ra == rb:
                continue
            for x in range(wa):
                i = x * 3
                if ra[i:i + 3] == rb[i:i + 3]:
                    continue
                if ex[0] <= x < ex[2] and ex[1] <= y < ex[3]:
                    inside += 1
                else:
                    outside += 1
        print(f"panepixels: containment vs {args.exclude}")
        print(f"  differing INSIDE  {inside}")
        print(f"  differing OUTSIDE {outside}"
              f"   {'CONTAINED' if outside == 0 else 'NOT CONTAINED'}")
        if inside == 0:
            print("  NOTE: nothing changed inside either - the arm did not "
                  "fire, so 'contained' here is vacuous.")
        return 0 if outside == 0 and inside > 0 else 1

    if args.pair:
        ref, cands = args.pair[0], args.pair[1:]
        if not cands:
            print("panepixels: --pair needs a reference and candidates",
                  file=sys.stderr)
            return 2
        rw, rh, rpx = read_ppm(ref)
        ex = (tuple(int(v) for v in args.rect_a.split(","))
              if args.rect_a else None)
        # Blank the excluded rect on BOTH sides by replacing it with the
        # frame's own mean, so the surface under test contributes no
        # structure to the pairing score on either side. Excluding it only
        # from the reference would leave the candidate free to score on it.
        cols = int(args.cols.split(",")[0]) if args.cols else 8

        def masked(w, h, px):
            _, _, L = crop_lum(w, h, px, (0, 0, w, h))
            if ex:
                # rects are given in the REFERENCE's pixel coordinates; scale
                # to this image, which may be a different height (the core
                # hands back 640x240 where the native window is 640x480).
                sx, sy = w / rw, h / rh
                x0 = int(ex[0] * sx); x1 = int(ex[2] * sx)
                y0 = int(ex[1] * sy); y1 = int(ex[3] * sy)
                flat = [v for r in L for v in r]
                m = sum(flat) / len(flat)
                for y in range(max(0, y0), min(h, y1)):
                    for x in range(max(0, x0), min(w, x1)):
                        L[y][x] = m
            return resample(w, h, L, cols, max(2, round(cols * 3 / 4)),
                            cap=args.cap)

        gref = masked(rw, rh, rpx)
        scored = []
        for c in cands:
            try:
                cw, ch, cpx = read_ppm(c)
            except Exception as exc:                     # noqa: BLE001
                print(f"  {c}: unreadable ({exc})", file=sys.stderr)
                continue
            scored.append((pearson(gref, masked(cw, ch, cpx)), c))
        scored = [s for s in scored if s[0] == s[0]]     # drop NaN
        scored.sort(reverse=True)
        print(f"panepixels: pairing {ref} against {len(cands)} candidates, "
              f"grid {cols}x{max(2, round(cols * 3 / 4))}, "
              f"excluded rect {args.rect_a or 'NONE'}")
        for r, c in scored[:args.top]:
            print(f"  r = {r:+.4f}   {c}")
        if not scored:
            print("  NO CANDIDATE SCORED - nothing to pair with.")
            return 1
        return 0

    if args.corr:
        pa, pb = args.corr
        wa, ha, a = read_ppm(pa)
        wb, hb, b = read_ppm(pb)
        ra = tuple(int(v) for v in args.rect_a.split(","))
        rb = tuple(int(v) for v in args.rect_b.split(","))
        aw, ah, LA = crop_lum(wa, ha, a, ra)
        bw, bh, LB = crop_lum(wb, hb, b, rb)
        print(f"panepixels: corr {pa}{ra} ({aw}x{ah}) vs {pb}{rb} "
              f"({bw}x{bh})")
        for cols in (int(v) for v in args.cols.split(",")):
            # Rows from the FIRST region's aspect, used for both, so the two
            # sides are sampled onto the same grid shape.
            rows = max(2, round(cols * ah / aw))
            ga = resample(aw, ah, LA, cols, rows)
            gb = resample(bw, bh, LB, cols, rows)
            r = pearson(ga, gb)
            print(f"  grid {cols:3d}x{rows:<3d}  r = {r:+.4f}")
        return 0

    if args.png or args.grid:
        src, dst = args.png or args.grid
        w, h, px = read_ppm(src)
        if args.crop:
            cx0, cy0, cx1, cy1 = (int(v) for v in args.crop.split(","))
            cw, ch = cx1 - cx0, cy1 - cy0
            px = b"".join(px[((cy0 + y) * w + cx0) * 3:
                             ((cy0 + y) * w + cx1) * 3] for y in range(ch))
            w, h = cw, ch
        if args.grid:
            px = grid_overlay(w, h, px, args.grid_step)
        if args.scale > 1:
            s = args.scale
            nw, nh = w // s, h // s
            out = bytearray(nw * nh * 3)
            for y in range(nh):
                for x in range(nw):
                    i = ((y * s) * w + x * s) * 3
                    o = (y * nw + x) * 3
                    out[o:o + 3] = px[i:i + 3]
            w, h, px = nw, nh, bytes(out)
        if args.zoom > 1:
            z = args.zoom
            nw, nh = w * z, h * z
            out = bytearray(nw * nh * 3)
            for y in range(nh):
                sy = y // z
                for x in range(nw):
                    i = (sy * w + x // z) * 3
                    o = (y * nw + x) * 3
                    out[o:o + 3] = px[i:i + 3]
            w, h, px = nw, nh, bytes(out)
        write_png(dst, w, h, px)
        print(f"panepixels: {src} -> {dst} ({w}x{h})")
        return 0

    regions = []
    for spec in args.region:
        nm, _, r = spec.partition("=")
        regions.append((nm, tuple(int(v) for v in r.split(","))))
    if not regions and not args.whole:
        print("panepixels: nothing to do - give --region or --whole",
              file=sys.stderr)
        return 2

    for f in args.stats:
        w, h, px = read_ppm(f)
        print(f"\n{f}  ({w}x{h})")
        if args.whole:
            print("  " + fmt("WHOLE FRAME", analyse(w, h, px, (0, 0, w, h))))
        for nm, r in regions:
            print("  " + fmt(nm, analyse(w, h, px, r)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
