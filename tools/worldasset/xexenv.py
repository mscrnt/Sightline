"""Derive the remaster's STAGE-FOG environment metadata from the user's own
executable (#68), as a small normalised Sightline file the renderer reads.

WHY THIS EXISTS. The remaster's replacement background is drawn by pixel
programs that end in a fog blend,

    rgb = lerp(fog colour, material colour, f)
    f   = saturate((fog end - d) / (fog end - fog start))

and the fog colour and fog end are not in any world-asset item and not in the
N64 ROM. They are a per-environment EXTENSION the remaster appended to each row
of the game's environment table - the same table `src/game/bgfog.c` carries as
`fog_tables[]`, whose rows are keyed by the game's own environment identity
(stage id plus a variant: +900 cinema, +100 alt, +200..400 player count). This
module finds that table in the user's executable and writes out, per row, the
three values the renderer needs and nothing else.

WHAT IS AND IS NOT HERE. Generic XEX2 container handling (the header, the
optional-header directory, the file-format descriptor and the BASIC block
layout), a STRUCTURAL locator, validation, and the writer. No executable byte,
no table row, no offset into any particular executable and no value read from
one is in this file: the locator identifies the table by the original game's
own rows, which it reads from the decomp source already in this tree.

THE CONTAINER. An XEX2 file is a big-endian header, an optional-header
directory of (key, value) pairs, and the image data at the header's declared
data offset. Key 0x000003FF names the file-format descriptor: its encryption
type and compression type. Compression 1 ("basic") stores the image as a list
of (data length, zero-fill length) blocks - the image is those blocks
concatenated, with each zero fill materialised. Compression 0 is the image
stored whole. Anything ENCRYPTED, or compressed with anything but those two
(the LZX "normal" and delta forms), is REFUSED with a message that says so:
decrypting or decompressing an executable is a loader, and a loader is out of
scope for a preparation step (see `XexError`).

THE TABLE, and how it is found without an address. The remaster's row is a
compact form of the game's: every distance and intensity a 16-bit big-endian
field, then the sky bytes, then the extension. Each ORIGINAL row therefore has
a fixed byte signature built from values the decomp already states -

    u16 id, blend, far fog, near fog, max vis, max obfuscation,
        intensity difference, far intensity
    u8  red, green, blue, clouds
    u16 cloud repeat

- and the locator finds every place each original row's signature occurs,
requires the matches to fall on ONE fixed stride in the original table's own
order, and walks that table from its first row to its terminator. A table is
accepted only when:

  * at least MIN_MATCHED original rows are found, in order, on one stride;
  * exactly one such table exists in the image (two candidates is ambiguous
    and is refused, never chosen between);
  * every row of the walked table has a unique id and a well-formed extension
    (below).

THE EXTENSION is the last EXT_LEN bytes of each row:

    +0  u32  fog end                 (world units)
    +4  u8   fog red, green, blue, 0
    +8  u32  far plane               (world units)
    +12 f32  near plane
    +16 f32  (not used here)

The far and near planes are read only to validate the row (0 < near < far,
0 < fog end <= far): they were measured as the remaster's own projection for
the same row, and a row that violates them is not the structure this reader
knows. Only id, fog end and fog colour are written out.

THE FOG START is not in the table. It is the remaster's fixed fog formula
offset, FOG_START world units, measured as the SAME value in two environment
states with different fog ends, so it is a property of the formula rather than
of any row, and it is written into every row so the renderer applies one
formula to whatever the file says.

THE DISTANCE UNIT is the renderer's contract, not this file's, but the number
written here is in it: the remaster's view-space distance, which was measured
to be the game's WORLD units after the stage's level scale and before its
visibility scale. See `src/gfx/sl_gfx_worldasset.h` (SLEN).

This module parses a file the USER supplies. It contains no source bytes, no
identifier of any particular source, and no way to obtain one.
"""
import math
import os
import re
import struct


class XexError(Exception):
    """The executable is not one this reader can take apart. Carries a message
    written for the person running the preparation."""


# ---- the XEX2 container ----------------------------------------------------

XEX_MAGIC = b'XEX2'
KEY_FILE_FORMAT = 0x000003FF
ENC_NONE = 0
COMP_NONE = 0
COMP_BASIC = 1
MAX_IMAGE = 256 * 1024 * 1024      # an image larger than this is not a game
MAX_OPT_HEADERS = 256


