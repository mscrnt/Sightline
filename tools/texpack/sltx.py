"""The SLTX runtime image format - writer and bounds.

SLTX is what src/gfx/sl_gfx_texprov.c reads: a 28-byte little-endian header
and tightly packed RGBA8, rows top first. docs/texture-packs.md is the
contract; the parser there rejects anything this writer would not produce.

    +0  'SLTX'   +4 u32 version=1   +8 u32 id   +12 u16 n64_w  +14 u16 n64_h
    +16 u16 phys_w  +18 u16 phys_h  +20 u32 flags=0  +24 u32 payload=w*h*4
    +28 payload

The bounds below are the runtime's own (sl_gfx_texprov.h). They are checked
HERE so a bad conversion fails at the developer's desk rather than being
reported once per texture at play time.
"""
import os
import struct

VERSION = 1
HEADER = 28
MAX_PHYS = 1024          # SL_TEXPROV_MAX_DIM
MAX_N64 = 255            # SL_TEXPROV_MAX_N64
MAX_ID = 4096            # SL_TEXPROV_MAX_ID


class SltxError(Exception):
    pass


def header(tid, n64_w, n64_h, phys_w, phys_h):
    if not (0 <= tid < MAX_ID):
        raise SltxError('id %r out of range' % (tid,))
    if not (0 < n64_w <= MAX_N64 and 0 < n64_h <= MAX_N64):
        raise SltxError('id %04x: N64 size %rx%r out of range' % (tid, n64_w, n64_h))
    if not (0 < phys_w <= MAX_PHYS and 0 < phys_h <= MAX_PHYS):
        raise SltxError('id %04x: replacement %rx%r exceeds the runtime bound %d'
                        % (tid, phys_w, phys_h, MAX_PHYS))
    h = struct.pack('<4sIIHHHHII', b'SLTX', VERSION, tid,
                    n64_w, n64_h, phys_w, phys_h, 0, phys_w * phys_h * 4)
    assert len(h) == HEADER
    return h


def write(path, tid, n64_w, n64_h, phys_w, phys_h, rgba):
    """Write one SLTX file. `rgba` is exactly phys_w*phys_h*4 bytes."""
    if len(rgba) != phys_w * phys_h * 4:
        raise SltxError('id %04x: %d payload bytes for a %dx%d image'
                        % (tid, len(rgba), phys_w, phys_h))
    h = header(tid, n64_w, n64_h, phys_w, phys_h)
    tmp = path + '.part'
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(tmp, 'wb') as f:
        f.write(h)
        f.write(rgba)
    os.replace(tmp, path)          # idempotent and atomic: a rerun never
    return HEADER + len(rgba)      # leaves a half-written file behind
