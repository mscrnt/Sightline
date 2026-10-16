"""Xbox 360 (Xenos) texture memory -> RGBA8.

Untiling (XGAddress2DTiledOffset) plus DXT1 / DXT3 / DXT5 / A8R8G8B8 / L8
decode. Measured against a real source during the #47 recon; the constants
are the console's, not a guess.

WHERE LEVEL 0 BEGINS (#68). A texture's allocation is not simply its base
level at the origin. The console's layout has three parts, and the third one
is the reason this module used to read the wrong pixels out of a correct
allocation:

  * the base level uses the ROW PITCH the fetch constant carries, in texels
    divided by 32 - so the pitch, converted to blocks, is a multiple of 32
    blocks, and the height in blocks is padded to 32 as well. This container
    stores no pitch of its own, so the pitch IS the declared width;
  * mip levels have a power-of-two derived layout of their own;
  * when packed mips are on, every level whose width OR HEIGHT has fallen to
    16 texels or less is not stored on its own. Those levels share one tile -
    the mip tail - placed in the 32-texel padding of the last level above it.

The third rule is on the SHORTER side, so a texture that is already 16 or
less on one side has a tail that begins at level 0: THE BASE LEVEL ITSELF
lives inside the tail, and is therefore NOT at allocation coordinate (0, 0).
A 256x16 texture's base sits 16 texels down, with its own smaller levels
stacked in the rows above it - which is exactly what this module used to
decode and hand over as the picture.

Packed mips can only place a level in a tail when a chain exists at all, so
a texture the container declares with a single level is read at the origin.
Measured on a 64x1 source texture: real pixels at the origin, nothing but
zeroes where the packed position would be.

This module reads no file and knows no path: it is handed bytes and returns
pixels, so it carries nothing that could be mistaken for an asset.
"""
import numpy as np

# Tiles are 32x32, and a row pitch is padded to 32 blocks.
TILE = 32

# format -> (block width, block height, bytes per block). A non-block format
# is a 1x1 block, which keeps one set of layout arithmetic for both.
UNITS = {2: (1, 1, 1), 6: (1, 1, 4), 18: (4, 4, 8), 19: (4, 4, 16),
         20: (4, 4, 16)}


def _log2_ceil(v):
    return (v - 1).bit_length() if v > 1 else 0


def _align(v, a):
    return (v + a - 1) // a * a


def packed_mip_level(width, height):
    """The level at which the mip tail begins; 0 means the base is packed."""
    log2_short = _log2_ceil(min(width, height))
    return log2_short - 4 if log2_short > 4 else 0


def packed_mip_offset(width, height, fmt, level):
    """(x, y) in BLOCKS of `level` inside the tail, or None if it is not packed.

    The level above the tail keeps the origin; the packed levels are laid out
    along the SHORT axis, so a wider-than-tall texture stacks them downwards
    and a taller-than-wide one stacks them across. The first three sit at a
    halving offset from the tail's own size; past that they fold back along
    the long axis, which is where the tiny 4x4-and-under levels go.
    """
    block_w, block_h, _ = UNITS[fmt]
    log2_w, log2_h = _log2_ceil(width), _log2_ceil(height)
    log2_short = min(log2_w, log2_h)
    if log2_short > 4 + level:
        return None                       # the short side is still above 16
    first = log2_short - 4 if log2_short > 4 else 0
    nth = level - first                   # which packed level this one is
    if nth < 3:
        step = 16 >> nth
        x, y = (0, step) if log2_w > log2_h else (step, 0)
    elif log2_w > log2_h:
        x, y = (1 << (log2_w - first)) >> (nth - 2), 0
    else:
        x, y = 0, (1 << (log2_h - first)) >> (nth - 2)
    # the placements above are counted in texels; storage counts blocks
    return x // block_w, y // block_h


def base_block_origin(width, height, fmt, mip_levels):
    """Where level 0 begins inside its own allocation, in blocks.

    (0, 0) unless the texture is small enough on its short side that the mip
    tail starts at level 0 - and a tail needs a chain, so a single-level
    texture is always at the origin.
    """
    if mip_levels <= 1 or packed_mip_level(width, height) != 0:
        return 0, 0
    return packed_mip_offset(width, height, fmt, 0) or (0, 0)


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


def untile(data, width, height, texel_bytes, tiled=True, origin=(0, 0),
           pitch=None):
    """Linear (height, width, texel_bytes) array from a tiled buffer.

    width / height are in texels (blocks for a block format). `pitch` is the
    surface's row pitch in the same units and defaults to the width, which is
    what this container's textures carry. `origin` is where the level being
    read begins inside the surface - (0, 0) for everything except a level
    that lives in a packed mip tail.

    The buffer is padded to 32-unit multiples, and a short buffer is
    zero-filled rather than raising: a truncated source must report as a bad
    texture, not as a stack trace. A level placed OUTSIDE the surface is a
    different matter - that is a layout this reader has got wrong, so it is
    refused rather than decoded out of padding.
    """
    x0, y0 = origin
    aw = _align(width if pitch is None else pitch, TILE)
    ah = _align(height, TILE)
    if x0 < 0 or y0 < 0 or x0 + width > aw or y0 + height > ah:
        raise ValueError('a level at %r does not fit a %dx%d surface'
                         % ((x0, y0), aw, ah))
    need = aw * ah * texel_bytes
    buf = np.frombuffer(bytes(data[:need]).ljust(need, b'\0'), dtype=np.uint8)
    if not tiled:
        return buf.reshape(ah, aw, texel_bytes)[y0:y0 + height, x0:x0 + width]
    key = (aw, ah, texel_bytes, x0, y0, width, height)
    idx = _tile_cache.get(key)
    if idx is None:
        ys, xs = np.mgrid[y0:y0 + height, x0:x0 + width].astype(np.int64)
        idx = tiled_offset(xs, ys, aw, texel_bytes)
        _tile_cache[key] = idx
    src = buf.reshape(-1, texel_bytes)
    idx = np.clip(idx, 0, src.shape[0] - 1)
    return src[idx]


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


def decode(data, fmt, w, h, tiled=True, mip_levels=1):
    """A texture's allocation (tiled, padded) -> (h, w, 4) uint8 RGBA.

    `mip_levels` is the level count the container declares for this texture.
    It is what decides whether a mip tail exists, and so whether the BASE
    level is at the origin or inside one - see the module docstring.
    """
    if fmt not in UNITS:
        raise ValueError('unsupported texture format %d' % fmt)
    block_w, block_h, unit = UNITS[fmt]
    origin = base_block_origin(w, h, fmt, mip_levels)
    cols = (w + block_w - 1) // block_w
    rows = (h + block_h - 1) // block_h
    pitch = _align(w, block_w) // block_w
    t = untile(data, cols, rows, unit, tiled, origin, pitch)
    if fmt == 6:        # 8_8_8_8, 8in32 endian: bytes on disk are A R G B
        return t[..., [1, 2, 3, 0]].copy()
    if fmt == 2:        # single 8-bit channel -> white with that alpha
        out = np.empty((h, w, 4), np.uint8)
        out[..., :3] = 255
        out[..., 3] = t[..., 0]
        return out
    img = {18: decode_dxt1_blocks, 19: decode_dxt3_blocks,
           20: decode_dxt5_blocks}[fmt](t)
    return img[:h, :w].copy()