def _u32(b, o):
    if o < 0 or o + 4 > len(b):
        raise XexError('the executable is truncated (a header field at %#x '
                       'lies past its end)' % o)
    return struct.unpack_from('>I', b, o)[0]


def xex_image(data):
    """The executable's IMAGE bytes from a raw XEX2 file, for the two storage
    forms that need no decryption and no decompressor. Raises XexError for
    anything else."""
    if len(data) < 0x18 or data[:4] != XEX_MAGIC:
        raise XexError('not an XEX2 executable')
    data_off = _u32(data, 0x08)
    nopt = _u32(data, 0x14)
    if nopt > MAX_OPT_HEADERS:
        raise XexError('the executable declares %d optional headers' % nopt)
    if not (0x18 + nopt * 8 <= data_off <= len(data)):
        raise XexError('the executable\'s data offset %#x is outside it' % data_off)
    fmt = None
    for i in range(nopt):
        key = _u32(data, 0x18 + i * 8)
        val = _u32(data, 0x18 + i * 8 + 4)
        if key == KEY_FILE_FORMAT:
            fmt = val
    if fmt is None:
        raise XexError('the executable has no file-format descriptor')
    size = _u32(data, fmt)
    if size < 8 or fmt + size > data_off:
        raise XexError('the executable\'s file-format descriptor is malformed')
    enc, comp = struct.unpack_from('>HH', data, fmt + 4)
    if enc != ENC_NONE:
        raise XexError('the executable is ENCRYPTED (type %d). Decrypting it '
                       'is out of scope for this step; the replacement '
                       'background stays unfogged' % enc)
    body = data[data_off:]
    if comp == COMP_NONE:
        if len(body) > MAX_IMAGE:
            raise XexError('the executable image is implausibly large')
        return bytes(body)
    if comp != COMP_BASIC:
        raise XexError('the executable is stored with compression type %d, '
                       'which needs a decompressor. That is out of scope for '
                       'this step; the replacement background stays unfogged'
                       % comp)
    nblk = (size - 8) // 8
    out = bytearray()
    pos = 0
    for i in range(nblk):
        dlen = _u32(data, fmt + 8 + i * 8)
        zlen = _u32(data, fmt + 12 + i * 8)
        if pos + dlen > len(body):
            raise XexError('the executable\'s block %d runs past its end' % i)
        if len(out) + dlen + zlen > MAX_IMAGE:
            raise XexError('the executable image is implausibly large')
        out += body[pos:pos + dlen]
        out += b'\0' * zlen
        pos += dlen
    if not out:
        raise XexError('the executable\'s image is empty')
    return bytes(out)


# ---- the original game's rows, from the decomp source ----------------------

def _enum_values(text):
    """name -> int for every enumerator in `text`, evaluating the C rules the
    tree's own enums use: an explicit `= expr` (integers, earlier names, + and
    -), otherwise one more than the previous enumerator."""
    vals = {}
    for body in re.findall(r'enum\s+\w*\s*\{(.*?)\}', text, re.S):
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        body = re.sub(r'//[^\n]*', '', body)
        nxt = 0
        for item in body.split(','):
            item = item.strip()
            if not item:
                continue
            m = re.match(r'([A-Za-z_]\w*)\s*(?:=\s*(.+))?$', item, re.S)
            if not m:
                continue
            name, expr = m.group(1), m.group(2)
            if expr is not None:
                v = _eval(expr, vals)
                if v is None:
                    continue
                nxt = v
            vals[name] = nxt
            nxt += 1
    for name, v in re.findall(r'#define\s+([A-Za-z_]\w*)\s+(0x[0-9A-Fa-f]+|\d+)\b', text):
        vals.setdefault(name, int(v, 0))
    return vals


def _eval(expr, vals):
    total = 0
    sign = 1
    for tok in re.findall(r'0x[0-9A-Fa-f]+|\d+|[A-Za-z_]\w*|[+-]', expr):
        if tok == '+':
            sign = 1
        elif tok == '-':
            sign = -1
        else:
            if tok[0].isdigit():
                v = int(tok, 0)
            elif tok in vals:
                v = vals[tok]
            else:
                return None
            total += sign * v
            sign = 1
    return total


def _num(tok):
    tok = tok.strip()
    try:
        return int(tok, 0)
    except ValueError:
        return int(float(tok))


