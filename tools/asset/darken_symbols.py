#!/usr/bin/env python3
r"""Darken and thicken the button-symbol glyphs in a controller base-colour tile.

WHY THIS EXISTS
    The DualSense package's cross / circle / square / triangle are 2..3-texel
    grey lines (RGB ~159) on the lilac button tops (RGB ~231/225/237) of
    textures/dualsense_basecolor_1.png. A 1024-square tile lands on a ~12 px
    button on the watch's controller page and on a ~13 px icon beside its
    label, sixty to eighty times minified: the renderer's mip chain averages
    a 3-texel line at 72-point contrast into nothing, and the symbols read
    as plain white discs (measured 2026-09-20, #63 / #64 rounds 5 and 6; the
    owner: "Would like for the symbols to show on the buttons too").

    A darker, thicker glyph is the fix, and CC-BY-4.0 permits the edit - but
    the tile is the third party's work, so the edit is a SCRIPT with its
    region, threshold and stroke recorded, not a hand-painted PNG: anyone can
    re-derive the committed tile from the original (its SHA-1 is in the
    package's metadata.json) and see exactly what changed.

WHAT IT DOES, per region (the measured bounding box of one symbol):
    1. GLYPH texels, INSIDE the box only: luminance in [lum_lo, lum_hi] with
       saturation (max - min channel) <= sat_max - the grey stroke and its
       antialiased edge. Only the box is searched, because the tile's flat
       grey areas (RGB 188 / 205) sit within a few texels of the square and
       would read as glyph; a painted stroke (luminance ~56) is below
       lum_lo, so re-running on the output finds no glyph and is a no-op;
    2. the glyph mask is DILATED by `stroke` texels (a disc), up to `margin`
       texels outside the box, but only onto texels lighter than the stroke
       (luminance > lum_hi: the lilac button top and the stroke's own light
       edge) - so the thickened stroke never bleeds onto anything darker;
    3. every mask texel is painted `colour`, alpha kept.
    Nothing outside the regions is touched; the tile's size and format are
    unchanged (RGBA8, re-encoded with the importer's own PNG writer).

USAGE
    python tools/asset/darken_symbols.py dualsense            # edit in place
    python tools/asset/darken_symbols.py dualsense --check    # verify: a
        re-run would change nothing, and the tile's SHA-1 is the one
        metadata.json records under sightline_edits (exit 1 otherwise)
    python tools/asset/darken_symbols.py dualsense --source <original.png>
        --out <edited.png>                                    # re-derive

The per-package tables below ARE the record: the regions were MEASURED on
the original tile (2026-09-20, scratch measure_glyphs.py: 4-connected
components of grey texels; the four symbols are the only grey components
on lilac larger than 400 texels) and are repeated in the package's
ATTRIBUTION.md and metadata.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltf_import as gi  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTROLLERS = REPO_ROOT / "data" / "asset-overrides" / "source" / "controllers"

# One entry per package: the tile, the symbol regions (x0, y0, x1, y1
# inclusive, on the original 1024-square tile), the thresholds and the
# stroke. `margin` widens each region so the dilation has room.
PACKAGES = {
    "dualsense": {
        "texture": "textures/dualsense_basecolor_1.png",
        "regions": {
            "cross":    (585, 165, 634, 215),
            "circle":   (653, 370, 701, 417),
            "triangle": (684, 450, 729, 490),
            "square":   (11,  563, 63,  616),
        },
        "margin": 6,
        "lum_lo": 120, "lum_hi": 205, "sat_max": 16,   # the grey stroke (measured RGB 159) and its edge
        "stroke": 3,                                   # dilation radius, texels: 2..3 -> 8..9 wide
        "colour": (56, 56, 64),                        # the painted stroke
    },
}


def lum(r: int, g: int, b: int) -> int:
    return (r * 299 + g * 587 + b * 114) // 1000


def apply(w: int, h: int, px: bytearray, cfg: dict) -> int:
    """Edit px in place; returns the number of texels painted."""
    painted = 0
    stroke = cfg["stroke"]
    disc = [(dy, dx) for dy in range(-stroke, stroke + 1) for dx in range(-stroke, stroke + 1)
            if dx * dx + dy * dy <= stroke * stroke]
    for name, (x0, y0, x1, y1) in cfg["regions"].items():
        m = cfg["margin"]
        rx0, ry0 = max(0, x0 - m), max(0, y0 - m)
        rx1, ry1 = min(w - 1, x1 + m), min(h - 1, y1 + m)
        rw, rh = rx1 - rx0 + 1, ry1 - ry0 + 1
        glyph = bytearray(rw * rh)
        bg = bytearray(rw * rh)
        for y in range(ry0, ry1 + 1):
            for x in range(rx0, rx1 + 1):
                o = (y * w + x) * 4
                r, g, b = px[o], px[o + 1], px[o + 2]
                L = lum(r, g, b)
                sat = max(r, g, b) - min(r, g, b)
                k = (y - ry0) * rw + (x - rx0)
                inside = x0 <= x <= x1 and y0 <= y <= y1
                if inside and cfg["lum_lo"] <= L <= cfg["lum_hi"] and sat <= cfg["sat_max"]:
                    glyph[k] = 1
                elif L > cfg["lum_hi"]:
                    bg[k] = 1
        # dilate onto background texels only
        out = bytearray(glyph)
        for y in range(rh):
            for x in range(rw):
                if not glyph[y * rw + x]:
                    continue
                for dy, dx in disc:
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < rh and 0 <= xx < rw and bg[yy * rw + xx]:
                        out[yy * rw + xx] = 1
        cr, cg, cb = cfg["colour"]
        n = 0
        for y in range(rh):
            for x in range(rw):
                if out[y * rw + x]:
                    o = ((ry0 + y) * w + (rx0 + x)) * 4
                    if (px[o], px[o + 1], px[o + 2]) != (cr, cg, cb):
                        px[o], px[o + 1], px[o + 2] = cr, cg, cb
                        n += 1
        painted += n
        print("  %-9s region (%d,%d)-(%d,%d): glyph %d texels -> stroke %d, painted %d" % (
            name, rx0, ry0, rx1, ry1, sum(glyph), sum(out), n))
    return painted


def sha1_of(p: Path) -> str:
    return hashlib.sha1(p.read_bytes()).hexdigest().upper()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("package", choices=sorted(PACKAGES))
    ap.add_argument("--source", type=Path, help="read this PNG instead of the package's tile")
    ap.add_argument("--out", type=Path, help="write here instead of in place")
    ap.add_argument("--check", action="store_true",
                    help="verify the package tile: a re-run changes nothing and its SHA-1 is the recorded one")
    args = ap.parse_args(argv)

    cfg = PACKAGES[args.package]
    pkg = CONTROLLERS / args.package
    src = args.source or (pkg / cfg["texture"])
    dst = args.out or (pkg / cfg["texture"])

    w, h, px = gi.png_decode(src.read_bytes())
    print("%s: %dx%d, sha1 %s" % (src, w, h, sha1_of(src)))
    painted = apply(w, h, px, cfg)

    if args.check:
        meta = json.loads((pkg / "metadata.json").read_text(encoding="utf-8"))
        want = (meta.get("sightline_edits") or {}).get(cfg["texture"], {}).get("sha1", "")
        have = sha1_of(src)
        ok = painted == 0 and want and want.upper() == have
        print("check: re-run painted %d texel(s); sha1 recorded %s, have %s -> %s" % (
            painted, want or "(none)", have, "OK" if ok else "MISMATCH"))
        return 0 if ok else 1

    if painted == 0 and dst == src:
        print("nothing to do (the tile is already edited)")
        return 0
    dst.write_bytes(gi.png_encode(w, h, px))
    print("wrote %s: %d texel(s) painted, sha1 %s" % (dst, painted, sha1_of(dst)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
