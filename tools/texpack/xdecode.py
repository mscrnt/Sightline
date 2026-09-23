"""Xbox 360 (Xenos) texture memory -> RGBA8.

Untiling (XGAddress2DTiledOffset) plus DXT1 / DXT3 / DXT5 / A8R8G8B8 / L8
decode. Measured against a real source during the #47 recon; the constants
are the console's, not a guess.

This module reads no file and knows no path: it is handed bytes and returns
pixels, so it carries nothing that could be mistaken for an asset.
"""
import numpy as np


def tiled_offset(x, y, width, texel_bytes):
    """XGAddress2DTiledOffset - texel index for (x, y) in a tiled surface.

    width is the ALIGNED width in texels; texel_bytes is bytes per texel
    (for a block format: bytes per block, with x / y / width in blocks).
    Pure integer arithmetic, so numpy broadcasts it over a whole grid.
    """
    aligned_width = (width + 31) & ~31
    log_bpp = (texel_bytes >> 2) + ((texel_bytes >> 1) >> (texel_bytes >> 2))
    macro = ((x >> 5) + (y >> 5) * (aligned_width >> 5)) << (log_bpp + 7)
    micro = (((x & 7) + ((y & 6) << 2)) << log_bpp)
    offset = (macro + ((micro & ~15) << 1) + (micro & 15)
              + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4))
    return (((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63)
            + ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp


_tile_cache = {}


def untile(data, width, height, texel_bytes, tiled=True):
    """Linear (height, width, texel_bytes) array from a tiled buffer.

    width / height are in texels (blocks for a block format). The buffer is
    padded to 32-texel multiples, and a short buffer is zero-filled rather
    than raising: a truncated source must report as a bad texture, not as a
    stack trace.
    """
    aw = (width + 31) & ~31
    ah = (height + 31) & ~31
    need = aw * ah * texel_bytes
    buf = np.frombuffer(bytes(data[:need]).ljust(need, b'\0'), dtype=np.uint8)
    if not tiled:
        return buf.reshape(ah, aw, texel_bytes)[:height, :width]
    key = (aw, ah, texel_bytes)
    idx = _tile_cache.get(key)
    if idx is None:
        ys, xs = np.mgrid[0:ah, 0:aw].astype(np.int64)
        idx = tiled_offset(xs, ys, aw, texel_bytes)
        _tile_cache[key] = idx
    src = buf.reshape(-1, texel_bytes)
    idx = np.clip(idx, 0, src.shape[0] - 1)
    return src[idx][:height, :width]


def _rgb565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.int32)


def _colour_block(cb, endian16):
    """(..., 8) uint8 DXT colour block -> (c0, c1, bits)."""
    b = cb.astype(np.uint32)
    if endian16:
        c0 = (b[..., 0] << 8) | b[..., 1]
        c1 = (b[..., 2] << 8) | b[..., 3]
        bits = ((b[..., 4] << 8) | b[..., 5]) | (((b[..., 6] << 8) | b[..., 7]) << 16)
    else:
        c0 = b[..., 0] | (b[..., 1] << 8)
        c1 = b[..., 2] | (b[..., 3] << 8)
        bits = b[..., 4] | (b[..., 5] << 8) | (b[..., 6] << 16) | (b[..., 7] << 24)
    return c0, c1, bits


def decode_dxt1_blocks(blocks, endian16=True, force_four=False):
    """(bh, bw, 8) uint8 -> (bh*4, bw*4, 4) uint8 RGBA."""
    bh, bw = blocks.shape[:2]
    c0, c1, bits = _colour_block(blocks, endian16)
    p0 = _rgb565(c0)
    p1 = _rgb565(c1)
    four = (c0 > c1) | force_four
    p2 = np.where(four[..., None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four[..., None], (p0 + 2 * p1) // 3, 0)
    a3 = np.where(four, 255, 0).astype(np.int32)
    pal = np.stack([p0, p1, p2, p3], -2)
    apal = np.stack([np.full_like(a3, 255)] * 3 + [a3], -1)
    out = np.zeros((bh * 4, bw * 4, 4), np.uint8)
    for py in range(4):
        for px in range(4):
            sel = ((bits >> (2 * (py * 4 + px))) & 3).astype(np.int64)
            rgb = np.take_along_axis(pal, sel[..., None, None].repeat(3, -1), -2)[..., 0, :]
            a = np.take_along_axis(apal, sel[..., None], -1)[..., 0]
            out[py::4, px::4, :3] = rgb
            out[py::4, px::4, 3] = a
    return out


def decode_dxt5_blocks(blocks, endian16=True):
    bh, bw = blocks.shape[:2]
    ab = blocks[..., 0:8].astype(np.uint64)
    if endian16:
        ab = ab[..., [1, 0, 3, 2, 5, 4, 7, 6]]
    a0 = ab[..., 0].astype(np.int32)
    a1 = ab[..., 1].astype(np.int32)
    abits = np.zeros((bh, bw), np.uint64)
    for i in range(6):
        abits |= ab[..., 2 + i] << np.uint64(8 * i)
    apal = np.zeros((bh, bw, 8), np.int32)
    apal[..., 0] = a0
    apal[..., 1] = a1
    eight = a0 > a1
    for i in range(2, 8):
        v8 = ((8 - i) * a0 + (i - 1) * a1) // 7
        if i < 6:
            v6 = ((6 - i) * a0 + (i - 1) * a1) // 5
        else:
            v6 = np.full_like(a0, 0 if i == 6 else 255)
        apal[..., i] = np.where(eight, v8, v6)
    out = decode_dxt1_blocks(blocks[..., 8:16], endian16, force_four=True)
    for py in range(4):
        for px in range(4):
            asel = ((abits >> np.uint64(3 * (py * 4 + px))) & np.uint64(7)).astype(np.int64)
            out[py::4, px::4, 3] = np.take_along_axis(apal, asel[..., None], -1)[..., 0]
    return out


def decode_dxt3_blocks(blocks, endian16=True):
    bh, bw = blocks.shape[:2]
    ab = blocks[..., 0:8].astype(np.uint64)
    if endian16:
        ab = ab[..., [1, 0, 3, 2, 5, 4, 7, 6]]
    abits = np.zeros((bh, bw), np.uint64)
    for i in range(8):
        abits |= ab[..., i] << np.uint64(8 * i)
    out = decode_dxt1_blocks(blocks[..., 8:16], endian16, force_four=True)
    for py in range(4):
        for px in range(4):
            a4 = ((abits >> np.uint64(4 * (py * 4 + px))) & np.uint64(15)).astype(np.uint8)
            out[py::4, px::4, 3] = a4 * 17
    return out


SUPPORTED = (2, 6, 18, 19, 20)


def decode(data, fmt, w, h, tiled=True):
    """Base-level bytes (tiled, padded) -> (h, w, 4) uint8 RGBA."""
    if fmt == 6:        # 8_8_8_8, 8in32 endian: bytes on disk are A R G B
        t = untile(data, w, h, 4, tiled)
        return t[..., [1, 2, 3, 0]].copy()
    if fmt == 2:        # single 8-bit channel -> white with that alpha
        t = untile(data, w, h, 1, tiled)[..., 0]
        out = np.empty((h, w, 4), np.uint8)
        out[..., :3] = 255
        out[..., 3] = t
        return out
    if fmt in (18, 19, 20):
        bw, bh = (w + 3) // 4, (h + 3) // 4
        tb = 8 if fmt == 18 else 16
        blocks = untile(data, bw, bh, tb, tiled)
        img = {18: decode_dxt1_blocks, 19: decode_dxt3_blocks,
               20: decode_dxt5_blocks}[fmt](blocks)
        return img[:h, :w].copy()
    raise ValueError('unsupported texture format %d' % fmt)