def reference_rows(bgfog_text, constants_text):
    """The ORIGINAL environment rows as (id, blend, far, near, maxvis, maxobf,
    intensity difference, far intensity, red, green, blue, clouds, cloud
    repeat), in table order, read from `fog_tables[]` of the NON-EU build in
    src/game/bgfog.c. The EU table uses a different record and is skipped."""
    vals = _enum_values(constants_text)
    m = re.search(r'#else\s*/\*\*[^/]*?\*/\s*EnvironmentRecord\s+fog_tables\[\]\s*=\s*\{(.*?)\n\};',
                  bgfog_text, re.S)
    if not m:
        m = re.search(r'#else\s*.*?EnvironmentRecord\s+fog_tables\[\]\s*=\s*\{(.*?)\n\};',
                      bgfog_text, re.S)
    if not m:
        return []
    rows = []
    for line in m.group(1).splitlines():
        line = re.sub(r'//.*', '', line).strip()
        if not line.startswith('{') or 'ENVIRONMENTDATA_END' in line:
            continue
        f = [x.strip() for x in line.strip('{},').split(',')]
        if len(f) < 16:
            continue
        rid = _eval(f[0], vals)
        try:
            num = [_num(x) for x in f[1:16]]
        except ValueError:
            continue
        if rid is None:
            continue
        # US record order: blend, far, near, maxvis, maxobf, minvis, intensity,
        # difference, far intensity, r, g, b, clouds, cloud repeat, ...
        blend, far, near, mvis, mobf, _minv, _inten, dif, fint, r, g, b_, cl, crep = num[:14]
        rows.append((rid, blend, far, near, mvis, mobf, dif, fint, r, g, b_, cl, crep))
    return rows


# ---- the structural locator ------------------------------------------------

EXT_LEN = 20          # the remaster's per-row extension, at the END of a row
MIN_MATCHED = 8       # original rows that must be found on one stride
MIN_STRIDE = 24
MAX_STRIDE = 256
MAX_ROWS = 512
FOG_START = -100.0    # the fog formula's fixed offset, world units (above)


def _signature(row):
    rid, blend, far, near, mvis, mobf, dif, fint, r, g, b_, cl, crep = row
    for v in (rid, blend, far, near, mvis, mobf, dif, fint, crep):
        if not (0 <= v <= 0xFFFF):
            return None
    return struct.pack('>8H4BH', rid, blend, far, near, mvis, mobf, dif, fint,
                       r & 0xFF, g & 0xFF, b_ & 0xFF, cl & 0xFF, crep)


def _find_all(img, sig):
    out = []
    i = img.find(sig)
    while i >= 0:
        out.append(i)
        i = img.find(sig, i + 1)
    return out


def _candidate_tables(img, refs):
    """Every (start, stride, matched) that places at least MIN_MATCHED
    original rows at start + k * stride with k strictly increasing in the
    original order. The start is an occurrence of the FIRST original row; each
    stride in range is tried and only the best-scoring one per start is kept,
    so a multiple of the true stride (which can only match a subset) never
    competes with it."""
    sigs = [_signature(r) for r in refs]
    hits = [_find_all(img, s) if s is not None else [] for s in sigs]
    if not hits or not hits[0]:
        return []
    out = []
    for start in hits[0]:
        best = []
        for stride in range(MIN_STRIDE, MAX_STRIDE + 1, 2):
            matched = 0
            last_k = -1
            for poss in hits:
                ks = [(p - start) // stride for p in poss
                      if p >= start and (p - start) % stride == 0]
                ks = [k for k in ks if last_k < k < MAX_ROWS]
                if ks:
                    last_k = min(ks)
                    matched += 1
            if matched >= MIN_MATCHED:
                if not best or matched > best[0][2]:
                    best = [(start, stride, matched)]
                elif matched == best[0][2]:
                    best.append((start, stride, matched))
        # A DIVISOR of the true row size places every matched row too (each
        # multiple of 56 is a multiple of 28), so ties along one divisor chain
        # are the same table and the LARGEST stride is its row size. Ties that
        # are not one chain are genuinely different readings and stay, for
        # `locate` to refuse.
        if best:
            top = max(b[1] for b in best)
            if all(top % b[1] == 0 for b in best):
                best = [b for b in best if b[1] == top]
        out.extend(best)
    return out


def locate(img, refs):
    """The environment table's rows as dicts (id, fog_end, rgb), or raises
    XexError naming why the executable was refused."""
    if len(refs) < MIN_MATCHED:
        raise XexError('the decomp source yielded %d original environment rows; '
                       'at least %d are needed to recognise the table'
                       % (len(refs), MIN_MATCHED))
    cands = _candidate_tables(img, refs)
    if not cands:
        raise XexError('no environment table in this executable matches the '
                       'original game\'s rows on one stride')
    if len(set((s, st) for (s, st, _m) in cands)) > 1:
        raise XexError('%d candidate environment tables match; refusing to '
                       'choose between them' % len(cands))
    start, stride, matched = cands[0]
    if stride <= EXT_LEN + 20:
        raise XexError('the environment rows are %d bytes, too short to carry '
                       'the remaster extension' % stride)
    rows = []
    seen = set()
    o = start
    while True:
        if o + stride > len(img):
            raise XexError('the environment table runs off the end of the image')
        rid = struct.unpack_from('>H', img, o)[0]
        if rid == 0:
            break                                   # ENVIRONMENTDATA_END
        if rid in seen:
            raise XexError('environment id %d occurs twice in the table' % rid)
        seen.add(rid)
        e = o + stride - EXT_LEN
        fog_end, r, g, b_, pad, far = struct.unpack_from('>I4BI', img, e)
        near, _unused = struct.unpack_from('>ff', img, e + 12)
        if pad != 0 or not (math.isfinite(near) and 0.0 < near < far) \
                or not (0 < fog_end <= far):
            raise XexError('environment row %d does not carry a well-formed '
                           'remaster extension' % rid)
        rows.append(dict(id=rid, fog_end=float(fog_end), rgb=(r, g, b_)))
        o += stride
        if len(rows) > MAX_ROWS:
            raise XexError('the environment table has no terminator')
    if len(rows) < matched:
        raise XexError('the environment table is shorter than the rows that '
                       'identified it')
    return rows, dict(stride=stride, matched=matched, rows=len(rows))


# ---- the generated file ("SLEN") -------------------------------------------
#
# Little-endian throughout. Mirrors src/gfx/sl_gfx_worldasset.h, which is the
# authority on it.
#
#   header (32)  'S' 'L' 'E' 'N'  u32 version  u32 row_count  u32 row_size
#                u32 file_size    u32 reserved[3] (0)
#   row    (16)  u32 environment id  f32 fog start  f32 fog end
#                u8 red, green, blue  u8 flags (0)

SLEN_VERSION = 1
SLEN_HEADER = 32
SLEN_ROW = 16


def slen_bytes(rows):
    body = b''.join(struct.pack('<Iff4B', r['id'], FOG_START, r['fog_end'],
                                r['rgb'][0], r['rgb'][1], r['rgb'][2], 0)
                    for r in rows)
    size = SLEN_HEADER + len(body)
    return struct.pack('<4s4I3I', b'SLEN', SLEN_VERSION, len(rows), SLEN_ROW,
                       size, 0, 0, 0) + body


def write_slen(path, rows):
    data = slen_bytes(rows)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, path)
    return len(data)


def derive(xex_path, repo, out_path):
    """Read the executable, locate its table, write the metadata. Returns the
    row count; raises XexError on a refusal."""
    with open(xex_path, 'rb') as f:
        data = f.read()
    img = xex_image(data)
    with open(os.path.join(repo, 'src', 'game', 'bgfog.c'), 'r', errors='replace') as f:
        bgfog = f.read()
    with open(os.path.join(repo, 'src', 'bondconstants.h'), 'r', errors='replace') as f:
        consts = f.read()
    refs = reference_rows(bgfog, consts)
    rows, info = locate(img, refs)
    write_slen(out_path, rows)
    return rows, info


def main(argv=None):
    import argparse
    import sys
    ap = argparse.ArgumentParser(description='derive the remaster stage-fog '
                                             'environment metadata (#68)')
    ap.add_argument('--xex', required=True, help='the user\'s own executable')
    ap.add_argument('--out', required=True, help='the world-asset root')
    a = ap.parse_args(argv)
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out_path = os.path.join(a.out, 'xbla', 'environment.slen')
    try:
        rows, info = derive(a.xex, repo, out_path)
    except (XexError, OSError) as exc:
        print('  environment : NOT derived - %s' % exc)
        print('                the replacement background draws without the '
              'remaster\'s stage fog')
        return 2
    print('  environment : %d rows on a %d-byte stride (%d identified by the '
          'original game\'s rows) -> %s'
          % (info['rows'], info['stride'], info['matched'], out_path))
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())
