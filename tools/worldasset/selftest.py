"""Checks for the world-asset preparation tooling (#68).

Every fixture is SYNTHETIC - a bundle this file lays out itself, a few bytes
that are deliberately not one - so this needs no source, no prepared asset and
no ROM, and it is the check to run after touching anything under
tools/worldasset. CI runs it with nothing installed.

    .venv\\Scripts\\python.exe tools\\worldasset\\selftest.py
"""
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import slwa                                                  # noqa: E402
import xblaworld                                             # noqa: E402

_pass = _fail = 0


def ck(cond, what):
    global _pass, _fail
    if cond:
        _pass += 1
    else:
        _fail += 1
        print('FAIL: %s' % what)


def quad(n=4):
    return [(float(i), float(i * 2), float(i * 3), 0.25 * i, 0.5,
             10 + i, 20, 30, 255) for i in range(n)]


def read_back(path):
    with open(path, 'rb') as f:
        d = f.read()
    assert d[:4] == b'SLWA'
    g = lambda o: struct.unpack_from('<I', d, o)[0]          # noqa: E731
    return dict(blob=d, version=g(4), cls=g(8),
                path=d[12:44].split(b'\0')[0].decode('ascii'),
                nv=g(72), ni=g(76), nm=g(80), nt=g(84), nmesh=g(88),
                v_off=g(92), i_off=g(96), m_off=g(100), t_off=g(104),
                h_off=g(108), size=g(112))


# ---- #68 the stage-fog environment extractor -------------------------------
#
# EVERYTHING HERE IS FABRICATED: the "original" rows, the executable, its
# table, the extension values and the shader tables are made up for the test.

def _fake_refs():
    # (id, blend, far, near, maxvis, maxobf, dif, farint, r, g, b, clouds, rep)
    return [(11 + i, 3 + i, 1000 + 250 * i, 40 + i, 50 + i, 60 + i, 900 + i,
             1000, (7 * i) & 0xff, (11 * i) & 0xff, (13 * i) & 0xff, i & 1, 700 + i)
            for i in range(10)]


def _fake_row(ref, ext, stride=56):
    base = struct.pack('>8H4BH', *ref[:8], *ref[8:12], ref[12])
    base += b'\x5a' * (stride - 20 - len(base))
    fog_end, rgb, far, near = ext
    return base + struct.pack('>I4BIff', fog_end, rgb[0], rgb[1], rgb[2], 0,
                              far, near, 0.5)


def _fake_table(refs, extra_after=3, stride=56):
    rows = []
    for i, r in enumerate(refs):
        rows.append(_fake_row(r, (2000 + i, (i, 2 * i, 3 * i), 9000 + i, 2.0), stride))
        if i == extra_after:          # a row the original table never had
            extra = (r[0] + 500,) + r[1:]
            rows.append(_fake_row(extra, (4321, (9, 8, 7), 8000, 3.0), stride))
    rows.append(b'\0' * stride)       # the terminator
    return b''.join(rows)


def _fake_xex(image, enc=0, comp=1, split=None):
    """A minimal XEX2 around `image`: one optional header (the file-format
    descriptor) and, for BASIC compression, two data blocks with the zero run
    between them taken OUT of the stored data - so the reader has to put it
    back."""
    if split is None:
        split = (len(image) // 3, 64)
    a, z = split
    if comp == 1:
        stored = image[:a] + image[a + z:]
        blocks = [(a, z), (len(image) - a - z, 0)]
    else:
        stored = image
        blocks = []
    fmt = 0x18 + 8
    desc = struct.pack('>IHH', 8 + 8 * len(blocks), enc, comp)
    desc += b''.join(struct.pack('>II', d, zz) for (d, zz) in blocks)
    data_off = fmt + len(desc)
    data_off = (data_off + 15) & ~15
    hdr = b'XEX2' + struct.pack('>IIIII', 0, data_off, 0, 0, 1)
    hdr += struct.pack('>II', 0x000003FF, fmt)
    hdr += desc
    hdr += b'\0' * (data_off - len(hdr))
    return hdr + stored


def envtests(tmp):
    import xexenv

    refs = _fake_refs()
    table = _fake_table(refs)
    junk = bytes((i * 37) & 0xff for i in range(333))
    image = junk + table + junk
    # the zero run the BASIC form stores out of line sits inside the junk
    for i in range(200, 264):
        image = image[:i] + b'\0' + image[i + 1:]

    x = _fake_xex(image, split=(200, 64))
    ck(xexenv.xex_image(x) == image,
       'a BASIC-compressed image is rebuilt with its zero fill put back')
    ck(xexenv.xex_image(_fake_xex(image, comp=0)) == image,
       'an uncompressed image is read whole')
    for what, blob in (('an ENCRYPTED executable is refused',
                        _fake_xex(image, enc=1)),
                       ('a compression form that needs a decompressor is refused',
                        _fake_xex(image, comp=2)),
                       ('a file that is not an XEX2 is refused', b'NOPE' + x[4:]),
                       ('a truncated executable is refused', x[:0x20])):
        try:
            xexenv.xex_image(blob)
            ck(False, what)
        except xexenv.XexError:
            ck(True, what)

    rows, info = xexenv.locate(image, refs)
    ck(info['stride'] == 56 and info['matched'] == 10,
       'the table is found by its original rows, on its own stride')
    ck(len(rows) == 11, 'the walk covers the rows the original never had, '
                        'up to the terminator')
    ck(rows[0]['id'] == 11 and rows[0]['fog_end'] == 2000.0
       and rows[0]['rgb'] == (0, 0, 0), 'a row yields its id, fog end and colour')
    ck(rows[4]['id'] == 514 and rows[4]['fog_end'] == 4321.0
       and rows[4]['rgb'] == (9, 8, 7), 'an inserted row is carried with its own extension')

    def refused(what, img, rf=refs):
        try:
            xexenv.locate(img, rf)
            ck(False, what)
        except xexenv.XexError:
            ck(True, what)

    refused('two matching tables are ambiguous and refused', image + table)
    refused('too few original rows cannot identify a table', image, refs[:5])
    bad = bytearray(image)
    o = len(junk) + 2 * 56 + 56 - 20 + 12          # row 2's near plane
    bad[o:o + 4] = struct.pack('>f', 1.0e9)
    refused('a row whose extension is malformed refuses the table', bytes(bad))
    refused('an image without the table is refused', junk * 3)

    # the ORIGINAL rows, read from decomp-shaped source text
    consts = ('typedef enum X { A_NONE = -1, A_ONE, A_TWO = 10, A_THREE } X;\n'
              'typedef enum Y { V_BASE, V_CIN = 900 } Y;\n')
    src = ('#if defined(VERSION_EU)\nEnvironmentRecord fog_tables[] = {\n'
           '    {A_ONE, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0 },\n};\n'
           '#else\n/**\n * Address 0x0.\n*/\nEnvironmentRecord fog_tables[] = {\n'
           '    {A_ONE             , 5, 1500, 33, 44, 6, 0, 0x3E7, 0x3E3, 0x3E8, 0x10, 0x30, 0x60, 1, 5000, 0, 0, 255, 255, 255, 0, 0,0,0, -1000, 0, 0, 0, 0, 0, 0.0 },\n'
           '    {A_THREE + V_CIN   , 30, 1500, 33, 44, 6, 0, 0x3E7, 0x3E3, 0x3E8, 0x10, 0x30, 0x60, 1, 5000, 0, 0, 255, 255, 255, 0, 0,0,0, -1000, 0, 0, 0, 0, 0, 7.5 },\n'
           '    {ENVIRONMENTDATA_END},\n};\n#endif\n')
    rr = xexenv.reference_rows(src, consts)
    ck(len(rr) == 2, 'the non-EU table is the one read, terminator excluded')
    ck(rr and rr[0][:4] == (0, 5, 1500, 33) and rr[1][0] == 911,
       'ids resolve through the enums, fields in the compact row\'s order')
    ck(rr and rr[0][6:8] == (0x3E3, 0x3E8) and rr[0][8:11] == (0x10, 0x30, 0x60),
       'the intensity pair and the colour are read past the two unused fields')

    blob = xexenv.slen_bytes(rows)
    ck(blob[:4] == b'SLEN' and struct.unpack_from('<IIII', blob, 4)
       == (xexenv.SLEN_VERSION, 11, xexenv.SLEN_ROW, len(blob)),
       'the metadata header states version, rows, row size and length')
    r4 = struct.unpack_from('<Iff4B', blob, xexenv.SLEN_HEADER + 4 * xexenv.SLEN_ROW)
    ck(r4 == (514, xexenv.FOG_START, 4321.0, 9, 8, 7, 0),
       'a metadata row carries id, fog start, fog end and colour and nothing else')
    ck(len(blob) == xexenv.SLEN_HEADER + 11 * xexenv.SLEN_ROW,
       'no executable byte beyond those values is written')
    pth = os.path.join(tmp, 'root', 'xbla', 'environment.slen')
    ck(xexenv.write_slen(pth, rows) == len(blob) and open(pth, 'rb').read() == blob,
       'the file is written whole')

    # which programs consume the stage-fog contract
    g2 = (b'junk c_fog\0c_fogcolour\0 c_other\0ps_3_0\0 c_modelview\0vs_3_0\0'
          b'c_fogcolour\0c_fog\0ps_3_0\0')
    ck(xblaworld.fog_programs(g2) == (2, 2),
       'every pixel program declaring both fog constants is counted')
    g3 = g2 + b'c_fog\0c_ambient\0ps_3_0\0'
    ck(xblaworld.fog_programs(g3) == (3, 2),
       'a pixel program missing one of the pair is not')
    ck(xblaworld.fog_programs(b'c_fog\0c_fogcolour\0vs_3_0\0') == (0, 0),
       'a vertex program is not a pixel program')


# ---- #68 D-023 the generated zenith clouds ----------------------------------
#
# A SYNTHETIC polar dome: rings about a vertical axis through a two-vertex
# pole, u linear in azimuth, v a NON-linear function of the polar angle, and
# an image that is a flat blue gradient at the pole over a textured band made
# here from a fixed-seed noise field. Nothing is read from any source.

def _synthetic_dome(np, cap=True, rings=24, segs=32, size=256):
    u0, R = 0.3, 1000.0
    vof = lambda t: 0.005 + 0.9 * (t / 60.0) ** 1.3                 # noqa: E731
    verts = [(0.0, R, 0.0, 0.7, 0.005, 255, 255, 255, 255),
             (0.0, R, 0.0, 0.2, 0.005, 255, 255, 255, 255)]
    for k in range(1, rings + 1):
        t = np.radians(2.5 * k)
        for j in range(segs + 1):                           # +1: the seam duplicate
            u = u0 + j / float(segs)
            phi = np.radians(-(u - u0) * 360.0)
            verts.append((float(R * np.sin(t) * np.cos(phi)), float(R * np.cos(t)),
                          float(R * np.sin(t) * np.sin(phi)), u, vof(2.5 * k),
                          255, 255, 255, 255))
    idx = []
    for j in range(segs):
        idx += [j % 2, 2 + j, 2 + j + 1]
    for k in range(rings - 1):
        a0, a1 = 2 + k * (segs + 1), 2 + (k + 1) * (segs + 1)
        for j in range(segs):
            idx += [a0 + j, a1 + j, a1 + j + 1, a0 + j, a1 + j + 1, a0 + j + 1]
    # the image: rows are rings; theta of each row from the inverse of vof
    rows = (np.arange(size) + 0.5) / size
    th = 60.0 * np.clip((rows - 0.005) / 0.9, 0, None) ** (1 / 1.3)
    rng = np.random.default_rng(7)
    noise = rng.standard_normal((size, size))
    f = np.fft.rfft2(noise)
    fy = np.fft.fftfreq(size)[:, None]
    fx = np.fft.rfftfreq(size)[None, :]
    noise = np.fft.irfft2(f * np.exp(-(fx * fx + fy * fy) * 400.0), s=(size, size))
    noise /= noise.std()
    band = np.stack([175 + 22 * noise, 190 + 16 * noise, 205 + 10 * noise], -1)
    grad = np.stack([50 + th, 95 + th, 135 + th], -1)[:, None, :] * np.ones((1, size, 1))
    w = np.clip((th - 20.0) / 5.0, 0, 1)[:, None, None] if cap else np.ones((size, 1, 1))
    img = grad * (1 - w) + band * w
    rgba = np.concatenate([np.clip(np.round(img), 0, 255), np.full((size, size, 1), 255.0)],
                          -1).astype(np.uint8)
    return verts, idx, rgba


def watertests():
    """The 36-byte vertex layout (xblaworld.POOL_LAYOUTS) and the translucent
    base-slot rule (prepare.translucent_base). Synthetic fixtures only."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('wa_prepare_w', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)

    # ---- the layout table: named for 36, default everywhere else ----------
    ck(xblaworld.pool_layout(36) == (4, 20, 128.0),
       'a 36-byte pool: position at +4, coordinate at +20, scale 128')
    ck(all(xblaworld.pool_layout(s) == (0, None, None) for s in (12, 24, 32)),
       'every other stride keeps position +0 and a measured coordinate')

    # ---- a 36-byte pool with every field where the vertex program reads it
    #   +0 s16 pair (the wave term's, deliberately NOT the coordinate)
    #   +4 f32 x, y, z   +16 normal   +20 s16 u, v   +24/+28 frame   +32 colour
    def wvtx(x, z, u, v, col=0xff9b8b7e):
        return (struct.pack('>hh', -899 - int(x), 1000 + int(z))
                + struct.pack('>fff', x, -192.5, z)
                + struct.pack('>I', 0x0007fc00)
                + struct.pack('>hh', u, v)
                + struct.pack('>II', 0x1ff00000, 0x37b001ed)
                + struct.pack('>I', col))
    grid = [(float(i % 3) * 96.0, float(i // 3) * 96.0) for i in range(9)]
    pool = b''.join(wvtx(x, z, int(x * 0.25), -int(z * 0.25)) for (x, z) in grid)
    ck(len(pool) == 9 * 36, 'the fixture is nine 36-byte vertices')
    tris = [(0, 1, 4), (0, 4, 3), (1, 2, 5), (1, 5, 4), (3, 4, 7), (3, 7, 6), (4, 5, 8), (4, 8, 7)]
    lo, hi = [-1e4] * 3, [1e4] * 3
    got = xblaworld._uv_format(pool, 0, 36, 9, tris, pos=4, texcoord=20, scale=128.0)
    ck(got is not None and got[0] == 20 and got[1] == 128.0,
       'the named coordinate word and scale are used, and the edge test passes on it')
    if got:
        ck(abs(got[2] - 4.0) < 1e-6, 'the edge ratio is measured from the position at +4')
    vs = xblaworld._pool_vertices(pool, 0, 36, 9, lo, hi, 20, 128.0, pos=4)
    ck(vs is not None and len(vs) == 9, 'the 36-byte pool decodes at the named layout')
    if vs:
        ck(abs(vs[4][0] - 96.0) < 1e-4 and abs(vs[4][1] + 192.5) < 1e-4
           and abs(vs[4][2] - 96.0) < 1e-4, 'the position reads from +4, big-endian')
        ck(abs(vs[4][3] - 24 / 128.0) < 1e-9 and abs(vs[4][4] + 24 / 128.0) < 1e-9,
           'the coordinate reads from +20 over 128, not from the +0 pair')
        ck(vs[0][5:] == (0x7e, 0x8b, 0x9b, 0xff),
           'the colour is the last word, byte-reversed like every other pool')

    # ---- and what it refuses ------------------------------------------------
    ck(xblaworld._pool_vertices(pool, 0, 36, 9, lo, hi, 20, 128.0, pos=0) is None,
       'read from +0 the position is the packed pair and the pool is refused')
    ck(xblaworld._pool_vertices(pool[:-36], 0, 36, 9, lo, hi, 20, 128.0, pos=4) is None,
       'a truncated pool is refused')
    ck(xblaworld._pool_vertices(pool, 0, 36, 9, lo, hi, 8, 128.0, pos=4) is None,
       'a coordinate word that overlaps the position is refused')
    ck(xblaworld._uv_format(pool, 0, 36, 9, tris, pos=4, texcoord=8, scale=128.0) is None,
       'the measurement refuses the same overlap')
    ck(xblaworld._pool_vertices(pool, 0, 36, 9, lo, hi, 20, 128.0, pos=24) is None,
       'a position that runs into the colour word is refused')
    ck(xblaworld._pool_vertices(pool, 0, 36, 9, [0.0] * 3, [50.0] * 3, 20, 128.0, pos=4) is None,
       'a 36-byte pool whose positions leave the item\'s bounds is refused')
    ck(xblaworld._uv_format(pool, 0, 36, 9, [(0, 1, 99)], pos=4, texcoord=20, scale=128.0) is None,
       'a triangle indexing past the pool is refused')

    # ---- the translucent base-slot rule -------------------------------------
    T = [dict(has_alpha=False), dict(has_alpha=False), dict(has_alpha=False),
         dict(has_alpha=True), dict(has_alpha=True)]
    four = {0: 0, 1: 1, 2: 2, 3: 3}
    ck(prepare.translucent_base(four, 0x4702, T) == 3,
       'a translucent four-slot draw takes the one slot whose image carries alpha')
    ck(prepare.translucent_base(four, 0x4701, T) is None,
       'an opaque draw is left to the normal-map rule')
    ck(prepare.translucent_base({0: 0, 1: 3, 2: 4}, 0x4702, T) is None,
       'two alpha images do not decide it')
    ck(prepare.translucent_base({0: 0, 1: 1}, 0x2702, T) is None,
       'no alpha image does not decide it')
    ck(prepare.translucent_base({0: 3}, 0x4702, T) is None,
       'a one-slot draw keeps its only image')
    ck(prepare.translucent_base(four, None, T) is None, 'a draw with no style is untouched')

    # ---- the animation fields of the 36-byte layout (#69) -------------------
    ck(xblaworld.pool_aux_layout(36) == (0, 16) and xblaworld.pool_aux_layout(24) == (None, None),
       'the 36-byte layout names its wave pair at +0 and its normal at +16; no other does')
    ck(xblaworld.snorm10x3(0x0007fc00) == (0.0, 1.0, 0.0),
       'the measured +y normal word decodes to (0, 1, 0)')
    n = xblaworld.snorm10x3(0x201 | (0x3ff << 20))          # x = -511, z = -1
    ck(abs(n[0] + 1.0) < 1e-9 and n[1] == 0.0 and abs(n[2] + 1.0 / 511.0) < 1e-9,
       'a negative lane is two\'s complement over 511')
    ax = xblaworld._pool_aux(pool, 0, 36, 9, 0, 16, 4, 20)
    ck(ax is not None and len(ax) == 9 and ax[4] == (-899 - 96, 1000 + 96, 0.0, 1.0, 0.0),
       'the wave pair reads from +0 as a raw signed pair and the normal from +16')
    ck(xblaworld._pool_aux(pool, 0, 36, 9, 4, 16, 4, 20) is None,
       'a wave pair overlapping the position is refused')
    ck(xblaworld._pool_aux(pool, 0, 36, 9, 0, 20, 4, 20) is None,
       'a normal overlapping the coordinate is refused')
    ck(xblaworld._pool_aux(pool, 0, 36, 9, 0, 0, 4, 20) is None,
       'the same word for both fields is refused')
    zn = b''.join(wvtx(x, z, 0, 0) for (x, z) in grid)
    zn = zn[:16] + b'\0\0\0\0' + zn[20:]                     # one zero normal
    ck(xblaworld._pool_aux(zn, 0, 36, 9, 0, 16, 4, 20) is None,
       'a pool with a zero-length normal is refused as animation fields')

    # ---- the program's literal bank, found from its own microcode -----------
    # A synthetic program: literal bank, then k control-flow pairs whose first
    # is an exec with address k, then the wave fetch in slot k. The fetch
    # is a FULL 16_16 signed integer fetch, stride 9, offset 0.
    def wave_fetch(stride=9, off=0, full=1, mini=0, fmt=25, signed=1, integer=1):
        w0 = (0 & 31) | (full << 19)
        w1 = (signed << 12) | (integer << 13) | (fmt << 16) | (mini << 30)
        w2 = stride | (off << 8)
        return struct.pack('>III', w0, w1, w2)

    def program(lit, k=7, fetch=None):
        cf = struct.pack('>III', k | (6 << 12), 1 << 12, 0)   # exec, addr k
        cf += b'\0' * (12 * (k - 1))
        return struct.pack('>ffff', *lit) + cf + (fetch or wave_fetch())
    blob = b'\0' * 64 + program((0.0, 1.0, 1 / 128.0, 0.0)) + b'\0' * 32 \
        + program((0.0, 1.0, 1 / 128.0, 0.0), k=3)
    got = xblaworld.wave_program_literal(blob)
    ck(got == {(0.0, 1.0, 1 / 128.0, 0.0): 2},
       'both programs are found through their exec address and read the same bank')
    ck(xblaworld.wave_program_literal(b'\0' * 64 + program((0.0, 1.0, 0.25, 0.0), fetch=wave_fetch(stride=6))) == {},
       'a program fetching another stride is not a wave program')
    ck(xblaworld.wave_program_literal(b'\0' * 64 + program((0.0, 1.0, 0.25, 0.0), fetch=wave_fetch(mini=1))) == {},
       'a mini fetch does not name a program')
    bad = b'\0' * 64 + struct.pack('>ffff', 0.0, 1.0, 0.25, 0.0) \
        + struct.pack('>III', 5 | (6 << 12), 1 << 12, 0) + b'\0' * 72 + wave_fetch()
    ck(xblaworld.wave_program_literal(bad) == {},
       'a fetch whose exec does not address it names no program')

    # ---- the animation record, from the upload, the block and the bank -------
    tbl = [(2.0, 1.0, 0.0, 0.0), (0.0,) * 4, (0.25, 0.5, 0.0, 0.0),
           (-3.0, -1.0, 0.0, 0.0), (-3.0, 2.0, 0.0, 0.0), (0.0,) * 4, (0.0,) * 4]
    misc = [(0.0, 0.5, 1.0, 4.0), (0.98, 0.16, 0.0, 6.2831855),
            (1.0, -1 / 6.0, 1 / 120.0, -1 / 5040.0), (-0.5, 1 / 24.0, -1 / 720.0, 1 / 40320.0)]
    block = b'\0' * 16 + struct.pack('>I', 8) + b''.join(struct.pack('>ffff', *r) for r in misc + tbl)
    upload = [(32, [c for r in tbl for c in r])]
    rec, why = xblaworld.wave_record(upload, 16, block, (0.0, 1.0, 1 / 128.0, 0.0))
    ck(rec is not None and rec['coord_scale'] == 1 / 128.0 and rec['amplitude'][:2] == (2.0, 1.0)
       and rec['speed'][:2] == (0.25, 0.5) and rec['freq_b'] == (-3.0, -1.0, 0.0, 0.0)
       and rec['freq_a'] == (-3.0, 2.0, 0.0, 0.0) and rec['turn_bias'] == 0.5
       and abs(rec['turn_scale'] - 0.98) < 1e-6 and abs(rec['turn'] - 6.2831855) < 1e-6
       and abs(rec['poly'][1] + 1 / 6.0) < 1e-7,
       'the record carries the upload, the block\'s evaluation row and the bank\'s scale')
    ck(xblaworld.wave_record(upload, 16, block, None)[0] is None,
       'no literal bank, no record')
    ck(xblaworld.wave_record([], 16, block, (0.0, 1.0, 1 / 128.0, 0.0))[0] is None,
       'no upload, no record')
    ck(xblaworld.wave_record(upload, None, block, (0.0, 1.0, 1 / 128.0, 0.0))[0] is None,
       'no default block, no record')
    ck(xblaworld.wave_record(upload + [(32, [9.0] + [c for r in tbl for c in r][1:])], 16, block,
                             (0.0, 1.0, 1 / 128.0, 0.0))[0] is None,
       'variants that upload different tables are refused')
    wrong = block[:16 + 4 + 16 * 4] + struct.pack('>ffff', 9.0, 1.0, 0.0, 0.0) + block[16 + 4 + 16 * 5:]
    ck(xblaworld.wave_record(upload, 16, wrong, (0.0, 1.0, 1 / 128.0, 0.0))[0] is None,
       'a block that does not repeat the upload is refused')
    ck(xblaworld.wave_record(upload, 16, block, (0.0, 1.0, 0.0, 0.0))[0] is None,
       'a bank with no coordinate scale is refused')
    zt = [(0.0,) * 4] * 7
    ck(xblaworld.wave_record([(32, [c for r in zt for c in r])], 16,
                             block[:20 + 16 * 4] + b''.join(struct.pack('>ffff', *r) for r in zt),
                             (0.0, 1.0, 1 / 128.0, 0.0))[0] is None,
       'a table whose every amplitude is zero animates nothing')

    # ---- the constant state is scoped to the program binding ----------------
    def rec_(op, *args):
        return (0, op, 0, tuple(args))
    words = [struct.unpack('>I', struct.pack('>f', c))[0] for r in tbl for c in r]
    recs = [rec_(xblaworld.OP_VS_BIND, 0, 1, 0, 0, 2, 0),
            rec_(xblaworld.OP_VS_DEFAULTS, 0, 0, 0, 0x34c0, 0),
            rec_(xblaworld.OP_VS_CONSTANTS, 0, (32 << 16) | 7, *words),
            rec_(xblaworld.OP_DRAW, 13, 8, 0x10),
            rec_(xblaworld.OP_VS_BIND, 0, 1, 0, 0, 3, 0),
            rec_(xblaworld.OP_DRAW, 13, 8, 0x20)]
    dr = xblaworld.level_draw_records(recs)
    ck(len(dr) == 2 and dr[0]['vs_block'] == 0x34c0 and len(dr[0]['vs_uploads']) == 1
       and dr[0]['vs_uploads'][0][0] == 32 and abs(dr[0]['vs_uploads'][0][1][0] - 2.0) < 1e-9,
       'a draw sees the block and the upload of its own program binding')
    ck(dr[1]['vs_block'] is None and dr[1]['vs_uploads'] == [],
       'the next binding starts with nothing - the state does not leak forward')

    # ---- the preparation's assembly: aux beside the record table ------------
    P = [dict(aux=[(1, 2, 0.0, 1.0, 0.0), (3, 4, 0.0, 1.0, 0.0)]), dict(aux=None)]
    D = [dict(pool=0, indices=[0, 1, 0], wave=rec), dict(pool=1, indices=[0, 0, 0], wave=None)]
    aux, anims, arep = prepare.material_animation(P, D, {0: 0, 1: 2}, 3)
    ck(anims == [rec] and aux == [(1, 2, 0.0, 1.0, 0.0, 1), (3, 4, 0.0, 1.0, 0.0, 1), (0, 0, 0.0, 0.0, 0.0, 0)]
       and arep['vertices'] == 2 and arep['draws'] == 1,
       'moved vertices carry their fields and the record\'s 1-based index; the rest carry zeros')
    D2 = [dict(pool=0, indices=[0, 1, 0], wave=rec), dict(pool=0, indices=[0, 0, 0], wave=rec)]
    aux, anims, arep = prepare.material_animation(P, D2, {0: 0, 1: 2}, 3)
    ck(len(anims) == 1 and arep['vertices'] == 2, 'two draws sharing a record write it once')
    rec2 = dict(rec, amplitude=(9.0, 0.0, 0.0, 0.0))
    D3 = [dict(pool=0, indices=[0, 1, 0], wave=rec), dict(pool=0, indices=[1, 1, 1], wave=rec2)]
    aux, anims, arep = prepare.material_animation(P, D3, {0: 0, 1: 2}, 3)
    ck(aux is None and anims == [] and arep['conflicts'] == 1,
       'a vertex two records would move is a conflict and the animation is refused whole')
    D4 = [dict(pool=1, indices=[0, 0, 0], wave=rec)]
    aux, anims, arep = prepare.material_animation(P, D4, {0: 0, 1: 2}, 3)
    ck(aux is None and anims == [] and arep['nofields'] == 1,
       'a draw with a record but no fields refuses the animation')
    ck(prepare.material_animation(P, [dict(pool=0, indices=[0], wave=None)], {0: 0}, 2)[0] is None,
       'nothing to animate writes no auxiliary section')

    # ---- the tangent frame of the 36-byte layout (#69 material) -------------
    ck(xblaworld.pool_frame_layout(36) == (28, 24) and xblaworld.pool_frame_layout(24) == (None, None),
       'the 36-byte layout names its first tangent at +28 and its second at +24; no other does')
    fr = xblaworld._pool_frame(pool, 0, 36, 9, 28, 24, 4, 20)
    ck(fr is not None and len(fr) == 9,
       'the frame decodes from the two 2_10_10_10 words')
    if fr:
        x28 = xblaworld.snorm10x3(0x37b001ed)
        y24 = xblaworld.snorm10x3(0x1ff00000)
        ck(fr[4] == x28 + y24, 'the +28 vector comes first, the +24 vector second')
    ck(xblaworld._pool_frame(pool, 0, 36, 9, 20, 24, 4, 20) is None,
       'a tangent overlapping the coordinate is refused')
    ck(xblaworld._pool_frame(pool, 0, 36, 9, 24, 24, 4, 20) is None,
       'the same word for both axes is refused')
    zf = pool[:24] + b'\0\0\0\0' + pool[28:]                       # one zero axis
    ck(xblaworld._pool_frame(zf, 0, 36, 9, 28, 24, 4, 20) is None,
       'a zero-length axis is refused as a frame')

    # ---- the pixel programs, walked from their own control flow --------------
    # A synthetic program: a literal bank of four rows, then two exec pairs
    # (exec addr 4 count 3 seq 0b010101 - three fetches; exec-end addr 7
    # count 1 seq 0b01 - one more), then the four fetches at slots 4..7 of
    # fetch constants 0, 2, 1, 3.
    def tfetch(ci):
        return struct.pack('>III', 1 | (ci << 20), 0x1f1ffe88, 0x4000)

    def cf(op, addr, cnt, seq):
        return (addr & 0xfff) | ((cnt & 7) << 12) | ((seq & 0xfff) << 16) | (op << 44)

    def pair(c0, c1):
        return struct.pack('>III', c0 & 0xffffffff, ((c0 >> 32) & 0xffff) | ((c1 & 0xffff) << 16),
                           (c1 >> 16) & 0xffffffff)
    bank = (struct.pack('>ffff', 0, 0, 0, 0) * 2 + struct.pack('>ffff', 3.0, 0.25, 0, 0)
            + struct.pack('>ffff', 0.4, -0.4, -0.9, 0.7))
    prog = (bank + pair(cf(1, 4, 3, 0b010101), cf(2, 7, 1, 0b01))
            + pair(cf(0, 0, 0, 0), cf(0, 0, 0, 0)) + b'\0' * 24            # slots 2, 3: nop pairs
            + tfetch(0) + tfetch(2) + tfetch(1) + tfetch(3))
    pl = b'\0' * 32 + prog + b'\0' * 32
    got = xblaworld.pixel_programs(pl)
    ck(len(got) == 1 and got[0][1] == [0, 2, 1, 3] and got[0][2][2][0] == 3.0
       and abs(got[0][2][3][2] + 0.9) < 1e-6,
       'the program is found from its fetch, its fetches enumerated from the exec sequence bits, its bank read')
    # an ALU word with the same low bits inside the program is not a fetch
    prog2 = (bank + pair(cf(1, 4, 2, 0b0101), cf(2, 6, 2, 0b0001))
             + pair(cf(0, 0, 0, 0), cf(0, 0, 0, 0)) + b'\0' * 24
             + tfetch(0) + tfetch(1) + tfetch(3) + struct.pack('>III', 0xc8070001, 0, 0))
    got2 = xblaworld.pixel_programs(b'\0' * 32 + prog2)
    ck(len(got2) == 1 and got2[0][1] == [0, 1, 3],
       'a slot the sequence bits call ALU is not counted as a fetch, whatever its low bits')
    ck(xblaworld.pixel_programs(b'\0' * 64 + tfetch(0) * 4) == [],
       'a fetch with no exec addressing it names no program')

    # ---- the material record, from the uploads, the programs and the slots --
    programs = got
    up = [(12, [0.25, 0.25, 0.25, 0.0, 0.05, 0.05, 0.05, 0.05, 0.75, 0.75, 0.75, 1.0]),
          (16, [0.6] * 4)]
    slots = {0: 68, 1: 69, 2: 70, 3: 71}
    rec, why = xblaworld.material_record(up, programs, slots, {1: 8})
    ck(rec is not None and rec['mode'] == 'normal_sphere' and rec['normal'] == 68
       and rec['sphere'] == 69 and rec['offset'] == 70 and rec['base'] == 71
       and abs(rec['reflect'][0] - 0.25) < 1e-9 and abs(rec['warp'] - 0.05) < 1e-9
       and abs(rec['base_weight'][2] - 0.75) < 1e-9 and rec['normal_scale'] == 3.0
       and abs(rec['normal_bias'] + 0.9) < 1e-6
       and abs(rec['sphere_scale'][0] - 0.4) < 1e-6 and abs(rec['sphere_scale'][1] + 0.4) < 1e-6
       and abs(rec['sphere_bias'][0] - 0.4) < 1e-6 and abs(rec['sphere_bias'][1] - 0.4) < 1e-6
       and abs(rec['mean_weight'] - 0.25) < 1e-6
       and abs(rec['alpha_floor'] - 0.7) < 1e-6 and rec['sphere_clamp'] == 1,
       'the record carries the uploads, the bank and the slots, register by register')
    ck(xblaworld.material_record(up, programs, slots, {1: 0})[0]['sphere_clamp'] == 0,
       'the clamp comes from the sphere slot\'s flag bit alone')
    ck(xblaworld.material_record(up, programs, {0: 68, 3: 71}, None)[0] is None,
       'a draw without the four slots has no record')
    ck(xblaworld.material_record(up, [], slots, None)[0] is None,
       'no four-slot program, no record')
    ck(xblaworld.material_record(up, programs + [(0, [0, 1, 2, 3], [(0,) * 4] * 4)], slots, None)[0] is None,
       'four-slot programs with different banks are refused')
    ck(xblaworld.material_record(up[1:], programs, slots, None)[0] is None,
       'a draw that uploads no c12..c14 has no record')
    ck(xblaworld.material_record(up + [(12, [9.0] + up[0][1][1:])], programs, slots, None)[0] is None,
       'variants that upload different constants are refused')
    bad = [(0, [0, 2, 1, 3], [(0,) * 4, (0,) * 4, (0.0, 0.25, 0, 0), (0.4, -0.4, -0.9, 0.7)])]
    ck(xblaworld.material_record(up, bad, slots, None)[0] is None,
       'a bank that scales the normal map by zero is refused')

    # ---- the uploads are scoped to the binding, pixel and vertex alike ------
    pw = [struct.unpack('>I', struct.pack('>f', c))[0] for c in up[0][1]]
    recs = [rec_(xblaworld.OP_VS_BIND, 0, 1, 0, 0, 2, 0),
            rec_(xblaworld.OP_MATERIAL, 0x6ac4, 0x00040000, 68, 0, 69, 0x00010008, 70, 0x00020000, 71, 0x00030000),
            rec_(xblaworld.OP_PS_CONSTANTS, 0, (12 << 16) | 3, *pw),
            rec_(xblaworld.OP_DRAW, 13, 8, 0x10),
            rec_(xblaworld.OP_VS_BIND, 0, 1, 0, 0, 3, 0),
            rec_(xblaworld.OP_DRAW, 13, 8, 0x20)]
    dr = xblaworld.level_draw_records(recs)
    ck(len(dr) == 2 and dr[0]['ps_uploads'] and dr[0]['ps_uploads'][0][0] == 12
       and dr[0]['slots'] == slots and dr[0]['slot_flags'] == {0: 0, 1: 8, 2: 0, 3: 0}
       and dr[0]['vs_uploads'] == [],
       'a draw sees its own program binding\'s pixel uploads, slots and slot flags')
    ck(dr[1]['ps_uploads'] == [], 'the next binding starts with no pixel uploads either')

    # ---- the preparation's frames beside the records -------------------------
    PF = [dict(frame=[(1.0, 0.0, 0.0, 0.0, 0.0, 1.0)] * 2), dict(frame=None)]
    frames, nfr = prepare.material_frames(PF, {0: 1, 1: 3}, 4)
    ck(nfr == 2 and frames[0] == (0.0,) * 6 and frames[1] == (1.0, 0.0, 0.0, 0.0, 0.0, 1.0)
       and frames[2] == frames[1] and frames[3] == (0.0,) * 6,
       'vertices of a pool with a frame carry it; the rest carry zeros')


def zenithtests():
    try:
        import numpy as np
    except ImportError:
        print('worldasset tools selftest: numpy not installed - the zenith-cloud '
              'checks are skipped')
        return
    import importlib.util
    import zenith
    # by path: tools/texpack has a prepare.py too, and xblaworld puts it on
    # the search path first
    spec = importlib.util.spec_from_file_location('wa_prepare', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)

    verts, idx, rgba = _synthetic_dome(np)
    cv, ci, crgba, rep = zenith.generate(rgba, verts)
    g, b = rep['g'], rep['b']
    ck(abs(g['axis'][1] - 1.0) < 1e-6 and abs(g['sign'] + 1.0) < 1e-9
       and abs(((g['u0'] - 0.3) + 0.5) % 1.0 - 0.5) < 1e-6,
       'the pole, the axis and u = u0 - phi/360 are read from the mesh')
    ck(abs(b['th_cap'] - 20.0) < 1.5 and 20.0 < b['th_bs'] < 27.0,
       'the clear cap and the settled band are found by row statistics '
       '(cap to %.1f, band at %.1f; made 20 / 25)' % (b['th_cap'], b['th_bs']))
    # THE CARRIER: a cap of directions about the vertical, with the coverage
    # profile in its vertex alpha - none at LOW, whole from LOW + FADE up,
    # never decreasing toward the zenith.
    P = np.array([v[:3] for v in cv], np.float64)
    el = np.degrees(np.arcsin(P[:, 1] / np.linalg.norm(P, axis=1)))
    alpha = np.array([v[8] for v in cv])
    ck(len(ci) % 3 == 0 and max(ci) < len(cv) and crgba.shape[2] == 4,
       'the carrier is a well-formed triangle list with an RGBA image')
    ck(abs(el.min() - rep['low']) < 1e-3 and el.max() > 89.9,
       'the carrier spans the sky from the lowest profile elevation (%.1f) to the zenith'
       % el.min())
    ck(alpha[np.abs(el - rep['low']) < 1e-3].max() == 0
       and alpha[el >= rep['low'] + rep['fade'] - 1e-3].min() == 255,
       'transparent at the foot of the profile, opaque from the top of its fade up')
    order = np.argsort(el)
    ck(np.all(np.diff(alpha[order]) >= 0),
       'the coverage never falls toward the zenith (no ring)')
    wide, _i = zenith.sky_cap(rep['radius'], rep['low'] - 4.0, rep['fade'])
    ck(len(wide) > len(cv) and min(np.degrees(np.arcsin(v[1] / np.linalg.norm(v[:3]))) for v in wide)
       < rep['low'] - 3.9, 'a broader profile reaches lower in the same tessellation')

    # THE TILE: one period of a torus - no seam where it repeats. The step
    # across the tile's own edge is no larger than the steps inside it.
    t = crgba[..., :3].astype(np.float64)
    inner = np.abs(np.diff(t, axis=1)).mean()
    edge = np.abs(t[:, 0] - t[:, -1]).mean()
    inner_v = np.abs(np.diff(t, axis=0)).mean()
    edge_v = np.abs(t[0] - t[-1]).mean()
    ck(edge < 1.5 * inner and edge_v < 1.5 * inner_v,
       'the tile repeats without a seam (edge step %.2f / %.2f against %.2f / %.2f inside)'
       % (edge, edge_v, inner, inner_v))

    # THE TILE IS THE BAND'S OWN: its tone and its contrast.
    size = 2 * int(b['span'] * b['ppd'])
    c = (np.arange(size) + 0.5 - size / 2) / b['ppd']
    X, Y = np.meshgrid(c, c)
    TH = np.hypot(X, Y)
    plane = zenith.reproject(rgba[..., :3].astype(np.float64), g, TH, np.degrees(np.arctan2(Y, X)))
    bandpx = plane[(TH > b['th_bs']) & (TH < b['th_bs'] + b['dex'])]
    tilepx = t.reshape(-1, 3)
    ck(tilepx.std(0).mean() > 0.5 * bandpx.std(0).mean(),
       'the tile carries the detail of the band (std %.1f against %.1f)'
       % (tilepx.std(0).mean(), bandpx.std(0).mean()))
    ck(np.abs(tilepx.mean(0) - bandpx.mean(0)).max() < 0.5 * bandpx.std(0).mean() + 2,
       'the tile takes the tone of the band (means %s against %s)'
       % (np.round(tilepx.mean(0)), np.round(bandpx.mean(0))))

    # DETERMINISTIC.
    cv2, ci2, crgba2, _ = zenith.generate(rgba, verts)
    ck(cv2 == cv and ci2 == ci and crgba2.tobytes() == crgba.tobytes(),
       'the same image and mesh generate the same bytes')

    # DEGRADES SAFELY: no clear cap, or no dome, adds nothing.
    _v, _i, nocap = _synthetic_dome(np, cap=False)
    try:
        zenith.generate(nocap, verts)
        ck(False, 'an image with no clear cap is refused')
    except zenith.NotApplicable:
        ck(True, 'an image with no clear cap is refused')
    try:
        zenith.generate(rgba, [(float(i % 7), float(i % 5), float(i % 3), 0.1 * (i % 9),
                                0.01 * i, 255, 255, 255, 255) for i in range(64)])
        ck(False, 'a mesh that is not a polar dome is refused')
    except zenith.NotApplicable:
        ck(True, 'a mesh that is not a polar dome is refused')

    # THE BUNDLE: the source's records are untouched and the carrier is one
    # more draw, flagged - so leaving flagged draws out is the source exactly.
    tex = {'has_alpha': False, 'rgba': rgba}
    V, I = list(verts), list(idx)
    M, T, H = [(0, slwa.BLEND_OPAQUE)], [(256, 256, rgba.tobytes())], [(0, 0, len(idx))]
    snap = (list(V), list(I), list(M), list(T), list(H))
    n = prepare.add_zenith_clouds([(0, verts, tex)], V, I, M, T, H)
    ck(n == 1 and (V[:len(snap[0])], I[:len(snap[1])], M[:1], T[:1], H[:1]) == snap,
       'the carrier is appended after the source records, which are untouched')
    ck(M[1] == (1, slwa.BLEND_ALPHA, slwa.MAT_GENERATED | slwa.MAT_CLOUD_PLANE) and H[1][0] == 1,
       'the carrier is a blended draw of its own image, flagged GENERATED and CLOUD_PLANE')
    V2, I2, M2, T2, H2 = list(verts), list(idx), [(0, 0)], [(256, 256, nocap.tobytes())], [(0, 0, len(idx))]
    ck(prepare.add_zenith_clouds([(0, verts, {'has_alpha': False, 'rgba': nocap})],
                                 V2, I2, M2, T2, H2) == 0 and len(M2) == 1 and len(V2) == len(verts),
       'an image with no clear cap leaves the bundle exactly as it was')
    ck(slwa.MAT_GENERATED == 4 and slwa.MAT_COVERAGE_BLUE == 8 and slwa.MAT_PANORAMA == 16
       and slwa.MAT_CLOUD_PLANE == 32 and slwa.MAT_CULL_BACK == 64
       and slwa.MAT_GAME_WATER == 128 and slwa.MAT_KNOWN == 255,
       'the GENERATED, COVERAGE_BLUE, PANORAMA, CLOUD_PLANE, CULL_BACK and GAME_WATER flags '
       'mirror src/gfx/sl_gfx_worldasset.h')
    for bad in (slwa.MAT_CLOUD_PLANE, slwa.MAT_CLOUD_PLANE | slwa.MAT_GENERATED):
        cls = slwa.CLASS_SKYDOME if bad == slwa.MAT_CLOUD_PLANE else slwa.CLASS_BACKGROUND
        try:
            slwa.write(os.path.join(tempfile.gettempdir(), 'sl_cloud_bad.slwa'), cls, 'dam',
                       quad(4), [0, 1, 2], [(0, slwa.BLEND_ALPHA, bad)],
                       [(64, 16, bytes(range(256)) * 16)], [(0, 0, 3)])
            ck(False, 'a CLOUD_PLANE material off a generated skydome draw is refused')
        except slwa.SlwaError:
            ck(True, 'a CLOUD_PLANE material off a generated skydome draw is refused')


def overlaytests(tmp):
    """#78: a shaped draw lying wholly on an earlier opaque surface is an
    overlay and takes the decal pass; nothing else does. Synthetic items."""
    import prepare

    def quad(x0, y0, x1, y1, z=0.0):
        vs = [(x0, y0, z, 0.0, 0.0, 255, 255, 255, 255), (x1, y0, z, 1.0, 0.0, 255, 255, 255, 255),
              (x1, y1, z, 1.0, 1.0, 255, 255, 255, 255), (x0, y1, z, 0.0, 1.0, 255, 255, 255, 255)]
        return vs, [0, 1, 2, 0, 2, 3]

    def item(parts):
        V, I, M = [], [], []
        for mat, (vs, ix) in parts:
            M.append((mat, len(I), len(ix)))
            I += [len(V) + i for i in ix]
            V += vs
        return V, I, M
    O, X, D, T = slwa.BLEND_OPAQUE, slwa.BLEND_CUTOUT, slwa.MAT_DECAL, slwa.BLEND_ALPHA
    mats = [(0, O, 0), (1, X, 0), (2, X, 0), (3, T, 0), (4, X, D)]
    wall = quad(0.0, 0.0, 100.0, 50.0)
    V, I, M = item([(0, wall), (1, quad(10.0, 10.0, 20.0, 30.0)), (2, quad(30.0, 10.0, 40.0, 30.0, 5.0))])
    ck(prepare.overlay_meshes(V, I, M, mats) == {1},
       "a shaped quad in the wall's plane after the wall is an overlay; one standing off it is not")
    V, I, M = item([(1, quad(10.0, 10.0, 20.0, 30.0)), (0, wall)])
    ck(prepare.overlay_meshes(V, I, M, mats) == set(),
       'a shaped quad drawn BEFORE the surface it lies on is not taken')
    V, I, M = item([(0, wall), (1, quad(90.0, 10.0, 130.0, 30.0))])
    ck(prepare.overlay_meshes(V, I, M, mats) == set(),
       'a shaped quad only partly on the surface is not an overlay')
    V, I, M = item([(0, wall), (3, quad(10.0, 10.0, 20.0, 30.0)), (4, quad(30.0, 10.0, 40.0, 30.0))])
    ck(prepare.overlay_meshes(V, I, M, mats) == set(),
       'a translucent draw, and one the source already marks a decal, are left as they are')
    V, I, M = item([(0, wall), (1, quad(10.0, 10.0, 20.0, 30.0, 0.004))])
    ck(prepare.overlay_meshes(V, I, M, mats) == {1},
       'within the coplanar tolerance (%.2f item units) it is still on the surface' % prepare.OVERLAY_EPS)
    # #95 an overlay whose image nothing else draws, and the mesh table without it
    mats2 = [(0, O, 0), (1, X, 0), (2, X, 0), (2, X, D)]
    V, I, M = item([(0, wall), (1, quad(10.0, 10.0, 20.0, 30.0)), (2, quad(30.0, 10.0, 40.0, 30.0)),
                    (3, quad(50.0, 10.0, 60.0, 30.0, 5.0))])
    over = prepare.overlay_meshes(V, I, M, mats2)
    ck(over == {1, 2} and prepare.overlay_alone(M, mats2, over) == {1},
       '#95 of two overlays, the one whose image no other mesh draws is alone; the one whose '
       'image a decal draws elsewhere is not')
    G = xblaworld.STYLE_DECAL
    ck(prepare.overlays_withheld({1, 2, 3}, {1: {0x4b01}, 2: {0x4b01}, 3: {0x2702}}, {1, 3}) == {1},
       '#95 an overlay is withheld only in no decal-pass gate (%#x) and with its image drawn '
       'nowhere else' % G)
    M2, I2, renum = prepare.drop_meshes(M, I, {1})
    ck(len(M2) == 3 and renum == {0: 0, 2: 1, 3: 2} and len(I2) == len(I) - 6
       and M2[1] == (2, 6, 6) and I2[6:12] == I[12:18],
       '#95 a withheld mesh leaves the table with its triangles; the rest keep their order')


def panoramatests(tmp):
    """D-026: the panorama shell is found from the geometry alone - the
    outermost set of meshes that lies wholly beyond the rest and surrounds the
    centre - and its materials are the only ones marked. Synthetic items."""
    import math
    import prepare

    def ring(r, y0, y1, cols, cx=0.0, cz=0.0, arc=360.0):
        vs, ix = [], []
        for k in range(cols + (0 if arc >= 360.0 else 1)):
            a = math.radians(arc * k / cols)
            for y in (y0, y1):
                vs.append((cx + r * math.sin(a), y, cz + r * math.cos(a), 0.0, 0.0, 255, 255, 255, 255))
        n = len(vs) // 2
        for k in range(n if arc >= 360.0 else n - 1):
            a0, a1 = 2 * k, 2 * ((k + 1) % n)
            ix += [a0, a1, a0 + 1, a1, a1 + 1, a0 + 1]
        return vs, ix

    def item(parts):
        V, I, M = [], [], []
        for mat, (vs, ix) in parts:
            M.append((mat, len(I), len(ix)))
            I += [len(V) + i for i in ix]
            V += vs
        return V, I, M

    ground = ring(900.0, 0.0, 50.0, 24)          # scenery, within 900
    hills = ring(700.0, 0.0, 400.0, 30)
    shell = ring(1200.0, 100.0, 900.0, 16)       # the outer ring
    cap = ring(1200.0, 900.0, 1100.0, 16)        # its fade band, same ring
    V, I, M = item([(0, ground), (1, hills), (2, shell), (3, cap)])
    mats, rep = prepare.panorama_shell(V, I, M, 4)
    ck(mats == {2, 3} and rep['meshes'] == [2, 3],
       'the outer ring and its band are the shell, and nothing inside them')
    ck(rep['inner'] < rep['near'] and rep['gap'] < prepare.SHELL_SURROUND,
       'the shell lies wholly beyond the rest and closes the circle')
    far = ring(1500.0, 0.0, 300.0, 6, arc=60.0)   # a lone far rock: not a ring
    V, I, M = item([(0, ground), (1, far)])
    ck(prepare.panorama_shell(V, I, M, 2)[0] is None,
       'something far out that does not surround the item is not a shell')
    inner_edge = ring(100.0, 0.0, 50.0, 24)       # terrain from near the centre ...
    terrain = (inner_edge[0] + ground[0],
               inner_edge[1] + [len(inner_edge[0]) + i for i in ground[1]])
    V, I, M = item([(0, terrain), (1, hills)])  # ... out to 900: nothing lies beyond the rest
    ck(prepare.panorama_shell(V, I, M, 2)[0] is None,
       'an item with no separate outer ring marks none of its scenery')
    V, I, M = item([(0, ground), (2, hills), (2, shell)])
    ck(prepare.panorama_shell(V, I, M, 3)[0] is None,
       'a ring that shares its material with scenery inside it is refused')
    over = ring(1250.0, 0.0, 30.0, 40)            # scenery reaching past the ring
    V, I, M = item([(0, ground), (1, over), (2, shell)])
    mats, _r = prepare.panorama_shell(V, I, M, 3)
    ck(mats is None or 2 not in mats,
       'a ring with scenery beyond it is not the outermost - it is not a shell')
    # D-023 and D-026 are decided per item: the Dam's, D-023 Runway's and Surface's too, D-026 Surface's
    ck(prepare.divergence_applies('D-023', 'dam') and prepare.divergence_applies('D-026', 'dam'),
       'the zenith clouds and the panorama shell are written for the Dam')
    ck(prepare.divergence_applies('D-023', 'runway') and prepare.divergence_applies('D-023', 'surface')
       and prepare.divergence_applies('D-023', 'frigate'),
       "the zenith clouds are written for Runway's, Surface's and Frigate's domes")
    ck(prepare.divergence_applies('D-023', 'surface2')
       and not prepare.divergence_applies('D-026', 'surface2')
       and not prepare.divergence_applies('D-034', 'surface2'),
       "Surface 2's own dome takes the zenith clouds; it has no background item of its own")
    ck(not prepare.divergence_applies('D-026', 'frigate')
       and not prepare.divergence_applies('D-023', 'bunker'),
       "Frigate's background keeps no panorama shell, and Bunker 1 prepares no dome cap")
    # D-034: Frigate's dome alone is written as its cloud coverage
    ck(prepare.divergence_applies('D-034', 'frigate')
       and not prepare.divergence_applies('D-034', 'surface')
       and not prepare.divergence_applies('D-034', 'dam'),
       "the cloud cover is written for Frigate's dome alone")
    import numpy as _np
    px = _np.array([[[120, 170, 230, 255], [235, 235, 240, 200], [240, 200, 200, 255],
                     [150, 150, 160, 255], [0, 0, 0, 255], [40, 40, 45, 17]]], dtype=_np.uint8)
    cv = prepare.cloud_cover(px)
    ck(cv[0, 0, 0] == 0 and cv[0, 4, 0] == 0,
       'clear blue sky and black are no cloud')
    ck(cv[0, 1, 0] >= 240 and cv[0, 2, 0] >= 120 and 150 <= cv[0, 3, 0] <= 200,
       'white cloud is full cover, pink cloud partial, a grey cloud by its brightness')
    ck(all(cv[0, k, 0] == cv[0, k, 1] == cv[0, k, 2] for k in range(6))
       and list(cv[0, :, 3]) == [255, 200, 255, 255, 255, 17],
       'the cover is grey and every alpha is kept')
    recs = [(6, 1, px.tobytes())]
    ck(prepare.grade_cloud_cover(recs) == 1 and recs[0][:2] == (6, 1)
       and recs[0][2] == _np.ascontiguousarray(cv).tobytes(),
       'grading a bundle\'s records replaces each image by its cover, in place')
    os.environ['SL_WA_AS_AUTHORED'] = 'D-034'
    ck(not prepare.divergence_applies('D-034', 'frigate'),
       'SL_WA_AS_AUTHORED=D-034 prepares the dome as authored')
    del os.environ['SL_WA_AS_AUTHORED']
    ck(prepare.divergence_applies('D-026', 'surface'),
       "the panorama shell is marked on Surface's background (D-030)")
    ck(not prepare.divergence_applies('D-023', 'facility')
       and not prepare.divergence_applies('D-026', 'runway')
       and not prepare.divergence_applies('D-026', 'facility')
       and not prepare.divergence_applies('D-999', 'dam'),
       'another stage, or a code with no decision, is prepared as the source authors it')
    # the writer: the bit round-trips on a background and is refused elsewhere
    d = os.path.join(tmp, 'pano.slwa')
    slwa.write(d, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2],
               [(0, slwa.BLEND_OPAQUE, slwa.MAT_PANORAMA | slwa.MAT_FOG)],
               [(64, 16, bytes(range(256)) * 16)], [(0, 0, 3)])
    raw = open(d, 'rb').read()
    ck(struct.unpack_from('<I', raw, read_back(d)['m_off'] + 8)[0]
       == slwa.MAT_PANORAMA | slwa.MAT_FOG
       and read_back(d)['version'] == slwa.VERSION_STATIC,
       'the PANORAMA bit round-trips beside FOG at the same version')
    try:
        slwa.write(os.path.join(tmp, 'pano-sky.slwa'), slwa.CLASS_SKYDOME, 'dam', quad(4),
                   [0, 1, 2], [(0, slwa.BLEND_OPAQUE, slwa.MAT_PANORAMA)],
                   [(64, 16, bytes(range(256)) * 16)], [(0, 0, 3)])
        ck(False, 'a PANORAMA material is refused on a skydome')
    except slwa.SlwaError:
        ck(True, 'a PANORAMA material is refused on a skydome')
    # #96 THE FACE CULL: the bit round-trips on a background, is refused
    # elsewhere, and the reader carries the RS_CULL value standing over a draw
    d = os.path.join(tmp, 'cull.slwa')
    slwa.write(d, slwa.CLASS_BACKGROUND, 'control', quad(4), [0, 1, 2],
               [(0, slwa.BLEND_OPAQUE, slwa.MAT_CULL_BACK | slwa.MAT_FOG)],
               [(64, 16, bytes(range(256)) * 16)], [(0, 0, 3)])
    ck(struct.unpack_from('<I', open(d, 'rb').read(), read_back(d)['m_off'] + 8)[0]
       == slwa.MAT_CULL_BACK | slwa.MAT_FOG and read_back(d)['version'] == slwa.VERSION_STATIC,
       'the CULL_BACK bit round-trips beside FOG at the same version')
    try:
        slwa.write(os.path.join(tmp, 'cull-sky.slwa'), slwa.CLASS_SKYDOME, 'dam', quad(4),
                   [0, 1, 2], [(0, slwa.BLEND_OPAQUE, slwa.MAT_CULL_BACK)],
                   [(64, 16, bytes(range(256)) * 16)], [(0, 0, 3)])
        ck(False, 'a CULL_BACK material is refused on a skydome')
    except slwa.SlwaError:
        ck(True, 'a CULL_BACK material is refused on a skydome')

    def crec(op, *args):
        return (0, op, 0, tuple(args))
    crecs = [crec(0x0c, xblaworld.RS_CULL, 0), crec(xblaworld.OP_DRAW, 4, 3, 0x10),
             crec(0x0d, xblaworld.RS_CULL, 0x10), crec(xblaworld.OP_DRAW, 4, 3, 0x20),
             crec(0x0c, 0x48, 6), crec(xblaworld.OP_DRAW, 4, 3, 0x30),
             crec(0x0d, xblaworld.RS_CULL, 0), crec(xblaworld.OP_DRAW, 4, 3, 0x40)]
    cd = xblaworld.level_draw_records(crecs)
    ck([d_['cull'] for d_ in cd] == [0, 0x10, 0x10, 0],
       'a draw carries the RS_CULL value written last before it, by either op, and '
       'another register written between leaves it standing: %r' % [d_['cull'] for d_ in cd])
    ck(xblaworld.level_draw_records([crec(xblaworld.OP_DRAW, 4, 3, 0x10)])[0]['cull'] == 0,
       'a draw no RS_CULL record precedes is not culled')

    # #92 the image limit: Depot's background item carries 136 images, past
    # the 128 the format accepted before. The writer's limit is the runtime's.
    import re
    hdr = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                            'src', 'gfx', 'sl_gfx_worldasset.h')).read()
    m = re.search(r'#define SL_WA_MAX_TEXTURES\s+(\d+)u', hdr)
    ck(m is not None and int(m.group(1)) == slwa.MAX_TEXTURES == 256,
       'the image limit (256) mirrors src/gfx/sl_gfx_worldasset.h')
    d = os.path.join(tmp, 'images136.slwa')
    try:
        slwa.write(d, slwa.CLASS_BACKGROUND, 'depot', quad(4), [0, 1, 2],
                   [(135, slwa.BLEND_OPAQUE)], [(4, 4, bytes(range(64)))] * 136, [(0, 0, 3)])
        n136 = struct.unpack_from('<I', open(d, 'rb').read(), 84)[0]
    except slwa.SlwaError:
        n136 = None
    ck(n136 == 136, "a background bundle of 136 images is written (Depot's item)")
    try:
        slwa.write(os.path.join(tmp, 'images-past.slwa'), slwa.CLASS_BACKGROUND, 'depot',
                   quad(4), [0, 1, 2], [(0, slwa.BLEND_OPAQUE)],
                   [(4, 4, bytes(range(64)))] * (slwa.MAX_TEXTURES + 1), [(0, 0, 3)])
        ck(False, 'an image count past the limit is refused')
    except slwa.SlwaError:
        ck(True, 'an image count past the limit is refused')


def chartests(tmp):
    """#70: the skinned item - the version-4 writer, the pose reader and the
    vertex reader, on synthetic bytes only."""
    import struct as st
    import xblachar

    out = os.path.join(tmp, 'charroot')
    p = os.path.join(out, 'xbla', 'char', 'testguard.slwa')
    img = bytes(range(256)) * 16                              # 64x16 RGBA8
    joints = [('SKEL_BASE', -1, (0.0, 0.0, 0.0)), ('SKEL_NECK', 0, (0.0, 10.0, 0.0))]
    skin = [((0, 255),), ((1, 255),), ((0, 128), (1, 127)), ((1, 255),)]
    meshes = [(0, 0, 3, 0), (0, 3, 3, slwa.MESH_BUILTIN_HEAD)]
    n = slwa.write_skinned(p, slwa.CLASS_CHAR, 'testguard', quad(4), [0, 1, 2, 1, 3, 2],
                           [(0, slwa.BLEND_OPAQUE)], [(64, 16, img)], meshes, skin, joints)
    b = open(p, 'rb').read()
    ck(len(b) == n and st.unpack_from('<I', b, 112)[0] == n,
       'the skinned writer declares the size it wrote')
    ck(st.unpack_from('<II', b, 4) == (slwa.VERSION_SKINNED, slwa.CLASS_CHAR),
       'a skinned item is version 4 of the character class')
    ko, jo, nj = st.unpack_from('<III', b, 116)
    ck(nj == 2 and ko % 4 == 0 and jo % 4 == 0 and st.unpack_from('<I', b, 44)[0] == 0,
       'the skin and joint sections are declared, aligned, and +44 stays reserved')
    ck(b[ko + 2 * 8: ko + 2 * 8 + 8] == bytes((0, 1, 0, 0, 128, 127, 0, 0)),
       'a blended vertex carries both joints and weights')
    ck(b[jo + 48: jo + 57] == b'SKEL_NECK' and st.unpack_from('<I', b, jo + 24)[0] == 0xFFFFFFFF
       and st.unpack_from('<I', b, jo + 48 + 24)[0] == 0
       and st.unpack_from('<f', b, jo + 48 + 32)[0] == 10.0,
       'the joint table carries names, parents and binds')
    ho = st.unpack_from('<I', b, 108)[0]
    ck(st.unpack_from('<I', b, ho + 16 + 12)[0] == slwa.MESH_BUILTIN_HEAD,
       'the built-in-head mark is written on its mesh')
    to = st.unpack_from('<I', b, 104)[0]
    w_, h_, doff, dlen = st.unpack_from('<IIII', b, to)
    ck((w_, h_, dlen) == (64, 16, len(img)) and b[doff:doff + dlen] == img and doff % 4 == 0,
       'the image payload sits where its record says, after the new sections')
    q = os.path.join(out, 'xbla', 'char', 'again.slwa')
    slwa.write_skinned(q, slwa.CLASS_CHAR, 'testguard', quad(4), [0, 1, 2, 1, 3, 2],
                       [(0, slwa.BLEND_OPAQUE)], [(64, 16, img)], meshes, skin, joints)
    ck(open(q, 'rb').read() == b, 'the skinned writer is deterministic')

    def refuses(what, **over):
        a = dict(asset_class=slwa.CLASS_CHAR, skin=skin, joints=joints, meshes=meshes)
        a.update(over)
        try:
            slwa.write_skinned(os.path.join(out, 'bad.slwa'), a['asset_class'], 'testguard',
                               quad(4), [0, 1, 2, 1, 3, 2], [(0, slwa.BLEND_OPAQUE)],
                               [(64, 16, img)], a['meshes'], a['skin'], a['joints'])
        except slwa.SlwaError:
            ck(True, what)
            return
        ck(False, what)
    refuses('a sky is not a skinned class', asset_class=slwa.CLASS_SKYDOME)
    refuses('weights that do not sum to 255', skin=[((0, 255),), ((1, 254),)] + skin[2:])
    refuses('a joint past the table', skin=[((2, 255),)] + skin[1:])
    refuses('more than four influences',
            skin=[((0, 51), (1, 51), (0, 51), (1, 51), (0, 51))] + skin[1:])
    refuses('a parent that is not an earlier joint',
            joints=[('SKEL_BASE', 1, (0, 0, 0)), ('SKEL_NECK', 0, (0, 10, 0))])
    refuses('a joint name the field cannot carry',
            joints=[('SKEL_base', -1, (0, 0, 0)), ('SKEL_NECK', 0, (0, 10, 0))])
    refuses('a mesh flag the format does not define', meshes=[(0, 0, 3, 0), (0, 3, 3, 2)])
    refuses('a skin entry per vertex, exactly', skin=skin[:3])

    # the POSE reader, on a block laid out by hand: three joints, the bind of
    # each its parent's plus its offset, names in the table after the records
    def pose_block(rel, parents, names, rot_w=1.0, selfs=None):
        n_ = len(rel)
        g = bytearray(b'rendergraph\x00'.ljust(64, b'\x00'))
        at = len(g)
        g += b'pose\x00' + b'\x00' * (0x18 - 5) + st.pack('>I', n_)
        g += b'\x00' * (0x38 - (len(g) - at))
        absp = []
        for j in range(n_):
            base = (0.0, 0.0, 0.0) if parents[j] < 0 else absp[parents[j]]
            absp.append(tuple(base[k] + rel[j][k] for k in range(3)))
        for j in range(n_):
            me = j if selfs is None else selfs[j]
            g += st.pack('>3f', *rel[j]) + st.pack('>3f', *absp[j]) + st.pack('>4f', 0, 0, 0, rot_w)
            g += st.pack('>6H', 0xffff if parents[j] < 0 else parents[j], 0xffff, 0xffff, me, 0xffff, 0)
        strs = len(g) + 12 * n_ + 16
        table = b''
        blob = b''
        for j, nm in enumerate(names):
            table += st.pack('>III', strs + len(blob), 0, j)
            blob += nm.encode() + b'\x00'
        g += table + b'\x00' * 16 + blob
        while len(g) % 4:
            g += b'\x00'
        return bytes(g)
    rel = [(0.0, 0.0, 0.0), (5.0, 190.0, 0.0), (450.0, -470.0, 0.0)]
    par = [-1, 0, 1]
    nm = ['SKEL_POSITION', 'SKEL_BASE', 'SKEL_LF_HIP']
    js = xblachar.read_pose(pose_block(rel, par, nm), 'fixture')
    ck([j['name'] for j in js] == nm and [j['parent'] for j in js] == par,
       'the pose reader names every joint and its parent')
    ck(js[2]['bind'] == (455.0, -280.0, 0.0), 'a bind is the parent chain of the offsets')

    def pose_refused(what, g):
        try:
            xblachar.read_pose(g, 'fixture')
        except xblachar.CharacterError:
            ck(True, what)
            return
        ck(False, what)
    pose_refused('a record that does not name itself is refused',
                 pose_block(rel, par, nm, selfs=[0, 2, 1]))
    pose_refused('a bind rotation is refused rather than ignored',
                 pose_block(rel, par, nm, rot_w=0.5))
    pose_refused('a joint with no name is refused', pose_block(rel, par, nm[:2]))
    pose_refused('no pose block at all is refused', b'rendergraph\x00' + b'\x00' * 200)

    # #80 the snow guard's stray root: a side's joint authored as a second
    # root takes the mirror of its mirror's parent, and the order is mended
    rel_s = [(-376.0, -4662.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (456.0, -466.0, 0.0),
             (-425.0, -477.0, 0.0), (-60.0, -1958.0, 0.0), (37.0, -2020.0, 0.0),
             (24.0, -2173.0, 0.0)]
    par_s = [-1, -1, 1, 2, 2, 3, 4, 6]
    nm_s = ['SKEL_LF_ANKLE', 'SKEL_POSITION', 'SKEL_BASE', 'SKEL_LF_HIP', 'SKEL_RT_HIP',
            'SKEL_LF_KNEE', 'SKEL_RT_KNEE', 'SKEL_RT_ANKLE']
    rel_s[0] = (376.0, -4662.0, 0.0)
    js = xblachar.read_pose(pose_block(rel_s, par_s, nm_s), 'fixture')
    mj, perm = xblachar.mend_stray_roots(js, 'fixture')
    names = [j['name'] for j in mj]
    ck(sum(1 for j in mj if j['parent'] < 0) == 1
       and mj[names.index('SKEL_LF_ANKLE')]['parent'] == names.index('SKEL_LF_KNEE')
       and all(j['parent'] < k for k, j in enumerate(mj))
       and [names[perm[k]] for k in range(len(nm_s))] == nm_s
       and mj[names.index('SKEL_LF_ANKLE')]['bind'] == js[0]['bind'],
       'a side joint authored as a second root takes its mirror\'s parent, parents first (#80)')
    same, perm1 = xblachar.mend_stray_roots(xblachar.read_pose(pose_block(rel, par, nm), 'fixture'))
    ck(perm1 == [0, 1, 2] and [j['parent'] for j in same] == par,
       'a skeleton with one root is left as read')
    rel_f = list(rel_s)
    rel_f[0] = (376.0, -9000.0, 0.0)
    jf = xblachar.read_pose(pose_block(rel_f, par_s, nm_s), 'fixture')
    ck(xblachar.mend_stray_roots(jf)[0][0]['parent'] == -1,
       'a stray root whose bone would not match its mirror\'s is left for the runtime to refuse')

    # the VERTEX reader: a blended stride-36 and a rigid stride-32 record
    v36 = (st.pack('>3f', 1.0, 2.0, 3.0) + st.pack('>HH', 3, 0) + st.pack('>HH', 0xf000, 0xf000)
           + bytes((0, 0, 55, 200)) + b'\x00' * 4 + st.pack('>HH', 16384, 8192)
           + bytes((255, 10, 20, 30)))
    vert, infl = xblachar._vertex(v36, 0, 36, 0, [4, 7], [0, 5, 6, 7, 8, 9, 10, 11], 'fixture')
    ck(vert == (1.0, 2.0, 3.0, 0.5, 0.25, 30, 20, 10, 255),
       'a blended record reads position, coordinate (1/32768) and colour a,b,g,r')
    ck(list(infl) == [(11, 200), (8, 55)],
       'its weights pair in reverse lane order with slots through the palette and bones')
    v32 = (st.pack('>3f', 4.0, 5.0, 6.0) + st.pack('>HH', 0, 0xf000) + st.pack('>HH', 0xf000, 0xf000)
           + b'\x00' * 4 + st.pack('>HH', 0, 32767) + bytes((255, 255, 255, 255)))
    vert, infl = xblachar._vertex(v32, 0, 32, 0, [2], [0, 1, 9], 'fixture')
    ck(list(infl) == [(9, 255)] and vert[:3] == (4.0, 5.0, 6.0),
       'a rigid record binds wholly to its one slot')
    try:
        bad = v36[:20] + bytes((0, 0, 55, 199)) + v36[24:]
        xblachar._vertex(bad, 0, 36, 0, [4, 7], [0, 5, 6, 7, 8, 9, 10, 11], 'fixture')
        ck(False, 'weights short of 255 are refused')
    except xblachar.CharacterError:
        ck(True, 'weights short of 255 are refused')

    # #90 THE RECORD THE PROGRAMS DECLARE: a full fetch of the position at +0
    # and mini fetches of the rest. A 20-byte record whose programs fetch +16
    # as a 16_16 coordinate and no 8_8_8_8 colour reads its coordinate there
    # and white for its colour; one fetching +16 as a colour reads as before.
    def fetches(stride, words):
        out = st.pack('>3I', 1 << 19, xblachar.FETCH_FLOAT3 << 16, stride // 4)
        for off, fmt in words:
            out += st.pack('>3I', 0, (1 << 30) | (fmt << 16), (off // 4) << 8)
        return out + st.pack('>3I', 0x7, 0x7, 0x7)
    coord = fetches(20, ((12, 7), (16, xblachar.FETCH_COORD)))
    colour = fetches(20, ((12, 7), (16, xblachar.FETCH_COLOUR)))
    ck(xblachar.static_fetches(coord) == {20: [{0: xblachar.FETCH_FLOAT3, 12: 7,
                                                 16: xblachar.FETCH_COORD}]},
       'the programs\' record layout is read from the fetches')
    ck(xblachar.coord_only_strides(coord) == {20},
       'a 20-byte record fetched as normal and coordinate carries no colour')
    ck(xblachar.coord_only_strides(colour) == set()
       and xblachar.coord_only_strides(coord + colour) == set()
       and xblachar.coord_only_strides(b'\x00' * 64) == set(),
       'a colour fetch anywhere, or no declaration, keeps the colour layout')
    v20 = st.pack('>3f', 7.0, 8.0, 9.0) + st.pack('>I', 0x08f79bc5) + st.pack('>hh', 16384, 8192)
    ck(xblachar._static_vertex(v20, 0, 20, 0, 'fixture', {20})
       == (7.0, 8.0, 9.0, 0.5, 0.25, 255, 255, 255, 255),
       'a coordinate-only record reads its coordinate at +16 and white')
    ck(xblachar._static_vertex(v20, 0, 20, 0, 'fixture')[5:] == (0x00, 0x20, 0x00, 0x40),
       'without the declaration +16 is the colour word, as before')


# ---- #70 a stage's character set, and its preparation ----------------------
#
# EVERYTHING HERE IS FABRICATED: a miniature decomp tree (enums, model records,
# random pools, a setup, the AI command headers) that exists only for this
# test, and an archive index of made-up paths.

_FIX_BONDCONSTANTS = r'''
#define _mkshort(a, b)            ((a << 8) | (b & 0xff))
#define _mkword(a, b)             ((a << 16) | (b & 0xffff))
typedef enum GUARD_SETUP_FLAG // u16
{
    GUARD_SETUP_FLAG_NONE      = 0,
    GUARD_SETUP_FLAG_CHR_CLONE = 0x0004, // set CHRFLAG_CLONE
} GUARD_SETUP_FLAG;
    typedef enum BODIES
    {
#ifndef __sgi
        SL_BODY_FORCE_SIGNED = -1,
#endif
        BODY_Soldier,
        BODY_Officer,
        #ifdef ALL_BONDS
        BODY_Never,
        #endif
        BODY_Lady,
        BODY_Hooded,
        BODY_Spare,
        HEAD_START,
        BODY_Head_A = HEAD_START,
        BODIES_MAX
    } BODIES;
    typedef enum HEADS
    {
        HEAD_A = HEAD_START,
        HEAD_B,
        HEAD_C,
        HEAD_F_START,
        HEAD_Female_X = HEAD_F_START,
        HEAD_Female_Y,
        HEAD_END,
        HEAD_COUNT = HEAD_END - HEAD_START,
#ifdef __sgi
        HEAD_RANDOM       = 0xFFFFFF9F
#else
        HEAD_RANDOM       = -97 /* keeps the enum signed */
#endif
    } HEADS;
'''

_FIX_MODELS = [  # dir, file, male, has_head
    ('soldier', 'CsoldierZ', 1, 0), ('officer', 'CofficerZ', 1, 0),
    ('lady', 'CladyZ', 0, 0), ('hooded', 'ChoodedZ', 1, 1), ('spare', 'CspareZ', 1, 0),
    ('heada', 'CheadaZ', 1, 0), ('headb', 'CheadbZ', 1, 0), ('headc', 'CheadcZ', 1, 0),
    ('headx', 'CheadxZ', 0, 0), ('heady', 'CheadyZ', 0, 0),
]

_FIX_SETUP = r'''
#include "bondaicommands.h"
s32 propDefs[];
stagesetup UsetupfooZ = { &pathwaypoints, &pathsets, &intro, &propDefs, &patrolpaths };
s32 propDefs[] = {
    /* a fixed head on the officer */
    _mkword(0, _mkshort(0, 9)), _mkword(1, 5), _mkword(1, 1036), 0xffffffff, _mkword(1000, 100), _mkword(0, 7), 0,
    /* an objective between the guards: four words the walk must step over */
    _mkword(0, _mkshort(0, 23)), 2, 11288, 0.0f,
    /* a random head on the soldier */
    _mkword(0, _mkshort(0, 9)), _mkword(2, 6), _mkword(0, 1036), 0xffffffff, _mkword(1000, 100), _mkword(0, 0xffff), 0,
    /* the lady, a clone, random head */
    _mkword(0, _mkshort(0, 9)), _mkword(3, 7), _mkword(2, 2), 0xffffffff, _mkword(1000, 100), _mkword(4, 0xffff), 0,
    /* the hooded body carries its own head: none is worn */
    _mkword(0, _mkshort(0, 9)), _mkword(4, 8), _mkword(3, 2), 0xffffffff, _mkword(1000, 100), _mkword(0, 0xffff), 0,
    _mkword(0, _mkshort(0, 48))
};
u8 ai_1000[] = {
    chr_try_spawning_at_pad(0x01, 0xff, 0x0010, 0x0f04, 0x00000000, 0x07)
    ai_end
};
'''


def _fixture_tree(base):
    def put(rel, text):
        p = os.path.join(base, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w', newline='\n') as f:
            f.write(text)
    put('tools/trace/sltrace/levelboot.py',
        'LEVEL_IDS = {\n    "fooland": 3,\n    "bare": 1,\n    "broken": 2,\n    "random": 4,\n}\n')
    put('src/game/chraidata.c',
        'u8 m_x[] = { ai_end };\n/* comment */\nchar *setup_text_pointers[] = {\n'
        '    NULL, NULL, "UsetupbrokenZ", "UsetupfooZ", "UsetuprandZ", NULL\n};\n')
    put('src/bondconstants.h', _FIX_BONDCONSTANTS)
    put('src/bondaicommands.h',
        '#define chr_try_spawning_at_pad(body_num, head_num, pad, ai_list, bitfield, label) \\\n'
        '        0xBD, body_num, head_num, pad, ai_list, bitfield, label,\n'
        '#define chr_other(prop_num, item_num) 0xBF, prop_num, item_num,\n')
    put('src/aicommands2.h',
        '#define TRYSpawningChrAtPad(BODY_NUM, HEAD_NUM, PAD, AI_LIST_ID, BITFIELD, GOTOLABEL)  \\\n'
        '                    AI_TRYSpawningChrAtPad, BODY_NUM, HEAD_NUM,\n')
    put('src/game/chr.c',
        's32 list_of_bodies[] = {\n    BODY_Soldier, BODY_Spare, -1,\n};\n'
        's32 random_male_heads[] = {\n    HEAD_A, HEAD_B,\n#ifdef __sgi\n    HEAD_Female_X,\n#endif\n'
        '    HEAD_C, -1\n};\n'
        's32 random_female_heads[] = {\n    HEAD_Female_X, HEAD_Female_Y, -1\n};\n')
    inc = ['struct ChrModelFileRecord c_item_entries[] = {']
    for d, f, male, hh in _FIX_MODELS:
        inc.append('    #include <assets/obseg/chr/%s/chrModelFileRecord.inc.c>' % d)
        put('assets/obseg/chr/%s/chrModelFileRecord.inc.c' % d,
            '{&%s_header, "%s", 1.0, 1.0, 0x%02x, 0x%02x, 0x00, 0x00},\n' % (d, f, male, hh))
    inc.append('    {0, 0, 1.0, 0.0, 0x00, 0x00, 0x00, 0x00}\n};\n')
    put('assets/obseg/chr/chrModelFileRecords.inc.c', '\n'.join(inc))
    put('assets/obseg/setup/UsetupfooZ.c', _FIX_SETUP)
    put('assets/obseg/setup/UsetupbrokenZ.c',
        'stagesetup UsetupbrokenZ = { &a, &b, &c, &propDefs };\n'
        's32 propDefs[] = { _mkword(0, _mkshort(0, 9)), 1, 2 };\n')
    # the random-body door: body 0xFFFF draws from list_of_bodies
    put('assets/obseg/setup/UsetuprandZ.c',
        'stagesetup UsetuprandZ = { &a, &b, &c, &propDefs };\n'
        's32 propDefs[] = {\n'
        '    _mkword(0, _mkshort(0, 9)), _mkword(1, 5), _mkword(0xffff, 2), 0, 0, _mkword(0, 0xffff), 0,\n'
        '    _mkword(0, _mkshort(0, 48)) };\n')


def stagecharstests(tmp):
    import stagechars as sc

    base = os.path.join(tmp, 'stageroot')
    _fixture_tree(base)

    # (1) discovery from the tables: every door, every body, every head
    inv = sc.inventory('fooland', base)
    bkeys = [r['key'] for r in inv['bodies']]
    hkeys = [r['key'] for r in inv['heads']]
    ck(inv['levelid'] == 3 and inv['setup'] == 'UsetupfooZ' and inv['guards'] == 4,
       'the level resolves through LEVEL_IDS and setup_text_pointers to its setup, '
       'and the propdef walk finds every guard record')
    ck(bkeys == ['char/soldier', 'char/officer', 'char/lady', 'char/hooded'],
       'every body a guard record or an AI spawn names is in the set, and only those: %r'
       % bkeys)
    ck(hkeys == ['head/heada', 'head/headb', 'head/headc', 'head/headx', 'head/heady'],
       'the heads are the male pool, the female pool and the fixed head: %r' % hkeys)
    why = dict((r['key'], r['reasons']) for r in inv['bodies'] + inv['heads'])
    ck(why['char/lady'] == ['setup guard', 'clone'] and why['char/hooded'] == ['setup guard'],
       'a clone-flagged guard is recorded as a clone door; a body with its own head '
       'takes none: %r' % (why['char/lady'],))
    ck(why['char/officer'] == ['setup guard', 'AI chr_try_spawning_at_pad'],
       'an AI spawn command is a door of its own: %r' % (why['char/officer'],))
    ck(any(w.startswith('random_male_heads via ') for w in why['head/headc'])
       and 'setup guard, explicit head' in why['head/headc'],
       'a head can be both fixed and drawn from the pool: %r' % (why['head/headc'],))
    ck(why['head/headx'] == ['random_female_heads via setup guard, clone'],
       'a female body draws the female pool: %r' % (why['head/headx'],))

    # (2) the random pool expands to every candidate, exactly once
    male = [r['key'] for r in inv['heads']
            if any(w.startswith('random_male_heads') for w in r['reasons'])]
    ck(male == ['head/heada', 'head/headb', 'head/headc'] and len(set(hkeys)) == len(hkeys),
       'the male pool is expanded whole, each head once, and the __sgi-only entry is '
       'not in it: %r' % male)
    rinv = sc.inventory('random', base)
    ck([r['key'] for r in rinv['bodies']] == ['char/soldier', 'char/spare']
       and rinv['bodies'][1]['reasons'] == ['setup guard (list_of_bodies)']
       and [r['key'] for r in rinv['heads']] == ['head/heada', 'head/headb', 'head/headc'],
       'the random body expands to list_of_bodies, each then drawing its pool')

    # (3) the provider key is the runtime's own rule
    ck(sc.provider_key('char', 'Cgreatguard2Z') == 'char/greatguard2'
       and sc.provider_key('head', 'CheadleeZ') == 'head/headlee'
       and sc.provider_key('char', 'CGreatGuard2Z') == 'char/greatguard2',
       'CfooZ -> char/foo and CheadbarZ -> head/headbar, lowered')
    ck(sc.item_of('PfooZ') is None and sc.item_of('Cfoo') is None
       and sc.item_of('Cfoo-barZ') is None and sc.item_of('CZ') is None
       and sc.item_of('C' + 'a' * 32 + 'Z') is None and sc.item_of('C' + 'a' * 31 + 'Z'),
       'a name that is not C<item>Z, or too long for the runtime field, has no key')

    # (8) a stage the tables cannot answer for fails, and says why
    for lvl, frag in (('nowhere', 'unknown level'), ('bare', 'has no setup'),
                      ('broken', 'runs past the list')):
        try:
            sc.inventory(lvl, base)
            ck(False, 'level %r is refused' % lvl)
        except sc.InventoryError as exc:
            ck(frag in str(exc), 'level %r is refused with "%s": %s' % (lvl, frag, exc))

    # (6) the same tables give the same answer, in model order
    ck(sc.inventory('fooland', base) == inv
       and [r['index'] for r in inv['heads']] == sorted(r['index'] for r in inv['heads']),
       'the inventory is deterministic and ordered by model index')

    # (4)(5)(7) preparation: present, absent, current, refused
    out = os.path.join(tmp, 'stageout')
    stamps = os.path.join(tmp, 'stagestamps')
    os.makedirs(stamps, exist_ok=True)
    paths = ['Top\\files\\new\\char\\soldier\\default.bin',
             'Top\\files\\original\\head\\heada\\default.bin',   # not the remastered item
             'Top\\files\\new\\head\\headb\\default.bin',
             'Top\\files\\new\\head\\headb_hits\\default.rbh',
             'Top\\files\\new\\head\\headc\\notes.txt']
    keys = ['char/soldier', 'head/heada', 'head/headb', 'head/headc']
    stale = os.path.join(out, 'xbla', 'head', 'heada.slwa')
    os.makedirs(os.path.dirname(stale), exist_ok=True)
    open(stale, 'wb').write(b'an older derivation')
    rows = sc.plan(keys, paths, out, stamps, 'S1')
    st = dict((r['key'], r['state']) for r in rows)
    ck([r['key'] for r in rows] == keys, 'the plan keeps the order it was given')
    ck(st == {'char/soldier': sc.BUILD, 'head/heada': sc.ABSENT, 'head/headb': sc.BUILD,
              'head/headc': sc.ABSENT},
       'a remastered item is planned, a missing one (or only another generation, or no '
       'bundle file) is SOURCE ABSENT: %r' % st)
    ck(rows[0]['files'] == ['Top\\files\\new\\char\\soldier\\default.bin']
       and rows[2]['files'] == ['Top\\files\\new\\head\\headb\\default.bin'],
       'only the item\'s own files are extracted, not a neighbour sharing its prefix')
    calls = []

    def extract(dest, lst):
        calls.append(('x', open(lst).read().split()))
        return 0

    def convert(cls, item, tree):
        calls.append(('c', cls, item))
        p = os.path.join(out, 'xbla', cls, item + '.slwa')
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(b'bundle')
        return 'converted'
    sc.execute(rows, 'S1', os.path.join(tmp, 'stagescratch'), extract, convert)
    st = dict((r['key'], r['state']) for r in rows)
    ck(st['char/soldier'] == sc.PREPARED and st['head/headb'] == sc.PREPARED
       and st['head/heada'] == sc.ABSENT,
       'present items are prepared; the absent one is reported, not an error')
    ck(calls[0] == ('x', ['Top\\files\\new\\char\\soldier\\default.bin',
                          'Top\\files\\new\\head\\headb\\default.bin'])
       and [c for c in calls if c[0] == 'c'] == [('c', 'char', 'soldier'), ('c', 'head', 'headb')],
       'everything is extracted in ONE pass, then each item converted')
    ck(not os.path.exists(stale) and 'removed the stale' in rows[1]['reason'],
       'an absent item leaves no older bundle answering for it')
    ck(open(os.path.join(stamps, 'char-soldier.stamp')).read().strip() == 'S1',
       'a prepared item is stamped with the source and tools it came from')

    calls[:] = []
    rows = sc.plan(keys, paths, out, stamps, 'S1')
    sc.execute(rows, 'S1', os.path.join(tmp, 'stagescratch'), extract, convert)
    ck([r['state'] for r in rows] == [sc.CURRENT, sc.ABSENT, sc.CURRENT, sc.ABSENT]
       and calls == [],
       'a rerun over an unchanged source rebuilds nothing and extracts nothing')
    ck([r['state'] for r in sc.plan(keys, paths, out, stamps, 'S2')][0] == sc.BUILD
       and [r['state'] for r in sc.plan(keys, paths, out, stamps, 'S1', force=True)][0] == sc.BUILD,
       'a changed source or tool, or -Force, builds again')

    rows = sc.plan(['char/soldier', 'head/headb'], paths, out, stamps, 'S3')

    def refuse(cls, item, tree):
        if item == 'headb':
            raise ValueError('headb: a vertex is bound to nothing')
        return convert(cls, item, tree)
    sc.execute(rows, 'S3', os.path.join(tmp, 'stagescratch'), extract, refuse)
    ck(rows[0]['state'] == sc.PREPARED and rows[1]['state'] == sc.REFUSED
       and rows[1]['reason'].startswith('headb: a vertex is bound to nothing')
       and not os.path.exists(os.path.join(out, 'xbla', 'head', 'headb.slwa')),
       'a refused conversion names its reason, the others still prepare, and the '
       'refused item keeps no bundle')
    rows = sc.plan(['char/soldier'], paths, out, stamps, 'S4')
    sc.execute(rows, 'S4', os.path.join(tmp, 'stagescratch'), lambda d, l: 2, convert)
    ck(rows[0]['state'] == sc.REFUSED and '7-Zip exit 2' in rows[0]['reason'],
       'a source that will not extract is a refusal, with the extractor\'s exit code')

    # the command line: a level's absent part is a fallback (exit 0); a named
    # item the source lacks is refused (exit 1)
    idx = os.path.join(tmp, 'stageindex.txt')
    open(idx, 'w').write('\n'.join(paths) + '\n')
    for k in ('char/soldier', 'char/officer', 'char/lady', 'char/hooded', 'head/headb'):
        c, i = k.split('/')
        p = os.path.join(out, 'xbla', c, i + '.slwa')
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(b'bundle')
        open(os.path.join(stamps, '%s-%s.stamp' % (c, i)), 'w').write('S9\n')
    common = ['--index', idx, '--archive', 'none', '--sevenzip', 'none', '--out', out,
              '--stamps', stamps, '--stamp', 'S9', '--root', base]
    import io
    import contextlib
    sink = io.StringIO()
    with contextlib.redirect_stdout(sink):
        rc_level = sc.main(['prepare', '--level', 'fooland'] + common)
        rc_items = sc.main(['prepare', '--items', 'char/soldier,head/heada'] + common)
        rc_bad = sc.main(['prepare', '--level', 'nowhere'] + common)
    text = sink.getvalue()
    ck(rc_level == 0 and 'SOURCE ABSENT' in text and "draws as the game's own" in text,
       'a stage run with absent parts succeeds and says those parts fall back')
    ck(rc_items == 1 and 'holds no remastered head/heada' in text,
       'the diagnostic list refuses an item the source does not hold')
    ck(rc_bad == 1 and "unknown level 'nowhere'" in text,
       'an unknown level fails before anything is read')

    # the texture coordinate's scale is measured per item
    import xblachar as xc

    def vx(u, v):
        return (0.0, 0.0, 0.0, u, v, 255, 255, 255, 255)
    ck(xc.texcoord_scale([vx(0.1, 0.99), vx(0.5, 0.2)]) == 1.0,
       'an item that reaches its atlas edge at the base scale keeps it')
    ck(xc.texcoord_scale([vx(0.0, 0.25), vx(0.5, 0.49)]) == 2.0
       and xc.texcoord_scale([vx(0.2, 0.24)]) == 4.0,
       'an item that reaches only half (a quarter) of the way is read at twice '
       '(four times) the scale')
    for what, vs in (('reaches no edge', [vx(0.3, 0.1)]), ('is all zero', [vx(0.0, 0.0)]),
                     ('has no vertices', []), ('stays too small', [vx(0.01, 0.01)])):
        try:
            xc.texcoord_scale(vs)
            ck(False, 'an item whose coordinate range %s is refused' % what)
        except xc.CharacterError:
            ck(True, 'an item whose coordinate range %s is refused' % what)

    # the gated block of a HEAD item is the head itself
    try:
        slwa.write_skinned(os.path.join(tmp, 'gh.slwa'), slwa.CLASS_HEAD, 'gh', quad(4),
                           [0, 1, 2], [(0, slwa.BLEND_OPAQUE)],
                           [(64, 16, bytes(range(256)) * 16)],
                           [(0, 0, 3, slwa.MESH_BUILTIN_HEAD)],
                           [((0, 255),)] * 4, [('SKEL_NECK', -1, (0.0, 0.0, 0.0))])
        ck(False, 'a head item may not mark a draw as a body\'s built-in head')
    except slwa.SlwaError:
        ck(True, 'a head item may not mark a draw as a body\'s built-in head')
    try:
        import numpy                                          # noqa: F401
    except ImportError:
        print('worldasset tools selftest: numpy not installed - the gated-head '
              'preparation check is skipped')
        return
    import prepare
    import xblachar
    real = xblachar.read_character
    tree = os.path.join(tmp, 'gatedtree')
    img = dict(name='t', w=64, h=16, rgba=bytes(range(256)) * 16)
    fake = dict(joints=[dict(name='SKEL_NECK', parent=-1, bind=(0.0, 0.0, 0.0))],
                vertices=quad(4), skin=[((0, 255),)] * 4, textures=[img],
                draws=[dict(image=0, builtin_head=False, indices=[0, 1, 2]),
                       dict(image=0, builtin_head=True, indices=[1, 3, 2])])
    xblachar.read_character = lambda path: fake
    try:
        flags = {}
        for cls in ('char', 'head'):
            d = os.path.join(tree, 'Top', prepare.REMASTERED, cls, 'gated')
            os.makedirs(d, exist_ok=True)
            open(os.path.join(d, 'default.bin'), 'wb').write(b'x')
            prepare.convert_character(cls, 'gated', tree, os.path.join(tmp, 'gatedout'))
            b = open(os.path.join(tmp, 'gatedout', 'xbla', cls, 'gated.slwa'), 'rb').read()
            ho = struct.unpack_from('<I', b, 108)[0]
            flags[cls] = [struct.unpack_from('<I', b, ho + 16 * m + 12)[0] for m in range(2)]
    finally:
        xblachar.read_character = real
    ck(flags == {'char': [0, slwa.MESH_BUILTIN_HEAD], 'head': [0, 0]},
       'a gated draw is the built-in head on a body and the head itself on a head '
       'item: %r' % flags)


def viewmodeltests(tmp):
    """#70: the first-person view model and the player's body - the writer's
    class GUN and own-head mark, the condition table, the view-model and
    own-head preparation rules, the executable's name table and the stage's
    player derivation. Synthetic bytes, except the decomp tables."""
    import struct as st
    import xblachar
    import viewmodels as vm

    # the switch condition word
    ck(slwa.mesh_switch_flags([]) == 0 and slwa.mesh_switch_flags([11]) == 12 << 8
       and slwa.mesh_switch_flags([13, 11]) == (14 << 8) | (12 << 16),
       'a draw names none, one or two game switches, each as 1 + its index')
    for bad in ([1, 2, 3], [64], [-1]):
        try:
            slwa.mesh_switch_flags(bad)
            ck(False, 'switch conditions %r are refused' % bad)
        except slwa.SlwaError:
            ck(True, 'switch conditions %r are refused' % bad)

    img = bytes(range(256)) * 16
    gj = [('SKEL_TOP', -1, (0.0, -10.0, -20.0), 0), ('SKEL_FINGER', 0, (-4.0, -8.0, -12.0), 7),
          ('BEAN_END', 1, (-4.0, -8.0, -12.0), 0)]
    gskin = [((0, 255),), ((1, 255),), ((0, 255),), ((1, 255),)]
    gm = [(0, 0, 3, 0), (0, 3, 3, slwa.mesh_switch_flags([11]))]
    p = os.path.join(tmp, 'vm', 'xbla', 'gun', 'testgun.slwa')
    slwa.write_skinned(p, slwa.CLASS_GUN, 'testgun', quad(4), [0, 1, 2, 1, 3, 2],
                       [(0, slwa.BLEND_OPAQUE)], [(64, 16, img)], gm, gskin, gj)
    b = open(p, 'rb').read()
    ko, jo, nj = st.unpack_from('<III', b, 116)
    ho = st.unpack_from('<I', b, 108)[0]
    ck(st.unpack_from('<II', b, 4) == (slwa.VERSION_SKINNED, slwa.CLASS_GUN) and nj == 3
       and st.unpack_from('<I', b, jo + 48 + 40)[0] == 7
       and st.unpack_from('<I', b, jo + 40)[0] == 0
       and st.unpack_from('<I', b, ho + 16 + 12)[0] == 12 << 8,
       'a view model is version 4 of class GUN, its joint part and draw condition written')

    def refuses(what, cls, joints, meshes):
        try:
            slwa.write_skinned(os.path.join(tmp, 'vm', 'bad.slwa'), cls, 'bad', quad(4),
                               [0, 1, 2, 1, 3, 2], [(0, slwa.BLEND_OPAQUE)],
                               [(64, 16, img)], meshes, gskin, joints)
            ck(False, what)
        except slwa.SlwaError:
            ck(True, what)
    refuses('a view model joint without its part is refused', slwa.CLASS_GUN,
            [j[:3] for j in gj], gm)
    refuses('a game part past the table is refused', slwa.CLASS_GUN,
            [gj[0], ('SKEL_FINGER', 0, (0.0, 0.0, 0.0), 65), gj[2]], gm)
    refuses('the built-in-head mark is refused on a view model', slwa.CLASS_GUN, gj,
            [(0, 0, 3, 0), (0, 3, 3, slwa.MESH_BUILTIN_HEAD)])
    refuses('a character joint carries no part', slwa.CLASS_CHAR, gj, [(0, 0, 3, 0), (0, 3, 3, 0)])
    cj = [j[:3] for j in gj]
    refuses('a character draw carries no switch condition', slwa.CLASS_CHAR, cj, gm)
    refuses('the own-head mark on some meshes only is refused', slwa.CLASS_CHAR, cj,
            [(0, 0, 3, 0), (0, 3, 3, slwa.MESH_OWN_HEAD)])
    refuses('a head item cannot carry the own-head mark', slwa.CLASS_HEAD, cj,
            [(0, 0, 3, slwa.MESH_OWN_HEAD), (0, 3, 3, slwa.MESH_OWN_HEAD)])
    refuses('a built-in head beside the own-head mark is refused', slwa.CLASS_CHAR, cj,
            [(0, 0, 3, slwa.MESH_OWN_HEAD),
             (0, 3, 3, slwa.MESH_OWN_HEAD | slwa.MESH_BUILTIN_HEAD)])

    # the condition table
    g = bytearray(0x400)
    st.pack_into('>I', g, 0x28, 0x100)
    st.pack_into('>II', g, 0x100, 2, 0x110)
    g[0x300:0x316] = b'|TOP|wppk|SW_FLASH\x00\x00\x00\x00'
    g[0x320:0x340] = b'|TOP|gun|SW_HANDOFF4\x00' + b'\x00' * 11
    st.pack_into('>IIII', g, 0x110, 0x300, 0x30a, 0, 4)
    st.pack_into('>IIII', g, 0x160, 0x320, 0x329, 1, 1)
    ck(xblachar.read_conditions(bytes(g), 'fx') == ['SW_FLASH', 'SW_HANDOFF4'],
       'the condition table reads each record\'s leaf by its index')
    st.pack_into('>I', g, 0x160 + 8, 5)
    try:
        xblachar.read_conditions(bytes(g), 'fx')
        ck(False, 'a condition record naming another index is refused')
    except xblachar.CharacterError:
        ck(True, 'a condition record naming another index is refused')
    ck(xblachar.read_conditions(bytes(0x40), 'fx') == [], 'no table is no conditions')

    # the game's vocabulary, the loadout and the player's body, from the decomp
    names = vm.gun_switch_names()
    ck(len(names) == 36 and names[1] == 'SW_FLASH' and names[6] == 'FINGER'
       and names[7] == 'SLIDE' and names[11] == 'SW_HANDOFF4' and names[35] == 'SW_HANDOFF7',
       'the game\'s weapon switch vocabulary is read from the decomp')
    ck(vm.stage_loadout('dam') == [('ITEM_FIST', 1), ('ITEM_WPPKSIL', 5), ('ITEM_BUG', 47)],
       'the Dam loadout is the fist and the intro list\'s ordinary-play items: %r'
       % vm.stage_loadout('dam'))
    ck(vm.player_body('dam') == ('CUFF_BOILER', 'CboilerbondZ', 'CheadbrosnanboilerZ'),
       'the Dam player body follows its intro costume: %r' % (vm.player_body('dam'),))
    recs = vm.gun_records()
    ck(recs[5]['name'] == 'wppksil' and recs[5]['pos'] == [26.0, 162.0, 668.0]
       and recs[1]['name'] == 'fist' and recs[0] is None,
       'the gun record table reads in the game\'s index order')

    # the executable's own record table, located by the game's record values
    base = 0x82000000
    image = bytearray(0x20000)
    tab = 0x8000
    strings = 0x100
    for r in recs:
        if r is None:
            continue
        o = tab + r['index'] * vm.RECORD_SIZE
        st.pack_into('>5f', image, o + vm.POS_AT, *(r['pos'] + r['rot']))
        st.pack_into('>3f', image, o + vm.ORIGIN_AT, *r['origin'])
        nm = ('src%d' % r['index']).encode()
        image[strings:strings + len(nm)] = nm
        st.pack_into('>I', image, o + vm.FILENAME_AT, base + strings)
        strings += 16
    got = vm.locate_gun_names(bytes(image), base, recs)
    ck(got.get(5) == 'src5' and got.get(1) == 'src1' and len(got) >= len(recs) // 2,
       'the table is found by the game\'s own values and read through its file-name field')
    twice = bytearray(image)
    twice[0x10000:0x10000 + 0x8000] = image[tab - 0x100:tab - 0x100 + 0x8000]
    try:
        vm.locate_gun_names(bytes(twice), base, recs)
        ck(False, 'two whole tables are refused rather than chosen between')
    except vm.ViewModelError:
        ck(True, 'two whole tables are refused rather than chosen between')
    try:
        vm.locate_gun_names(bytes(0x1000), base, recs)
        ck(False, 'an executable without the table is refused')
    except vm.ViewModelError:
        ck(True, 'an executable without the table is refused')
    hdr = b'XEX2' + bytes(16) + st.pack('>I', 2) + st.pack('>II', 0x10001, 5) + st.pack('>II', 0x10201, base)
    ck(vm.xex_base(hdr) == base, 'the image base is read from the header directory')

    # ---- THE FIRST-PERSON SET, from the decomp's own tables --------------
    fp = vm.first_person_set('dam')
    byname = {r['item']: r for r in fp}
    ck([r['index'] for r in fp] == sorted(r['index'] for r in fp)
       and fp == vm.first_person_set('dam'),
       'the first-person set is derived in item order, the same every time')
    ck([v for _, v in vm.all_guns()] == list(range(1, 33)),
       'ALL GUNS reaches every item above ITEM_UNARMED and below ITEM_BOMBCASE (bondinv.c)')
    ck(byname['ITEM_WPPKSIL']['drawn'] and byname['ITEM_BUG']['via'] == 'the stage loadout'
       and byname['ITEM_AK47']['via'] == 'ALL GUNS'
       and byname['ITEM_SUIT_LF_HAND']['key'] == 'gun/suit_lf_hand',
       'the set is the stage loadout, every ALL GUNS item and the watch hand')
    ck(not byname['ITEM_GRENADE']['drawn'] and not byname['ITEM_REMOTEMINE']['drawn']
       and not byname['ITEM_BUG']['drawn'] and not byname['ITEM_TANKSHELLS']['drawn']
       and byname['ITEM_TANKSHELLS']['file'] is None,
       'a thrown or placed item and a record with no model are not drawn in first person')
    ck(byname['ITEM_WPPKSIL']['dual'] and byname['ITEM_WPPKSIL']['mirror']
       and byname['ITEM_AK47']['dual'] and not byname['ITEM_AK47']['mirror']
       and not byname['ITEM_FIST']['dual'],
       'each weapon carries the stats\' dual-wield and mirrored-left flags')
    ck(len([r for r in fp if r['drawn']]) == 28,
       'Dam draws 27 weapon models and the watch hand in first person: %d'
       % len([r for r in fp if r['drawn']]))
    rows = vm.view_rows('dam')
    ck(all(r['cls'] == 'gun' for r in rows) and rows[-1]['source_cls'] == 'char'
       and rows[0]['item'] == 'fist',
       'every drawn model is a gun/ row named by its own file; the watch hand\'s source is '
       'a character-class item')
    sflags = vm.weapon_flags()
    ck('CAN_DUAL_WIELD' in sflags['wppksil'] and 'HIDE_FIRST_PERSON_HAND' in sflags['grenade'],
       'the compiled weapon stats are read, flags by name')

    # ---- PREPARATION OUTCOMES: an unchanged rerun does no work -----------
    pout, pst = os.path.join(tmp, 'vmplan', 'out'), os.path.join(tmp, 'vmplan', 'st')
    os.makedirs(pst, exist_ok=True)
    os.makedirs(os.path.join(pout, 'xbla', 'gun'), exist_ok=True)

    def prow(item):
        return dict(key='gun/' + item, cls='gun', item=item, source=None, source_cls='gun',
                    index=1, why='t')
    rs = [prow('a'), prow('b'), prow('c')]
    vm.plan(rs, pout, pst, 'S1')
    ck([r['state'] for r in rs] == [None] * 3, 'with no record every row is left to resolve')
    open(os.path.join(pout, 'xbla', 'gun', 'a.slwa'), 'wb').write(b'x')
    rs[0]['state'], rs[0]['source'] = 'PREPARED', 'srca'
    rs[1]['state'], rs[1]['reason'] = 'CONVERSION REFUSED', 'no skeleton'
    rs[2]['state'], rs[2]['reason'] = 'SOURCE ABSENT', 'none'
    open(rs[0]['stamp'], 'w').write('S1\n')
    vm.keep_outcomes(rs, pst, 'S1')
    again = [prow('a'), prow('b'), prow('c')]
    vm.plan(again, pout, pst, 'S1')
    ck([r['state'] for r in again] == ['CURRENT', 'CONVERSION REFUSED', 'SOURCE ABSENT']
       and again[0]['source'] == 'srca' and again[1]['reason'] == 'no skeleton',
       'under the same stamp every outcome is answered from its record - no extraction, '
       'no conversion: %r' % [r['state'] for r in again])
    later = [prow('a'), prow('b'), prow('c')]
    vm.plan(later, pout, pst, 'S2')
    ck([r['state'] for r in later] == [None] * 3,
       'a changed source or tool re-opens every row')
    vm.resolve(later, ['X/new/gun/b/default.bin'])
    ck(later[1]['state'] == 'SOURCE ABSENT',
       'a row whose source is not yet named is SOURCE ABSENT, never guessed at')
    later[1]['source'] = 'B'
    later[1]['state'] = None
    vm.resolve(later, ['X/new/gun/b/default.bin'])
    ck(later[1]['state'] == 'BUILD' and later[1]['files'] == ['X/new/gun/b/default.bin'],
       'a named source item is found in the index without regard to case')

    # ---- THE READER'S CONTRACTS (#70 weapon coverage) ---------------------
    g = bytearray(0x40)
    st.pack_into('>IIII', g, 0x12, 32, 0x22, 0x100, 64)      # 2 mod 4
    ck(xblachar.vertex_buffer_at(bytes(g), 0x200, 0x12) == (0x100, 32, 2),
       'the vertex buffer is read from the binding\'s descriptor, at any byte offset')
    for bad, what in ((st.pack('>IIII', 24, 0, 0, 48), 'a stride the reader does not take'),
                      (st.pack('>IIII', 32, 0, 0x1f0, 64), 'a range past the blob')):
        try:
            xblachar.vertex_buffer_at(bad, 0x200, 0)
            ck(False, '%s is refused' % what)
        except xblachar.CharacterError:
            ck(True, '%s is refused' % what)
    try:
        xblachar.vertex_buffer_at(bytes(g), 0x200, None)
        ck(False, 'a draw before any binding is refused')
    except xblachar.CharacterError:
        ck(True, 'a draw before any binding is refused')
    v28 = st.pack('>3f4H', 1.0, 2.0, 3.0, 0, 0xf000, 0xf000, 0xf000) + b'\x00\x00\x00\x00' \
        + bytes([0xff, 0x30, 0x20, 0x10])
    vert, infl = xblachar._vertex(v28, 0, 28, 0, [4], [0, 1, 2, 3, 9], 'fx')
    ck(vert == (1.0, 2.0, 3.0, 0.0, 0.0, 0x10, 0x20, 0x30, 0xff) and infl == [(9, 255)],
       'a plain (stride 28) vertex is rigid, carries its colour and no coordinate')
    v32 = st.pack('>3f4H', 0.0, 0.0, 0.0, 0, 0xf000, 0xf000, 0xf000) + bytes(4) \
        + st.pack('>hh', -2920, 16371) + bytes([0xff, 1, 2, 3])
    vert, _ = xblachar._vertex(v32, 0, 32, 0, [0], [0], 'fx')
    ck(vert[3] < 0 and abs(vert[3] * 32768 + 2920) < 1e-3,
       'the texture coordinate is SIGNED: a wrap below zero is not read as one past the edge')

    def vx(u, v):
        return (0.0, 0.0, 0.0, u, v, 255, 255, 255, 255)
    # a launcher: two draws stop at 2047/2048 of their image, one tiles on to 3736
    tiling = [vx(3736 / 32768.0, 0.0)] * 6 + [vx(2047 / 32768.0, 0.0)] * 10 \
        + [vx(2042 / 32768.0, 0.0)] * 10 + [vx(-0.5, 0.0)] * 30
    groups = [list(range(0, 6)), list(range(6, 16)), list(range(16, 26)), list(range(26, 56))]
    ck(xblachar.texcoord_scale(tiling, 'fx', groups) == 16.0,
       'the scale is the one most non-tiling draws reach their edge at (1/2048), not the '
       'largest coordinate\'s (1/4096); a draw below zero counts for nothing')
    ck(xblachar.texcoord_scale([vx(1022 / 32768.0, 0.1 / 32)]) == 32.0,
       'a weapon read at 1/1024 is measured as the world reader measures, down to 1/64')
    try:
        xblachar.texcoord_scale([vx(-0.5, 0.2)], 'fx', [[0]])
        ck(False, 'an item whose every draw tiles below zero and reaches no edge is refused')
    except xblachar.CharacterError:
        ck(True, 'an item whose every draw tiles below zero and reaches no edge is refused')
    # THE WRAPPED LIP: one wrapping draw on quarters of 16384 with a small lip below zero
    # (the Dam gate's own values) reaches its far edge at 1/16384
    lip = [vx(c / 32768.0, 0.0) for c in (-1219, -62, 0, 93, 971, 4096, 8192, 12288, 16384)]
    ck(xblachar.texcoord_scale(lip, 'fx', [list(range(len(lip)))]) == 2.0,
       'a wrapping draw with a lip below zero is measured by the edge it reaches')
    try:
        xblachar.texcoord_scale(lip, 'fx', [list(range(len(lip)))], [False])
        ck(False, 'the same draw on a CLAMPING image is never read as a lip')
    except xblachar.CharacterError:
        ck(True, 'the same draw on a CLAMPING image is never read as a lip')
    # THE FACE (#81): one draw, two faces over the whole image at 1/16384, four
    # side faces running the bottom strip 1.456 times along their length
    door = [vx(0.0, 0.0), vx(0.5, 0.0), vx(0.5, 0.5), vx(0.0, 0.5),
            vx(0.0, 0.485), vx(0.728, 0.485), vx(0.728, 0.5)]
    dtris = [(0, 1, 2), (0, 2, 3), (0, 1, 2), (0, 2, 3), (0, 1, 2), (0, 2, 3),
             (4, 5, 6), (4, 5, 6), (4, 5, 6), (4, 5, 6)]
    ck(xblachar.texcoord_scale(door, 'fx', [list(range(7))], None, None, dtris) == 2.0,
       'a draw laying its image whole over its faces and wrapping a strip along its '
       'sides is read at the scale of the faces')
    try:
        xblachar.texcoord_scale(door, 'fx', [list(range(7))])
        ck(False, 'without its triangles the same draw is refused, as before')
    except xblachar.CharacterError:
        ck(True, 'without its triangles the same draw is refused, as before')
    noface = [vx(0.0, 0.0), vx(0.5, 0.0), vx(0.5, 0.3), vx(0.0, 0.485), vx(0.728, 0.485),
              vx(0.728, 0.5)]
    try:
        xblachar.texcoord_scale(noface, 'fx', [list(range(6))], None, None,
                                [(0, 1, 2)] * 6 + [(3, 4, 5)] * 4)
        ck(False, 'most triangles inside but no face over the whole image is refused')
    except xblachar.CharacterError:
        ck(True, 'most triangles inside but no face over the whole image is refused')
    # THE FACE AS A PLANE: a door's face laid over the whole image as a 2 x 4 grid
    # of quads (no one triangle on all four edges), two edge strips running 0.073
    # of the image past either side (the remaster's Archives steeldoor3)
    def grid(z_of):
        vs, ts = [], []
        for i in range(2):
            for j in range(4):
                b = len(vs)
                z = z_of(i, j)
                for di, dj in ((0, 0), (1, 0), (1, 1), (0, 1)):
                    vs.append((125.0 * (i + di), 150.0 * (j + dj), z,
                               0.25 * (i + di), 0.125 * (j + dj), 255, 255, 255, 255))
                ts += [(b, b + 1, b + 2), (b, b + 2, b + 3)]
        for u0, u1, x in ((0.5, 0.5365, 250.0), (0.0, -0.0365, 0.0)):
            b = len(vs)
            vs += [(x, 0.0, 0.0, u0, 0.5, 255, 255, 255, 255),
                   (x, 0.0, -16.7, u1, 0.5, 255, 255, 255, 255),
                   (x, 600.0, -16.7, u1, 0.0, 255, 255, 255, 255),
                   (x, 600.0, 0.0, u0, 0.0, 255, 255, 255, 255)]
            ts += [(b, b + 1, b + 2), (b, b + 2, b + 3)]
        return vs, ts
    gv, gt = grid(lambda i, j: 0.0)
    try:
        got = xblachar.texcoord_scale(gv, 'fx', [list(range(len(gv)))], None, None, gt)
    except xblachar.CharacterError:
        got = None
    ck(got == 2.0,
       'a face laid over the whole image as a grid of quads in one plane is read at '
       'the scale of the face (1/16384)')
    pv, pt = grid(lambda i, j: 40.0 * (4 * i + j))
    try:
        xblachar.texcoord_scale(pv, 'fx', [list(range(len(pv)))], None, None, pt)
        ck(False, 'the same quads on eight different planes are no face, and are refused')
    except xblachar.CharacterError:
        ck(True, 'the same quads on eight different planes are no face, and are refused')
    # THE WRAPPED OVERRUN (#93): the Train brake unit - one wrapping draw, a front face
    # over most of the image at 1/16384 (u 0 .. 16384, v 0 .. 14004) and pipe strips
    # below it, and a back face running to 17066 (0.042 past the far edge)
    brake = [vx(0.0, 0.0), vx(0.5, 0.0), vx(0.5, 14004 / 32768.0), vx(0.0, 14004 / 32768.0),
             vx(2457 / 32768.0, 14027 / 32768.0), vx(5384 / 32768.0, 16388 / 32768.0),
             vx(7509 / 32768.0, 3276 / 32768.0), vx(17066 / 32768.0, 3276 / 32768.0),
             vx(17066 / 32768.0, 11468 / 32768.0)]
    btris = [(0, 1, 2), (0, 2, 3)] * 4 + [(4, 5, 3)] * 4 + [(6, 7, 8)]
    try:
        got = xblachar.texcoord_scale(brake, 'fx', [list(range(9))], [True], None, btris)
    except xblachar.CharacterError:
        got = None
    ck(got == 2.0,
       'a wrapping draw whose back face runs 0.042 past the far edge is read at the scale '
       'of the rest (1/16384)')
    for what, w, v7 in (('on a CLAMPING image', False, 17066),
                        ('running 0.2 past the edge (it tiles)', True, 19660)):
        ov = brake[:7] + [vx(v7 / 32768.0, 3276 / 32768.0), vx(v7 / 32768.0, 11468 / 32768.0)]
        try:
            xblachar.texcoord_scale(ov, 'fx', [list(range(9))], [w], None, btris)
            ck(False, 'the same overrun %s is refused' % what)
        except xblachar.CharacterError:
            ck(True, 'the same overrun %s is refused' % what)
    try:
        xblachar.texcoord_scale(brake, 'fx', [list(range(9))], [True], None,
                                [(0, 1, 2)] + [(6, 7, 8)] * 2)
        ck(False, 'an overrun on most of the triangles is refused')
    except xblachar.CharacterError:
        ck(True, 'an overrun on most of the triangles is refused')
    # THE WHOLE IMAGE: the Jungle tree's own ranges (raw, at 1/32768) and vertex counts -
    # bark and buttress roots tiling, two leaf clumps, a vine card over its whole image
    # at 1/8192, five draws each on its own wrapping image
    def ranges(spans):
        vs, gs = [], []
        for (u0, u1, v0, v1, n) in spans:
            gs.append(list(range(len(vs), len(vs) + n)))
            vs += [vx(u0 / 32768.0, v0 / 32768.0), vx(u1 / 32768.0, v1 / 32768.0)] * (n // 2)
        return vs, gs
    tree_spans = [(-3217, 19573, -8192, 16384, 48), (-20479, 28672, 0, 8192, 48),
                  (34, 7742, -347, 8064, 24), (-1099, 15155, -1469, 7862, 32),
                  (-33, 8192, 0, 8192, 8)]
    tv, tg = ranges(tree_spans)
    why = {}
    try:
        got = xblachar.texcoord_scale(tv, 'fx', tg, [True] * 5, why=why)
    except xblachar.CharacterError:
        got = None
    ck(got == 4.0, 'a vine card over its whole image beside tiling bark and roots is read at '
                   'its scale (1/8192)')
    ck(why.get('rule') == 'whole', '#95 the answer says THE WHOLE IMAGE decided it (%r)'
                                   % why.get('rule'))
    why = {}
    xblachar.texcoord_scale([vx(0.1, 0.99), vx(0.5, 0.2)], why=why)
    ck(why.get('rule') == 'reach' and 'whole' in xblachar.TEXCOORD_RULES,
       '#95 an item the reach decides says so (%r)' % why.get('rule'))
    for what, spans, wr in (
            ('a draw running partly past its image without tiling',
             tree_spans[:2] + [(4000, 12000, -347, 8064, 24)] + tree_spans[3:], [True] * 5),
            ('a draw tiling a CLAMPING image', tree_spans, [True, False, True, True, True]),
            ('a second power of two with a whole draw (a quarter read whole)',
             tree_spans + [(-20, 4096, -20, 4096, 8)], [True] * 6)):
        bv, bg = ranges(spans)
        try:
            xblachar.texcoord_scale(bv, 'fx', bg, wr)
            ck(False, 'the whole-image rule refuses %s' % what)
        except xblachar.CharacterError:
            ck(True, 'the whole-image rule refuses %s' % what)
    # a locker: two wrapping draws, u 0 .. 16384, v exactly one repeat either side of zero
    locker = [vx(0.0, -0.4998), vx(0.5, 0.5)] * 2 + [vx(0.0, -0.5), vx(0.5, 0.5)] * 8
    ck(xblachar.texcoord_scale(locker, 'fx', [list(range(4)), list(range(4, 20))]) == 2.0,
       'a wrapping draw starting exactly one repeat below zero shows the image twice (1/16384)')
    deep = [vx(-1027 / 32768.0, 0.0)] * 15 + [vx(1024 / 32768.0, 0.0)] * 15 \
        + [vx(-28523 / 32768.0, 14240 / 32768.0)] * 6
    try:
        xblachar.texcoord_scale(deep, 'fx', [list(range(30)), list(range(30, 36))])
        ck(False, 'a draw reaching past a whole repeat below zero still tiles (refused)')
    except xblachar.CharacterError:
        ck(True, 'a draw reaching past a whole repeat below zero still tiles (refused)')
    ck(xblachar.texcoord_scale(tiling + [vx(-0.01, 0.9)] * 90, 'fx',
                               groups + [list(range(56, 146))]) == 16.0,
       'the lip rule never overrides an item the inside-zero rule already measures')
    # THE FLAT DRAW: a glass draw on a one-colour tint (the glassware's) says nothing
    # about the scale; measured without it, or kept as read when every draw is flat
    tint = [vx(-0.236, 0.532), vx(0.264, 0.865)] * 3
    try:
        xblachar.texcoord_scale(tint, 'fx', [list(range(6))])
        ck(False, 'a lone tint draw below zero reaching no edge is refused without the flag')
    except xblachar.CharacterError:
        ck(True, 'a lone tint draw below zero reaching no edge is refused without the flag')
    ck(xblachar.texcoord_scale(tint, 'fx', [list(range(6))], None, [True]) == 1.0,
       'an item whose every draw is flat keeps its coordinates as read')
    cap = tint + [vx(0.0, 0.0), vx(0.47, 0.2)] * 2
    ck(xblachar.texcoord_scale(cap, 'fx', [list(range(6)), list(range(6, 10))], None,
                               [True, False]) == 2.0,
       'with the flat draw set aside the other draws decide the scale (1/16384)')
    ck(xblachar.texcoord_scale(tiling, 'fx', groups, None, [True, False, False, False]) == 16.0,
       'the flat rule never changes an item the first two rules measure')
    # THE ROUNDED ZERO (#78): the wooden table's own values - a top tiling far below
    # zero and a quad over its whole second image one raw unit below zero
    table = [vx(-12272 / 32768.0, 0.0), vx(22333 / 32768.0, 6553 / 32768.0)] * 32 \
        + [vx(0.0, -1 / 32768.0), vx(4100 / 32768.0, 4094 / 32768.0)] * 2
    tg = [list(range(64)), list(range(64, 68))]
    ck(xblachar.texcoord_scale(table, 'fx', tg) == 8.0,
       'a draw one raw unit below zero is measured from zero: the quad reaches its edge at 1/4096')
    ck([v[4] for v in table[64:68:2]] == [-1 / 32768.0] * 2,
       'the rounded zero is a measurement only: the coordinates themselves are not moved')
    try:
        xblachar.texcoord_scale(table[:64] + [vx(0.0, -2 / 32768.0), vx(4100 / 32768.0, 0.1)] * 2,
                                'fx', tg)
        ck(False, 'two raw units below zero is not a rounded zero: still refused')
    except xblachar.CharacterError:
        ck(True, 'two raw units below zero is not a rounded zero: still refused')
    ck(xblachar.texcoord_scale(tiling + [vx(-1 / 32768.0, 0.9)] * 4, 'fx',
                               groups + [list(range(56, 60))]) == 16.0,
       'the rounded zero never changes an item the earlier rules measure')
    ck(xblachar.base_image({0: 5, 1: 6}, [0, 0, 0, 0, 0, 65536, 262144]) == 6
       and xblachar.base_image({0: 0, 1: 1}, [262144, 65536]) == 0
       and xblachar.base_image({0: 1, 1: 2}, [0, 2916, 1024]) == 1
       and xblachar.base_image({3: 2, 1: 1}, [0, 100, 100]) == 1,
       'a material\'s texture is its LARGEST image, the lowest slot on a tie')
    # #77 glass: the translucent pane's colour is the one image with coverage
    ck(xblachar.coverage_base({0: 1, 1: 2}, {0, 1}) == 2,
       'a glass material (opaque sphere image + a tint with alpha) is coloured by the tint')
    ck(xblachar.coverage_base({0: 4, 1: 5}, {5}) == 4,
       'the coverage image is found in either slot order')
    ck(xblachar.coverage_base({0: 0, 1: 1}, set()) is None
       and xblachar.coverage_base({0: 0, 1: 1}, {0, 1}) is None,
       'two images with coverage, or none, decide nothing (the largest stands)')
    # #82 the environment pair (a 256x256 sphere image, its 256x128 sky in
    # the next slot) is a static item's reflection, never its colour
    dm = [(256, 256), (256, 128), (85, 324), (512, 512), (256, 256)]
    tx = [w * h for (w, h) in dm]
    ck(xblachar.static_base({0: 2, 1: 0, 2: 1}, tx, dm) == 2
       and xblachar.static_base({0: 0, 1: 1, 2: 4}, tx, dm) == 4,
       'a material naming the environment pair is coloured by its other image '
       '(the missile lettering decal; the oil drum lid that tied the sphere)')
    ck(xblachar.static_base({0: 3, 1: 0, 2: 1, 3: 3}, tx, dm) == 3
       and xblachar.static_base({0: 0, 1: 1}, tx, dm) == 0
       and xblachar.static_base({0: 1, 1: 3}, tx, dm) == 3
       and xblachar.static_base({0: 1, 1: 0}, tx, dm) == 0
       and xblachar.static_base({0: 3, 1: 0}, tx, dm) == 3,
       'with no pair, or nothing beside it, the largest stands as base_image takes it')

    # the writer: a reflection on a view model only
    p = os.path.join(tmp, 'vm', 'xbla', 'gun', 'shiny.slwa')
    slwa.write_skinned(p, slwa.CLASS_GUN, 'shiny', quad(4), [0, 1, 2, 1, 3, 2],
                       [(0, slwa.BLEND_OPAQUE), (0, slwa.BLEND_OPAQUE)],
                       [(64, 16, img), (64, 16, img)], [(0, 0, 3, 0), (1, 3, 3, 0)], gskin, gj,
                       [None, 1])
    b = open(p, 'rb').read()
    mo = st.unpack_from('<I', b, 100)[0]
    ck(st.unpack_from('<I', b, mo + 12)[0] == 0 and st.unpack_from('<I', b, mo + 16 + 12)[0] == 2,
       'a view model\'s material names its reflection image as 1 + index')
    # ... and on a prop (Egyptian's Golden Gun pickup: its gold is its reflection)
    p = os.path.join(tmp, 'vm', 'xbla', 'prop', 'shinyprop.slwa')
    slwa.write_skinned(p, slwa.CLASS_PROP, 'shinyprop', quad(4), [0, 1, 2, 1, 3, 2],
                       [(0, slwa.BLEND_OPAQUE), (0, slwa.BLEND_OPAQUE)],
                       [(64, 16, img), (64, 16, img)], [(0, 0, 3, 0), (1, 3, 3, 0)],
                       [((0, 255),)] * 4, [('SKEL_ROOT', -1, (0.0, 0.0, 0.0))], [None, 1],
                       bounds=((0.0, 0.0, 0.0), (3.0, 6.0, 9.0)))
    b = open(p, 'rb').read()
    mo = st.unpack_from('<I', b, 100)[0]
    ck(st.unpack_from('<I', b, mo + 12)[0] == 0 and st.unpack_from('<I', b, mo + 16 + 12)[0] == 2,
       'a prop\'s material names its reflection image as 1 + index')
    for what, cls, refl in (('a reflection on a character', slwa.CLASS_CHAR, [None, 1]),
                            ('a reflection past the images', slwa.CLASS_GUN, [None, 2]),
                            ('reflections that do not match the materials', slwa.CLASS_GUN, [1])):
        try:
            slwa.write_skinned(os.path.join(tmp, 'vm', 'bad.slwa'), cls, 'bad', quad(4),
                               [0, 1, 2, 1, 3, 2], [(0, slwa.BLEND_OPAQUE), (0, slwa.BLEND_OPAQUE)],
                               [(64, 16, img), (64, 16, img)], [(0, 0, 3, 0), (1, 3, 3, 0)],
                               gskin, [j[:3] for j in gj] if cls == slwa.CLASS_CHAR else gj, refl)
            ck(False, '%s is refused' % what)
        except slwa.SlwaError:
            ck(True, '%s is refused' % what)

    try:
        import numpy                                          # noqa: F401
    except ImportError:
        print('worldasset tools selftest: numpy not installed - the view-model '
              'preparation checks are skipped')
        return
    import prepare
    real = xblachar.read_character
    real_static = xblachar.is_static
    xblachar.is_static = lambda path: False     # the fixtures are skinned items
    tree = os.path.join(tmp, 'vmtree')
    img_rec = dict(name='t', w=64, h=16, rgba=img)

    def fake(draws, joints, skin=None, conditions=None):
        return dict(joints=[dict(name=n, parent=p_, bind=b_) for (n, p_, b_) in joints],
                    vertices=quad(4), skin=skin or [((0, 255),)] * 4, textures=[img_rec],
                    draws=draws, conditions=conditions or [], texcoord_scale=1.0)
    d = os.path.join(tree, 'Top', prepare.REMASTERED, 'gun', 'srcgun')
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'default.bin'), 'wb').write(b'x')
    joints = [('SKEL_TOP', -1, (0.0, 0.0, 0.0)), ('SKEL_FINGER', 0, (1.0, 0.0, 0.0)),
              ('BEAN_END', 1, (1.0, 0.0, 0.0))]
    ok_draws = [dict(image=0, indices=[0, 1, 2], conditions=[], pass_mask=0x4701, builtin_head=False),
                dict(image=0, indices=[1, 3, 2], conditions=['SW_finger', 'SW_HANDOFF6'],
                     pass_mask=0x4701, builtin_head=True)]
    out = os.path.join(tmp, 'vmout')
    try:
        xblachar.read_character = lambda path: fake(ok_draws, joints,
                                                    [((0, 255),), ((1, 255),), ((0, 255),), ((1, 255),)])
        prepare.convert_viewmodel('game', 'srcgun', tree, out, names)
        b = open(os.path.join(out, 'xbla', 'gun', 'game.slwa'), 'rb').read()
        ho = st.unpack_from('<I', b, 108)[0]
        jo = st.unpack_from('<I', b, 120)[0]
        ck([st.unpack_from('<I', b, ho + 16 * m + 12)[0] for m in range(2)] == [0, 14 << 8]
           and [st.unpack_from('<I', b, jo + 48 * j + 40)[0] for j in range(3)] == [0, 7, 0],
           'the view model keeps the game switches its draws stand under (a switch the game '
           'never indexes gates nothing) and names each joint\'s game part')
        for what, draws, sk in (
                ('a draw in neither the opaque nor the translucent pass',
                 [dict(ok_draws[0], pass_mask=0x4700)], None),
                ('a draw under a condition the table does not name',
                 [dict(ok_draws[0], conditions=[None])], None)):
            xblachar.read_character = lambda path, dr=draws, s_=sk: fake(dr, joints, s_)
            try:
                prepare.convert_viewmodel('game', 'srcgun', tree, out, names)
                ck(False, '%s is refused' % what)
            except prepare.CharacterRefused:
                ck(True, '%s is refused' % what)
        # a vertex on a joint the game positions no part for rides the nearest
        # ancestor it does (BEAN_END under FINGER -> FINGER)
        xblachar.read_character = lambda path: fake(ok_draws, joints, [((2, 255),)] * 4)
        prepare.convert_viewmodel('game', 'srcgun', tree, out, names)
        b = open(os.path.join(out, 'xbla', 'gun', 'game.slwa'), 'rb').read()
        ko = st.unpack_from('<I', b, 116)[0]
        ck([b[ko + 8 * v] for v in range(4)] == [1] * 4 and [b[ko + 8 * v + 4] for v in range(4)] == [255] * 4,
           'a vertex bound to a leaf the game positions no part for rides its positioned parent')
        # THE TRANSLUCENT PASS: carried blended; the muzzle flash is left to the game
        tl = [ok_draws[0], dict(ok_draws[1], pass_mask=0x4702, conditions=[]),
              dict(ok_draws[1], pass_mask=0x4702, conditions=['SW_FLASH'])]
        xblachar.read_character = lambda path: fake(tl, joints)
        summary = prepare.convert_viewmodel('game', 'srcgun', tree, out, names)
        b = open(os.path.join(out, 'xbla', 'gun', 'game.slwa'), 'rb').read()
        nm_, mo = st.unpack_from('<I', b, 88)[0], st.unpack_from('<I', b, 100)[0]
        ck(nm_ == 2 and st.unpack_from('<I', b, 80)[0] == 2
           and st.unpack_from('<I', b, mo + 16 + 4)[0] == slwa.BLEND_ALPHA
           and '1 blended' in summary and '1 flash left to the game' in summary,
           'a translucent draw is carried blended and a translucent draw under SW_FLASH is not: %s'
           % summary)
        # THE REFLECTION: a draw that names one gets a material carrying it
        rf = [dict(ok_draws[0], reflect=0)]
        xblachar.read_character = lambda path: fake(rf, joints)
        prepare.convert_viewmodel('game', 'srcgun', tree, out, names)
        b = open(os.path.join(out, 'xbla', 'gun', 'game.slwa'), 'rb').read()
        mo, ho = st.unpack_from('<I', b, 100)[0], st.unpack_from('<I', b, 108)[0]
        mi = st.unpack_from('<I', b, ho)[0]
        ck(mi == 1 and st.unpack_from('<I', b, mo + 16 * mi + 12)[0] == 1
           and st.unpack_from('<I', b, mo + 12)[0] == 0,
           'a reflective draw gets its own material naming the reflection image (1 + index)')
        # A STATIC weapon item: one rigid piece, placed by its original generation's box
        od = os.path.join(tree, 'Top', prepare.ORIGINAL, 'gun', 'srcgun')
        os.makedirs(od, exist_ok=True)
        open(os.path.join(od, 'default.bin'), 'wb').write(b'x')
        real_rs, real_sb = xblachar.read_static, xblachar.static_bounds
        try:
            xblachar.is_static = lambda path: True
            xblachar.read_static = lambda path, images=True: fake(
                [ok_draws[0]], [('SKEL_ROOT', -1, (0.0, 0.0, 0.0))])
            xblachar.static_bounds = lambda path: ((-1.0, -2.0, -3.0), (1.0, 2.0, 3.0))
            prepare.convert_viewmodel('game', 'srcgun', tree, out, names)
            b = open(os.path.join(out, 'xbla', 'gun', 'game.slwa'), 'rb').read()
            ck(st.unpack_from('<I', b, 124)[0] == 1
               and st.unpack_from('<6f', b, 48) == (-1.0, -2.0, -3.0, 1.0, 2.0, 3.0),
               'a static weapon item is one joint carrying its original generation\'s box')
        finally:
            xblachar.is_static = lambda path: False
            xblachar.read_static, xblachar.static_bounds = real_rs, real_sb
        # the own-head rule on a body
        cj = [('SKEL_BASE', -1, (0.0, 0.0, 0.0)), ('SKEL_NECK', 0, (0.0, 1.0, 0.0))]
        cd = os.path.join(tree, 'Top', prepare.REMASTERED, 'char', 'body')
        os.makedirs(cd, exist_ok=True)
        open(os.path.join(cd, 'default.bin'), 'wb').write(b'x')
        flags = {}
        for tag, gated, conds in (('own', False, []), ('attach', True, ['CP_HEADCHILD'])):
            xblachar.read_character = lambda path, g_=gated, c_=conds: fake(
                [dict(image=0, indices=[0, 1, 2], builtin_head=False),
                 dict(image=0, indices=[1, 3, 2], builtin_head=g_)], cj, None, c_)
            prepare.convert_character('char', 'body', tree, os.path.join(tmp, 'own-' + tag))
            b = open(os.path.join(tmp, 'own-' + tag, 'xbla', 'char', 'body.slwa'), 'rb').read()
            ho = st.unpack_from('<I', b, 108)[0]
            flags[tag] = [st.unpack_from('<I', b, ho + 16 * m + 12)[0] for m in range(2)]
    finally:
        xblachar.read_character = real
        xblachar.is_static = real_static
    ck(flags == {'own': [slwa.MESH_OWN_HEAD] * 2, 'attach': [0, slwa.MESH_BUILTIN_HEAD]},
       'a body with no condition carries its own head on every mesh; one with a head attach '
       'point gates its built-in head: %r' % flags)


def proptests(tmp):
    """#70: the stage's rigid props - the game's tables, the executable's
    name table, the preparation's outcome rules and the PROP bundle."""
    import struct as st
    import stageprops as sp
    recs = sp.prop_records()
    ck(len(recs) == 340 and recs[48]['name'] == 'mainframe1' and recs[48]['scale'] == 0.1,
       'the prop file table reads in the game\'s index order, scales defaulted to 0.1')
    rows = sp.stage_props('dam')
    by = {r['item']: r for r in rows}
    ck([r['index'] for r in rows] == sorted(r['index'] for r in rows)
       and by['mainframe1']['count'] == 2 and by['desk1']['count'] == 4
       and 'single monitor' in by['tv1']['why'] and by['tv1']['key'] == 'prop/tv1'
       and 'collectable' in by['chrkalash']['why'] and 'alarm' in by['alarm2']['why']
       and by['window']['glass'] and 'glass' in by['window']['why']
       and not by['desk1']['glass'],
       'the Dam\'s standard props and single monitors, by model, from its setup: %r'
       % sorted(by))

    # THE MOVING PROP: the Dam's vehicle, and its matrices by the game's names
    truck = [r for r in rows if r['item'] == 'miltruck']
    ck(len(truck) == 1 and 'vehicle' in truck[0]['why']
       and truck[0]['matrices'] == ['base', 'wheel1', 'wheel2', 'wheel3', 'wheel4']
       and by['desk1']['matrices'] is None,
       'the Dam\'s vehicle is taken, its matrices named by its skeleton; a one-piece prop '
       'has none: %r' % truck)
    ck('multi-screen monitor' in by['doorpanel']['why'] and by['doorpanel']['count'] == 4
       and 'rigid door' in by['damchaindoor']['why'] and 'rigid door' in by['damtundoor']['why'],
       'the Dam\'s multi-screen door panels are taken; its doors the game moves whole are '
       'taken as rigid doors: %r'
       % [by[k]['why'] for k in ('doorpanel', 'damchaindoor', 'damtundoor')])
    ck(by['damgatedoor']['count'] == 3 and 'dynamic door' in by['damgatedoor']['why']
       and by['door_rollertrain']['count'] == 1 and by['gas_plant_met1_do1']['count'] == 4,
       'the Dam\'s CLIP_TO_BBOX sliding and vertical doors are taken as dynamic doors: %r'
       % [(k, by[k]['why']) for k in ('damgatedoor', 'door_rollertrain', 'gas_plant_met1_do1')])
    ck(sp.door_taken([0] * 38 + [0x000c0000]) == 'dynamic door'
       and sp.door_taken([0] * 38 + [0x00040004]) == 'dynamic door'
       and sp.door_taken([0] * 38 + [0x00080000]) == 'rigid door'
       and sp.door_taken([0] * 38 + [0x00000005]) == 'rigid door'
       and sp.door_taken([0] * 38 + [0x00020005]) == 'rigid door'
       and sp.door_taken([0] * 38 + [0x00040005]) is None
       and sp.door_taken([0] * 38 + [0x00000008]) is None and sp.door_taken([0] * 30) is None,
       'a door is taken for CLIP_TO_BBOX on a sliding or vertical door (cut at its box), or '
       'without it on a sliding, vertical or swinging one (riding its matrix)')
    # THE PARTED DOORS (#96): an eye or iris door without CLIP_TO_BBOX rides
    # its moving parts by the skeleton's names; with the flag it is not taken
    ck(sp.door_taken([0] * 38 + [0x00000006]) == 'parted door'
       and sp.door_taken([0] * 38 + [0x00080007]) == 'parted door'
       and sp.door_taken([0] * 38 + [0x00040006]) is None
       and sp.door_taken([0] * 38 + [0x00040007]) is None
       and sp.door_taken([0] * 38 + [0x00000008]) is None,
       'an eye or iris door is taken as a parted door without CLIP_TO_BBOX only')
    # #96 AZTEC'S CHAIRS: type 9 turns by the swinging door's matrix - a rigid door
    ck(sp.door_taken([0] * 38 + [0x00000009]) == 'rigid door'
       and sp.door_taken([0] * 38 + [0x00040009]) is None,
       '#96 an Aztec chair door is taken as a rigid door, and not with CLIP_TO_BBOX')
    try:
        cav = {r['item']: r for r in sp.stage_props('caverns')}
    except (sp.PropError, sp.stagechars.InventoryError) as exc:
        cav = {}
        ck(False, 'the Caverns derivation reads: %s' % exc)
    if cav:
        ck(cav.get('door_eyelid', {}).get('count') == 9 and 'parted door' in cav['door_eyelid']['why']
           and cav['door_eyelid']['matrices'] == ['SKEL_TOP', 'SKEL_UPPER', 'SKEL_LOWER']
           and cav.get('door_iris', {}).get('count') == 9 and 'parted door' in cav['door_iris']['why']
           and cav['door_iris']['matrices'][:3] == ['SKEL_TOP', 'SKEL_OUT1', 'SKEL_IN1']
           and len(cav['door_iris']['matrices']) == 13,
           "Caverns' nine eyelid and nine iris airlock doors are parted doors, each matrix "
           "named by the skeleton: %r" % [(k, cav[k]['why'], cav[k]['matrices'])
                                         for k in ('door_eyelid', 'door_iris') if k in cav])
    # A STAGE'S ACCEPTED SET: the Dam holds back the two models the
    # derivation took after its set was accepted; Facility holds none.
    hd = sp.held_items('dam')
    ck(sorted(hd) == ['damtundoor', 'hatpeaked'] and sp.held_items('facility') == {},
       'the Dam holds back its tunnel door and peaked hat, Facility nothing: %r' % sorted(hd))
    hroot = os.path.join(tmp, 'heldroot')
    os.makedirs(os.path.join(hroot, 'data', 'overrides', 'x'))
    with open(os.path.join(hroot, 'data', 'overrides', 'x', 'props-held.txt'), 'w',
              newline='\n') as f:
        f.write('# comment\n\nCrate  a reason # tail\nbarrel\n')
    ck(sp.held_items('x', hroot) == {'crate': 'a reason', 'barrel': 'held'}
       and sp.held_items('y', hroot) == {},
       'a held list reads one item per line, its reason after it, comments and blanks '
       'ignored; a stage with no list holds nothing')
    fac = {r['item']: r for r in sp.stage_props('facility')}
    ck('gas prop' in fac['gastank']['why'] and fac['gastank']['count'] == 10
       and 'locker3' in fac and 'labbench' in fac and fac['glassware1']['glass']
       and fac['glassware2']['count'] == 12 and 'window' not in fac,
       'Facility\'s standard props, gas props and glassware are taken (its windows are '
       'tinted glass): %r' % sorted(fac))
    ck('rigid door' in fac['gas_plant_sw2_do1']['why'] and fac['gas_plant_sw2_do1']['count'] == 18
       and 'rigid door' in fac['gas_plant_wc_cub1']['why'] and fac['gas_plant_wc_cub1']['count'] == 9,
       'Facility\'s swing and cubicle doors are taken as rigid doors')
    ck(fac['hattbird']['count'] == 47 and 'hat' in fac['hattbird']['why']
       and 'body armour' in fac['bodyarmour']['why'],
       'Facility\'s hats and body armour are taken')
    mroot = os.path.join(tmp, 'mtxroot')

    def mput(rel, text):
        q = os.path.join(mroot, *rel.split('/'))
        os.makedirs(os.path.dirname(q), exist_ok=True)
        with open(q, 'w', newline='\n') as f:
            f.write(text)
    mput('assets/obseg/prop/cart/ModelFileHeader.inc.c',
         'MODELFILEHEADER(cart, 0, &SKELETON(twowheel), 0, 0x4, 0x3, 10.0, 0, 0x2)\n')
    mput('assets/obseg/prop/crate/ModelFileHeader.inc.c',
         'MODELFILEHEADER(crate, 0, &SKELETON(plain), 0, 0, 1, 10.0, 0, 0x1)\n')
    mput('assets/obseg/prop/bad/ModelFileHeader.inc.c',
         'MODELFILEHEADER(bad, 0, &SKELETON(twowheel), 0, 0x4, 0x4, 10.0, 0, 0x2)\n')
    mput('assets/embedded/skeletons/s.inc.c',
         '#ifdef DEBUG\nchar* cart_JointNames[] = {\n    "base",\n    "left",\n    "right" };\n'
         '#endif\nModelJoint JOINTLIST(twowheel)[] = { {0x0002, 0, 0}, {0x0002, 3, 3}, '
         '{0x0002, 6, 6} };\nMODELSKELETON(twowheel, 3, 0xF)\n'
         '//No Joint Names\nModelJoint JOINTLIST(plain)[] = { {0x0002, 0, 0} };\n'
         'MODELSKELETON(plain, 1, 3)\n')
    ck(sp.matrix_names('cart', mroot) == ['base', 'left', 'right']
       and sp.matrix_names('crate', mroot) is None,
       'a model\'s matrix names are its skeleton\'s, from the name table right above its '
       'joint list; a skeleton with none gives none')
    try:
        sp.matrix_names('bad', mroot)
        ck(False, 'names that disagree with the model\'s matrix count are refused')
    except sp.PropError:
        ck(True, 'names that disagree with the model\'s matrix count are refused')
    mput('assets/obseg/prop/barrow/ModelFileHeader.inc.c',
         'MODELFILEHEADER(barrow, 0, &SKELETON(twowheel), 0, 0x4, 0x2, 10.0, 0, 0x2)\n')
    ck(sp.matrix_names('barrow', mroot) == ['base', 'left'],
       'a model with fewer matrices than its skeleton has joints takes the first names')
    # #78: Runway's moving kinds, by the game's own skeletons
    ck(sp.matrix_names('gun_runway1') == ['SKEL_TOP', 'SKEL_YROT', 'SKEL_XROT', 'SKEL_BARREL',
                                          'SKEL_CARTRIDGE']
       and sp.matrix_names('plane') == ['SKEL_POSITION', 'SKEL_TOP', 'SKEL_ROTOR']
       and sp.matrix_names('tank') == ['SKEL_TOP', 'SKEL_TURRET', 'SKEL_HATCH', 'SKEL_GUN',
                                       'SKEL_MUZZLE'],
       'the autogun, the plane and the tank name their matrices (a name table under #if 0 '
       'is not the game\'s)')
    rw = {r['item']: r for r in sp.stage_props('runway')}
    ck('ammo box' in rw['ammo_crate2']['why'] and rw['ammo_crate3']['count'] == 2
       and 'autogun' in rw['gun_runway1']['why'] and rw['gun_runway1']['count'] == 3
       and 'aircraft' in rw['plane']['why'] and 'tank' in rw['tank']['why'],
       'Runway\'s ammo boxes, autoguns, plane and tank are taken: %r'
       % [(k, rw[k]['why']) for k in ('ammo_crate2', 'gun_runway1', 'plane', 'tank') if k in rw])
    # #80 THE COLLECTABLE: Surface's keys, alarm and weapons, carried or lying
    sf = {r['item']: r for r in sp.stage_props('surface')}
    ck('key' in sf['chrkeybolt']['why'] and 'key' in sf['chrkeyyale']['why']
       and 'alarm' in sf['alarm2']['why'] and 'collectable' in sf['chrgrenadelaunch']['why']
       and sf['chrkalash']['count'] == 36,
       'Surface\'s keys, alarm and collectables are taken: %r'
       % [(k, sf[k]['why']) for k in ('chrkeybolt', 'alarm2', 'chrgrenadelaunch') if k in sf])

    # #80 a material record that binds only the engine's images leaves the draw's own
    import xblachar as xc
    tex10 = [256 * 128] * 10
    ck(xc.own_slots({2: 26, 1: 25}, tex10) is None and xc.own_slots({0: 3}, tex10) == {0: 3}
       and xc.own_slots({0: 3, 2: 26}, tex10) == {0: 3} and xc.own_slots(None, tex10) is None,
       'a material record naming only images the item does not carry names none of its own')

    # #80 THE MODEL IS THE ENTITY WITH A COMMAND LIST
    import xblaworld as xw
    fake = dict(ents=[dict(name=1, sec='.gpu', kind=5, size=8288, off=0),
                      dict(name=2, sec='.gpu', kind=5, size=9088, off=8288),
                      dict(name=1, sec='.stream', kind=2, size=4818, off=0)])
    ck(xw.model_entity(fake, fake['ents'][:2])['name'] == 1,
       'the model entity is the one with a command list, not the larger program pool')
    fake['ents'][2]['size'] = 0
    ck(xw.model_entity(fake, fake['ents'][:2])['name'] == 2,
       'with no command list anywhere the largest entity is taken, as before')

    # the executable's own table, found by the game's scale sequence
    base = 0x82000000
    image = bytearray(0x20000)
    tab, strings, stride = 0x4000, 0x100, 16
    for r in recs:
        o = tab + r['index'] * stride
        st.pack_into('>f', image, o + sp.SCALE_AT, r['scale'])
        nm = ('p%d' % r['index']).encode()
        image[strings:strings + len(nm)] = nm
        st.pack_into('>I', image, o + sp.NAME_AT, base + strings)
        strings += 8
    got = sp.locate_prop_names(bytes(image), base, recs)
    ck(got.get(48) == 'p48' and len(got) == len(recs),
       'the prop table is found by the game\'s scales, on its own stride, and read through '
       'its name field')
    try:
        sp.locate_prop_names(bytes(0x1000), base, recs)
        ck(False, 'an executable without the prop table is refused')
    except sp.PropError:
        ck(True, 'an executable without the prop table is refused')

    # both generations are needed
    rs = [dict(key='prop/a', cls='prop', item='a', source='A', index=1, state=None, reason=''),
          dict(key='prop/b', cls='prop', item='b', source='B', index=2, state=None, reason='')]
    sp.resolve(rs, ['X/new/prop/A/default.bin', 'X/original/prop/A/default.bin',
                    'X/new/prop/B/default.bin'])
    ck(rs[0]['state'] == 'BUILD' and len(rs[0]['files']) == 2 and rs[1]['state'] == 'SOURCE ABSENT',
       'a prop is built from both of the source\'s generations, and absent without either')

    # the bundle: one rigid joint, the reference box in the header
    img = bytes(range(256)) * 16
    p = os.path.join(tmp, 'prop', 'xbla', 'prop', 'testprop.slwa')
    slwa.write_skinned(p, slwa.CLASS_PROP, 'testprop', quad(4), [0, 1, 2, 1, 3, 2],
                       [(0, slwa.BLEND_OPAQUE)], [(64, 16, img)], [(0, 0, 6, 0)],
                       [((0, 255),)] * 4, [('SKEL_ROOT', -1, (0.0, 0.0, 0.0))],
                       bounds=((-1.0, 0.0, -2.0), (1.0, 3.0, 0.0)))
    b = open(p, 'rb').read()
    ck(st.unpack_from('<II', b, 4) == (slwa.VERSION_SKINNED, slwa.CLASS_PROP)
       and st.unpack_from('<6f', b, 48) == (-1.0, 0.0, -2.0, 1.0, 3.0, 0.0),
       'a prop bundle is version 4 of class PROP with the reference box as its bounds')
    for what, cls, bnd, joints in (
            ('a prop without its reference box', slwa.CLASS_PROP, None,
             [('SKEL_ROOT', -1, (0.0, 0.0, 0.0))]),
            ('a reference box on a character', slwa.CLASS_CHAR, ((0, 0, 0), (1, 1, 1)),
             [('SKEL_ROOT', -1, (0.0, 0.0, 0.0))]),
            ('a prop of two joints', slwa.CLASS_PROP, ((0, 0, 0), (1, 1, 1)),
             [('SKEL_ROOT', -1, (0.0, 0.0, 0.0)), ('SKEL_B', 0, (0.0, 1.0, 0.0))]),
            ('an unordered reference box', slwa.CLASS_PROP, ((0, 2, 0), (1, 1, 1)),
             [('SKEL_ROOT', -1, (0.0, 0.0, 0.0))]),
            ('a prop joint naming a Switches[] part', slwa.CLASS_PROP, ((0, 0, 0), (1, 1, 1)),
             [('BASE', -1, (0.0, 0.0, 0.0), slwa.part_matrix(0)),
              ('WHEEL', 0, (0.0, 1.0, 0.0), 3)])):
        try:
            slwa.write_skinned(os.path.join(tmp, 'prop', 'bad.slwa'), cls, 'bad', quad(4),
                               [0, 1, 2, 1, 3, 2], [(0, slwa.BLEND_OPAQUE)], [(64, 16, img)],
                               [(0, 0, 6, 0)], [((0, 255),)] * 4, joints, bounds=bnd)
            ck(False, '%s is refused' % what)
        except slwa.SlwaError:
            ck(True, '%s is refused' % what)

    # a MOVING prop: several joints, each naming the matrix it rides
    p = os.path.join(tmp, 'prop', 'xbla', 'prop', 'movprop.slwa')
    slwa.write_skinned(p, slwa.CLASS_PROP, 'movprop', quad(4), [0, 1, 2, 1, 3, 2],
                       [(0, slwa.BLEND_OPAQUE)], [(64, 16, img)], [(0, 0, 6, 0)],
                       [((0, 255),), ((0, 255),), ((1, 255),), ((1, 255),)],
                       [('BASE', -1, (0.0, 0.0, 0.0), slwa.part_matrix(0)),
                        ('WHEEL1', 0, (0.0, 1.0, 0.0), slwa.part_matrix(2))],
                       bounds=((-1.0, 0.0, -2.0), (1.0, 3.0, 0.0)))
    b = open(p, 'rb').read()
    jo = st.unpack_from('<I', b, 120)[0]
    ck(st.unpack_from('<I', b, 124)[0] == 2
       and st.unpack_from('<I', b, jo + 40)[0] == slwa.part_matrix(0)
       and st.unpack_from('<I', b, jo + 48 + 40)[0] == slwa.part_matrix(2),
       'a moving prop carries its joints with the model matrix each rides')

    try:
        import numpy                                          # noqa: F401
    except ImportError:
        return
    import prepare
    import xblachar
    # which matrix each item joint rides: its own name, else its parent's
    jts = [dict(name='BASE', parent=-1, bind=(0.0, 0.0, 0.0)),
           dict(name='PASTED__BASE', parent=-1, bind=(0.0, 0.0, 0.0)),
           dict(name='WHEEL2', parent=0, bind=(1.0, 0.0, 0.0)),
           dict(name='BEAN_END2', parent=2, bind=(2.0, 0.0, 0.0))]
    got = prepare.prop_joints(jts, ['base', 'wheel1', 'wheel2'])
    ck([g[3] for g in got] == [slwa.part_matrix(0), 0, slwa.part_matrix(2), slwa.part_matrix(2)],
       'a joint rides the matrix its name is the game\'s for, a nameless one its parent\'s, a '
       'nameless root none: %r' % [g[3] for g in got])
    try:
        prepare.prop_joints(jts[1:2], ['base', 'wheel1'])
        ck(False, 'an item no joint of which bears a matrix name is refused')
    except prepare.CharacterRefused:
        ck(True, 'an item no joint of which bears a matrix name is refused')

    # the item's own NAME TABLE: two joints named in lower case right after
    # their records, and a style name further on that the older passes would
    # take for the root
    g = bytearray(0x400)
    at = 0x40
    g[at:at + 5] = b'pose\0'
    st.pack_into('>I', g, at + xblachar.POSE_COUNT_AT, 2)
    first = at + xblachar.POSE_FIRST
    for j, (par, rel) in enumerate(((0xffff, (0.0, 1.0, 0.0)), (0, (2.0, -1.0, 0.0)))):
        o = first + j * xblachar.POSE_RECORD
        ab = rel if j == 0 else (2.0, 0.0, 0.0)
        st.pack_into('>3f3f4f4H', g, o, *rel, *ab, 0.0, 0.0, 0.0, 1.0, par, 0xffff, 0xffff, j)
    names = 0x300
    for k, nm in enumerate((b'base\0', b'wheel1\0', b'R1_GEOMETRY_STYLE\0')):
        g[names + 32 * k:names + 32 * k + len(nm)] = nm
    tab = first + 2 * xblachar.POSE_RECORD + 16
    st.pack_into('>III', g, tab, names, 0, 0)
    st.pack_into('>III', g, tab + 12, names + 32, 0, 1)
    st.pack_into('>III', g, 0x200, names + 64, 0, 0)
    jn = xblachar.read_pose(bytes(g), 'truck', table_names=True)
    ck([j['name'] for j in jn] == ['BASE', 'WHEEL1'],
       'a prop item\'s joints are named from its own name table, lower-case names '
       'upper-cased: %r' % [j['name'] for j in jn])
    try:
        xblachar.read_pose(bytes(g), 'truck')
        ck(False, 'without the table the lower-case joint stays unnamed and is refused')
    except xblachar.CharacterError:
        ck(True, 'without the table the lower-case joint stays unnamed and is refused')
    tree = os.path.join(tmp, 'proptree')
    for gen in (prepare.REMASTERED, prepare.ORIGINAL):
        d = os.path.join(tree, 'Top', gen, 'prop', 'srcprop')
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, 'default.bin'), 'wb').write(b'x')
    fake = dict(joints=[dict(name='SKEL_ROOT', parent=-1, bind=(0.0, 0.0, 0.0))],
                vertices=quad(4), skin=[((0, 255),)] * 4,
                textures=[dict(name='t', w=64, h=16, rgba=img)], texcoord_scale=1.0,
                conditions=[],
                draws=[dict(image=0, indices=[0, 1, 2], pass_mask=0x4701, conditions=[]),
                       dict(image=0, indices=[1, 3, 2], pass_mask=0x4702, conditions=[])])
    real_s, real_b = xblachar.read_static, xblachar.static_bounds
    try:
        xblachar.read_static = lambda path, images=True: fake
        xblachar.static_bounds = lambda path: ((1.0, 2.0, 3.0), (4.0, 5.0, 6.0))
        summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
        b = open(os.path.join(tmp, 'propout', 'xbla', 'prop', 'game.slwa'), 'rb').read()
        mo = st.unpack_from('<I', b, 100)[0]
        ck(st.unpack_from('<6f', b, 48) == (1.0, 2.0, 3.0, 4.0, 5.0, 6.0)
           and st.unpack_from('<I', b, 80)[0] == 2
           and st.unpack_from('<I', b, mo + 16 + 4)[0] == slwa.BLEND_ALPHA
           and '1 blended' in summary,
           'a prop carries its original generation\'s box and its translucent pass blended: %s'
           % summary)
        # the decal pass: a draw under the source's decal style bit is marked
        # DECAL in either pass, keeps its own material, and an ordinary draw of
        # the same image is left unmarked
        fake['draws'] = [dict(image=0, indices=[0, 1, 2], pass_mask=0x4701, conditions=[]),
                         dict(image=0, indices=[1, 3, 2], pass_mask=0x2702, conditions=[]),
                         dict(image=0, indices=[0, 1, 2], pass_mask=0x2701, conditions=[]),
                         dict(image=0, indices=[1, 3, 2], pass_mask=0x4702, conditions=[])]
        summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
        b = open(os.path.join(tmp, 'propout', 'xbla', 'prop', 'game.slwa'), 'rb').read()
        mo = st.unpack_from('<I', b, 100)[0]
        ho = st.unpack_from('<I', b, 108)[0]
        nm = st.unpack_from('<I', b, 80)[0]
        mat = [st.unpack_from('<4I', b, mo + 16 * k) for k in range(nm)]
        mesh = [st.unpack_from('<4I', b, ho + 16 * k)[0] for k in range(4)]
        want = [(slwa.BLEND_OPAQUE, 0), (slwa.BLEND_ALPHA, slwa.MAT_DECAL),
                (slwa.BLEND_OPAQUE, slwa.MAT_DECAL), (slwa.BLEND_ALPHA, 0)]
        ck(nm == 4 and [(mat[m][1], mat[m][2]) for m in mesh] == want
           and len(set(mesh)) == 4 and '2 decal' in summary,
           'a prop\'s decal-pass draws are marked DECAL in both passes and never share a '
           'material with an ordinary draw of the same image: %r %s'
           % ([(mat[m][1], mat[m][2]) for m in mesh], summary))
        # blue coverage: a BLENDED draw whose program takes coverage from the
        # image's blue ('z') is marked COVERAGE_BLUE, beside DECAL when it is a
        # decal; real alpha ('w'), no texture factor (''), an unread program
        # (None), an opaque draw and a draw without the field are left unmarked
        fake['draws'] = [dict(image=0, indices=[0, 1, 2], pass_mask=0x2702, conditions=[],
                              coverage='z'),
                         dict(image=0, indices=[1, 3, 2], pass_mask=0x4702, conditions=[],
                              coverage='z'),
                         dict(image=0, indices=[0, 1, 2], pass_mask=0x2702, conditions=[],
                              coverage='w'),
                         dict(image=0, indices=[1, 3, 2], pass_mask=0x4702, conditions=[],
                              coverage=''),
                         dict(image=0, indices=[0, 1, 2], pass_mask=0x4702, conditions=[],
                              coverage=None),
                         dict(image=0, indices=[1, 3, 2], pass_mask=0x4701, conditions=[],
                              coverage='z'),
                         dict(image=0, indices=[0, 1, 2], pass_mask=0x4702, conditions=[])]
        summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
        b = open(os.path.join(tmp, 'propout', 'xbla', 'prop', 'game.slwa'), 'rb').read()
        mo = st.unpack_from('<I', b, 100)[0]
        ho = st.unpack_from('<I', b, 108)[0]
        nm = st.unpack_from('<I', b, 80)[0]
        mat = [st.unpack_from('<4I', b, mo + 16 * k) for k in range(nm)]
        mesh = [st.unpack_from('<4I', b, ho + 16 * k)[0] for k in range(7)]
        blue = slwa.MAT_COVERAGE_BLUE
        want = [(slwa.BLEND_ALPHA, slwa.MAT_DECAL | blue), (slwa.BLEND_ALPHA, blue),
                (slwa.BLEND_ALPHA, slwa.MAT_DECAL), (slwa.BLEND_ALPHA, 0),
                (slwa.BLEND_ALPHA, 0), (slwa.BLEND_OPAQUE, 0), (slwa.BLEND_ALPHA, 0)]
        ck([(mat[m][1], mat[m][2]) for m in mesh] == want
           and '2 blue coverage' in summary and '2 decal' in summary
           and mat[mesh[0]][0] == mat[mesh[1]][0] == 0,
           'a blended draw whose program takes coverage from blue is marked COVERAGE_BLUE '
           '(beside DECAL on a decal) and nothing else is: %r %s'
           % ([(mat[m][1], mat[m][2]) for m in mesh], summary))
        # the moving path: a skinned item, read with its name table, its
        # joints carried with the matrices they ride
        moving = dict(fake, joints=[dict(name='BASE', parent=-1, bind=(0.0, 0.0, 0.0)),
                                    dict(name='WHEEL1', parent=0, bind=(0.0, 1.0, 0.0))],
                      skin=[((0, 255),), ((0, 255),), ((1, 255),), ((1, 255),)],
                      draws=[dict(image=0, indices=[0, 1, 2], pass_mask=0x4701, conditions=[]),
                             dict(image=0, indices=[1, 3, 2], pass_mask=0x4701, conditions=[])])
        real_c, real_i = xblachar.read_character, xblachar.is_static
        seen = {}

        def fake_character(path, images=True, table_names=False, plain_sphere=False,
                           layers=False):
            seen['table_names'] = table_names
            seen['plain_sphere'] = plain_sphere
            seen['layers'] = layers
            return moving
        try:
            xblachar.is_static = lambda path: False
            xblachar.read_character = fake_character
            summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'),
                                           ['base', 'wheel1'])
            b = open(os.path.join(tmp, 'propout', 'xbla', 'prop', 'game.slwa'), 'rb').read()
            jo = st.unpack_from('<I', b, 120)[0]
            ck(seen.get('table_names') is True and seen.get('plain_sphere') is True
               and seen.get('layers') is True
               and st.unpack_from('<I', b, 124)[0] == 2
               and st.unpack_from('<I', b, jo + 48 + 40)[0] == slwa.part_matrix(1)
               and 'matrices 0 1' in summary,
               'a moving prop is read with its name table and its two-layer draws, and carries '
               'its wheels\' matrices: %s' % summary)
            # #96 A TWO-LAYER DRAW's layer (xblachar.STRIDE_RIGID_UV2): blended
            # over its base with the decal priority, by the image's blue.
            layered = dict(moving, draws=moving['draws'] + [
                dict(image=0, indices=[1, 3, 2], pass_mask=0x4701, conditions=[],
                     layer=dict(channel=2))])
            xblachar.read_character = lambda path, **kw: layered
            summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'),
                                           ['base', 'wheel1'])
            ck('3 draws (1 blended) (1 decal) (1 blue coverage)' in summary,
               '#96 a layer draw is blended, a decal, and weighed by blue: %s' % summary)
            layered['draws'][-1]['layer'] = dict(channel=3)
            summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'),
                                           ['base', 'wheel1'])
            ck('3 draws (1 blended) (1 decal),' in summary and 'blue' not in summary,
               '#96 a layer weighed by its image\'s alpha is blended and a decal, no blue: %s'
               % summary)
            xblachar.read_character = fake_character
            xblachar.is_static = lambda path: True
            summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'),
                                           ['base', 'wheel1'])
            ck('matrices' not in summary, 'a static item stays one rigid piece whatever the model')
            # ONE RIGID PIECE ON A SKELETON (a hat): every vertex on one joint
            def skeleton(path, images=True):
                raise xblachar.CharacterError('hat: the item has a skeleton; it is no static item')
            xblachar.read_static = skeleton
            onejoint = dict(moving, skin=[((3, 255),)] * 4)
            xblachar.read_character = lambda path, images=True, table_names=False: onejoint
            summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
            b = open(os.path.join(tmp, 'propout', 'xbla', 'prop', 'game.slwa'), 'rb').read()
            ck(st.unpack_from('<I', b, 124)[0] == 1 and 'matrices' not in summary,
               'an item skinned to one joint of a skeleton is one rigid piece (a hat): %s'
               % summary)
            xblachar.read_character = lambda path, images=True, table_names=False: moving
            try:
                prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
                ck(False, 'an item skinned over two joints is refused for a one-piece model')
            except prepare.CharacterRefused:
                ck(True, 'an item skinned over two joints is refused for a one-piece model')
            # #96 THE GAME'S COORDINATES FOR A STATIC ITEM: a listed item no
            # texture-scale rule decides is read at the game model's scale
            seen_uv = []

            def noedge(path, images=True, uv_scale=None):
                if images and uv_scale is None:
                    raise xblachar.CharacterError('game: the texture coordinates reach no atlas '
                                                  'edge at any power-of-two scale')
                seen_uv.append(uv_scale)
                return dict(moving, skin=[((0, 255),)] * 4, joints=[moving['joints'][0]])
            xblachar.read_static = noedge
            import gameshade as gs96
            real_gcs = gs96.game_coords_scale
            real_list = prepare.GAME_COORDS_STATIC
            gs96.game_coords_scale = lambda rom, name, v, d, n: (16.0, {0: 294})
            try:
                try:
                    prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'),
                                         rom='rom')
                    ck(False, '#96 an item the list does not name is refused as before')
                except prepare.CharacterRefused as exc:
                    ck('reach no atlas edge' in str(exc),
                       '#96 an item the list does not name is refused as before (%s)' % exc)
                prepare.GAME_COORDS_STATIC = ('game',)
                summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'),
                                               rom='rom')
                ck(seen_uv[-1:] == [16.0] and "the game's coordinates: texture scale 1/2048" in summary,
                   '#96 a listed item is read at the scale the game\'s coordinates decide: %s'
                   % summary)
                try:
                    prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
                    ck(False, '#96 without the ROM a listed item is refused')
                except prepare.CharacterRefused as exc:
                    ck('no ROM' in str(exc), '#96 without the ROM a listed item is refused (%s)' % exc)
                def otherwise(path, images=True, uv_scale=None):
                    if images and uv_scale is None:
                        raise xblachar.CharacterError('game: a vertex position is not a number')
                    return noedge(path, images, uv_scale)
                xblachar.read_static = otherwise
                try:
                    prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'),
                                         rom='rom')
                    ck(False, '#96 a listed item refused for another reason keeps that refusal')
                except prepare.CharacterRefused as exc:
                    ck('not a number' in str(exc) and "game's coordinates" not in str(exc),
                       '#96 a listed item refused for another reason keeps that refusal (%s)' % exc)
                xblachar.read_static = noedge
                gs96.game_coords_scale = lambda rom, name, v, d, n: (None, '0 scales carry it')
                try:
                    prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'),
                                         rom='rom')
                    ck(False, '#96 a listed item the game\'s coordinates do not decide is refused')
                except prepare.CharacterRefused as exc:
                    ck('decide nothing' in str(exc),
                       '#96 a listed item the game\'s coordinates do not decide is refused (%s)' % exc)
                # #96 THE ATLAS TRIANGLES FOR A STATIC ITEM: a listed item no
                # texture-scale rule decides is read at the atlas triangles' scale
                real_ats, real_alist = xblachar.atlas_triangles_scale, prepare.ATLAS_TRIANGLES_STATIC
                prepare.GAME_COORDS_STATIC = ()
                xblachar.atlas_triangles_scale = lambda v, d: 4.0
                try:
                    try:
                        prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
                        ck(False, '#96 an item the atlas list does not name is refused as before')
                    except prepare.CharacterRefused as exc:
                        ck('reach no atlas edge' in str(exc) and 'atlas triangles' not in str(exc),
                           '#96 an item the atlas list does not name is refused as before (%s)' % exc)
                    prepare.ATLAS_TRIANGLES_STATIC = ('game',)
                    summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
                    ck(seen_uv[-1:] == [4.0] and 'the atlas triangles: texture scale 1/8192' in summary,
                       '#96 a listed item is read at the atlas triangles\' scale, no ROM needed: %s'
                       % summary)
                    xblachar.read_static = otherwise
                    try:
                        prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
                        ck(False, '#96 an atlas-listed item refused for another reason keeps that refusal')
                    except prepare.CharacterRefused as exc:
                        ck('not a number' in str(exc) and 'atlas triangles' not in str(exc),
                           '#96 an atlas-listed item refused for another reason keeps that refusal '
                           '(%s)' % exc)
                    xblachar.read_static = noedge
                    xblachar.atlas_triangles_scale = lambda v, d: None
                    try:
                        prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
                        ck(False, '#96 a listed item the atlas triangles do not decide is refused')
                    except prepare.CharacterRefused as exc:
                        ck('atlas triangles decide nothing' in str(exc),
                           '#96 a listed item the atlas triangles do not decide is refused (%s)' % exc)
                finally:
                    xblachar.atlas_triangles_scale = real_ats
                    prepare.ATLAS_TRIANGLES_STATIC = real_alist
                ck(prepare.ATLAS_TRIANGLES_STATIC == ('shuttle',),
                   '#96 the atlas triangles read for Aztec\'s shuttle alone')
            finally:
                gs96.game_coords_scale = real_gcs
                prepare.GAME_COORDS_STATIC = real_list
            ck(prepare.GAME_COORDS_STATIC == ('door_azt_desk', 'door_azt_desk_top', 'cryptdoor4'),
               '#96 the game\'s coordinates read for Aztec\'s desk doors and Egyptian\'s cryptdoor4 alone')
        finally:
            xblachar.read_character, xblachar.is_static = real_c, real_i
            xblachar.read_static = lambda path, images=True: fake
        fake['draws'] = [dict(image=0, indices=[0, 1, 2], pass_mask=0x4701, conditions=[]),
                         dict(image=0, indices=[1, 3, 2], pass_mask=0x4702, conditions=[])]
        fake['draws'][1]['pass_mask'] = 0x4700
        try:
            prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
            ck(False, 'a prop draw in neither pass is refused')
        except prepare.CharacterRefused:
            ck(True, 'a prop draw in neither pass is refused')
        # THE UNSEEN IMAGE: an opaque image every draw of which carries a wholly
        # black vertex colour is drawn with the lane lifted; a black draw of an
        # image another draw shows lit, and a translucent black draw, are not
        black = [(float(i), 1.0, 2.0, 0.1 * i, 0.5, 0, 0, 0, 200) for i in range(4)]
        lit = [(float(i), 1.0, 2.0, 0.1 * i, 0.5, 10 + i, 20, 30, 255) for i in range(4)]
        verts = black + lit                                    # 0..3 black, 4..7 lit
        draws = [dict(image=1, indices=[0, 1, 2], pass_mask=0x4701),   # unseen: image 1
                 dict(image=1, indices=[1, 2, 3], pass_mask=0x4701),
                 dict(image=0, indices=[4, 5, 6], pass_mask=0x4701),   # lit
                 dict(image=0, indices=[0, 1, 5], pass_mask=0x4701),   # black, image shown lit
                 dict(image=2, indices=[0, 2, 3], pass_mask=0x4702)]   # translucent black
        ck(xblachar.unseen_images(verts, draws, 0x1) == [1]
           and xblachar.unseen_images(verts, draws[:2], 0x1) == [1]
           and xblachar.unseen_images(verts, draws[2:], 0x1) == []
           and xblachar.unseen_images(lit, [dict(image=0, indices=[0, 1])], 0x1) == [],
           'an image is unseen when every draw of it is opaque and wholly black: %r'
           % xblachar.unseen_images(verts, draws, 0x1))
        skin = [((0, 255),)] * 8
        v2, d2, s2, lifted = xblachar.lift_unseen(verts, draws, skin, 0x1)
        ck(lifted == [1] and len(v2) == 12 and len(s2) == 12
           and d2[0]['indices'] == [8, 9, 10] and d2[1]['indices'] == [9, 10, 11]
           and all(v2[k][5:] == (255, 255, 255, 200) for k in (8, 9, 10, 11))
           and all(v2[k][5:] == (0, 0, 0, 200) for k in (0, 1, 2, 3))
           and v2[8][:5] == verts[0][:5] and v2[11][:5] == verts[3][:5] and s2[9] == skin[1]
           and d2[3]['indices'] == [0, 1, 5] and d2[4]['indices'] == [0, 2, 3]
           and [d['image'] for d in d2] == [d['image'] for d in draws]
           and verts[2][5:] == (0, 0, 0, 200),
           'lifting copies the unseen draws\' vertices white where another draw keeps them '
           'black, and leaves the input alone: %r %r %r'
           % (lifted, [d['indices'] for d in d2], [v[5:] for v in v2]))
        own = [dict(image=3, indices=[0, 1, 2], pass_mask=0x4701)] + draws[2:4]
        v3, d3, s3, l3 = xblachar.lift_unseen(verts, own, skin, 0x1)
        ck(l3 == [3] and len(v3) == 10 and d3[0]['indices'] == [8, 9, 2]
           and v3[2][5:] == (255, 255, 255, 200) and v3[0][5:] == (0, 0, 0, 200),
           'a vertex only the lifted draws read is whitened in place: %r %r'
           % ([d['indices'] for d in d3], [v[5:] for v in v3]))
        rest = draws[2:]
        same = xblachar.lift_unseen(verts, rest, skin, 0x1)
        ck(same[0] is verts and same[1] is rest and same[2] is skin and same[3] == [],
           'nothing unseen: the item passes through untouched')
        fake['vertices'] = verts
        fake['skin'] = skin
        fake['textures'] = [dict(name='t', w=64, h=16, rgba=img), dict(name='u', w=64, h=16, rgba=img),
                            dict(name='v', w=64, h=16, rgba=img)]
        fake['draws'] = [dict(d, conditions=[]) for d in draws]
        summary = prepare.convert_prop('game', 'srcprop', tree, os.path.join(tmp, 'propout'))
        b = open(os.path.join(tmp, 'propout', 'xbla', 'prop', 'game.slwa'), 'rb').read()
        vo = st.unpack_from('<I', b, 92)[0]
        nv = st.unpack_from('<I', b, 72)[0]
        col = [st.unpack_from('<4B', b, vo + 24 * k + 20) for k in range(nv)]
        ck(nv == 12 and 'lane lifted on unseen image 1' in summary
           and all(col[k] == (255, 255, 255, 200) for k in (8, 9, 10, 11))
           and all(col[k] == (0, 0, 0, 200) for k in (0, 1, 2, 3)) and col[4] == (10, 20, 30, 255),
           'a prop is prepared with its unseen image\'s lane lifted and says so: %s %r'
           % (summary, col))
        fake['vertices'], fake['skin'] = quad(4), [((0, 255),)] * 4
        fake['textures'] = [dict(name='t', w=64, h=16, rgba=img)]
    finally:
        xblachar.read_static, xblachar.static_bounds = real_s, real_b


# ---- #71 WHICH IMAGE CHANNEL A BLENDED DRAW'S COVERAGE COMES FROM -----------
#
# Every program below is ASSEMBLED here, instruction by instruction, from the
# microcode layout xblaworld reads (control flow in the first slot, then
# fetch and ALU instructions of three big-endian dwords): no source bytes.

def _swz(c):
    """An ALU source swizzle replicating component `c` into every lane."""
    return sum(((c - i) & 3) << (2 * i) for i in range(4))


def _fetch(dst, dst_swizzle, const=0):
    return (1 | (1 << 5) | (dst << 12) | (const << 20), dst_swizzle, 0)


def _mul(dst, lane, a, ca, b, cb, export=False, pred=False, op=1):
    """dst.lane = a.ca * b.cb, `a` and `b` temporaries (None: a constant)."""
    ra, rb = (a if a is not None else 255), (b if b is not None else 255)
    return (dst | ((1 if export else 0) << 15) | ((1 << lane) << 16),
            (_swz(ca) << 16) | (_swz(cb) << 8) | ((1 if pred else 0) << 28),
            (op << 24) | (ra << 16) | (rb << 8)
            | ((1 if a is not None else 0) << 31) | ((1 if b is not None else 0) << 30))


def _program(fetches, alus):
    """exec(fetches) then exec-end(alus), instructions from slot 1 on."""
    nf, na = len(fetches), len(alus)
    cf0 = 1 | (nf << 12) | (((1 << (2 * nf)) - 1) & 0x555) << 16 | (1 << 44)
    cf1 = (1 + nf) | (na << 12) | (2 << 44)
    words = [cf0 & 0xffffffff, ((cf0 >> 32) & 0xffff) | ((cf1 & 0xffff) << 16),
             (cf1 >> 16) & 0xffffffff]
    for ins in list(fetches) + list(alus):
        words.extend(ins)
    return struct.pack('>%dI' % len(words), *words)


def coveragetests():
    X, Y, Z, W = 0, 1, 2, 3
    # blue: the fetch routes the image's blue into r1.w (swizzle x_yz, no alpha
    # lane), r1.y = r1.w * colour.a, oC0.a = r1.y * (a non-texture term)
    blue = _program([_fetch(1, 0 | (7 << 3) | (1 << 6) | (2 << 9))],
                    [_mul(1, Y, 1, W, 3, W), _mul(0, W, 1, Y, 2, X, export=True)])
    alpha = _program([_fetch(4, 0x688)],
                     [_mul(1, Y, 4, W, 3, W), _mul(0, W, 1, Y, 2, X, export=True)])
    none = _program([_fetch(1, 0 | (1 << 3) | (2 << 6) | (7 << 9))],
                    [_mul(0, W, 2, X, 3, W, export=True)])
    other = _program([_fetch(1, 0 | (7 << 3) | (1 << 6) | (2 << 9), const=1)],
                     [_mul(1, Y, 1, W, 3, W), _mul(0, W, 1, Y, 2, X, export=True)])
    pred = _program([_fetch(1, 0 | (7 << 3) | (1 << 6) | (2 << 9))],
                    [_mul(1, Y, 1, W, 3, W, pred=True), _mul(0, W, 1, Y, 2, X, export=True)])
    two = _program([_fetch(1, 0x688)],
                   [_mul(0, W, 1, W, 1, Z, export=True)])
    viamax = _program([_fetch(1, 0 | (7 << 3) | (1 << 6) | (2 << 9))],
                      [_mul(1, Y, 1, W, 3, W, op=2), _mul(0, W, 1, Y, 2, X, export=True)])
    ck(xblaworld.coverage_channel(blue) == 'z',
       'a program multiplying the fetched blue into the exported alpha has blue coverage')
    ck(xblaworld.coverage_channel(alpha) == 'w',
       'a program multiplying the fetched alpha into it has the image\'s alpha')
    ck(xblaworld.coverage_channel(none) == '',
       'a program whose exported alpha has no texture factor has none')
    ck(xblaworld.coverage_channel(other) == '',
       'a factor fetched from another image is not the base image\'s coverage')
    ck(xblaworld.coverage_channel(viamax) == '',
       'a fetched lane reaching the alpha through anything but a multiply is no factor')
    ck(xblaworld.coverage_channel(pred) is None,
       'a predicated write leaves the program unread rather than guessed')
    ck(xblaworld.coverage_channel(two) is None,
       'two image channels multiplied together are not one coverage channel')
    ck(xblaworld.coverage_channel(b'\x00' * 12) is None,
       'a program that never exports is unread')

    # #82 a fetch at a coordinate the program computed: fetch 0 samples r1 as
    # interpolated, an ALU writes r1.x, fetch 1 then samples r1 - a reflection
    def cf(addr, cnt, seq, op):
        return addr | (cnt << 12) | (seq << 16) | (op << 44)

    def slot(c0, c1):
        return [c0 & 0xffffffff, ((c0 >> 32) & 0xffff) | ((c1 & 0xffff) << 16),
                (c1 >> 16) & 0xffffffff]
    words = (slot(cf(2, 1, 1, 1), cf(3, 1, 0, 1)) + slot(cf(4, 1, 1, 2), 0)
             + list(_fetch(5, 0x688, const=0)) + list(_mul(1, X, 2, X, 3, X))
             + list(_fetch(6, 0x688, const=1)))
    refl = struct.pack('>%dI' % len(words), *words)
    ck(xblaworld.computed_fetches(refl) == {1},
       'a fetch whose coordinate an earlier instruction wrote is a computed one, '
       'one at the interpolator is not: %r' % (xblaworld.computed_fetches(refl),))
    ck(xblaworld.computed_fetches(blue) == set(),
       'a program fetching only at its interpolators computes no coordinate')
    ck(xblaworld.computed_fetches(b'\x00' * 12) is None,
       'a program whose control flow does not walk is unread')
    # #95 the same reflection with the coordinate's write and the second
    # fetch in predicated execs (control-flow ops 5 and 6, Control's floors):
    # read as the instructions that may run, the fetch is still computed;
    # the last-write question (coverage) still refuses such a program
    words = (slot(cf(2, 1, 1, 1), cf(3, 1, 0, 5)) + slot(cf(4, 1, 1, 6), 0)
             + list(_fetch(5, 0x688, const=0)) + list(_mul(1, X, 2, X, 3, X))
             + list(_fetch(6, 0x688, const=1)))
    reflp = struct.pack('>%dI' % len(words), *words)
    ck(xblaworld.computed_fetches(reflp) == {1},
       'a fetch whose coordinate a predicated instruction wrote is a computed one: %r'
       % (xblaworld.computed_fetches(reflp),))
    ck(xblaworld.coverage_channel(reflp) is None,
       'a program with a predicated exec still gives no coverage answer')
    words = (slot(cf(2, 1, 1, 1), cf(3, 1, 0, 7)) + slot(cf(4, 1, 1, 2), 0)
             + list(_fetch(5, 0x688, const=0)) + list(_mul(1, X, 2, X, 3, X))
             + list(_fetch(6, 0x688, const=1)))
    ck(xblaworld.computed_fetches(struct.pack('>%dI' % len(words), *words)) is None,
       'a loop in the control flow still leaves the program unread')

    # draw -> program: one effect group (a pixel object, then its vertex
    # object), op 0x2e naming the vertex record, the pool holding the code
    def obj(kind, size, lit, total=64):
        w = [0x102a1100 | kind, total, size, 0, 0x24, lit] + [0] * 6
        return struct.pack('>12I', *w) + bytes(total - 48)
    graph = obj(0x00, len(blue), 0) + obj(0x41, 0, 0)
    pool = blue + bytes(64)
    recs = [(0x10, xblaworld.OP_VS_BIND, 0, [0, 0, 0, 0, 0x900, 0]),
            (0x30, xblaworld.OP_DRAW, 0, [13, 4, 0]),
            (0x40, xblaworld.OP_DRAW, 0, [13, 4, 0])]
    ck(xblaworld.draw_coverage(graph, [pool], recs) == {0x30: 'z', 0x40: 'z'},
       'a draw takes the coverage of the program its binding names, and a later draw '
       'under the same binding keeps it')
    ck(xblaworld.draw_coverage(graph + obj(0x41, 0, 0), [pool], recs) == {},
       'a bundle whose program records and shader objects do not pair is not read')
    ck(xblaworld.draw_coverage(obj(0x00, len(blue), 0) + obj(0x41, 0, 0), [bytes(len(pool))],
                               recs) == {},
       'a pool that does not hold a program where the layout says is not read')

    # #92 a group carrying its vertex half twice (Depot's first group: 3 pixel
    # objects, then 6 vertex objects, the last 3 copies of the first 3): the
    # copy pairs with the same pixel object, so both records read the program
    recs2 = [(0x10, xblaworld.OP_VS_BIND, 0, [0, 0, 0, 0, 0x900, 0]),
             (0x30, xblaworld.OP_DRAW, 0, [13, 4, 0]),
             (0x38, xblaworld.OP_VS_BIND, 0, [0, 0, 0, 0, 0x940, 0]),
             (0x40, xblaworld.OP_DRAW, 0, [13, 4, 0])]
    twice = obj(0x00, len(blue), 0) + obj(0x41, 0, 0) + obj(0x41, 0, 0)
    ck(xblaworld.draw_coverage(twice, [pool], recs2) == {0x30: 'z', 0x40: 'z'},
       'a vertex object repeated byte for byte pairs with its group\'s pixel object: %r'
       % (xblaworld.draw_coverage(twice, [pool], recs2),))
    unlike = obj(0x00, len(blue), 0) + obj(0x41, 0, 0) + obj(0x41, 0, 1)
    ck(xblaworld.draw_coverage(unlike, [pool], recs2) == {},
       'a second vertex run that is not a copy of the first leaves the list unpaired')
    ck(xblaworld._paired_pixel_objects(xblaworld.shader_objects(twice)) is None,
       'without the graph to compare the copies in, a repeated run is not paired')

    # #92 a two-image draw: slot 1 fetched into r4 (the colour), slot 0's blue
    # into r1.w and multiplied into the exported alpha (Depot's yard tag)
    bsw = 0 | (7 << 3) | (1 << 6) | (2 << 9)
    tag = _program([_fetch(4, 0x688, const=1), _fetch(1, bsw, const=0)],
                   [_mul(1, Y, 1, W, 3, W), _mul(0, W, 1, Y, 2, X, export=True)])
    ck(xblaworld.two_image_coverage([tag], {0: 5, 1: 6}) == (0, 'z', 1),
       'coverage from slot 0\'s blue, colour from slot 1: %r'
       % (xblaworld.two_image_coverage([tag], {0: 5, 1: 6}),))
    ck(xblaworld.two_image_coverage([tag, tag], {0: 5, 1: 6}) == (0, 'z', 1),
       'program variants that agree answer as one')
    ck(xblaworld.two_image_coverage([tag, alpha], {0: 5, 1: 6}) is None,
       'variants that disagree on the coverage channel are not read')
    ck(xblaworld.two_image_coverage([tag], {0: 5}) is None
       and xblaworld.two_image_coverage([tag], {0: 5, 1: 6, 2: 7}) is None
       and xblaworld.two_image_coverage([], {0: 5, 1: 6}) is None,
       'only a draw of exactly two slots with a program is this case')
    ck(xblaworld.two_image_coverage([none], {0: 5, 1: 6}) is None,
       'a program whose alpha takes nothing from either slot is not')
    ck(xblaworld.two_image_coverage([refl], {0: 5, 1: 6}) is None,
       'a slot sampled at a computed coordinate (a reflection) is no colour image')

    # ... and the preparation: one image, the colour's rgb under the coverage
    # channel; D-047 inverts a coverage that leaves out exactly the content
    import importlib.util
    import numpy as np
    spec = importlib.util.spec_from_file_location('wa_prepare_c', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    mask = np.full((8, 8, 4), 255, dtype=np.uint8)
    mask[2:6, 2:6, :3] = 0                          # a black silhouette on white
    colour = np.zeros((8, 8, 4), dtype=np.uint8)
    colour[:, :, 3] = 255
    colour[2:6, 2:6, :3] = (200, 150, 60)           # the tag inside it, black round it
    ck(prepare.coverage_inverted(colour, mask[:, :, 2]),
       'a coverage bright only where the colour image is empty is inverted')
    ck(not prepare.coverage_inverted(colour, 255 - mask[:, :, 2]),
       'a coverage bright where the content is is taken as authored')
    ck(not prepare.coverage_inverted(colour, np.full((8, 8), 255, dtype=np.uint8)),
       'an all-bright coverage decides nothing')
    rec = prepare.coverage_composite(dict(name='c', rgba=colour), dict(name='m', rgba=mask), 'z', True)
    ck(rec['rgba'][3, 3, 3] == 255 and rec['rgba'][0, 0, 3] == 0
       and tuple(rec['rgba'][3, 3, :3]) == (200, 150, 60) and rec['has_alpha']
       and (rec['w'], rec['h']) == (8, 8),
       'the composite carries the colour image\'s rgb and the inverted blue as alpha')
    rec = prepare.coverage_composite(dict(name='c', rgba=colour), dict(name='m', rgba=mask), 'z', False)
    ck(rec['rgba'][3, 3, 3] == 0 and rec['rgba'][0, 0, 3] == 255,
       'as authored, the blue is the alpha as the program reads it')
    ck(prepare.divergence_applies('D-047', 'depot')
       and not prepare.divergence_applies('D-047', 'streets'),
       'D-047 is written for Depot\'s background alone')


# ---- #72 A DRAW WHOSE PROGRAM BLENDS TWO IMAGES ------------------------------
#
# Assembled like the coverage programs above: a pixel program fetching slot 1
# through interpolator 2 and slot 0 through interpolator 1, then lerping the
# two coloured texels by the layer's weight - the contract xblaworld reads.

def _tfetch(dst, src, const):
    return (1 | (src << 5) | (dst << 12) | (const << 20), 0x688, 0)


def _alu(op, dst, mask, a, b, c=(0, 0, 0), export=False, pred=False):
    """dst.mask = op(a, b[, c]); each source a temporary (reg, swizzle, negate)."""
    (ra, sa, na), (rb, sb, nb), (rc, sc, nc) = a, b, c
    return (dst | ((1 if export else 0) << 15) | (mask << 16),
            sc | (sb << 8) | (sa << 16) | (nc << 24) | (nb << 25) | (na << 26)
            | ((1 if pred else 0) << 28),
            rc | (rb << 8) | (ra << 16) | (op << 24) | (7 << 29))


def _layer_program(texel_alpha=True, pred=False, product=False, fetches=2, texel_channel=None):
    """`texel_channel` (0..3): the weight is i4.a x that channel of t0, the
    tail number's blend (xblachar.STRIDE_RIGID_UV2) when it is 2."""
    MUL, ADD, MAD = 1, 0, 11
    f = [_tfetch(6, 2, 1), _tfetch(7, 1, 0)][:fetches]
    if product:
        return _program(f, [_alu(MUL, 0, 7, (7, 0, 0), (6, 0, 0), export=True)])
    weight = (8, _swz(3), 0) if texel_alpha else (4, _swz(3), 0)
    pre = []
    if texel_channel is not None:
        pre = [_alu(MUL, 10, 15, (7, _swz(texel_channel), 0), (4, _swz(3), 0))]   # t0.c x i4.a
        weight = (10, 0, 0)
    return _program(f, [
        _alu(MUL, 8, 15, (7, 0, 0), (4, 0, 0)),              # layer = t0 x i4
        _alu(MUL, 9, 7, (6, 0, 0), (5, 0, 0), pred=pred),    # base  = t1 x i5
        _alu(ADD, 8, 7, (8, 0, 0), (9, 0, 1))] + pre + [     # layer - base
        _alu(MAD, 0, 7, (8, 0, 0), weight, (9, 0, 0), export=True)])


def _fetch_decl(off, fmt, stride=40, full=False):
    """One vertex fetch of a declaration static_fetches reads: (offset, format)
    at `stride` bytes, a mini fetch unless `full`."""
    return [(1 << 19) if full else 0, (fmt << 16) | (0 if full else 1 << 30),
            ((off // 4) << 8) | (stride // 4)]


def twolayertests():
    """#96 the 40-byte two-layer record (xblachar.STRIDE_RIGID_UV2): its
    declaration, its program's blue weight, its two surfaces, and every
    malformed layout refused."""
    import xblachar
    # THE PROGRAM: a blue-weighted blend is no two-layer blend to the
    # unchanged test, and with channels it is, naming the channel.
    blue = _layer_program(texel_alpha=False, texel_channel=2)
    ck(xblaworld.layer_blend(blue) is None,
       '#96 a blend weighed by the layer image\'s blue is not read without channels (unchanged)')
    got = xblaworld.layer_blend(blue, channels=(0, 1, 2, 3, None))
    ck(got == dict(base=1, layer=0, base_colour=5, layer_colour=4, base_coord=2,
                   layer_coord=1, texel_alpha=False, texel_channel=2),
       '#96 with channels it is the two-layer blend weighed by blue: %r' % (got,))
    got = xblaworld.layer_blend(_layer_program(), channels=(0, 1, 2, 3, None))
    ck(got is not None and got['texel_channel'] == 3 and got['texel_alpha'] is True,
       '#96 an alpha-weighed blend read with channels names channel 3')
    ck(xblaworld.layer_blend(_layer_program()) == dict(
        base=1, layer=0, base_colour=5, layer_colour=4, base_coord=2, layer_coord=1,
        texel_alpha=True), '#96 without channels the record is exactly as before')

    # THE DECLARATION: the colours start at a second full fetch, read only
    # where asked.
    words = ([0] * 3 + _fetch_decl(0, 57, full=True) + _fetch_decl(12, 26) + _fetch_decl(20, 7)
             + _fetch_decl(24, 25) + _fetch_decl(28, 25) + _fetch_decl(32, 6, full=True)
             + _fetch_decl(36, 6) + [0x12345678] * 3)
    blob = struct.pack('>%dI' % len(words), *words)
    ck(xblachar.static_fetches(blob) == {40: [{0: 57, 12: 26, 20: 7, 24: 25, 28: 25}]},
       '#96 without follow_full the declaration stops at the second full fetch (unchanged)')
    ck(xblachar.static_fetches(blob, follow_full=True) == {40: [xblachar.RIGID_UV2_FETCHES]},
       '#96 with follow_full it is the two-layer record: %r'
       % (xblachar.static_fetches(blob, follow_full=True),))
    words2 = (words[:3] + _fetch_decl(0, 57, full=True) + _fetch_decl(12, 26)
              + _fetch_decl(32, 6, stride=32, full=True) + [0x12345678] * 3)
    ck(xblachar.static_fetches(struct.pack('>%dI' % len(words2), *words2), follow_full=True)
       == {40: [{0: 57, 12: 26}]}, '#96 a full fetch of another stride ends the declaration')

    # THE ROLES: base slot 1 at +28 / +36, layer slot 0 at +24 / +32.
    decl = [dict(xblachar.RIGID_UV2_FETCHES)]
    roles = xblachar.two_layer_roles(decl, [blue], {0: 7, 1: 9}, 'fixture', 0xc50)
    ck(roles == [(9, (28, 36), None), (7, (24, 32), dict(channel=2))],
       '#96 the base is slot 1 at +28 / +36, the layer slot 0 at +24 / +32, weighed by blue: %r'
       % (roles,))

    def refused(what, *args):
        try:
            xblachar.two_layer_roles(*args)
        except xblachar.CharacterError:
            return True
        print('    (not refused: %s)' % what)
        return False
    no36 = dict(xblachar.RIGID_UV2_FETCHES); del no36[36]
    red32 = dict(xblachar.RIGID_UV2_FETCHES); red32[32] = 25
    ck(refused('no declaration', None, [blue], {0: 7, 1: 9})
       and refused('no +36 colour', [no36], [blue], {0: 7, 1: 9})
       and refused('+32 a coordinate', [red32], [blue], {0: 7, 1: 9})
       and refused('one of two declarations other', decl + [no36], [blue], {0: 7, 1: 9}),
       '#96 MALFORMED LAYOUT: a 40-byte record declared any other way is refused')
    ck(refused('no program', decl, [], {0: 7, 1: 9})
       and refused('a product', decl, [_layer_program(product=True)], {0: 7, 1: 9})
       and refused('red weight', decl, [_layer_program(texel_alpha=False, texel_channel=0)],
                   {0: 7, 1: 9}),
       '#96 a program that is not the blend, or weighs by a channel no material realises, '
       'is refused')
    ck(refused('slots 0 and 2', decl, [blue], {0: 7, 2: 9})
       and refused('one slot', decl, [blue], {1: 9})
       and refused('no slots', decl, [blue], None),
       '#96 a material naming other slots than the program samples is refused')

    # THE SURFACE: each reads its own coordinate and colour words.
    rec = (struct.pack('>3f', 1.0, 2.0, 3.0) + struct.pack('>4H', 0, 0xf000, 0xf000, 0xf000)
           + bytes(4) + struct.pack('>hhhh', 100, 200, 300, 400)
           + bytes((0xbf, 1, 2, 3)) + bytes((0xff, 4, 5, 6)))
    v, infl = xblachar._vertex(rec, 0, 40, 0, [0], [5], 'fixture', (24, 32))
    ck(v[5:9] == (3, 2, 1, 0xbf) and infl == [(5, 255)]
       and abs(v[3] - 100 * xblachar.TEXCOORD_SCALE) < 1e-12,
       '#96 the layer surface reads +24 and the +32 colour, rigid on its one slot')
    v, _infl = xblachar._vertex(rec, 0, 40, 0, [0], [5], 'fixture', (28, 36))
    ck(v[5:9] == (6, 5, 4, 0xff) and abs(v[4] - 400 * xblachar.TEXCOORD_SCALE) < 1e-12,
       '#96 the base surface reads +28 and the +36 colour')
    try:
        xblachar._vertex(rec, 0, 40, 0, [0], [5], 'fixture')
        ok = False
    except xblachar.CharacterError:
        ok = True
    ck(ok, '#96 a two-layer record read with no surface is refused')
    ck(40 not in xblachar.STRIDES,
       '#96 the character strides do not take the 40-byte record: only a moving prop reads it')


def layertests():
    got = xblaworld.layer_blend(_layer_program())
    ck(got == dict(base=1, layer=0, base_colour=5, layer_colour=4, base_coord=2,
                   layer_coord=1, texel_alpha=True),
       'a program lerping slot 1 x i5 toward slot 0 x i4 by i4.a x t0.a is read as two layers')
    got = xblaworld.layer_blend(_layer_program(texel_alpha=False))
    ck(got is not None and got['texel_alpha'] is False and got['base'] == 1,
       'a weight of the colour alpha alone is read as such')
    ck(xblaworld.layer_blend(_layer_program(product=True)) is None,
       'a program multiplying the two images together is not two layers')
    ck(xblaworld.layer_blend(_layer_program(fetches=1)) is None,
       'a program with one image is not two layers')
    ck(xblaworld.layer_blend(_layer_program(pred=True)) is None,
       'a predicated program is left unread')
    ck(xblaworld.layer_blend(b'\x00' * 12) is None, 'a program that never exports is unread')

    # the pool read twice, each surface with its own words (LAYER_INPUTS)
    def vert(x, uv0, uv1, c0, c1):
        return (struct.pack('>fff', x, 1.0, 2.0) + bytes(4) + struct.pack('>hh', *uv0)
                + struct.pack('>hh', *uv1) + bytes(c0) + bytes(c1))
    blob = (vert(0.0, (0, 128), (256, 64), (139, 0, 0, 0), (255, 30, 20, 10))
            + vert(1.0, (128, 128), (384, 64), (139, 0, 0, 0), (255, 30, 20, 10))
            + vert(2.0, (128, 0), (384, 0), (139, 0, 0, 0), (255, 30, 20, 10)))
    pool = dict(off=0, stride=32, count=3, vertices=[()] * 3, uv_divisor=128.0)
    lo, hi = [-10.0] * 3, [10.0] * 3
    fields = xblaworld.LAYER_INPUTS[32]
    rec, why = xblaworld.draw_layers(_layer_program(), fields, {0: 7, 1: 9}, pool, blob, lo, hi)
    ck(rec is not None and rec['base'][0] == 9 and rec['base'][1] == (20, 28)
       and rec['layer'][0] == 7 and rec['layer'][1] == (16, 24),
       'the base takes slot 1 and the +20/+28 words, the layer slot 0 and +16/+24')
    ck(rec is not None and rec['base'][2][1][3:9] == (3.0, 0.5, 10, 20, 30, 255)
       and rec['layer'][2][1][3:9] == (1.0, 1.0, 0, 0, 0, 139),
       'each surface reads its own coordinate and colour')
    rec, why = xblaworld.draw_layers(_layer_program(), fields, {0: 7, 2: 9}, pool, blob, lo, hi)
    ck(rec is None and why, 'a draw whose slots are not the ones its program samples is refused')
    rec, why = xblaworld.draw_layers(_layer_program(product=True), fields, {0: 7, 1: 9},
                                     pool, blob, lo, hi)
    ck(rec is None and not why, 'a draw whose program is not the blend is left alone')
    ck(xblaworld._pool_vertices(blob, 0, 32, 3, lo, hi, 16, 128.0, co=16) is None,
       'a colour word that overlaps the coordinate is refused')

    # #85 THE LAYOUT'S ASSIGNMENT for a draw its program does not decide
    rec, why = xblaworld.layout_layers([], fields, {0: 7, 1: 9}, pool, blob, lo, hi)
    ck(rec is not None and rec['base'][0] == 9 and rec['base'][1] == (20, 28)
       and rec['layer'][0] == 7 and rec['layer'][1] == (16, 24) and rec['texel_alpha'] is None,
       '#85 a draw no program decides takes the layout\'s assignment (base slot 1 at +20/+28, '
       'layer slot 0 at +16/+24)')
    one = _program([_tfetch(7, 1, 0)], [_alu(1, 0, 15, (7, 0, 0), (4, 0, 0), export=True)])
    rec, why = xblaworld.layout_layers([one], fields, {0: 7, 1: 9}, pool, blob, lo, hi)
    ck(rec is not None, '#85 a program sampling slot 0 alone, at the layer\'s coordinate, '
                        'does not contradict the assignment')
    terrain = _program([_tfetch(6, 0, 1), _tfetch(7, 0, 0)],
                       [_alu(1, 0, 7, (7, 0, 0), (6, 0, 0), export=True)])
    rec, why = xblaworld.layout_layers([terrain], fields, {0: 7, 1: 9}, pool, blob, lo, hi)
    ck(rec is None and why, '#85 a program sampling both slots at one coordinate is not carried '
                            'by the assignment')
    rec, why = xblaworld.layout_layers([], fields, {0: 7, 2: 9}, pool, blob, lo, hi)
    ck(rec is None and why, '#85 a draw naming other slots is not carried by the assignment')
    ck(xblaworld.layout_scale([dict(vertices=[1], stride=32, uv_divisor=256.0),
                               dict(vertices=[1], stride=24, uv_divisor=64.0),
                               dict(vertices=None, stride=32, uv_divisor=None)], 32) == 256.0
       and xblaworld.layout_scale([dict(vertices=[1], stride=32, uv_divisor=256.0),
                                   dict(vertices=[1], stride=32, uv_divisor=128.0)], 32) is None
       and xblaworld.layout_scale([dict(vertices=[1], stride=24, uv_divisor=64.0)], 32) is None,
       '#85 a layout\'s scale is the one every validated pool of its stride agrees on, else none')


# ---- #86 THE IMAGE'S OWN REPEAT, and a position inside the item's own box ----
#
# Two 24-byte pools, each one 100 x 100 quad drawing image 3: the reference
# pool's coordinates run 0 .. 256 (one repeat at 1/256, 100 units a repeat),
# the other's the same 0 .. 256 (255 and 256 among them), measured at 1/4096,
# so 1600 units a repeat.

def _repeat_pools(raw_b, div_b, img_b=3):
    import xblaworld
    corners = [(0.0, 0.0, 0.0), (100.0, 0.0, 0.0), (100.0, 0.0, 100.0), (0.0, 0.0, 100.0)]

    def rec(p, uv):
        return struct.pack('>3f', *p) + b'\0\0\0\0' + struct.pack('>hh', *uv) + b'\xff\x80\x80\x80'
    raw_a = [(0, 0), (256, 0), (256, 256), (0, 256)]
    blob = b''.join(rec(p, uv) for p, uv in zip(corners, raw_a))
    blob += b''.join(rec(p, uv) for p, uv in zip(corners, raw_b))
    lo, hi = (-1.0, -1.0, -1.0), (1000.0, 1000.0, 1000.0)
    pools = []
    for k, (div, raw) in enumerate(((256.0, raw_a), (div_b, raw_b))):
        vs = [p + (u / div, v / div, 128, 128, 128, 255) for p, (u, v) in zip(corners, raw)]
        pools.append(dict(off=96 * k, stride=24, count=4, vertices=vs, uv_offset=16,
                          uv_divisor=div, uv_edge=1.0, uv_evidence=0.07, stream=None))
    ind = (0, 1, 2, 0, 2, 3)
    bound = [(dict(image=3, prim=xblaworld.PRIM_TRILIST), 0, ind),
             (dict(image=img_b, prim=xblaworld.PRIM_TRILIST), 1, ind)]
    return pools, bound, blob, lo, hi


def gamewatertests(tmp):
    """D-048: gameshade.water_draws over a synthetic square of the game's
    moving water (texture 1508) and remastered draws on it; the GAME_WATER bit
    round-trips on a background and is refused on a skydome; the stage table's
    item spelling resolves to its stage; the divergence names Caverns alone."""
    import importlib.util
    import gameshade                                        # noqa: E402
    spec = importlib.util.spec_from_file_location('wa_prepare_gw', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    sc = 7.44
    white = ((255, 255, 255, 255),) * 3

    def st(p):                                  # the game's s, t at a room point
        return (p[0] * 20.0, p[2] * 20.0)

    def game(tex=1508, y=0.0):
        sq = [((0, y, 0), (100, y, 0), (100, y, 100)), ((0, y, 0), (100, y, 100), (0, y, 100))]
        return [(1, 'primary', 0, tex, P, white, tuple(st(p) for p in P)) for P in sq]

    def draw(f, lift=0.0, dx=0.0, **extra):
        corners = [(10 + dx, 0, 10), (90 + dx, 0, 10), (90 + dx, 0, 90), (10 + dx, 0, 90)]
        verts = []
        for p in corners:
            u, v = f(*st(p))
            verts.append((p[0] * sc, p[1] * sc + lift, p[2] * sc, u, v, 255, 255, 255, 255))
        d = dict(at=0x854, pool=0, image=0, indices=[0, 1, 2, 0, 2, 3])
        d.update(extra)
        return [d], [dict(vertices=verts)]

    def game_coords(s, t):
        return s / 2048.0 + 0.3, 0.7 - t / 2048.0

    D, P = draw(game_coords, lift=3.6)
    m, rep = gameshade.water_draws(game(), sc, D, P)
    ck(m == [0] and rep['game'] == 2 and rep['marked'] == [dict(at=0x854, tris=2)],
       "a draw on the game's water with the game's coordinates, its plane 0.48 room units "
       "above the game's, moves with it")
    D, P = draw(lambda s, t: (s / 2048.0, t / 2048.0))
    m, rep = gameshade.water_draws(game(), sc, D, P)
    ck(m == [] and len(rep['refused']) == 1 and 'not the game' in rep['refused'][0]['why'],
       'coordinates running with t, not against it, are refused and reported')
    D, P = draw(lambda s, t: (s / 1024.0, -t / 1024.0))
    m, rep = gameshade.water_draws(game(), sc, D, P)
    ck(m == [] and len(rep['refused']) == 1,
       'coordinates at another repeat are refused and reported')
    D, P = draw(game_coords, dx=500.0)
    m, rep = gameshade.water_draws(game(), sc, D, P)
    ck(m == [] and rep['refused'] == [], 'a draw off the water is neither moved nor reported')
    D, P = draw(game_coords, lift=2.0 * sc)
    m, rep = gameshade.water_draws(game(), sc, D, P)
    ck(m == [] and rep['refused'] == [],
       "a plane two room units off the game's is not on its water")
    D, P = draw(game_coords)
    m, rep = gameshade.water_draws(game(tex=1511), sc, D, P)
    ck(m == [] and rep['game'] == 0, "the Dam's water texture (1511) is not this rule's")
    for k in ('material', 'wave', 'layer', 'xform'):
        D, P = draw(game_coords, **{k: 1})
        ck(gameshade.water_draws(game(), sc, D, P)[0] == [],
           'a draw with a %s of its own is left to it' % k)
    D, P = draw(game_coords)
    D.append(dict(D[0], at=0x864, indices=[0, 1, 2]))
    P[0]['vertices'].append(P[0]['vertices'][0])
    m, rep = gameshade.water_draws(game(), sc, D, P)
    ck(m == [0, 1], 'every draw on the water is moved, each on its own triangles')
    ck(gameshade._table_stages('cavern') == ('caverns',) and gameshade._table_stages('surface') == ()
       and gameshade._table_stages('nosuchitem') == (),
       "the stage table's model spelling resolves to its sole stage; a shared item does not")
    ck(prepare.DIVERGENCE_ITEMS.get('D-048') == ('cavern',),
       "D-048 is written into Caverns' background alone")
    d = os.path.join(tmp, 'water.slwa')
    slwa.write(d, slwa.CLASS_BACKGROUND, 'cavern', quad(4), [0, 1, 2],
               [(0, slwa.BLEND_OPAQUE, slwa.MAT_GAME_WATER | slwa.MAT_FOG)],
               [(64, 16, bytes(range(256)) * 16)], [(0, 0, 3)])
    ck(struct.unpack_from('<I', open(d, 'rb').read(), read_back(d)['m_off'] + 8)[0]
       == slwa.MAT_GAME_WATER | slwa.MAT_FOG and read_back(d)['version'] == slwa.VERSION_STATIC,
       'the GAME_WATER bit round-trips beside FOG at the same version')
    try:
        slwa.write(os.path.join(tmp, 'water-sky.slwa'), slwa.CLASS_SKYDOME, 'dam', quad(4),
                   [0, 1, 2], [(0, slwa.BLEND_OPAQUE, slwa.MAT_GAME_WATER)],
                   [(64, 16, bytes(range(256)) * 16)], [(0, 0, 3)])
        ck(False, 'a GAME_WATER material is refused on a skydome')
    except slwa.SlwaError:
        ck(True, 'a GAME_WATER material is refused on a skydome')


def gamecoordtests():
    """THE GAME'S COORDINATES (#96, gameshade.coords_scale) on synthetic
    coordinates: an item whose coordinates are the game model's at one scale
    is read at it; one that is not, or two images on one texture, or an image
    reaching only part of the game's texture, is refused."""
    import gameshade                                        # noqa: E402
    lid = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0), (-0.109, 1.289), (0.5, -0.026)]
    rim = [(0.25, 0.25), (0.75, 0.25), (0.75, 0.75)]
    game = {197: list(lid), 1161: list(rim)}

    def item(pairs_by_image, f):
        V, D = [], []
        for im, pairs in pairs_by_image:
            ix = []
            for u, v in pairs:
                ix.append(len(V))
                V.append((0.0, 0.0, 0.0, u / f, v / f, 255, 255, 255, 255))
            D.append(dict(image=im, indices=ix))
        return V, D
    V, D = item([(0, lid), (1, rim)], 2.0)
    f, m = gameshade.coords_scale(game, V, D, 9)
    ck(f == 2.0 and m == {0: 197, 1: 1161},
       "an item carrying the game model's coordinates at 1/16384 is read there, each image on "
       "its own texture")
    V, D = item([(0, [(u * 1.5, v) for u, v in lid]), (1, rim)], 2.0)
    ck(gameshade.coords_scale(game, V, D, 9)[0] is None,
       "coordinates the game's model does not carry decide nothing")
    V, D = item([(0, rim), (1, rim)], 2.0)
    ck(gameshade.coords_scale(game, V, D, 9)[0] is None,
       'two images on one game texture decide nothing')
    V, D = item([(0, lid[:2]), (1, rim)], 2.0)
    ck(gameshade.coords_scale(game, V, D, 9)[0] is None,
       "an image reaching only part of the game's texture decides nothing")
    ck(gameshade.coords_scale({}, V, D, 9)[0] is None,
       "a game model with no textured triangle decides nothing")


def repeattests():
    import xblaworld
    import xblachar
    rep = {}
    pools, bound, blob, lo, hi = _repeat_pools([(0, 0), (255, 1), (256, 256), (1, 255)], 4096.0)
    xblaworld.image_repeat(pools, bound, blob, lo, hi, rep)
    ck(pools[1]['uv_divisor'] == 256.0 and pools[1].get('image_repeat_from') == 4096.0
       and rep.get('image_repeat_pools') == 1 and pools[0]['uv_divisor'] == 256.0,
       '#86 a pool drawing an image 16x the repeat another pool draws it at reads at the divisor '
       'that agrees (1/4096 -> 1/256), the reference pool unchanged')
    ck(abs(pools[1]['vertices'][2][3] - 1.0) < 1e-6,
       '#86 the pool is read again at the new divisor')
    pools, bound, blob, lo, hi = _repeat_pools([(0, 0), (512, 0), (512, 512), (0, 512)], 256.0)
    xblaworld.image_repeat(pools, bound, blob, lo, hi, {})
    ck(pools[1]['uv_divisor'] == 256.0 and pools[1].get('image_repeat_from') is None,
       '#86 a pool within the factor (2x the reference) reads as measured')
    pools, bound, blob, lo, hi = _repeat_pools([(0, 0), (255, 1), (256, 256), (1, 255)], 4096.0,
                                               img_b=9)
    xblaworld.image_repeat(pools, bound, blob, lo, hi, {})
    ck(pools[1]['uv_divisor'] == 4096.0,
       '#86 a pool whose image no other pool draws reads as measured')
    pools, bound, blob, lo, hi = _repeat_pools([(1, 3), (255, 5), (257, 255), (3, 253)], 4096.0)
    xblaworld.image_repeat(pools, bound, blob, lo, hi, {})
    ck(pools[1]['uv_divisor'] == 4096.0,
       '#86 no divisor holding 1% of the values -> the pool reads as measured')

    box = ((-2600800.0, 116101.0, 697585.0), (-2554810.0, 188708.0, 698165.0))
    ck(xblachar.position_ok((1.0, -2.0, 999999.0)) and xblachar.position_ok((1.0, 2.0, 3.0), box),
       '#86 a position under 1e6 reads with or without a declared box')
    ck(xblachar.position_ok((-2577805.0, 152404.0, 697875.0), box),
       '#86 a position past 1e6 inside the item\'s declared box reads')
    ck(not xblachar.position_ok((-2577805.0, 152404.0, 697875.0))
       and not xblachar.position_ok((-3577805.0, 152404.0, 697875.0), box)
       and not xblachar.position_ok((float('nan'), 0.0, 0.0), box)
       and not xblachar.position_ok((1e30, 0.0, 0.0), box),
       '#86 past 1e6 with no box, outside the box, NaN and 1e30 are refused')
    graph = b'rendergraph' + b'\0' * (0x34 - 11) + struct.pack(
        '>7f', 42973.8, -2577805.0, 152404.3, 697875.0, 22995.0, 36302.8, 289.8)
    b = xblachar.declared_box(graph)
    ck(b is not None and abs(b[0][0] - (-2577805.0 - 22995.0 - 0.01 * 36302.8)) < 1.0
       and abs(b[1][2] - (697875.0 + 289.8 + 0.01 * 36302.8)) < 1.0,
       '#86 the declared box is the item\'s centre +- extent, widened by 1% of the largest extent')
    ck(xblachar.declared_box(b'notagraph' + b'\0' * 0x50) is None,
       '#86 an item declaring no bounds has no box')


# ---- D-042 A SHAPED LAYER LAID AS A BAND ONTO ITS BASE'S OWN SURFACE --------
#
# A pad (image 5) and a strip along its edge whose base is the pad's image and
# whose layer (image 7) is shaped by its alpha: the strip's inner corners 0
# and 1 stand on the pad's, the outer 2 and 3 do not. The layer's first
# coordinate runs across the strip (outer 0, inner 1), the second along it.

def _band(img_axis='v', opaque_low=True, base_uv_off=0.0, extra_tri=False, shaped=True,
          one_draw=False):
    import importlib.util
    import numpy as np
    spec = importlib.util.spec_from_file_location('wa_prepare_band', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    pad = [(0.0, 0.0, 0.0, 0.0, 0.0, 9, 9, 9, 255), (10.0, 0.0, 0.0, 1.0, 0.0, 9, 9, 9, 255),
           (0.0, 0.0, -10.0, 0.0, 1.0, 9, 9, 9, 255), (10.0, 0.0, -10.0, 1.0, 1.0, 9, 9, 9, 255)]
    pos = [(0.0, 0.0, 0.0), (10.0, 0.00001, 0.0), (0.0, 0.0, 5.0), (10.0, 0.0, 5.0)]
    if extra_tri:
        pos.append((5.0, 0.0, 9.0))
    base = [p + ((0.0, 0.0), (1.0, 0.0), (0.0, -0.5), (1.0, -0.5), (0.5, -0.9))[i]
            + (9, 9, 9, 255) for i, p in enumerate(pos)]
    base[1] = base[1][:3] + (1.0 + base_uv_off, 0.0) + base[1][5:]
    layer = [p + ((1.0, 0.0), (1.0, 1.0), (0.0, 0.0), (0.0, 1.0), (-0.5, 0.5))[i]
             + (200, 200, 200, 255) for i, p in enumerate(pos)]
    al = np.full((8, 8), 255, dtype=np.uint8)
    edge = slice(4, 8) if opaque_low else slice(0, 4)
    if img_axis == 'v':
        al[edge, :] = 0
    else:
        al[:, edge] = 0
    img = np.zeros((8, 8, 4), dtype=np.uint8)
    img[:, :, 3] = al if shaped else 255
    textures = [dict(has_alpha=False, rgba=np.full((8, 8, 4), 255, dtype=np.uint8))] * 7 \
        + [dict(has_alpha=shaped, rgba=img)]
    pools = [dict(vertices=pad), dict(vertices=base)]
    ind = [0, 1, 2, 1, 3, 2] + ([2, 3, 4] if extra_tri else [])
    rim = dict(at=0x40, pool=1, indices=ind, image=5, xform=None,
               layer=dict(base=(5, (20, 28), base), layer=(7, (16, 24), layer), texel_alpha=True))
    draws = [rim] if one_draw else [dict(at=0x20, pool=0, indices=[0, 1, 2, 1, 3, 2], image=5,
                                         xform=None, layer=None), rim]
    rep = prepare.band_fade(draws, pools, textures)
    return prepare, rep, rim['layer'], layer, base


def shadowtests():
    """D-049 (prepare.shadow_overlays): a shaped draw's black triangles, and a
    two-layer draw's black layer, take the remaster's shadow alpha on copies;
    nothing else moves. Synthetic fixtures only."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('wa_prepare_s', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    A = prepare.SHADOW_ALPHA
    T = [dict(has_alpha=True), dict(has_alpha=False)]

    def v(c, a=255):
        return (0.0, 0.0, 0.0, 0.0, 0.0, c, c, c, a)
    # pool 0: a black quad (0..3), a white quad (4..7) sharing nothing;
    # pool 1: a mixed draw - triangle 0 black, triangle 1 white, sharing 1, 2
    pools = [dict(vertices=[v(0)] * 4 + [v(255)] * 4),
             dict(vertices=[v(4), v(4), v(4), v(255)])]
    draws = [dict(at=1, pool=0, image=0, indices=[0, 1, 2, 0, 2, 3]),
             dict(at=2, pool=0, image=0, indices=[4, 5, 6, 4, 6, 7]),
             dict(at=3, pool=1, image=0, indices=[0, 1, 2, 2, 1, 3])]
    m = prepare.shadow_overlays(draws, pools, T)
    ck(sorted(m) == [1, 2] and len(draws) == 4,
       'D-049 the black draw and the mixed draw\'s black triangle are marked; one draw added (%r)' % m)
    ck(draws[0].get('shadow') and draws[0]['indices'] == [8, 9, 10, 8, 10, 11]
       and all(pools[0]['vertices'][i][8] == A for i in range(8, 12))
       and all(pools[0]['vertices'][i][8] == 255 for i in range(8)),
       'D-049 a wholly black draw moves onto copies of its vertices at the shadow alpha; '
       'the originals keep theirs')
    ck(not draws[1].get('shadow') and draws[1]['indices'] == [4, 5, 6, 4, 6, 7],
       'D-049 a white draw is untouched')
    ck(not draws[2].get('shadow') and draws[2]['indices'] == [2, 1, 3]
       and draws[3].get('shadow') and draws[3]['at'] == 3 and draws[3]['indices'] == [4, 5, 6]
       and [pools[1]['vertices'][i][8] for i in range(7)] == [255, 255, 255, 255, A, A, A],
       'D-049 a mixed draw keeps its other triangles and its black ones follow it as a '
       'shadow draw on copies; the shared vertices keep their alpha')
    # the threshold, the unshaped image, the instance
    pools = [dict(vertices=[v(prepare.SHADOW_BLACK + 1)] * 3), dict(vertices=[v(0)] * 3),
             dict(vertices=[v(0)] * 3)]
    draws = [dict(at=1, pool=0, image=0, indices=[0, 1, 2]),
             dict(at=2, pool=1, image=1, indices=[0, 1, 2]),
             dict(at=3, pool=2, image=0, indices=[0, 1, 2], xform=[1.0] * 16)]
    ck(prepare.shadow_overlays(draws, pools, T) == [] and len(draws) == 3
       and not any(d.get('shadow') for d in draws),
       'D-049 a colour channel above SHADOW_BLACK, an image with no alpha, an instanced draw: '
       'none is marked')
    # the two-layer draw's layer: black layers re-keyed, sharing one copy
    lv = [v(0)] * 4 + [v(200)] * 2
    words = (16, 24)
    draws = [dict(at=1, pool=0, image=1, indices=[0, 1, 2],
                  layer=dict(base=(1, (20, 28), [v(255)] * 6), layer=(0, words, lv), texel_alpha=True)),
             dict(at=2, pool=0, image=1, indices=[1, 2, 3],
                  layer=dict(base=(1, (20, 28), [v(255)] * 6), layer=(0, words, lv), texel_alpha=True)),
             dict(at=3, pool=0, image=1, indices=[3, 4, 5],
                  layer=dict(base=(1, (20, 28), [v(255)] * 6), layer=(0, words, lv), texel_alpha=True))]
    m = prepare.shadow_overlays(draws, [dict(vertices=[v(255)] * 6)], T)
    l0, l1, l2 = (d['layer']['layer'] for d in draws)
    ck(m == [1, 1] and l0[1] == words + ('D-049',) and l0[2] is l1[2]
       and [x[8] for x in l0[2][:4]] == [A] * 4 and [x[8] for x in lv] == [255] * 6
       and l2[1] == words and l2[2] is lv,
       'D-049 black layers take the alpha on one shared re-keyed copy; a layer that is not '
       'black, and the source surface, are untouched')
    ck(prepare.DIVERGENCE_ITEMS['D-049'] == ('cradle',),
       'D-049 names Cradle\'s background alone')
    os.environ[prepare.AS_AUTHORED] = 'D-049'
    try:
        ck(not prepare.divergence_applies('D-049', 'cradle'),
           'D-049 SL_WA_AS_AUTHORED=D-049 prepares Cradle as authored')
    finally:
        del os.environ[prepare.AS_AUTHORED]
    ck(prepare.divergence_applies('D-049', 'cradle') and not prepare.divergence_applies('D-049', 'dam'),
       'D-049 applies to Cradle and nowhere else')


def _checker(n=16, hi=(255, 0, 255), lo=(120, 0, 120), flip=False):
    import numpy as np                                       # noqa: E402
    a = np.empty((n, n, 4), dtype=np.uint8)
    a[...] = lo + (255,)
    h = n // 2
    if flip:
        a[:h, :h] = a[h:, h:] = hi + (255,)
    else:
        a[:h, h:] = a[h:, :h] = hi + (255,)
    return a


def placeholdertests():
    """D-051 (prepare.placeholder_checker, placeholder_opaque,
    withhold_placeholders): the magenta placeholder checker is recognised by
    its pattern; an opaque draw on it draws by its vertex colour and a shaped
    one is withheld. Synthetic fixtures only."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('wa_prepare_p', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    pc = prepare.placeholder_checker
    ck(pc(_checker()) and pc(_checker(flip=True)) and pc(_checker(lo=(117, 0, 117))),
       'D-051 the source\'s checker reads as the placeholder, either diagonal, either dark shade')
    ck(pc(_checker(hi=(250, 3, 247))),
       'D-051 a block-compression wobble within PLACEHOLDER_GREEN is the placeholder')
    bad = _checker()
    bad[0, 0, 1] = 40
    ck(not pc(bad), 'D-051 a texel with green is not the placeholder')
    bad = _checker()
    bad[5, 5, 2] = 0
    ck(not pc(bad), 'D-051 a texel whose red and blue differ is not the placeholder')
    bad = _checker()
    bad[3, 3, 3] = 0
    ck(not pc(bad), 'D-051 a texel with alpha below 255 is not the placeholder')
    ck(not pc(_checker(lo=(255, 0, 255))) and not pc(_checker(hi=(120, 0, 120))),
       'D-051 an even magenta image is not the placeholder')
    bad = _checker()
    bad[0, 0, 0] = bad[0, 0, 2] = 255
    ck(not pc(bad), 'D-051 a bright texel off the checker\'s diagonal is not the placeholder')
    ck(not pc(_checker()[:, :15]) and not pc(_checker(15)) and not pc(_checker()[:, :, :3]),
       'D-051 an image that is not square, even and RGBA is not the placeholder')
    import numpy as np                                       # noqa: E402
    T = [dict(rgba=_checker()), dict(rgba=np.full((16, 16, 4), 128, dtype=np.uint8)),
         dict(rgba=_checker(flip=True))]
    po = prepare.placeholder_opaque
    ck(po(0x4701) and po(0x0001) and not po(0x4b01) and not po(0x4702) and not po(0x4711)
       and not po(None) and not po(0x0802),
       'D-051 an opaque style outside the foliage group is whole in its geometry; the cutout '
       'gate, a translucent style and no style are not')
    draws = [dict(image=0, tris=7, style=0x4b01), dict(image=1, tris=3, style=0x4701),
             dict(image=None, tris=2, style=0x4701),
             dict(image=('coverage', 0, 1, 'w', False), tris=4, style=0x4701),
             dict(image=2, tris=5, style=0x4702), dict(image=9, tris=1, style=0x4701),
             dict(image=0, tris=11, style=0x4701)]
    opaque = draws[6]
    keep = [draws[1], draws[2], draws[3], draws[5], draws[6]]
    got = prepare.withhold_placeholders(draws, T)
    ck(got == ((2, 12), (1, 11), [0, 2]) and draws == keep and opaque['image'] is None,
       'D-051 a cutout and a translucent draw on placeholder images are withheld, an opaque '
       'one stays and names no image, every other draw is kept in order - untextured, '
       'composite and out-of-range ones included (%r)' % (got,))
    ck(prepare.withhold_placeholders(draws, T) == ((0, 0), (0, 0), []) and draws == keep,
       'D-051 a second pass withholds nothing and colours nothing')
    # THE FILL WITH CLEAR TEXELS (Egyptian's image 15): an 8x8 block of full
    # and dark magenta in a clear 16x16
    def fill(full=(255, 0, 255), dark=(120, 0, 120)):
        a = np.zeros((16, 16, 4), dtype=np.uint8)
        a[:8, :4, :3] = dark
        a[:8, 4:8, :3] = full
        a[:8, :8, 3] = 255
        return a
    pf = prepare.placeholder_fill
    ck(pf(fill()) and pf(fill(full=(250, 3, 247))) and not pc(fill()),
       'D-051 the fill with clear texels round it reads as the placeholder (not as the checker)')
    ck(not pf(fill(full=(255, 0, 255), dark=(255, 0, 255)))
       and not pf(fill(full=(120, 0, 120), dark=(120, 0, 120))),
       'D-051 a fill of one shade only is not the placeholder')
    bad = fill()
    bad[2, 2, 1] = 40
    ck(not pf(bad), 'D-051 an opaque texel with green is not the fill')
    bad = fill()
    bad[12, 12, 3] = 128
    ck(not pf(bad), 'D-051 a half-transparent texel is not the fill')
    ck(not pf(np.zeros((16, 16, 4), dtype=np.uint8)) and not pf(fill()[:, :, :3]),
       'D-051 an image with no opaque texel, or without alpha, is not the fill')
    ck(pf(_checker()), 'D-051 the opaque checker is a fill too')
    T2 = [dict(rgba=fill())]
    d2 = [dict(image=0, tris=9, style=0x4701), dict(image=0, tris=4, style=0x4b01)]
    got2 = prepare.withhold_placeholders(d2, T2)
    ck(got2 == ((1, 4), (1, 9), [0]) and len(d2) == 1 and d2[0]['image'] is None,
       'D-051 an opaque draw on the fill draws by its colour and a cutout on it is withheld (%r)'
       % (got2,))
    ck(prepare.DIVERGENCE_ITEMS['D-051'] == ('aztec', 'temple'),
       'D-051 names Aztec\'s and Egyptian\'s backgrounds alone')
    os.environ[prepare.AS_AUTHORED] = 'D-051'
    try:
        ck(not prepare.divergence_applies('D-051', 'aztec')
           and not prepare.divergence_applies('D-051', 'temple'),
           'D-051 SL_WA_AS_AUTHORED=D-051 prepares Aztec and Egyptian as authored')
    finally:
        del os.environ[prepare.AS_AUTHORED]
    ck(prepare.divergence_applies('D-051', 'aztec') and prepare.divergence_applies('D-051', 'temple')
       and not prepare.divergence_applies('D-051', 'cradle'),
       'D-051 applies to Aztec and Egyptian and nowhere else')
    ck(prepare.CHART_SHORT_ITEMS == ('temple',),
       'the chart short of its unit is asked of Egyptian\'s background alone')


def opaquedoortests():
    """D-054 (prepare.translucent_only, gameshade.prop_blends): a door item
    wholly in the translucent pass over a game model that blends nothing.
    The ROM read is replaced by a synthetic model file."""
    import importlib.util
    import struct
    spec = importlib.util.spec_from_file_location('wa_prepare_d54', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    import gameshade as gs
    import watchhand
    to = prepare.translucent_only
    ck(to([dict(pass_mask=0x4702), dict(pass_mask=0x2702)]) and not to([dict(pass_mask=0x4702),
                                                                         dict(pass_mask=0x4701)])
       and not to([]) and not to([dict(pass_mask=0x4703)]) and not to([dict(pass_mask=None)]),
       'D-054 translucent alone is every draw\'s gate 2 - an opaque draw, a gate admitting both '
       'passes, no gate or no draw is not')

    def model(alpha, b9=None):
        dl = [(0x04 << 24 | 2 << 20 | 0 << 16 | 48, gs.SEG_MODEL_VTX << 24)]
        if b9 is not None:
            dl.append((0xB9 << 24 | 3 << 8 | 29, b9))
        dl += [(0xBF << 24, 0 << 16 | 10 << 8 | 20), (0xB8 << 24, 0)]
        data = bytearray(4) + struct.pack('>H2xI4xI4xI', gs.OP_DL, 28, 0, 0)
        data += struct.pack('>II4xI', 92, 0, 44)
        for x, y in ((0, 0), (10, 0), (0, 10)):
            data += struct.pack('>hhhhhh', x, y, 0, 0, 0, 0) + bytes([200, 200, 200, alpha])
        return bytes(data) + b''.join(struct.pack('>II', w0, w1) for w0, w1 in dl)
    real_hdr, real_rom = gs.prop_header, watchhand.rom_file
    try:
        gs.prop_header = lambda name, root=None: (1, 0)
        got = []
        for data in (model(255), model(200), model(255, 0x00004000), model(255, 0x00000800)):
            watchhand.rom_file = lambda rom, rel, root=None, _d=data: _d
            got.append(gs.prop_blends('rom', 'cryptdoor2a'))
    finally:
        gs.prop_header, watchhand.rom_file = real_hdr, real_rom
    ck(got == [(0, 1), (1, 1), (1, 1), (1, 1)],
       'D-054 a game triangle blends under a corner alpha below 255, a forced blend or the '
       'translucent depth mode, and not otherwise (%r)' % (got,))
    ck(prepare.DIVERGENCE_ITEMS['D-054'] == ('cryptdoor1a', 'cryptdoor2a', 'cryptdoor2b',
                                             'cryptdoor3', 'cryptdoor4'),
       'D-054 names Egyptian\'s five translucent crypt doors alone (not cryptdoor1b)')
    ck(prepare.PROP_REFLECTIONS == ('chrgolden',),
       'a prop\'s reflection is carried for the Golden Gun\'s pickup alone')
    # the game texture under each draw: two coplanar quads of the game's
    # model, textures 406 and 410, and an item drawing one on each
    sq = lambda x0, tex: [(((x0, 0.0, 0.0), (x0 + 10.0, 0.0, 0.0), (x0, 10.0, 0.0)), tex),
                          (((x0 + 10.0, 0.0, 0.0), (x0 + 10.0, 10.0, 0.0), (x0, 10.0, 0.0)), tex)]
    gv = [(1.0, 1.0, 0.0), (3.0, 1.0, 0.0), (1.0, 3.0, 0.0), (21.0, 1.0, 0.0), (23.0, 1.0, 0.0),
          (21.0, 3.0, 0.0), (1.0, 1.0, 9.0), (3.0, 1.0, 9.0), (1.0, 3.0, 9.0)]
    gd = [dict(indices=[0, 1, 2]), dict(indices=[3, 4, 5]), dict(indices=[6, 7, 8])]
    ck(gs.draw_textures(sq(0.0, 406) + sq(20.0, 410), 100.0, gv, gd) == {0: 406, 1: 410},
       'D-054 a draw takes the game texture its triangles lie on; one off every game plane none')
    # the background's image for a game texture: room triangles over item
    # triangles of image 2 (two of 406's three; the third on the plane, in
    # image 2's grid cell, but outside its triangles) and of images 3 (one of
    # 410's) and 5 (two of them)
    lvl = dict(pools=[dict(vertices=[(0, 0, 0), (40, 0, 0), (0, 40, 0), (40, 40, 0),
                                     (100, 0, 0), (140, 0, 0), (100, 40, 0), (140, 40, 0)])],
               draws=[dict(image=2, pool=0, indices=[0, 1, 2, 1, 3, 2]),
                      dict(image=3, pool=0, indices=[4, 5, 6]), dict(image=5, pool=0, indices=[5, 7, 6])])
    room = [(406, ((1, 1, 0), (5, 1, 0), (1, 5, 0))), (406, ((6, 6, 0), (9, 6, 0), (6, 9, 0))),
            (406, ((12, 1, 0), (13, 1, 0), (12, 2, 0))),
            (410, ((26, 1, 0), (30, 1, 0), (26, 5, 0))), (410, ((34, 8, 0), (34, 9.5, 0), (31, 9.5, 0))),
            (410, ((33, 6, 0), (34, 6, 0), (34, 7, 0))), (410, ((60, 60, 0), (61, 60, 0), (60, 61, 0)))]
    ti = gs.texture_images(room, 4.0, lvl)
    ck(ti.get(406) == (2, 2, 3) and ti.get(410) == (5, 2, 4),
       'D-054 a game texture\'s image is the one drawing most of its room triangles, out of '
       'all of them; a triangle beside the image\'s, not on it, is not drawn by it (%r)' % (ti,))
    # the face art: drawn opaque at alpha 255, the owned image on its draw,
    # the face's own image left out; nothing owned, nothing done
    img = lambda n: dict(name=n, w=1, h=1, rgba=bytes(4))
    got = dict(vertices=[(0, 0, 0, 0, 0, 10, 20, 30, 0)] * 3, skin=[((0, 255),)] * 3,
               draws=[dict(image=0, indices=[0, 1, 2], pass_mask=0x4702),
                      dict(image=1, indices=[0, 1, 2], pass_mask=0x4702)],
               textures=[img('frame.rgb'), img('face.int')])
    bgt = [img('bg0'), img('bg1'), img('ornament')]
    new, taken, walls = prepare.door_face_art(got, {0: 410, 1: 406}, {406: (2, 24, 24), 410: (1, 3, 8)},
                                              bgt, 0.75)
    ck(new is not None and [d['pass_mask'] for d in new['draws']] == [0x4701, 0x4701]
       and [t['name'] for t in new['textures']] == ['frame.rgb', 'ornament']
       and [d['image'] for d in new['draws']] == [0, 1] and all(v[8] == 255 for v in new['vertices'])
       and taken == [(1, 406, 'ornament', 24, 24)] and walls == [] and len(new['vertices']) == 3
       and got['draws'][0]['pass_mask'] == 0x4702,
       'D-054 an owned image over 75%% draws the face, the frame keeps its own, both opaque at '
       'alpha 255, the unused image left out, the input unchanged (%r)' % (taken,))
    ck(prepare.door_face_art(got, {0: 410}, {410: (1, 3, 8)}, bgt, 0.75) == (None, [], []),
       'D-054 no owned image over the share, no face art: the game\'s door draws')
    ck(prepare.level_images({0: 410, 1: 406, 2: None}, {406: (2, 24, 24), 410: (1, 3, 8)}, 0.75)
       == {1: (2, 24, 24)}, 'D-054 level_images: a draw\'s texture over the share alone')
    ck(gs.triangle_textures(sq(0.0, 406) + sq(20.0, 410), 100.0, gv, gd) == {(0, 0): 406, (1, 0): 410},
       'D-054 each triangle takes the game texture it lies on; one off every game plane none')
    # a door's face continues its wall. The placement: the door's face in the
    # plane x = 0 facing +x, s along -z and t up, 10 room units either way.
    place = dict(pad=0, centre=(0.0, 0.0, 0.0), normal=(1.0, 0.0, 0.0), thin=0,
                 axes=((0.0, 0.0, -1.0), (0.0, 1.0, 0.0)), half=(10.0, 10.0))
    ck(gs.door_plane(place, (5.0, 3.0, -2.0)) == (2.0, 3.0), 'D-054 door_plane: (s, t) in the door\'s axes')
    # the wall: one triangle beside the door at s 12 .. 18, t -5 .. 5, at
    # level scale 2, its texture map u = 0.1 s + 0.5 and v = 0.5 - 0.05 t;
    # the same turned to face away; one beyond the wall's reach
    wpts = [(0, -5, -12), (0, -5, -18), (0, 5, -12)]
    wuv = [(1.7, 0.75), (2.3, 0.75), (1.7, 0.25)]
    far = [(0, -5, -40), (0, -5, -46), (0, 5, -40)]
    wl = dict(pools=[dict(vertices=[tuple(2.0 * c for c in p) + uv for p, uv in zip(wpts + far, wuv + wuv)])],
              draws=[dict(image=1, pool=0, indices=[0, 1, 2, 0, 2, 1]), dict(image=1, pool=0, indices=[3, 4, 5])])
    wm = gs.wall_maps(place, 2.0, wl)
    ok = (len(wm) == 1 and wm[0][0] == 1 and wm[0][1] == (-5.0, 5.0)
          and all(abs(a - b) < 1e-9 for a, b in zip(wm[0][2] + wm[0][3], (0.1, 0.0, 0.5, 0.0, -0.05, 0.5))))
    ck(ok, 'D-054 wall_maps: the wall\'s band and texture map over the door\'s (s, t), the triangle '
           'facing away and the one beyond the reach left out (%r)' % (wm,))
    wt = gs.wall_textures([(404, wpts), (404, list(reversed(wpts))), (None, wpts), (410, far)], place)
    ck(wt == [(404, (-5.0, 5.0))], 'D-054 wall_textures: the facing room triangles in reach (%r)' % (wt,))
    # the door item: its box +-1 x +-1 x +-0.5, one triangle on its +z face
    # (t -10 .. 10 at the placement, centroid t -3.3), one on its -z face
    ref = ((-1.0, -1.0, -0.5), (1.0, 1.0, 0.5))
    dv = [(-1, -1, 0.5), (1, -1, 0.5), (-1, 1, 0.5), (-1, -1, -0.5), (-1, 1, -0.5), (1, -1, -0.5)]
    dd = [dict(indices=[0, 1, 2]), dict(indices=[3, 4, 5])]
    tt = {(0, 0): 404, (1, 0): 404}
    m1 = (1, (-5.0, 5.0), (0.1, 0.0, 0.5), (0.0, -0.05, 0.5))
    walls1 = [(place, [m1], [(404, (-5.0, 5.0))])]
    cw = prepare.continue_wall(dv, dd, tt, ref, walls1)
    want = {(0, 0): (1, ((-0.5, 1.0), (1.5, 1.0), (-0.5, 0.0)))}
    ck(set(cw) == {(0, 0)} and cw[(0, 0)][0] == 1
       and all(abs(a - b) < 1e-9 for p, q in zip(cw[(0, 0)][1], want[(0, 0)][1]) for a, b in zip(p, q)),
       'D-054 the +z face takes the wall\'s image at the wall\'s own coordinates; the -z face '
       'nothing (%r)' % (cw,))
    m2 = (1, (-5.0, 5.0), (0.1, 0.0, 0.5001), (0.0, -0.05, 0.5))
    for wl_, tt_, skip, why in (
            ([(place, [m1], [(410, (-5.0, 5.0))])], tt, (), 'the game drawing the wall in another texture'),
            ([(place, [m1, (2,) + m1[1:]], [(404, (-5.0, 5.0))])], tt, (), 'two images at that height'),
            ([(place, [m1, m1[:2] + ((0.1, 0.0, 0.6),) + m1[3:]], [(404, (-5.0, 5.0))])], tt, (),
             'two texture maps at that height'),
            ([(place, [m1], [(404, (-5.0, 5.0)), (406, (-5.0, 5.0))])], tt, (), 'two game textures there'),
            ([(place, [m1[:1] + ((6.0, 9.0),) + m1[2:]], [(404, (-5.0, 5.0))])], tt, (), 'no wall at its height'),
            ([(dict(place, thin=2), [m1], [(404, (-5.0, 5.0))])], tt, (), 'a placement thin on another axis'),
            ([(place, [m1], [(404, (-5.0, 5.0))]), (place, [m1[:2] + ((0.1, 0.0, 0.7),) + m1[3:]],
                                                     [(404, (-5.0, 5.0))])], tt, (), 'placements disagreeing'),
            (walls1, {}, (), 'no game texture under it'),
            (walls1, tt, {0}, 'a draw that took an image level-wide')):
        got_ = prepare.continue_wall(dv, dd, tt_, ref, wl_, skip)
        ck(got_ == {}, 'D-054 %s: the face takes nothing from its wall (%r)' % (why, got_))
    ck(set(prepare.continue_wall(dv, dd, tt, ref, walls1 + [(place, [m2], [(404, (-5.0, 5.0))])])) == {(0, 0)},
       'D-054 placements agreeing within WALL_MAP_TOL take the triangle')
    # moving the taken triangles: a frame draw keeps its other triangle and
    # the vertices it shares; a panel draw left empty goes, and its image;
    # coordinates moved by whole images; positions, colours and skin kept
    gw = dict(vertices=[(0, 0, 0, 0.1, 0.1, 1, 2, 3, 0), (1, 0, 0, 0.2, 0.1, 1, 2, 3, 0),
                        (1, 1, 0, 0.2, 0.2, 1, 2, 3, 0), (0, 1, 0, 0.1, 0.2, 1, 2, 3, 0),
                        (0, 2, 0, 0.0, 0.0, 4, 5, 6, 0), (1, 2, 0, 1.0, 0.0, 4, 5, 6, 0),
                        (0, 3, 0, 0.0, 1.0, 4, 5, 6, 0)],
              skin=[((0, 255),), ((0, 254),), ((0, 253),), ((0, 252),), ((0, 251),), ((0, 250),), ((0, 249),)],
              draws=[dict(image=0, indices=[0, 1, 2, 0, 2, 3], pass_mask=0x4702),
                     dict(image=1, indices=[4, 5, 6], pass_mask=0x4702)],
              textures=[img('frame.rgb'), img('panel.rgb')])
    wtk = {(0, 0): (1, ((3.2, 0.5), (3.6, 0.5), (3.6, 0.1))), (1, 0): (1, ((3.2, 0.9), (3.6, 0.9), (3.2, 0.6)))}
    nw, tk, ws = prepare.door_face_art(gw, {0: 410, 1: 404}, {}, bgt, 0.75, wtk)
    tri = lambda g: sorted(tuple(tuple(g['vertices'][i][:3]) + tuple(g['vertices'][i][5:8]) + (g['skin'][i],)
                                 for i in d['indices'][q:q + 3])
                           for d in g['draws'] for q in range(0, len(d['indices']), 3))
    kept = nw['draws'][0]
    ck(nw is not None and tk == [] and ws == [('bg1', 2, (0, 1), (404, 410))]
       and [t['name'] for t in nw['textures']] == ['frame.rgb', 'bg1'] and len(nw['draws']) == 2
       and [nw['vertices'][i][3:5] for i in kept['indices']] == [(0.1, 0.1), (0.2, 0.2), (0.1, 0.2)]
       and sorted(tuple(round(c, 6) for c in nw['vertices'][i][3:5]) for i in nw['draws'][1]['indices'])
       == sorted([(0.2, 0.5), (0.6, 0.5), (0.6, 0.1), (0.2, 0.9), (0.6, 0.9), (0.2, 0.6)])
       and tri(nw) == tri(gw) and len(nw['vertices']) == 3 + 6 and all(v[8] == 255 for v in nw['vertices'])
       and gw['vertices'][0][3:5] == (0.1, 0.1),
       'D-054 the taken triangles move to a draw of their image at its coordinates less whole '
       'images, on copies of their vertices; the frame keeps its other triangle and shared '
       'vertices; the emptied draw and its image go; positions, colours, skin kept (%r)' % (ws,))
    ck(set(prepare.DOOR_STAGE) == set(prepare.DOOR_BG) and set(prepare.DOOR_STAGE.values()) == {'egypt'},
       'D-054 every named door reads its placements from Egyptian\'s setup')
    ck(gs.BG_IMAGE_SHARE == 0.75 and set(prepare.DOOR_BG) == set(prepare.DIVERGENCE_ITEMS['D-054']),
       'D-054 the share is 75% and every named door reads its stage\'s background')
    os.environ[prepare.AS_AUTHORED] = 'D-054'
    try:
        ck(not prepare.divergence_applies('D-054', 'cryptdoor2b'),
           'D-054 SL_WA_AS_AUTHORED=D-054 prepares the crypt doors as authored')
    finally:
        del os.environ[prepare.AS_AUTHORED]


def originalgentests():
    """D-053 (prepare.ORIGINAL_GENERATION, decal_stand_ins;
    gameshade.model_triangles' mode, decal_alpha_draws) and D-052's wall
    (gameshade.in_door_wall, prepare.item_face_tone, door_wall_tone).
    Synthetic fixtures only."""
    import importlib.util
    import struct
    spec = importlib.util.spec_from_file_location('wa_prepare_d53', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    import gameshade as gs
    import numpy as np                                       # noqa: E402

    def v(x, y, z, u=0.0, w=0.0, c=255):
        return (x, y, z, u, w, c, c, c, 255)
    img = lambda n, g=200: dict(name=n, w=2, h=2, rgba=bytes([g, g, g, 255] * 4))
    ck(prepare.ORIGINAL_GENERATION == ('shuttle',), 'D-053 the shuttle alone is the original generation')
    # a model file: one DL node (root at 4: one switch), its vertex table and
    # one list - a triangle, a B9 setting the decal blend, a triangle
    dl = [(0x04 << 24 | 2 << 20 | 0 << 16 | 48, gs.SEG_MODEL_VTX << 24), (0xBF << 24, 0 << 16 | 10 << 8 | 20),
          (0xB9 << 24 | 3 << 8 | 29, 0xc4104dd8), (0xBF << 24, 0 << 16 | 10 << 8 | 20), (0xB8 << 24, 0)]
    data = bytearray(4) + struct.pack('>H2xI4xI4xI', gs.OP_DL, 28, 0, 0)
    data += struct.pack('>II4xI', 92, 0, 44)
    for x, y, a in ((0, 0, 89), (10, 0, 89), (0, 10, 89)):
        data += struct.pack('>hhhhhh', x, y, 0, 0, 0, 0) + bytes([255, 255, 255, a])
    data += b''.join(struct.pack('>II', w0, w1) for w0, w1 in dl)
    try:
        mt = gs.model_triangles(bytes(data), 1, 0)
    except gs.ShadeError as exc:
        mt = exc
    ck(isinstance(mt, list) and [t['mode'] for t in mt] == [None, 0xc4104dd8]
       and (mt[1]['mode'] & gs.RM_ZMODE) == gs.RM_ZMODE_DEC and mt[1]['mode'] & gs.RM_FORCE_BL
       and all(c[3] == 89 for c in mt[1]['C']),
       'D-053 a model list\'s B9 sets the mode of the triangles after it, the decal blend read '
       '(%r)' % (mt if not isinstance(mt, list) else [t['mode'] for t in mt],))
    blended = [(((0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (0.0, 10.0, 0.0)), (89, 89, 89)),
               (((0.0, 0.0, 50.0), (10.0, 0.0, 50.0), (0.0, 10.0, 50.0)), (255, 255, 255))]
    verts = [v(1, 1, 0), v(3, 1, 0), v(1, 3, 0), v(20, 20, 0), v(22, 20, 0), v(20, 22, 0),
             v(1, 1, 50), v(3, 1, 50), v(1, 3, 50), v(1, 1, 5), v(3, 1, 5), v(1, 3, 5)]
    skin = [((k, 255),) for k in range(len(verts))]
    draws = [dict(image=0, indices=[0, 1, 2], pass_mask=0x2702),                 # on: alpha
             dict(image=0, indices=[0, 1, 2], pass_mask=0x2701),                 # opaque, whole
             dict(image=0, indices=[0, 1, 2, 3, 4, 5], pass_mask=0x2701),        # opaque, part
             dict(image=0, indices=[0, 1, 2], pass_mask=0x4701),                 # no decal pass
             dict(image=0, indices=[0, 1, 2], pass_mask=0x2702, layer={'channel': 0}),
             dict(image=0, indices=[6, 7, 8], pass_mask=0x2702),                 # on alpha 255
             dict(image=0, indices=[9, 10, 11], pass_mask=0x2702)]               # off the plane
    before = (list(verts), list(skin), [dict(d) for d in draws])
    gv, gsk, gd, rep = gs.decal_alpha_draws(blended, 100.0, verts, skin, draws)
    ck((verts, skin, draws) == before, 'D-053 the decal alpha leaves its inputs as they were')
    ck(len(gv) == 15 and gv[:12] == verts and [x[8] for x in gv[12:]] == [89, 89, 89]
       and [x[:8] for x in gv[12:]] == [x[:8] for x in verts[:3]] and gsk[12:] == skin[:3],
       'D-053 a corner taking the game\'s alpha is a copy at alpha x a / 255, one per corner and '
       'alpha, its skin entry copied (%r)' % (gv[12:],))
    ck(len(gd) == 8 and gd[0]['indices'] == [12, 13, 14] and gd[0]['pass_mask'] == 0x2702
       and gd[1]['indices'] == [12, 13, 14] and gd[1]['pass_mask'] == 0x2702
       and gd[2]['indices'] == [3, 4, 5] and gd[2]['pass_mask'] == 0x2701
       and gd[3]['indices'] == [12, 13, 14] and gd[3]['pass_mask'] == 0x2702
       and gd[4:] == draws[3:],
       'D-053 a translucent decal takes the alpha in place, an opaque one wholly on the game\'s '
       'blended decal moves to the translucent pass, one partly on it gives those triangles to a '
       'translucent copy straight after it; a draw outside the decal pass, a layer, one on a game '
       'decal at 255 and one off its plane stand (%r)' % ([(d['indices'], hex(d['pass_mask'])) for d in gd],))
    ck([(r['draw'], r['alphas'], r['tris'], r['opaque'], r['moved']) for r in rep]
       == [(0, {89: 1}, 1, False, 0), (1, {89: 1}, 1, True, 0), (2, {89: 1}, 2, True, 1)],
       'D-053 the report names each draw, its alphas and what moved (%r)' % (rep,))
    ck(gs.decal_alpha_draws([], 100.0, verts, skin, draws)[2] == draws
       and gs.decal_alpha_draws(blended[1:], 100.0, verts, skin, draws)[3] == [],
       'D-053 no blended game decal, or one at 255 alone, changes nothing')
    ck('draw 2: 1 at 89/255 of 2 triangles (opaque: 1 to a translucent copy)'
       in gs.describe_decal_alpha(rep) and 'no decal' in gs.describe_decal_alpha([]),
       'D-053 the summary names the alphas and the moves')
    pools = [dict(vertices=[v(0, 0, 0, c=255), v(1, 0, 0, c=102), v(0, 1, 0, c=255),
                            (0, 0, 0, 0, 0, 255, 255, 0, 128), (1, 0, 0, 0, 0, 250, 250, 0, 128),
                            (0, 1, 0, 0, 0, 255, 178, 101, 128)])]
    draws = [dict(image=None, pool=0, pass_mask=0x2702, indices=[0, 1, 2, 3, 4, 5, 0, 1, 2], tris=3),
             dict(image=None, pool=0, pass_mask=0x4701, indices=[0, 1, 2], tris=1),
             dict(image=0, pool=0, pass_mask=0x2702, indices=[0, 1, 2], tris=1),
             dict(image=None, pool=0, pass_mask=0x2702, indices=[0, 1, 2], tris=1)]
    got = prepare.decal_stand_ins(draws, pools)
    ck(got == (2, 3) and len(draws) == 3 and draws[0]['indices'] == [3, 4, 5] and draws[0]['tris'] == 1
       and draws[1]['indices'] == [0, 1, 2] and draws[2]['image'] == 0,
       'D-053 neutral-grey untextured triangles under a translucent-only gate are withheld and an '
       'emptied draw dropped; coloured ones, an opaque-gated draw and a textured one stand (%r)'
       % (got,))
    ck(prepare.decal_stand_ins(draws, pools) == (0, 0), 'D-053 a second pass withholds nothing')
    place = dict(centre=(0.0, 0.0, 0.0), normal=(1.0, 0.0, 0.0), axes=((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
                 half=(50.0, 80.0))
    ck(gs.in_door_wall([2.0, 0.0, 120.0], [1.0, 0.0, 0.0], place)
       and gs.in_door_wall([-10.0, 90.0, 0.0], [-1.0, 0.0, 0.0], place),
       'D-052 a wall triangle in the door\'s plane round it, either facing, is the wall')
    ck(not gs.in_door_wall([0.0, 10.0, 10.0], [1.0, 0.0, 0.0], place)
       and not gs.in_door_wall([40.0, 0.0, 120.0], [1.0, 0.0, 0.0], place)
       and not gs.in_door_wall([0.0, 0.0, 200.0], [1.0, 0.0, 0.0], place)
       and not gs.in_door_wall([0.0, 0.0, 120.0], [0.0, 1.0, 0.0], place)
       and not gs.in_door_wall([0.0, 0.0, 120.0], [0.0, 0.0, 0.0], place),
       'D-052 the door\'s own footprint, a surface off its plane, beyond its reach, facing '
       'elsewhere or degenerate is not the wall')
    ck(gs.in_door_wall([3.84, 0.0, 230.4], [1.0, 0.0, 0.0], place, 1.92)
       and not gs.in_door_wall([3.84, 0.0, 330.0], [1.0, 0.0, 0.0], place, 1.92),
       'D-052 the wall is read in the item\'s units at the stage scale')
    face = [v(0, 0, 0, c=128), v(0, 10, 0, c=128), v(0, 0, 10, c=128), v(0, 20, 20, c=255),
            v(1, 0, 0, c=0), v(0, 0, 3, c=0)]
    t = prepare.item_face_tone(face, [dict(image=0, indices=[0, 1, 2, 1, 3, 2, 0, 4, 5])],
                               [img('d', 200)])
    a1, a2 = 50.0, 0.5 * float(np.linalg.norm(np.cross(np.subtract((0, 20, 20), (0, 10, 0)),
                                                          np.subtract((0, 0, 10), (0, 10, 0)))))
    want = 200 * (a1 * 128 + a2 * (128 + 255 + 128) / 3) / (a1 + a2) / 255
    ck(t is not None and all(abs(x - want) < 1e-6 for x in t),
       'D-052 the door\'s tone is the area-weighted image mean x vertex colour over its principal '
       'faces, the edge faces left out (%r, %.3f)' % (t, want))
    ck(prepare.door_wall_tone('rom', 'steel_door1', 'out', (1, 1, 1), face, [], []) == (None, ''),
       'D-052 a door DOOR_WALL does not name takes the game\'s shade alone, silently')
    note = prepare.door_wall_tone('rom', 'door_aztec', os.path.join(HERE, 'no-such-root'), (1, 1, 1),
                                  face, [], [])
    ck(note[0] is None and 'no background/aztec bundle' in note[1],
       'D-052 without the background bundle the wall is not read and the summary says so')
    ck(prepare.DOOR_WALL == {'door_aztec': 'aztec'}, 'D-052 the wall is read for Aztec\'s door alone')
    ck(prepare.DIVERGENCE_ITEMS['D-053'] == ('aztec', 'shuttle'),
       'D-053 names Aztec\'s background and the shuttle alone')


def propshadetests():
    """D-052 (gameshade.prop_game_shade, shade_vertices): the game's shade on
    a pale item. The ROM reads are replaced by synthetic triangles."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('wa_prepare_d52', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    import gameshade as gs
    sv = gs.shade_vertices
    v = [(1.0, 2.0, 3.0, 0.5, 0.25, 255, 255, 255, 200, 7), (0.0, 0.0, 0.0, 0.0, 0.0, 74, 74, 54, 255)]
    got = sv(v, (110.0, 110.0, 85.0))
    ck(got[0] == (1.0, 2.0, 3.0, 0.5, 0.25, 110, 110, 85, 200, 7) and got[1][5:] == (32, 32, 18, 255),
       'D-052 the shade multiplies each vertex colour by shade / 255, alpha and the rest kept (%r)'
       % (got,))
    real = (gs.prop_header, gs.rom_rel, gs.model_triangles, gs.watchhand.rom_file)

    def tri(p, c, tex=1):
        return dict(tex=tex, P=p, C=(c + (255,),) * 3)
    big = ((0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (0.0, 2.0, 0.0))        # area 2
    small = ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))      # area 0.5
    try:
        gs.prop_header = lambda name, root=None: (0, 1)
        gs.watchhand.rom_file = lambda rom, rel, root=None: b''
        gs.rom_rel = lambda name, root=None: 'p'
        gs.model_triangles = lambda data, ns, nt: [tri(big, (110, 110, 85)), tri(small, (92, 92, 71)),
                                                   tri(big, (0, 0, 0), tex=None)]
        sh, n, seen = gs.prop_game_shade('rom', 'door_aztec')
        want = tuple((2.0 * a + 0.5 * b) / 2.5 for a, b in ((110, 92), (110, 92), (85, 71)))
        ck(n == 2 and all(abs(sh[k] - want[k]) < 1e-6 for k in range(3))
           and seen == [(92, 92, 71), (110, 110, 85)],
           'D-052 the shade is the area-weighted mean of the textured triangles\' corners; an '
           'untextured triangle is not counted (%r %r)' % (sh, want))
        gs.model_triangles = lambda data, ns, nt: [tri(big, (255, 255, 255))]
        ck(gs.prop_game_shade('rom', 'x')[0] is None, 'D-052 a model shaded white shades nothing')
        gs.model_triangles = lambda data, ns, nt: [tri(big, (9, 9, 9), tex=None)]
        try:
            gs.prop_game_shade('rom', 'x')
            ck(False, 'D-052 a model with no textured triangle is refused')
        except gs.ShadeError:
            ck(True, 'D-052 a model with no textured triangle is refused')
    finally:
        gs.prop_header, gs.rom_rel, gs.model_triangles, gs.watchhand.rom_file = real
    ck(prepare.DIVERGENCE_ITEMS['D-052'] == ('door_aztec',), 'D-052 names Aztec\'s stone door alone')
    os.environ[prepare.AS_AUTHORED] = 'D-052'
    try:
        ck(not prepare.divergence_applies('D-052', 'door_aztec'),
           'D-052 SL_WA_AS_AUTHORED=D-052 prepares the door as authored')
    finally:
        del os.environ[prepare.AS_AUTHORED]
    ck(prepare.divergence_applies('D-052', 'door_aztec')
       and not prepare.divergence_applies('D-052', 'steel_door1'),
       'D-052 applies to the stone door and nowhere else')


def atlastests():
    """#96 xblachar.atlas_triangles_scale and prepare.background_blend.
    Synthetic fixtures only."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('wa_prepare_a', os.path.join(HERE, 'prepare.py'))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    import xblachar
    import xblaworld

    def v(u, w):
        return (0.0, 0.0, 0.0, u, w, 255, 255, 255, 255)
    # an atlas draw: nine triangles reaching 0.235 at the base scale, one
    # stray vertex at 0.32; and a tiling draw below zero
    V = [v(0.0, 0.0), v(0.2, 0.0), v(0.0, 0.235)] * 9 + [v(0.0, 0.0), v(0.32, 0.0), v(0.0, 0.1)]
    V += [v(-0.4, 0.0), v(0.8, 0.0), v(0.0, 0.5)] * 20
    atlas = list(range(30))
    tiling = list(range(30, 90))
    ats = xblachar.atlas_triangles_scale
    ck(ats(V, [atlas, tiling]) == 4.0,
       '#96 the atlas triangles: the largest scale where most of the non-tiling draws\' '
       'triangles stay inside and one reaches the edge, a stray vertex past it allowed (%r)'
       % ats(V, [atlas, tiling]))
    ck(ats(V, [tiling]) is None and ats(V, []) is None,
       '#96 the atlas triangles: tiling draws alone decide nothing')
    short = [v(0.0, 0.0), v(0.12, 0.0), v(0.0, 0.1)]
    ck(ats(short * 3, [list(range(9))]) == 8.0,
       '#96 the atlas triangles: a draw short of the edge is taken to it at the largest scale')
    ck(ats([v(0.0, 0.0)] * 3, [[0, 1, 2]]) is None,
       '#96 the atlas triangles: a draw that reaches no edge at any scale decides nothing')
    two = [v(0.0, 0.0), v(0.47, 0.0), v(0.0, 0.1)] * 3 + [v(0.0, 0.0), v(0.95, 0.0), v(0.0, 0.1)] * 2
    ck(ats(two, [list(range(15))]) == 2.0,
       '#96 the atlas triangles: of two scales that qualify, the larger (%r)'
       % ats(two, [list(range(15))]))
    half = [v(0.0, 0.0), v(0.2, 0.0), v(0.0, 0.235)] * 2 + [v(0.0, 0.0), v(0.5, 0.0), v(0.0, 0.1)] * 2
    ck(ats(half, [list(range(12))]) == 2.0,
       '#96 the atlas triangles: half of the triangles is not a majority (%r)'
       % ats(half, [list(range(12))]))
    B, A, X, O = prepare.background_blend, slwa.BLEND_ALPHA, slwa.BLEND_CUTOUT, slwa.BLEND_OPAQUE
    T, P = xblaworld.PASS_TRANSLUCENT, xblaworld.PASS_OPAQUE
    ck(B(0x4702, False, False, 0x4701, False) == A and B(0x4701, True, False, 0, True) == A,
       '#96 background blend: style 2 and a shadow overlay blend, as before')
    ck(B(0x4701, False, False, 0x4701, True) == X and B(0x4701, False, False, 0x4701, False) == O
       and B(None, False, False, None, False) == O,
       '#96 background blend: an opaque style is a cutout with image alpha, else opaque')
    ck(B(0x4701, False, True, 0x2700 | T, False) == A,
       '#96 background blend: an untextured draw in the translucent pass alone blends')
    ck(B(0x4701, False, False, 0x2700 | T, False) == O and B(0x4701, False, False, 0x2700 | T, True) == X
       and B(0x4701, False, True, 0x2700 | T | P, False) == O
       and B(0x4701, False, True, 0x4700 | P, False) == O and B(0x4701, False, True, None, False) == O,
       '#96 background blend: a textured draw under that gate, an untextured one under both '
       'passes, the opaque pass or none keep the style word\'s answer')
    D, M = prepare.background_decal, slwa.MAT_DECAL
    ck(D(0x2701, False, 0x4701) == M and D(0x6701, True, None) == M and D(0x4701, False, 0x4701) == 0
       and D(None, False, None) == 0,
       '#96 background decal: the style word\'s decal bit decides, as before')
    ck(D(0x4701, True, 0x2702) == M,
       '#96 background decal: an untextured draw in the translucent pass alone takes its gate\'s '
       'decal pass (Aztec\'s launch-pad markings)')
    ck(D(0x4701, False, 0x2702) == 0 and D(0x4701, True, 0x0702) == 0 and D(0x4701, True, 0x2703) == 0
       and D(0x4701, True, 0x2701) == 0,
       '#96 background decal: a textured draw under that gate, an untextured one whose gate names '
       'no decal pass, or admits the opaque pass, keeps the style word\'s answer')


def bandfadetests():
    prepare, rep, ly, layer, base = _band()
    out = ly['layer'][2]
    ck(len(rep) == 1 and rep[0]['on'] == 2 and rep[0]['corners'] == 4,
       'D-042 the strip\'s two corners on the pad (one 0.00001 off) are on the base\'s surface')
    ck([v[8] for v in out] == [0, 0, 255, 255],
       'D-042 a corner on the base image\'s surface takes layer weight 0, the others keep theirs')
    ck(len(rep) == 1 and rep[0]['swap'] and [v[3:5] for v in out] == [v[4:2:-1] for v in layer],
       'D-042 the fade runs along u and the image\'s alpha along v: the layer\'s coordinates swap')
    ck(ly['layer'][1] == (16, 24, 'D-042') and ly['base'][2] == base
       and all(o[:3] == v[:3] and o[5:8] == v[5:8] for o, v in zip(out, layer)),
       'D-042 the layer copy alone changes, under its own key; positions and colours are kept')
    _p, rep, ly, layer, _b = _band(img_axis='u')
    ck(len(rep) == 1 and not rep[0]['swap'] and [v[3:5] for v in ly['layer'][2]] == [v[3:5] for v in layer]
       and [v[8] for v in ly['layer'][2]] == [0, 0, 255, 255],
       'D-042 an image whose alpha already runs along the fade is faded, not turned')
    _p, rep, ly, layer, _b = _band(opaque_low=False)
    ck(len(rep) == 1 and not rep[0]['swap'] and [v[3:5] for v in ly['layer'][2]] == [v[3:5] for v in layer],
       'D-042 a turn that would face the image\'s opaque half onto the base is not taken')
    _p, rep, ly, layer, _b = _band(extra_tri=True)
    ck(rep == [] and ly['layer'][2] == layer and ly['layer'][1] == (16, 24),
       'D-042 a draw with a triangle wholly off the surface is not a band - left as it was')
    _p, rep, ly, layer, _b = _band(base_uv_off=0.01)
    ck(rep == [] and ly['layer'][2] == layer,
       'D-042 a corner where the base image does not continue (another coordinate) is not on it')
    _p, rep, ly, layer, _b = _band(shaped=False)
    ck(rep == [] and ly['layer'][2] == layer, 'D-042 a layer whose image has no alpha is left as it was')
    _p, rep, ly, layer, _b = _band(one_draw=True)
    ck(rep == [] and ly['layer'][2] == layer, 'D-042 with no other draw of the base image nothing fades')
    ck(prepare.divergence_applies('D-042', 'bunker2')
       and not any(prepare.divergence_applies('D-042', s)
                   for s in ('bunker', 'dam', 'facility', 'runway', 'surface', 'silo', 'frigate')),
       'D-042 is written for Bunker 2\'s background alone')
    os.environ['SL_WA_AS_AUTHORED'] = 'd-037, D-042'
    ck(not prepare.divergence_applies('D-042', 'bunker2'),
       'SL_WA_AS_AUTHORED=D-042 prepares Bunker 2\'s background as authored')
    del os.environ['SL_WA_AS_AUTHORED']
    ck(prepare.describe_band_fade([])[0].startswith('D-042: no shaped layer'),
       'D-042 says so when nothing fades')


# ---- #70 THE WATCH HAND -----------------------------------------------------
#
# A synthetic arm model (the game's layout: Switches[] then the texture table
# then the node tree, pointers relative to 0x05000000), two cuffs, and two
# generations of a synthetic item: the ORIGINAL generation lying on the game's
# cuffs, the REMASTERED one carrying a sleeve per cuff and a hand.

def _arm_model(cuffs, common, hands_at=(-100.0, 50.0, 0.0)):
    """Model bytes: HEADER -> wrist GROUP (matrix 0, at the origin) whose
    children are one SWITCH per cuff (Switches[4 + k]) each over one DL, a
    common DL and the watch hand GROUPSIMPLE (Switches[0], matrix 1).
    cuffs: [(texture id, [(p0, p1, p2), ...])]; common likewise, one DL."""
    B = 0x05000000
    nsw, ntex = 10, 1 + len(cuffs)
    tex_ids = [c[0] for c in cuffs] + [common[0]]
    body = bytearray()
    at = 4 * nsw + 12 * ntex
    nodes, datas, dls = [], [], []

    def node(op):
        nodes.append([op, 0, 0, 0, 0, 0])
        return len(nodes) - 1

    head = node(1); wrist = node(2)
    sw_nodes, dl_nodes = [], []
    for k, _c in enumerate(cuffs):
        sw_nodes.append(node(18)); dl_nodes.append(node(4))
    com = node(4); hand = node(21)
    n_nodes = len(nodes)
    base_nodes = at
    data_at = base_nodes + 24 * n_nodes

    def ptr(o):
        return B + o
    # data records
    blob = bytearray()

    def put(b):
        o = data_at + len(blob)
        blob.extend(b)
        while len(blob) % 8:
            blob.append(0)
        return o
    d_head = put(struct.pack('>IIII', 0, 0, 0, 0))
    d_wrist = put(struct.pack('>fffHhhhIf', 0.0, 0.0, 0.0, 0, 0, 0, 0, 0, 10.0))
    d_hand = put(struct.pack('>fffhHf', hands_at[0], hands_at[1], hands_at[2], 1, 0, 1.0))

    def dl(tid, tris):
        verts = [p for t in tris for p in t]
        # every triangle loads its own three vertices (batch slots 0..2)
        vo = put(b''.join(struct.pack('>hhhHhhBBBB', int(v[0]), int(v[1]), int(v[2]), 0, 0, 0,
                                      255, 255, 255, 255) for v in verts))
        g = bytearray()
        g += struct.pack('>II', 0xC0000000, tid)
        for i in range(len(tris)):
            g += struct.pack('>II', 0x04200000 | 0x30, ptr(vo + 16 * 3 * i))
            g += struct.pack('>II', 0xBF000000, (0 << 16) | (10 << 8) | 20)
        g += struct.pack('>II', 0xB8000000, 0)
        go = put(bytes(g))
        return put(struct.pack('>IIIIHbb', ptr(go), 0, 0, ptr(vo), len(verts), 0, 0))
    d_dls = [dl(t, tris) for t, tris in cuffs]
    d_com = dl(common[0], common[1])
    d_sw = [put(struct.pack('>IHH', 0, k, 0)) for k in range(len(cuffs))]

    def no(i):
        return base_nodes + 24 * i
    nodes[head][1] = d_head; nodes[head][5] = ptr(no(wrist))
    nodes[wrist][1] = d_wrist; nodes[wrist][2] = ptr(no(head))
    kids = sw_nodes + [com, hand]
    nodes[wrist][5] = ptr(no(kids[0]))
    for a, b in zip(kids, kids[1:]):
        nodes[a][3] = ptr(no(b))
    for k, (sn, dn) in enumerate(zip(sw_nodes, dl_nodes)):
        nodes[sn][1] = d_sw[k]; nodes[sn][2] = ptr(no(wrist)); nodes[sn][5] = ptr(no(dn))
        nodes[dn][1] = d_dls[k]; nodes[dn][2] = ptr(no(sn))
    nodes[com][1] = d_com; nodes[com][2] = ptr(no(wrist))
    nodes[hand][1] = d_hand; nodes[hand][2] = ptr(no(wrist))
    out = bytearray()
    sw = [0] * nsw
    sw[0] = ptr(no(hand))
    for k, sn in enumerate(sw_nodes):
        sw[4 + k] = ptr(no(sn))
    out += struct.pack('>%dI' % nsw, *sw)
    for t in tex_ids:
        out += struct.pack('>IBBBBBBBB', t, 8 if t < 0x20 else 32, 8, 0, 0, 0, 0, 0, 0)
    for n in nodes:
        out += struct.pack('>HHIIIII', n[0], 0, ptr(n[1]) if n[1] else 0, n[2], n[3], n[4], n[5])
    assert len(out) == data_at
    return bytes(out + blob)


def _tube(x0, x1, r, n=6, rings=8):
    """Triangles of a tube along game x (the forearm), radius r, in rings."""
    import math as m
    ring = [(r * m.cos(2 * m.pi * i / n), r * m.sin(2 * m.pi * i / n)) for i in range(n)]
    tris = []
    for q in range(rings):
        xa = x0 + (x1 - x0) * q / rings
        xb = x0 + (x1 - x0) * (q + 1) / rings
        for i in range(n):
            a, b = ring[i], ring[(i + 1) % n]
            tris.append(((xa, a[0], a[1]), (xb, a[0], a[1]), (xb, b[0], b[1])))
            tris.append(((xa, a[0], a[1]), (xb, b[0], b[1]), (xa, b[0], b[1])))
    return tris


def watchhandtests(tmp):
    """#70 THE WATCH HAND: the format's matrix part and alternative mark, the
    game model reader, the relation, the original-generation identification,
    the sleeve features and the pairing rule. Synthetic throughout."""
    import watchhand as wh

    # the writer: a joint on a model matrix, a draw that is an alternative
    img = bytes([128] * (8 * 8 * 4))
    js = [('SKEL_ROOT', -1, (0.0, 0.0, 0.0), 0), ('SKEL_WRIST', 0, (1.0, 0.0, 0.0), slwa.part_matrix(0))]
    skin = [((1, 255),)] * 4
    p = os.path.join(tmp, 'wh', 'xbla', 'gun', 'arm.slwa')
    slwa.write_skinned(p, slwa.CLASS_GUN, 'arm', quad(4), [0, 1, 2, 1, 3, 2],
                       [(0, slwa.BLEND_OPAQUE)], [(8, 8, img)],
                       [(0, 0, 3, 0), (0, 3, 3, slwa.MESH_ALT | slwa.mesh_switch_flags([4]))], skin, js)
    b = open(p, 'rb').read()
    jo = struct.unpack_from('<I', b, 120)[0]
    ho = struct.unpack_from('<I', b, 108)[0]
    ck(struct.unpack_from('<I', b, jo + 48 + 40)[0] == 0x101
       and struct.unpack_from('<I', b, ho + 16 + 12)[0] == 0x01000000 | (5 << 8),
       'a watch hand writes its joint on model matrix 0 and a sleeve serving Switches[4]')
    for bad_j, bad_m, what in ((0x100, 0, 'a matrix part with no matrix'),
                               (slwa.PART_MATRIX + 65, 0, 'a matrix past the limit'),
                               (0, 1 << 25, 'a mesh bit the format does not define')):
        try:
            slwa.write_skinned(p, slwa.CLASS_GUN, 'arm', quad(4), [0, 1, 2, 1, 3, 2],
                               [(0, slwa.BLEND_OPAQUE)], [(8, 8, img)],
                               [(0, 0, 3, 0), (0, 3, 3, bad_m)], skin,
                               [js[0], ('SKEL_WRIST', 0, (1.0, 0.0, 0.0), bad_j or 0x101)])
            ck(False, '%s is refused' % what)
        except slwa.SlwaError:
            ck(True, '%s is refused' % what)
    try:
        slwa.write_skinned(p, slwa.CLASS_CHAR, 'arm', quad(4), [0, 1, 2, 1, 3, 2],
                           [(0, slwa.BLEND_OPAQUE)], [(8, 8, img)],
                           [(0, 0, 3, 0), (0, 3, 3, slwa.MESH_ALT)], skin, [j[:3] for j in js])
        ck(False, 'a character draw is no alternative sleeve')
    except slwa.SlwaError:
        ck(True, 'a character draw is no alternative sleeve')

    # the game's model: two cuffs along the forearm (game -x), a common hand
    dark_tid, light_tid, hand_tid = 0x10, 0x21, 0x30
    cuffA = _tube(-200, -1000, 50)            # reaches the wrist end
    cuffB = _tube(-600, -1000, 55)            # rolled half way up
    handT = _tube(0, 150, 30)
    data = _arm_model([(dark_tid, cuffA), (light_tid, cuffB)], (hand_tid, handT))
    arm = wh.read_game_arm(data, 10, 3)
    ck(sorted(arm['cuffs']) == [4, 5] and arm['common'] == {hand_tid}
       and sorted(arm['nodes']) == [0, 1] and arm['nodes'][1] == (-100.0, 50.0, 0.0),
       'the model reader finds the cuff switches, the common texture and the positioned nodes')
    ck(len(arm['cuffs'][4][0][1]) == len(cuffA) and arm['cuffs'][4][0][0] == dark_tid,
       "a cuff's triangles come with the texture they are drawn with")
    try:
        wh.read_game_arm(data[:60], 10, 3)
        ck(False, 'a model shorter than its tables is refused')
    except wh.WatchHandError:
        ck(True, 'a model shorter than its tables is refused')

    # the item: scale 0.5 and one quarter turn (item = 0.5 * turn(game, -1))
    rel_scale, wrist_item = 0.5, (10.0, 20.0, 0.0)

    def to_item(pg):
        a = wh.turn((pg[0] * rel_scale, pg[1] * rel_scale, pg[2] * rel_scale), -1)
        return (wrist_item[0] + a[0], wrist_item[1] + a[1], wrist_item[2] + a[2])
    hour_item = to_item((-100.0, 50.0, 0.0))
    joints = [dict(name='SKEL_POSITION__R', bind=(0.0, 0.0, 0.0), parent=-1),
              dict(name='SKEL_LF_WRIST_ER', bind=wrist_item, parent=0),
              dict(name='SKEL_HOUR', bind=hour_item, parent=1)]
    names = ['SKEL_LF_WRIST_ER', 'SKEL_HOUR']
    rel = wh.relate(joints, {1, 2}, names, arm['nodes'])
    ck(abs(rel['scale'] - 0.5) < 1e-6 and rel['turn'] == 1 and rel['pairs'] == {1: 0, 2: 1},
       'the relation is one scale and one quarter turn, joints paired by the game\'s names')
    ck(max(abs(a - b) for a, b in zip(wh.to_item(rel, (-500.0, 10.0, 5.0)),
                                      to_item((-500.0, 10.0, 5.0)))) < 1e-6,
       'a model point lands where the relation puts it in the item')
    try:
        wh.relate(joints, {0, 1, 2}, names, arm['nodes'])
        ck(False, 'a joint carrying vertices that the model does not position is refused')
    except wh.WatchHandError:
        ck(True, 'a joint carrying vertices that the model does not position is refused')
    bent = [dict(j) for j in joints]
    bent[2]['bind'] = (hour_item[0] + 20.0, hour_item[1], hour_item[2])
    bent.append(dict(name='SKEL_MINUTE', bind=(wrist_item[0] + 3.0, wrist_item[1] - 40.0, 0.0), parent=1))
    nodes2 = dict(arm['nodes'])
    nodes2[2] = (-100.0, 50.0, 0.0)
    try:
        wh.relate(bent, {1, 2, 3}, names + ['SKEL_MINUTE'], nodes2)
        ck(False, 'joints no single quarter turn places are refused')
    except wh.WatchHandError:
        ck(True, 'joints no single quarter turn places are refused')

    # two generations: images, triangles in item space
    def tex(rgb, w=8):
        return dict(w=w, h=8, rgba=bytes(list(rgb) + [255]) * (w * 8))

    def item(groups, textures):
        V, draws = [], []
        for img_i, tris in groups:
            ix = []
            for t in tris:
                for pnt in t:
                    ix.append(len(V))
                    V.append(tuple(pnt) + (0.5, 0.5, 255, 255, 255, 255))
            draws.append(dict(image=img_i, indices=ix))
        return dict(vertices=V, draws=draws, textures=textures, joints=joints,
                    skin=[((2, 255),)] + [((1, 255),)] * (len(V) - 1))

    def mapped(tris, grow=1.0):
        out = []
        for t in tris:
            out.append(tuple(to_item((p[0], p[1] * grow, p[2] * grow)) for p in t))
        return out
    dark, light, skinc = (30, 30, 34), (230, 230, 232), (180, 140, 120)
    original = item([(0, mapped(cuffA)), (1, mapped(cuffB)), (2, mapped(handT))],
                    [tex(dark), tex(light, 32), tex(skinc, 16)])
    new = item([(0, mapped(handT, 1.1)), (1, mapped(_tube(-640, -1100, 60))),
                (2, mapped(_tube(-190, -1150, 58)))],
               [tex(skinc), tex(light), tex(dark)])
    ident = wh.identify_original(wh.image_groups(original), {0: 8, 1: 32, 2: 16}, arm, rel)
    ck(ident == {0: dark_tid, 1: light_tid},
       'each original image is the cuff texture it lies on, the common hand none (%r)' % ident)
    ident = wh.identify_original(wh.image_groups(original), {0: 32, 1: 32, 2: 16}, arm, rel)
    ck(0 not in ident, 'an original image of another width is not that texture')

    rel2, mapping, sleeves, lines = wh.derive(new, original, arm, names)
    ck(sleeves == [1, 2], 'the sleeves are the groups beyond the wrist end; the hand is none (%r)'
       % sleeves)
    ck(mapping == {4: 2, 5: 1},
       'each cuff takes the sleeve that agrees on reach and tone (%r)' % mapping)

    # the pairing rule on its own
    F = lambda st, tn, c: dict(start=st, tone=tn, colour=c)       # noqa: E731
    cuffs = {4: F(0.0, 0.0, (25, 27, 35)), 5: None, 7: F(0.0, 0.6, (20, 20, 40)),
             8: F(0.4, 0.0, (70, 70, 20)), 9: F(0.0, 0.8, (170, 178, 186))}
    sleeves = {4: F(0.0, 0.7, (40, 44, 50)), 5: F(0.3, 0.0, (28, 34, 23)),
               6: F(0.0, 0.3, (158, 158, 161)), 7: F(0.0, 0.0, (31, 33, 35)),
               8: F(0.0, 0.4, (37, 42, 46))}
    m, why = wh.pair(cuffs, sleeves)
    ck(m.get(4) == 7 and m.get(8) == 5 and m.get(9) == 6,
       'the dark full cuff, the rolled one and the light one each take their only or '
       'clearly nearest sleeve (%r)' % m)
    ck(5 not in m and 'undetermined' in why[5], 'an undetermined cuff stays unmapped')
    ck(7 not in m, 'a cuff with two candidates at no clear margin stays unmapped (%s)' % why[7])
    m, why = wh.pair({4: F(0.0, 0.1, (25, 27, 35))}, {7: F(0.0, 0.0, (31, 33, 35))})
    ck(m == {}, 'an undecided wrist end matches nothing')
    m, why = wh.pair({6: F(0.0, 0.6, (226, 226, 226)), 9: F(0.0, 0.8, (170, 178, 186))},
                     {6: F(0.0, 0.3, (158, 158, 161))})
    ck(m == {9: 6}, 'a sleeve goes to the cuff it is nearest, not to every cuff it is a '
       'candidate of (%r)' % m)


def sheettests(tmp):
    """xblasheet: the contact sheet over a SYNTHETIC two-mesh bundle - one mesh
    lit, one whose vertex colour is black - and the empty-root case. Skipped,
    uncounted, when numpy or Pillow is absent (as on CI)."""
    import xblasheet
    if xblasheet.MISSING:
        print('xblasheet checks skipped: %s' % xblasheet.MISSING)
        return
    root = os.path.join(tmp, 'sheetroot')
    p = os.path.join(root, 'xbla', 'background', 'box.slwa')
    verts = [(0.0, 0.0, 0.0, 0.0, 0.0, 200, 200, 200, 255),
             (100.0, 0.0, 0.0, 1.0, 0.0, 200, 200, 200, 255),
             (0.0, 100.0, 0.0, 0.0, 1.0, 200, 200, 200, 255),
             (0.0, 0.0, 50.0, 0.0, 0.0, 0, 0, 0, 255),
             (100.0, 0.0, 50.0, 1.0, 0.0, 0, 0, 0, 255),
             (0.0, 100.0, 50.0, 0.0, 1.0, 0, 0, 0, 255)]
    img = bytes([255, 255, 255, 255]) * 64                   # 8x8 white
    slwa.write(p, slwa.CLASS_BACKGROUND, 'box', verts, [0, 1, 2, 3, 4, 5],
               [(0, slwa.BLEND_OPAQUE), (0, slwa.BLEND_OPAQUE)], [(8, 8, img)],
               [(0, 0, 3), (1, 3, 3)])
    out = os.path.join(tmp, 'sheetout')
    rc = xblasheet.main(['--root', root, '--out', out, '--items', 'background/box,prop/absent',
                         '--size', '64'])
    ck(rc == 0 and os.path.isfile(os.path.join(out, 'background-box.png'))
       and os.path.isfile(os.path.join(out, 'index.png')),
       'xblasheet: a sheet and the index are written, an absent bundle is passed over')
    import json
    rep = json.load(open(os.path.join(out, 'report.json')))
    ck(len(rep) == 1 and rep[0]['bundle'] == 'background/box' and rep[0]['triangles'] == 2,
       'xblasheet: the report names only the bundle it found and counts its triangles')
    m = rep[0]['meshes']
    ck(len(m) == 2 and m[0]['vertex_colour_black'] == 0.0 and m[1]['vertex_colour_black'] == 1.0,
       'xblasheet: a mesh whose vertex colour is black is reported as such, a lit one is not')
    ck(m[1]['lo'] == [0.0, 0.0, 50.0] and m[1]['hi'] == [100.0, 100.0, 50.0],
       'xblasheet: each mesh carries its own extent')
    rc2 = xblasheet.main(['--root', os.path.join(tmp, 'nowhere'), '--out', out, '--class', 'prop'])
    ck(rc2 == 0, 'xblasheet: no prepared bundles is a message and exit 0, not a failure')


def _model_blob(quads):
    """A synthetic model file: no switches, one texture, one DLCOLLISION node
    whose primary list loads each quad's four vertices (segment 4) and draws
    it as two triangles under its own texture id. quads: [(corners, rgba)]."""
    import struct as st
    base = 0x05000000
    tex = st.pack('>IBB6x', 0x7, 32, 32)
    node_at, rec_at = 12, 40
    vtx_at = 72
    vtx = b''
    for corners, rgba in quads:
        for (x, y, z) in corners:
            vtx += st.pack('>hhhHhh', x, y, z, 0, 0, 0) + bytes(rgba)
    dl_at = vtx_at + len(vtx)
    dl = b''
    for q, (_c, _rgba) in enumerate(quads):
        dl += st.pack('>II', 0xC0000000, 0x10 + q)
        dl += st.pack('>II', (0x04 << 24) | (3 << 20) | (0 << 16) | 64, (0x04 << 24) | (64 * q))
        dl += st.pack('>II', 0xBF000000, (0 << 16) | (10 << 8) | 20)
        dl += st.pack('>II', 0xBF000000, (0 << 16) | (20 << 8) | 30)
    dl += st.pack('>II', 0xB8000000, 0)
    node = st.pack('>HH5I', 24, 0, base + rec_at, 0, 0, 0, 0)
    rec = st.pack('>3I', base + dl_at, 0, base + vtx_at)
    blob = bytearray(vtx_at)
    blob[0:len(tex)] = tex
    blob[node_at:node_at + len(node)] = node
    blob[rec_at:rec_at + len(rec)] = rec
    return bytes(blob) + vtx + dl


def _bar_image(w=64, h=128):
    """A door-like image: grey metal (120), a dark frame column at 47 and a
    dark separator at rows 40-41, and inside them (cols 48.., rows 0..39)
    DIAGONAL bars alternating black and white, 4 texels wide, as a hazard
    stripe is painted (so no row or column of them is all one colour)."""
    px = bytearray()
    for r in range(h):
        for c in range(w):
            if c == 47 or (c >= 47 and r in (40, 41)):
                v = 20
            elif c >= 48 and r < 40:
                v = 255 if ((r + c) // 4) % 2 == 0 else 0
            else:
                v = 120
            px += bytes((v, v, v, 255))
    return bytes(px)


# ---- #90 THE UNTEXTURED DRAW'S MATERIAL COLOUR --------------------------------
#
# A fetch-less program assembled like the coverage programs: oC0 = r2 x c13,
# one lane an instruction, then the same with c12, and one that fetches.
def _mulc(dst, lane, a, ca, creg, cc, export=False):
    """dst.lane = a.ca * c<creg>.cc, `a` a temporary."""
    return (dst | ((1 if export else 0) << 15) | ((1 << lane) << 16),
            (_swz(ca) << 16) | (_swz(cc) << 8),
            (1 << 24) | (a << 16) | (creg << 8) | (1 << 31))


def untexturedtests():
    import prepare
    c13 = _program([], [_mulc(0, k, 2, k, 13, k, export=True) for k in range(4)])
    c12 = _program([], [_mulc(0, k, 2, k, 12, k, export=True) for k in range(4)])
    fetched = _program([_fetch(1, 0x688)],
                       [_mulc(0, k, 2, k, 13, k, export=True) for k in range(4)])
    ck(xblaworld.colour_times_c13_program([bytes(16) + c13 + bytes(64)]),
       'a fetch-less program exporting an interpolator times c13 is found in its pool')
    ck(not xblaworld.colour_times_c13_program([bytes(16) + c12 + bytes(64)]),
       'one multiplying by another register is not that program')
    ck(not xblaworld.colour_times_c13_program([bytes(16) + fetched + bytes(64)]),
       'one that fetches an image is not a fetch-less program')
    ck(xblaworld.untextured_colour([(12, [1.0, 1.0, 1.0, 1.0, 0.5, 0.25, 0.5, 1.0])])
       == (0.5, 0.25, 0.5),
       'the material colour is the draw\'s c13, read from an upload starting at c12')
    ck(xblaworld.untextured_colour([(12, [1.0, 1.0, 1.0, 1.0])]) is None
       and xblaworld.untextured_colour([]) is None,
       'a draw uploading no c13 has no material colour')
    ck(prepare.untextured_texel((0.5, 0.5, 0.5)) == (128, 128, 128)
       and prepare.untextured_texel((1.0, 1.0, 1.0)) is None
       and prepare.untextured_texel(None) is None,
       'a material colour is one texel; white, or none, keeps the white image')


def _st_model_blob(quads, tex_id=7, size=32):
    """A synthetic model file like _model_blob, one texture of `size` texels
    square under id `tex_id`, each quad's vertices carrying their own S,T
    (s10.5 texels). quads: [[(x, y, z, s, t)] * 4]."""
    import struct as st
    base = 0x05000000
    tex = st.pack('>IBB6x', tex_id, size, size)
    node_at, rec_at, vtx_at = 12, 40, 72
    vtx = b''
    for corners in quads:
        for (x, y, z, s, t) in corners:
            vtx += st.pack('>hhhHhh', x, y, z, 0, s, t) + bytes((255, 255, 255, 255))
    dl_at = vtx_at + len(vtx)
    dl = st.pack('>II', 0xC0000000, tex_id)
    for q in range(len(quads)):
        dl += st.pack('>II', (0x04 << 24) | (3 << 20) | (0 << 16) | 64, (0x04 << 24) | (64 * q))
        dl += st.pack('>II', 0xBF000000, (0 << 16) | (10 << 8) | 20)
        dl += st.pack('>II', 0xBF000000, (0 << 16) | (20 << 8) | 30)
    dl += st.pack('>II', 0xB8000000, 0)
    node = st.pack('>HH5I', 24, 0, base + rec_at, 0, 0, 0, 0)
    rec = st.pack('>3I', base + dl_at, 0, base + vtx_at)
    blob = bytearray(vtx_at)
    blob[0:len(tex)] = tex
    blob[node_at:node_at + len(node)] = node
    blob[rec_at:rec_at + len(rec)] = rec
    return bytes(blob) + vtx + dl


def gamerepeatstests():
    """#95 THE GAME'S REPEATS (gameshade.game_repeats): the game's model's
    image repeats per unit area against a remastered item's, read off a
    synthetic model; the correction the whole image's answer takes."""
    import gameshade as gs
    one = 32 * 32                       # one repeat of a 32-texel image, s10.5
    blob = _st_model_blob([[(0, 0, 0, 0, 0), (100, 0, 0, one, 0), (100, 100, 0, one, one),
                            (0, 100, 0, 0, one)]])
    tris = gs.model_triangles(blob, 0, 1)
    ck(len(tris) == 2 and tris[0]['ST'][1] == (one, 0) and tris[0]['scale'] == (1.0, 1.0),
       "#95 the game's triangles carry their S,T and the texture scale in force")
    ck(gs.model_texture_sizes(blob, 0, 1) == {7: (32, 32)},
       "#95 the model's texture table gives each texture id its size")
    game = [(t['P'], tuple((s / 32.0 / 32, u / 32.0 / 32) for s, u in t['ST'])) for t in tris]

    def item(rep):
        vs = [(0.0, 0.0, 0.0, 0.0, 0.0), (100.0, 0.0, 0.0, rep, 0.0), (100.0, 100.0, 0.0, rep, rep),
              (0.0, 100.0, 0.0, 0.0, rep), (0.0, 0.0, 50.0, 0.0, 0.0), (10.0, 0.0, 50.0, 1.0, 0.0),
              (10.0, 10.0, 50.0, 1.0, 1.0)]
        vs = [v + (255, 255, 255, 255) for v in vs]
        return vs, [dict(image=0, indices=[0, 1, 2, 0, 2, 3]), dict(image=0, indices=[4, 5, 6])]
    ident = (lambda p: p)
    vs, dr = item(0.5)
    rows = gs.repeat_rows(game, vs, dr, ident, 100.0)
    ck(len(rows) == 2 and all(abs(v + 1.0) < 1e-9 for v, _w in rows),
       '#95 a face at half the game\'s repeats on each axis reads -1; a triangle on no game '
       'face is not matched (%r)' % rows)
    f, med, share = gs.repeat_correction(rows)
    ck(f == 2.0 and abs(med + 1.0) < 1e-9 and share == 1.0,
       '#95 the whole image\'s answer is read at twice the scale when the game says so')
    vs, dr = item(1.0)
    ck(gs.repeat_correction(gs.repeat_rows(game, vs, dr, ident, 100.0))[0] == 1.0,
       '#95 an item at the game\'s repeats keeps its scale')
    ck(gs.repeat_correction([(1.0, 1.0)])[0] == 0.5 and gs.repeat_correction([])[0] == 1.0,
       '#95 twice the game\'s repeats halves the scale; no matched triangle changes nothing')
    ck(gs.repeat_correction([(-0.79, 0.64), (-0.3, 0.36)])[0] == 1.0,
       '#95 too little of the matched area near a power of two changes nothing '
       '(the filing cabinet: -0.791, 64%)')
    ck(gs.repeat_correction([(-0.6, 1.0)])[0] == 1.0,
       '#95 a median further than %.2f from a power of two changes nothing' % gs.REPEAT_NEAR)


def gameshadetests():
    """#82 D-032: the game's tint carried onto a remastered image whose
    vertex colour the remaster whitened (gameshade.py). Synthetic model,
    item and image; the decomp's own header and file list for the lookups."""
    import gameshade as gs
    import prepare

    # THE GAME'S MODEL: a white quad and a tinted one, read back per triangle
    white = [(0, 0, 0), (100, 0, 0), (100, 200, 0), (0, 200, 0)]
    bars = [(74, 140, 0), (100, 140, 0), (100, 200, 0), (74, 200, 0)]
    blob = _model_blob([(white, (255, 255, 255, 255)), (bars, (0xe4, 0xe4, 0x2d, 0x72))])
    tris = gs.model_triangles(blob, 0, 1)
    ck(len(tris) == 4 and {t['tex'] for t in tris} == {0x10, 0x11}
       and tris[2]['C'][0] == (0xe4, 0xe4, 0x2d, 0x72) and tris[2]['P'][1] == (100.0, 140.0, 0.0),
       'a DLCOLLISION node\'s triangles are read with their texture and vertex colours: %r'
       % [(t['tex'], t['P'], t['C'][0]) for t in tris])
    tinted = gs.tinted(tris)
    ck(len(tinted) == 2 and all(rgb == (0xe4, 0xe4, 0x2d) for _p, rgb in tinted),
       'only the triangles with one non-white colour are tinted, alpha aside')
    try:
        gs.tinted([dict(tex=1, P=bars[:3], C=((1, 2, 3, 255), (1, 2, 4, 255), (1, 2, 3, 255)))])
        ck(False, 'a triangle whose corners disagree is refused as a tint')
    except gs.ShadeError:
        ck(True, 'a triangle whose corners disagree is refused as a tint')
    ck(gs.prop_header('steel_door1') == (0, 4) and gs.rom_rel('STEEL_DOOR1') == 'assets/obseg/prop/Psteel_door1Z.bin',
       'the door\'s header and ROM file are the decomp\'s, matched without regard to case')

    # THE BOX: one scale and offset per axis, refused when the axes disagree
    place = gs.box_relation([(0, 0, 0), (100, 200, 10)], ((-50, 0, -5), (0, 100, 0)))
    ck(place((100, 200, 10)) == (0.0, 100.0, 0.0) and place((50, 100, 5)) == (-25.0, 50.0, -2.5),
       'the model box is related to the item\'s reference box axis by axis')
    try:
        gs.box_relation([(0, 0, 0), (100, 200, 10)], ((0, 0, 0), (100, 100, 10)))
        ck(False, 'a box that disagrees on its scale is refused')
    except gs.ShadeError:
        ck(True, 'a box that disagrees on its scale is refused')

    # THE FOOTPRINT on a synthetic item: one face, z = 0, u = x / 100,
    # v = 1 - y / 200, white vertex colour, one 64 x 128 image
    def item(colour=(255, 255, 255, 255)):
        V = [(float(x), float(y), 0.0, x / 100.0, 1.0 - y / 200.0) + colour for (x, y, _z) in white]
        return V, [dict(image=0, indices=[0, 1, 2, 0, 2, 3])]
    img = _bar_image()
    textures = [dict(w=64, h=128, rgba=img)]
    placed = [(tuple(tuple(float(c) for c in p) for p in P), rgb) for P, rgb in tinted]
    V, D = item()
    rects = gs.footprint_rects(V, D, textures, placed)
    ck(list(rects) == [0] and len(rects[0]) == 1
       and [round(v, 2) for v in rects[0][0][:4]] == [47.36, 0.0, 64.0, 38.4],
       'the tinted quad reads through the item\'s own coordinates as one texel rectangle: %r'
       % rects)
    V2, D2 = item((200, 200, 200, 255))
    ck(gs.footprint_rects(V2, D2, textures, placed) == {},
       'where the remaster kept a colour there is nothing to restore')
    other = [(P, (10, 20, 30)) for P, _rgb in placed[:1]]
    try:
        gs.footprint_rects(V, D, textures, placed + other)
        ck(False, 'one footprint claimed by two tints is refused')
    except gs.ShadeError:
        ck(True, 'one footprint claimed by two tints is refused')

    # THE SNAP AND THE MULTIPLY: the frame column and the separator rows stay
    # as painted, every bar texel is multiplied, and nothing else moves
    out, applied, changed = gs.tint_image(img, 64, 128, rects[0])
    ck(applied == [(48, 0, 64, 40, (0xe4, 0xe4, 0x2d))],
       'the edges move to where the luminance rises into the bars, the border to the border: %r'
       % applied)

    def px(b, r, c):
        return tuple(b[4 * (r * 64 + c):4 * (r * 64 + c) + 4])
    ck(px(out, 0, 50) == (0xe4, 0xe4, 0x2d, 255) and px(out, 5, 50) == (0, 0, 0, 255),
       'a white bar texel becomes the tint and a black one stays black')
    ck(all(px(out, r, c) == px(img, r, c) for r in range(128) for c in range(64)
           if not (c >= 48 and r < 40)),
       'no texel outside the snapped rectangle moves (frame column, separator, metal)')
    nwhite = sum(1 for r in range(40) for c in range(48, 64) if px(img, r, c)[0] == 255)
    ck(changed == nwhite and nwhite > 0, 'exactly the white bar texels change: %d of %d'
       % (changed, nwhite))

    # D-033 THE BACKGROUND'S WHITENED UNTEXTURED SURFACES. The stage's scale
    # is the game's own table's, and the source's `bunker` is Bunker 1.
    lid, rel, sc = gs.stage_bg('silo')
    ck(lid == 20 and rel.endswith('bg_silo_all_p.bin') and abs(sc - 100.0 / 29.069) < 1e-6
       and gs.stage_bg('bunker')[0] == 9,
       'a background item\'s stage, file and scale are the game\'s: %r' % ((lid, rel, sc),))
    lid, rel, sc = gs.stage_bg('temple')
    ck(lid == 32 and rel.endswith('bg_cryp_all_p.bin') and abs(sc - 100.0 / 23.584906) < 1e-6
       and gs._table_stages('temple') == ('egypt',),
       'the source\'s `temple` is Egyptian\'s background through the runtime\'s table, not the '
       'launcher\'s multiplayer Temple: %r' % ((lid, rel, sc),))
    ck(gs.carry((0, 47, 47), 1.218) == (0, 33, 33) and gs.carry((0, 68, 68), 1.218) == (0, 51, 51)
       and gs.carry((255, 0, 128), 1.0) == (255, 0, 128),
       'the curve carries the game\'s shade into the remaster\'s colours')
    ck(gs.colour_curve([(24, 36)] * 10) is None,
       'too few pairs relate no colours')
    # a synthetic stage at scale 2: rows of game quads shaded grey, each
    # drawn by the remaster untextured at 255 (36/255)^1.2; one more quad
    # whitened over a teal game quad, one white over a white game quad, one
    # whitened quad that shares a vertex with a textured draw
    g = lambda v: int(round(255.0 * (v / 255.0) ** 1.2))          # noqa: E731
    game, verts, untex, tex = [], [], [], []

    def quad(x0, shade, hd, into, z=0.0):
        P = [(x0, 0.0, z), (x0 + 10, 0.0, z), (x0 + 10, 10.0, z), (x0, 10.0, z)]
        for t in ((0, 1, 2), (0, 2, 3)):
            game.append((1, tuple(P[k] for k in t), tuple(shade + (255,) for _k in t)))
        base = len(verts)
        verts.extend(tuple(c * 2.0 for c in p) + (0.0, 0.0) + hd + (255,) for p in P)
        into.extend([base, base + 1, base + 2, base, base + 2, base + 3])
        return base
    for k in range(6):
        v = 20 + 15 * k
        quad(20.0 * k, (v, v, v), (g(v),) * 3, untex)
    quad(200.0, (0, 47, 47), (255, 255, 255), untex)
    quad(240.0, (255, 255, 255), (255, 255, 255), untex)
    b = quad(280.0, (0, 47, 47), (255, 255, 255), untex)
    tex.extend([b, b + 1, b + 2])
    pools = [dict(vertices=list(verts))]
    draws = [dict(pool=0, indices=untex, image=None, slots={}),
             dict(pool=0, indices=tex, image=0, slots={0: 0})]
    rep = gs.shade_pools(game, 2.0, pools, draws)
    V = pools[0]['vertices']
    ck(abs(rep['gamma'] - 1.2) < 0.01 and rep['white'] == 6 and len(rep['fixed']) == 2
       and rep['left'] == 2 and len(rep['refused']) == 2,
       'the curve is measured on the surfaces the remaster kept, and only the whitened '
       'surface over a shaded game surface takes a colour: %r'
       % {k: rep[k] for k in ('gamma', 'white', 'left', 'refused')})
    ck(all(V[24 + j][5] == 0 and abs(V[24 + j][6] - g(47)) <= 1 and V[24 + j][6] == V[24 + j][7]
           and V[24 + j][8] == 255 for j in range(4)),
       'the whitened quad takes the game\'s teal through the curve: %r' % [V[24 + j][5:9] for j in range(4)])
    ck(V[28][5:9] == (255, 255, 255, 255) and V[32][5:9] == (255, 255, 255, 255)
       and all(V[j] == verts[j] for j in range(24)),
       'a white game surface, a vertex a textured draw shares and every kept colour stay as '
       'authored')

    # D-035 THE GAME'S WATERLINE. Game quads in the plane z = 0 at scale 2: an
    # opaque (primary) hull side from y 0 up to 10 and a translucent fade
    # under it (black at its top, white at its foot, down to -10), so the
    # waterline is item y 0. The remaster draws a quad above it, one wholly
    # below it, one across it, and a cutout and a blended quad below it.
    ck(gs.is_fade(((0, 0, 0, 255), (0, 0, 0, 255), (255, 255, 255, 255)))
       and not gs.is_fade(((40, 40, 40, 255), (40, 40, 40, 255), (255, 255, 255, 255)))
       and not gs.is_fade(((129, 129, 129, 255),) * 3),
       'a fade runs the shade from black to white across its corners')
    game2, verts2, by = [], [], {}
    BLACK, WHITE_C = (0, 0, 0, 255), (255, 255, 255, 255)

    def gquad(y0, kind, blend, top, foot):
        P = [(0.0, y0 + 10, 0.0), (10.0, y0 + 10, 0.0), (10.0, y0, 0.0), (0.0, y0, 0.0)]
        C = [top, top, foot, foot]
        for t in ((0, 1, 2), (0, 2, 3)):
            game2.append((1, kind, blend, tuple(P[k] for k in t), tuple(C[k] for k in t)))

    def rquad(name, y0, top, foot):
        P = [(0.0, y0 + 10, 0.0), (10.0, y0 + 10, 0.0), (10.0, y0, 0.0), (0.0, y0, 0.0)]
        C = [top, top, foot, foot]
        base = len(verts2)
        verts2.extend(tuple(c * 2.0 for c in p) + (float(k & 1), float(k >> 1)) + c + (255,)
                      for k, (p, c) in enumerate(zip(P, C)))
        by[name] = [base, base + 1, base + 2, base, base + 2, base + 3]
    gquad(0.0, 'primary', False, (129, 129, 129, 255), (129, 129, 129, 255))
    gquad(-10.0, 'secondary', True, BLACK, WHITE_C)
    ramp = ((152, 152, 152), (255, 255, 255))
    rquad('above', 0.0, *ramp)
    rquad('below', -10.0, *ramp)
    rquad('across', -5.0, *ramp)
    rquad('cutout_below', -10.0, *ramp)
    rquad('blended_below', -10.0, *ramp)
    ck(gs.waterline(game2, 2.0) == 0.0
       and gs.waterline([g for g in game2 if g[1] == 'primary'], 2.0) is None,
       'the waterline is the lowest opaque game corner, and only where the game fades')
    pools2 = [dict(vertices=list(verts2), aux=None, frame=None)]
    textures2 = [dict(has_alpha=False), dict(has_alpha=True)]
    names = ('above', 'below', 'across')
    draws2 = [dict(pool=0, indices=list(by[nm]), image=0, style=0) for nm in names]
    draws2.append(dict(pool=0, indices=list(by['cutout_below']), image=1, style=0))
    draws2.append(dict(pool=0, indices=list(by['blended_below']), image=0, style=2))
    wrep = gs.withhold_pools(game2, 2.0, draws2, pools2, textures2)
    V2 = pools2[0]['vertices']
    cut = draws2[2]['indices']
    ck([len(d['indices']) for d in draws2] == [6, 0, 9, 6, 6]
       and len(wrep['withheld']) == 2 and len(wrep['clipped']) == 2 and wrep['draws'] == 2,
       'above stays, below is withheld, across is cut, a cutout and a blended draw stay: %r'
       % [len(d['indices']) for d in draws2])
    ck(min(V2[i][1] for i in cut) == 0.0 and max(V2[i][1] for i in cut) == 10.0
       and wrep['added'] == 3 and len(V2) == len(verts2) + 3,
       'the cut quad ends exactly on the waterline, on three new corners (the one on its '
       'diagonal shared by both halves): %r' % sorted(set(V2[i][1] for i in cut)))
    ck(all(V2[j][5:8] == (204, 204, 204) and V2[j][3] in (0.0, 0.5) and V2[j][4] == 0.5
           for j in range(len(verts2), len(V2))),
       'a new corner carries the colour and coordinate halfway down its edge: %r'
       % V2[len(verts2):])
    ck(V2[:len(verts2)] == verts2, 'no authored vertex moves')

    # D-036 A DECAL READS THE WAY THE GAME PAINTS IT. Five game decal quads
    # (ZMODE_DEC) in the plane z = 0 at scale 2, one texture, +s along +x and
    # +t along +y. The remaster draws one decal draw over them: three
    # placements agree with the game, one is turned half round, one is
    # mirrored (left alone: a mirror can be the remaster's own correction);
    # a corner of the turned placement is also used by another draw.
    game3 = []
    for k in range(5):
        x0 = 20.0 * k
        P = [(x0, 0.0, 0.0), (x0 + 10, 0.0, 0.0), (x0 + 10, 10.0, 0.0), (x0, 10.0, 0.0)]
        ST = [(0, 0), (2048, 0), (2048, 1024), (0, 1024)]
        for t in ((0, 1, 2), (0, 2, 3)):
            game3.append((1, 'secondary', 0x0c00 | 0x10, 0x1e0, tuple(P[j] for j in t),
                          ((255, 255, 255, 255),) * 3, tuple(ST[j] for j in t)))
    verts3, ind3 = [], []
    UV_OK = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    UV_TURNED = [(1.0, 1.0), (0.0, 1.0), (0.0, 0.0), (1.0, 0.0)]
    UV_MIRROR = [(1.0, 0.0), (0.0, 0.0), (0.0, 1.0), (1.0, 1.0)]
    for k, uvs in enumerate((UV_OK, UV_OK, UV_TURNED, UV_MIRROR, UV_OK)):
        x0 = 20.0 * k
        P = [(x0, 0.0, 0.0), (x0 + 10, 0.0, 0.0), (x0 + 10, 10.0, 0.0), (x0, 10.0, 0.0)]
        base = len(verts3)
        verts3.extend(tuple(c * 2.0 for c in p) + uv + (200, 200, 0, 255) for p, uv in zip(P, uvs))
        ind3.extend([base, base + 1, base + 2, base, base + 2, base + 3])
    pools3 = [dict(vertices=list(verts3))]
    draws3 = [dict(pool=0, indices=list(ind3), image=0, style=0x2702),
              dict(pool=0, indices=[8, 9, 0], image=1, style=0x0701)]
    orep = gs.orient_pools(game3, 2.0, draws3, pools3)
    V3 = pools3[0]['vertices']
    turned = [V3[i][3:5] for i in draws3[0]['indices'][12:18]]
    ck(len(orep['images']) == 1 and orep['images'][0]['placements'] == 5
       and len(orep['images'][0]['turned']) == 1,
       'one image over one game texture, five placements, one turned: %r' % orep['images'])
    ck(turned == [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 0.0), (1.0, 1.0), (0.0, 1.0)],
       'the turned placement reads as the others do: %r' % turned)
    ck([V3[i][3:5] for i in draws3[0]['indices'][18:24]]
       == [UV_MIRROR[j] for j in (0, 1, 2, 0, 2, 3)],
       'a mirrored placement is left alone')
    ck(V3[8] == verts3[8] and V3[9] == verts3[9] and len(V3) == len(verts3) + 2
       and draws3[1]['indices'] == [8, 9, 0],
       'the corners another draw also uses are copied, not moved')

    # D-037 THE GAME'S DECAL ALPHA. Three game decal quads (ZMODE_DEC) in the
    # plane z = 0 at scale 2: blended under environment alpha 102, blended
    # under 255, and opaque (no FORCE_BL) under 102. The remaster lays one
    # translucent decal draw (style low byte 2) over all three; an opaque
    # decal draw (low byte 1) over the first; a plain draw sharing a corner
    # of the first placement; an opaque decal draw over the first two; an
    # opaque decal stud a fifth of the first quad's width inside it; and an
    # opaque two-triangle placement with one triangle on the first quad and
    # one on the second.
    game4 = []
    for k, (mode, env) in enumerate(((0x0c00 | 0x4000, 102), (0x0c00 | 0x4000, 255),
                                     (0x0c00, 102))):
        x0 = 20.0 * k
        P = [(x0, 0.0, 0.0), (x0 + 10, 0.0, 0.0), (x0 + 10, 10.0, 0.0), (x0, 10.0, 0.0)]
        for t in ((0, 1, 2), (0, 2, 3)):
            game4.append((1, 'secondary', mode, 0x1fa, tuple(P[j] for j in t),
                          ((255, 255, 0, 255),) * 3, ((0, 0),) * 3, env))
    verts4, ind4 = [], []
    for k in range(3):
        x0 = 20.0 * k
        P = [(x0, 0.0, 0.0), (x0 + 10, 0.0, 0.0), (x0 + 10, 10.0, 0.0), (x0, 10.0, 0.0)]
        base = len(verts4)
        verts4.extend(tuple(c * 2.0 for c in p) + (0.0, 0.0, 255, 255, 0, 255) for p in P)
        ind4.extend([base, base + 1, base + 2, base, base + 2, base + 3])
    stud = len(verts4)                               # 4 x 4 item units inside 20 x 20
    verts4.extend((x, y, 0.0, 0.0, 0.0, 255, 255, 0, 255)
                  for x, y in ((8.0, 8.0), (12.0, 8.0), (12.0, 12.0), (8.0, 12.0)))
    span = len(verts4)                               # one triangle on each of quads 1, 2
    verts4.extend((x, y, 0.0, 0.0, 0.0, 255, 255, 0, 255)
                  for x, y in ((2.0, 2.0), (18.0, 2.0), (18.0, 18.0), (50.0, 2.0), (58.0, 18.0)))
    pools4 = [dict(vertices=list(verts4))]
    draws4 = [dict(pool=0, indices=list(ind4), image=0, style=0x2702),
              dict(pool=0, indices=list(ind4[:6]), image=1, style=0x2b01),
              dict(pool=0, indices=[0, 1, 4], image=2, style=0x0702),
              dict(pool=0, indices=list(ind4[:12]), image=3, style=0x2b01),
              dict(pool=0, indices=[stud, stud + 1, stud + 2, stud, stud + 2, stud + 3],
                   image=4, style=0x2b01),
              dict(pool=0, indices=[span, span + 1, span + 2, span + 2, span + 3, span + 4],
                   image=5, style=0x2b01)]
    prep4 = gs.opacity_pools(game4, 2.0, draws4, pools4)
    V4 = pools4[0]['vertices']
    a4 = [V4[i][8] for i in draws4[0]['indices']]
    ck([(d['draw'], d['alphas'], d['opaque'], d['moved']) for d in prep4['draws']]
       == [(0, {102: 2}, False, 0), (1, {102: 2}, True, 0), (3, {102: 2}, True, 2)],
       'the decals over the blended 102 quad take an alpha, translucent or opaque; the stud '
       'and the placement half on the 255 quad do not: %r' % prep4['draws'])
    ck(a4 == [102] * 6 + [255] * 12,
       'the first placement takes the game\'s 102, the others keep 255: %r' % a4)
    ck(draws4[1]['style'] == 0x2b02 and [V4[i][8] for i in draws4[1]['indices']] == [102] * 6,
       'an opaque decal wholly on the blended quad is drawn translucent at 102: %r' % draws4[1])
    ck(len(draws4) == 7 and draws4[3]['style'] == 0x2b01 and draws4[3]['indices'] == ind4[6:12]
       and draws4[4]['style'] == 0x2b02 and draws4[4]['image'] == 3
       and [V4[i][8] for i in draws4[4]['indices']] == [102] * 6,
       'an opaque decal half on it gives that placement to a translucent copy straight after '
       'it and keeps the rest: %r' % draws4[3:5])
    ck(draws4[5]['style'] == 0x2b01 and draws4[5]['indices'] == [stud, stud + 1, stud + 2, stud,
                                                                  stud + 2, stud + 3]
       and draws4[6]['style'] == 0x2b01 and draws4[6]['indices'][:3] == [span, span + 1, span + 2],
       'the stud (not the decal it lies on) and the split placement are left alone')
    ck(V4[:len(verts4)] == verts4 and draws4[2]['indices'] == [0, 1, 4]
       and len(V4) == len(verts4) + 4,
       'no authored corner moves: the four corners are copies, the other draws keep theirs')
    ck(abs(gs.decal_extent_iou([(0, 0, 0), (4, 0, 0), (4, 4, 0)],
                               [(0, 0, 0), (4, 0, 0), (0, 2, 0)]) - 0.5) < 1e-9,
       'the extent test is the intersection over union of the boxes in the plane')

    # THE SWITCH BACK: the preparation's list of divergences prepared as authored
    old = os.environ.pop(prepare.AS_AUTHORED, None)
    try:
        ck(prepare.divergence_applies('D-032', 'steel_door1')
           and not prepare.divergence_applies('D-032', 'desk1'),
           'D-032 is decided for the steel door alone')
        ck(prepare.divergence_applies('D-033', 'silo')
           and not any(prepare.divergence_applies('D-033', s)
                       for s in ('dam', 'facility', 'runway', 'surface', 'bunker')),
           'D-033 is decided for Silo alone - never a frozen stage')
        ck(prepare.divergence_applies('D-035', 'frigate')
           and not any(prepare.divergence_applies('D-035', s)
                       for s in ('dam', 'facility', 'runway', 'surface', 'bunker', 'silo')),
           'D-035 is decided for Frigate alone - never a frozen stage')
        ck(prepare.divergence_applies('D-036', 'frigate')
           and not any(prepare.divergence_applies('D-036', s)
                       for s in ('dam', 'facility', 'runway', 'surface', 'bunker', 'silo')),
           'D-036 is decided for Frigate alone - never a frozen stage')
        ck(prepare.divergence_applies('D-037', 'frigate')
           and not any(prepare.divergence_applies('D-037', s)
                       for s in ('dam', 'facility', 'runway', 'surface', 'bunker', 'silo')),
           'D-037 is decided for Frigate alone - never a frozen stage')
        os.environ[prepare.AS_AUTHORED] = 'd-023, D-032'
        ck(not prepare.divergence_applies('D-032', 'steel_door1')
           and not prepare.divergence_applies('D-023', 'dam')
           and prepare.divergence_applies('D-026', 'dam'),
           'SL_WA_AS_AUTHORED prepares exactly the listed divergences as authored')
    finally:
        os.environ.pop(prepare.AS_AUTHORED, None)
        if old is not None:
            os.environ[prepare.AS_AUTHORED] = old


def campaigntests(tmp):
    """prepare-xbla-assets.ps1 -All's plan (campaign.py): the runtime's tables
    parsed, aliases resolved, shared items prepared once and in order, domes
    the source lacks reported rather than requested."""
    import json
    import campaign                                          # noqa: E402
    wa = ('const char *sl_wa_stage_item_path(void);\n'
          'const char *sl_wa_stage_item_path(void)\n{\n    switch (g_StageNum) {\n'
          '    case LEVELID_DAM:      return "dam";\n'
          '    case LEVELID_SURFACE:  return wa_pack_look() ? (const char *) 0 : "surface";\n'
          '    case LEVELID_SURFACE2: return wa_pack_look() ? (const char *) 0 : "surface";\n'
          '    case LEVELID_BUNKER1:  return wa_pack_look() ? (const char *) 0 : "bunker";\n'
          '    case LEVELID_EGYPT:    return wa_pack_look() ? (const char *) 0 : "temple";\n'
          '    default:              return (const char *) 0;\n    }\n}\n\n'
          'const char *sl_wa_stage_sky_item_path(void)\n{\n'
          '    const char *item = sl_wa_stage_item_path();\n'
          '    if (item == (const char *) 0) return (const char *) 0;\n'
          '    if (g_StageNum == LEVELID_SURFACE2) return "surface2";\n'
          '    if (g_StageNum == LEVELID_EGYPT) return "egyptian";\n    return item;\n}\n')
    pres = ('} s_sky_borrow[] = {\n    { "xbla", "bunker", "surface", 36u },\n'
            '    { "other", "dam", "x", 1u }\n};\n'
            '} s_sky_lent[] = {\n    { "xbla", "streets", "frigate" },\n    { "xbla", "bunker", "lent" }\n};\n')
    rows = campaign.stage_items(wa)
    ck(rows == [('dam', 'dam'), ('surface', 'surface'), ('surface2', 'surface'), ('bunker1', 'bunker'),
                ('egypt', 'temple')], 'campaign: stage table rows in order, the ternary arms read (%r)' % (rows,))
    ck(campaign.sky_overrides(wa) == {'surface2': 'surface2', 'egypt': 'egyptian'},
       'campaign: the dome overrides read from sl_wa_stage_sky_item_path')
    borrow = campaign.sky_table(pres, 's_sky_borrow')
    lent = campaign.sky_table(pres, 's_sky_lent')
    ck(borrow == {'bunker': 'surface'} and lent == {'streets': 'frigate', 'bunker': 'lent'},
       'campaign: borrow/lent tables, xbla rows only (%r %r)' % (borrow, lent))
    domes = campaign.source_domes(['Bean\\files\\new\\skydome\\dam\\default.bin', 'x/skydome/surface/a.bin',
                                   'Bean\\files\\new\\skydome\\egyptian', 'skydome\\temple\\default.rba',
                                   'Bean\\files\\new\\skydome\\surface2\\default.bin'])
    ck(domes == {'dam', 'surface', 'surface2'},
       'campaign: source domes from either separator, .bin files only (%r)' % (domes,))
    p = campaign.plan(rows, campaign.sky_overrides(wa), borrow, lent, domes)
    ck(p['stages'] == ['dam', 'surface', 'surface2', 'bunker1', 'egypt']
       and p['backgrounds'] == ['dam', 'surface', 'bunker', 'temple']
       and p['skydomes'] == ['dam', 'surface', 'surface2'] and p['absent_skydomes'] == ['egyptian'],
       'campaign: a shared background once, borrow before lent before override, absent domes '
       'reported (%r)' % (p,))
    for bad, why in (([('dam', 'dam'), ('dam', 'dam')], 'a stage named twice'),
                     ([('nosuch', 'x')], 'a stage the launcher does not know')):
        try:
            campaign.plan(bad, {}, {}, {}, set(), known_stages={'dam'})
            ck(False, 'campaign: %s is refused' % why)
        except campaign.PlanError:
            ck(True, 'campaign: %s is refused' % why)
    try:
        campaign.stage_items('no function here')
        ck(False, 'campaign: a tree without the stage table is refused')
    except campaign.PlanError:
        ck(True, 'campaign: a tree without the stage table is refused')
    # the tree's own tables, against the eleven domes the source listing holds
    eleven = ['Bean\\files\\new\\skydome\\%s\\default.bin' % d for d in
              ('bunker', 'bunker2', 'caverns', 'complex', 'dam', 'egyptian', 'frigate', 'runway', 'surface',
               'surface2', 'temple')]
    real = campaign.campaign_plan(eleven)
    ck(len(real['stages']) == 20 and real['stages'][0] == 'dam' and 'temple' not in real['stages']
       and {'bunker1', 'statue', 'caverns', 'egypt', 'surface2'} <= set(real['stages']),
       'campaign: the tree plans the 20 campaign stages, not the multiplayer temple (%r)' % (real['stages'],))
    ck(len(real['backgrounds']) == 19 and 'surface2' not in real['backgrounds']
       and {'bunker', 'statuepark', 'cavern', 'temple'} <= set(real['backgrounds']),
       'campaign: 19 backgrounds, the source spellings, Surface 2 sharing Surface\'s (%r)'
       % (real['backgrounds'],))
    ck(real['skydomes'] == ['dam', 'runway', 'surface', 'surface2', 'bunker2', 'frigate', 'egyptian']
       and 'caverns' not in real['skydomes'] and 'bunker' not in real['skydomes'],
       'campaign: the 7 domes the stages draw and the source holds - Bunker 1 borrows Surface\'s, '
       'Streets/Depot/Cradle are lent Frigate\'s, Caverns asks for `cavern` (%r)' % (real['skydomes'],))
    idx = os.path.join(tmp, 'campaign-index.txt')
    with open(idx, 'w', encoding='utf-8') as f:
        f.write('\n'.join(eleven) + '\n')
    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = campaign.main(['plan', '--index', idx])
    ck(rc == 0 and json.loads(buf.getvalue())['skydomes'] == real['skydomes'],
       'campaign: the CLI prints the plan as JSON')
    with contextlib.redirect_stderr(io.StringIO()):
        ck(campaign.main(['plan', '--index', os.path.join(tmp, 'absent.txt')]) == 1,
           'campaign: an unreadable index fails the plan')


def main():
    tmp = tempfile.mkdtemp(prefix='sl_wa_tools_')
    out = os.path.join(tmp, 'root')
    p = os.path.join(out, 'xbla', 'skydome', 'dam.slwa')

    # ---- the writer ------------------------------------------------------
    img = bytes(range(256)) * 16                              # 64x16 RGBA8
    n = slwa.write(p, slwa.CLASS_SKYDOME, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
                   [(0, slwa.BLEND_OPAQUE)], [(64, 16, img)], [(0, 0, 6)])
    ck(os.path.isfile(p), 'the writer creates the bundle')
    ck(os.path.getsize(p) == n, 'the declared size is the file it wrote')
    h = read_back(p)
    ck(h['version'] == slwa.VERSION_STATIC and h['cls'] == slwa.CLASS_SKYDOME,
       'the header names the version and class')
    ck(h['path'] == 'dam', 'the header carries the item path')
    ck((h['nv'], h['ni'], h['nm'], h['nt'], h['nmesh']) == (4, 6, 1, 1, 1),
       'the header counts what was written')
    ck(h['size'] == len(h['blob']), 'the declared size is the real length')
    for name, off in (('vertices', h['v_off']), ('indices', h['i_off']),
                      ('materials', h['m_off']), ('textures', h['t_off']),
                      ('meshes', h['h_off'])):
        ck(off % 4 == 0 and off >= slwa.HEADER,
           'the %s section is aligned and past the header' % name)

    # determinism: the same input twice is the same bytes
    q = os.path.join(out, 'xbla', 'skydome', 'again.slwa')
    slwa.write(q, slwa.CLASS_SKYDOME, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
               [(0, slwa.BLEND_OPAQUE)], [(64, 16, img)], [(0, 0, 6)])
    ck(open(p, 'rb').read() == open(q, 'rb').read(),
       'the same input produces byte-identical output')

    # rerunning over an existing file replaces it completely
    slwa.write(p, slwa.CLASS_SKYDOME, 'dam', quad(8), list(range(6)),
               [(0, slwa.BLEND_ALPHA)], [(64, 16, img)], [(0, 0, 6)])
    ck(read_back(p)['nv'] == 8, 'rerunning replaces rather than appends')
    ck(not os.path.exists(p + '.tmp'), 'no temporary is left behind')

    # ---- MATERIAL ANIMATION (#69): the version-2 layout, and that a bundle
    # with nothing to animate is still the version-1 file it always was.
    rec = dict(mode='sinewaves', coord_scale=1 / 128.0,
               amplitude=(2.0, 1.0, 0.0, 0.0), phase=(0.0,) * 4, speed=(0.25, 0.5, 0.0, 0.0),
               freq_a=(-3.0, 2.0, 0.0, 0.0), freq_b=(-3.0, -1.0, 0.0, 0.0),
               turn_scale=0.98, turn_bias=0.5, turn=6.2831855,
               poly=(1.0, -1 / 6.0, 1 / 120.0, -1 / 5040.0))
    aux4 = [(-899, -965, 0.0, 1.0, 0.0, 1), (10, 20, 0.0, 1.0, 0.0, 1),
            (0, 0, 0.0, 0.0, 0.0, 0), (5, -5, 0.0, -1.0, 0.0, 1)]
    a2 = os.path.join(out, 'xbla', 'background', 'anim.slwa')
    n2 = slwa.write(a2, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
                    [(0, slwa.BLEND_ALPHA, slwa.MAT_FOG)], [(64, 16, img)], [(0, 0, 6)],
                    aux=aux4, anims=[rec])
    raw2 = open(a2, 'rb').read()
    h2 = read_back(a2)
    x_off, n_off, n_cnt = struct.unpack_from('<III', raw2, 116)
    ck(h2['version'] == slwa.VERSION_ANIMATED == 2 and n_cnt == 1 and x_off and n_off,
       'a bundle with a record writes version 2 and names both sections')
    ck(x_off % 4 == 0 and n_off % 4 == 0 and n_off == x_off + 4 * slwa.AUX_SIZE
       and h2['size'] == n2 == len(raw2),
       'the auxiliary section is one entry per vertex and the table follows it, aligned')
    e0 = struct.unpack_from('<hhhhhH', raw2, x_off)
    e3 = struct.unpack_from('<hhhhhH', raw2, x_off + 3 * slwa.AUX_SIZE)
    ck(e0 == (-899, -965, 0, 32767, 0, 1) and e3 == (5, -5, 0, -32767, 0, 1)
       and struct.unpack_from('<H', raw2, x_off + 2 * slwa.AUX_SIZE + 10)[0] == 0,
       'each entry carries the raw pair, the direction at 32767 = 1 and the record index')
    r0 = struct.unpack_from('<IfII', raw2, n_off)
    amp = struct.unpack_from('<4f', raw2, n_off + 16)
    fa = struct.unpack_from('<4f', raw2, n_off + 64)
    fb = struct.unpack_from('<4f', raw2, n_off + 80)
    tt = struct.unpack_from('<ffff', raw2, n_off + 96)
    po = struct.unpack_from('<4f', raw2, n_off + 112)
    ck(r0 == (slwa.ANIM_SINEWAVES, 1 / 128.0, slwa.ANIM_WAVES, 0) and amp[:2] == (2.0, 1.0)
       and fa[:2] == (-3.0, 2.0) and fb[:2] == (-3.0, -1.0) and abs(tt[0] - 0.98) < 1e-6
       and tt[1] == 0.5 and abs(tt[2] - 6.2831855) < 1e-6 and tt[3] == 0.0
       and abs(po[1] + 1 / 6.0) < 1e-7,
       'the record packs mode, scale, lanes, the table, the turn and the polynomial in order')
    tex_off = struct.unpack_from('<I', raw2, h2['t_off'] + 8)[0]
    ck(tex_off >= n_off + slwa.ANIM_SIZE, 'the image payload sits past the table')
    s1 = os.path.join(out, 'xbla', 'background', 'still.slwa')
    slwa.write(s1, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
               [(0, slwa.BLEND_ALPHA, slwa.MAT_FOG)], [(64, 16, img)], [(0, 0, 6)])
    raws = open(s1, 'rb').read()
    ck(struct.unpack_from('<I', raws, 4)[0] == slwa.VERSION_STATIC
       and struct.unpack_from('<III', raws, 116) == (0, 0, 0),
       'the same bundle with nothing to animate is a version-1 file with zero reserved words')
    slwa.write(s1, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
               [(0, slwa.BLEND_ALPHA, slwa.MAT_FOG)], [(64, 16, img)], [(0, 0, 6)],
               aux=None, anims=[])
    ck(open(s1, 'rb').read() == raws, 'an empty record table is the same version-1 file')
    ck(len(raw2) - len(raws) == 4 * slwa.AUX_SIZE + slwa.ANIM_SIZE,
       'the moving bundle is the still one plus exactly its two sections')

    # ---- what the writer refuses ----------------------------------------
    def refuses(what, *a, **k):
        try:
            slwa.write(*a, **k)
        except slwa.SlwaError:
            ck(True, what)
            return
        ck(False, what)

    bad = os.path.join(out, 'bad.slwa')
    refuses('an empty vertex array is refused', bad, slwa.CLASS_SKYDOME, 'dam',
            [], [0, 1, 2], [(0, 0)], [(64, 16, img)], [(0, 0, 3)])
    refuses('an index count that is not a multiple of 3 is refused', bad,
            slwa.CLASS_SKYDOME, 'dam', quad(4), [0, 1], [(0, 0)],
            [(64, 16, img)], [(0, 0, 3)])
    refuses('an index past the vertex array is refused', bad,
            slwa.CLASS_SKYDOME, 'dam', quad(4), [0, 1, 99], [(0, 0)],
            [(64, 16, img)], [(0, 0, 3)])
    refuses('a material naming a texture that is not there is refused', bad,
            slwa.CLASS_SKYDOME, 'dam', quad(4), [0, 1, 2], [(5, 0)],
            [(64, 16, img)], [(0, 0, 3)])
    refuses('an unknown blend mode is refused', bad, slwa.CLASS_SKYDOME, 'dam',
            quad(4), [0, 1, 2], [(0, 7)], [(64, 16, img)], [(0, 0, 3)])
    # CUTOUT is accepted at the SAME version, beside the two that came before
    # it: widening the accepted set can never make an older bundle invalid.
    c = os.path.join(out, 'cutout.slwa')
    slwa.write(c, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
               [(0, slwa.BLEND_CUTOUT)], [(64, 16, img)], [(0, 0, 6)])
    ck(read_back(c)['version'] == slwa.VERSION_STATIC,
       'a CUTOUT material writes at the same version as OPAQUE and ALPHA')
    slwa.write(c, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
               [(0, slwa.BLEND_OPAQUE), (0, slwa.BLEND_ALPHA),
                (0, slwa.BLEND_CUTOUT)], [(64, 16, img)], [(0, 0, 6)])
    ck(read_back(c)['nm'] == 3,
       'all three blend modes are carried together in one bundle')
    # THE DECAL FLAG, and the three things that have to hold for it: it
    # ROUND-TRIPS into the material's flags word, the two-field material form
    # every caller used before it existed still writes a zero there, and a bit
    # the format does not define is refused rather than written.
    d = os.path.join(out, 'decal.slwa')
    slwa.write(d, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
               [(0, slwa.BLEND_ALPHA, slwa.MAT_DECAL),
                (0, slwa.BLEND_ALPHA)], [(64, 16, img)], [(0, 0, 6)])
    h = read_back(d)
    raw = open(d, 'rb').read()
    f0 = struct.unpack_from('<I', raw, h['m_off'] + 8)[0]
    f1 = struct.unpack_from('<I', raw, h['m_off'] + slwa.MATERIAL_SIZE + 8)[0]
    ck(h['version'] == slwa.VERSION_STATIC,
       'a DECAL material writes at the same version as one without it')
    ck(f0 == slwa.MAT_DECAL, 'the decal flag round-trips into the flags word')
    ck(f1 == 0, 'a material written without flags still carries a zero there')
    refuses('a material flag bit the format does not define is refused', bad,
            slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2],
            [(0, slwa.BLEND_ALPHA, slwa.MAT_DECAL | 256)], [(64, 16, img)],
            [(0, 0, 3)])
    # #71 BLUE COVERAGE: round-trips beside the decal flag on an ALPHA
    # material, and is refused on an opaque or cutout one.
    d = os.path.join(out, 'cover.slwa')
    slwa.write(d, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
               [(0, slwa.BLEND_ALPHA, slwa.MAT_DECAL | slwa.MAT_COVERAGE_BLUE)],
               [(64, 16, img)], [(0, 0, 6)])
    raw = open(d, 'rb').read()
    ck(struct.unpack_from('<I', raw, read_back(d)['m_off'] + 8)[0]
       == slwa.MAT_DECAL | slwa.MAT_COVERAGE_BLUE,
       'blue coverage round-trips beside the decal flag')
    for blend in (slwa.BLEND_OPAQUE, slwa.BLEND_CUTOUT):
        refuses('blue coverage on a material that is not ALPHA-blended is refused', bad,
                slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2],
                [(0, blend, slwa.MAT_COVERAGE_BLUE)], [(64, 16, img)], [(0, 0, 3)])
    refuses('a GENERATED material is refused outside the skydome class', bad,
            slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2],
            [(0, slwa.BLEND_ALPHA, slwa.MAT_GENERATED)], [(64, 16, img)],
            [(0, 0, 3)])
    refuses('a material that is neither two nor three fields is refused', bad,
            slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2],
            [(0, slwa.BLEND_ALPHA, 0, 0)], [(64, 16, img)], [(0, 0, 3)])
    refuses('a mesh naming a material that is not there is refused', bad,
            slwa.CLASS_SKYDOME, 'dam', quad(4), [0, 1, 2], [(0, 0)],
            [(64, 16, img)], [(3, 0, 3)])
    refuses('a mesh range past the index array is refused', bad,
            slwa.CLASS_SKYDOME, 'dam', quad(4), [0, 1, 2], [(0, 0)],
            [(64, 16, img)], [(0, 0, 9)])
    refuses('an image payload that is not w*h*4 is refused', bad,
            slwa.CLASS_SKYDOME, 'dam', quad(4), [0, 1, 2], [(0, 0)],
            [(64, 16, b'\0' * 7)], [(0, 0, 3)])
    refuses('an oversized image is refused', bad, slwa.CLASS_SKYDOME, 'dam',
            quad(4), [0, 1, 2], [(0, 0)], [(9999, 1, b'\0' * (9999 * 4))],
            [(0, 0, 3)])
    refuses('an item path with a separator is refused', bad,
            slwa.CLASS_SKYDOME, 'a/b', quad(4), [0, 1, 2], [(0, 0)],
            [(64, 16, img)], [(0, 0, 3)])
    refuses('an item path with a capital is refused', bad,
            slwa.CLASS_SKYDOME, 'Dam', quad(4), [0, 1, 2], [(0, 0)],
            [(64, 16, img)], [(0, 0, 3)])
    refuses('an empty item path is refused', bad, slwa.CLASS_SKYDOME, '',
            quad(4), [0, 1, 2], [(0, 0)], [(64, 16, img)], [(0, 0, 3)])
    refuses('an unknown asset class is refused', bad, 99, 'dam',
            quad(4), [0, 1, 2], [(0, 0)], [(64, 16, img)], [(0, 0, 3)])

    # ---- the texture coordinate: FIXED POINT, 14 fractional bits --------
    #
    # THE REGRESSION THIS PINS. These bytes were first read as IEEE binary16,
    # which is why the first attempt at this asset drew radial streaks and a
    # moire contour fan instead of a sky: the dome's U reached 426.0 where the
    # model carries 1.479, so one texture axis was minified two orders of
    # magnitude past anything a filter can carry. The two readings AGREE on
    # sign and on smoothness, so only the arithmetic separates them - which is
    # exactly what these checks are.
    ck(xblaworld.TEXCOORD_ONE == 16384, '0x4000 is the coordinate scale')
    ck(xblaworld.texcoord(0x0000) == 0.0, 'raw 0 is zero of texture')
    ck(xblaworld.texcoord(0x4000) == 1.0, 'raw 0x4000 is exactly one texture')
    ck(xblaworld.texcoord(0x2000) == 0.5, 'raw 0x2000 is half a texture')
    ck(xblaworld.texcoord(0x5ea8) == 24232 / 16384.0,
       'a coordinate past 1.0 stays just past 1.0 - it is a wrap, not a flyaway')
    # the half-float reading of the same word, named so the two cannot be
    # confused again: 0x5ea8 as binary16 is 426.0, which is the defect.
    ck(abs(xblaworld.texcoord(0x5ea8) - 1.4790) < 1e-3,
       'the dome U that read as 426.0 reads as 1.479')
    ck(xblaworld.texcoord(0xffff) < 4.0,
       'no 16-bit value can reach a coordinate a sampler cannot carry')

    # ---- the mesh splitter, on a blob laid out by hand -------------------
    def vtx(x, y, z, u=0x4000, v=0x4000, col=0xffffffff):
        return struct.pack('>fff', x, y, z) + struct.pack('>III', 0, (u << 16) | v, col)

    blob = b''.join(vtx(100.0 + i, 200.0, 300.0) for i in range(20))
    blob += struct.pack('>%dH' % 6, 0, 1, 2, 1, 3, 2)
    blob += b'\xc8\x07\x80\x01' * 8                     # trailing non-geometry
    got = xblaworld.split_meshes(blob)
    ck(len(got) == 1, 'one vertex run and its indices read as one mesh')
    if got:
        ck(len(got[0][0]) == 20, 'every vertex in the run is read')
        ck(got[0][1] == [0, 1, 2, 1, 3, 2], 'the indices read big-endian')
        ck(abs(got[0][0][0][0] - 100.0) < 1e-4, 'the position reads big-endian')
        ck(got[0][0][0][3] == 1.0 and got[0][0][0][4] == 1.0,
           'the texture coordinate reads as two fixed-point 16-bit values')
        ck(got[0][0][0][5:] == (255, 255, 255, 255), 'the vertex colour reads RGBA8')

    two = b''.join(vtx(100.0 + i, 200.0, 300.0) for i in range(20))
    two += struct.pack('>%dH' % 3, 0, 1, 2)
    two += b''.join(vtx(400.0 + i, 500.0, 600.0) for i in range(24))
    two += struct.pack('>%dH' % 6, 0, 1, 2, 3, 4, 5)
    got = xblaworld.split_meshes(two)
    ck(len(got) == 2, 'two runs read as two meshes')
    if len(got) == 2:
        ck(len(got[0][0]) == 20 and len(got[1][0]) == 24,
           'each run keeps its own vertex count')
        ck(max(got[1][1]) < len(got[1][0]),
           'the second run\'s indices address the second run')

    # ---- the BINDING parse: draw records and the buffer table ------------
    #
    # The mesh set and its order are read from the item's own command list and
    # buffer table, not from the order runs happen to lie in the blob. These
    # fixtures are laid out here, so they test the parse and not a bundle.
    stream = (b'\xaa' * 32
              + struct.pack('>IIII', xblaworld.DRAW_OP, 4, 6, 0x1c0)
              + b'\xbb' * 16
              + struct.pack('>IIII', xblaworld.DRAW_OP, 4, 9, 0x220))
    got = xblaworld.draw_records(stream)
    ck(len(got) == 2, 'both draws are found in the command list')
    if len(got) == 2:
        ck(got[0][0] < got[1][0], 'draws come back in command-list order')
        ck((got[0][1], got[0][2]) == (6, 0x1c0)
           and (got[1][1], got[1][2]) == (9, 0x220),
           'each draw carries its index count and its buffer handle')
    ck(xblaworld.draw_records(b'') == [], 'an empty command list has no draw')
    ck(xblaworld.draw_records(
        struct.pack('>IIII', xblaworld.DRAW_OP, 4, 7, 0x1c0)) == [],
       'an index count that is not a multiple of 3 is not a triangle draw')

    #  vb0 [0, 480)  ib0 [480, 492)   vb1 [492, 972)  ib1 [972, 990)
    graph = (struct.pack('>III', 0x190, 0, 480)
             + struct.pack('>IIII', 0x1c0, 480, 12, 1)
             + struct.pack('>III', 0x1f0, 492, 480)
             + struct.pack('>IIII', 0x220, 972, 18, 1))
    ck(xblaworld.buffer_ranges(graph, 0x1c0, 6, 990) == (0, 20, 480, 6),
       'a draw resolves to the vertex and index range its handle names')
    ck(xblaworld.buffer_ranges(graph, 0x220, 9, 990) == (492, 20, 972, 9),
       'the second draw resolves to the second pair of buffers')
    ck(xblaworld.buffer_ranges(graph, 0x999, 6, 990) is None,
       'a handle the table does not carry resolves to nothing')
    ck(xblaworld.buffer_ranges(graph, 0x1c0, 9, 990) is None,
       'a record whose length is not the draw\'s index count is not its buffer')
    ck(xblaworld.buffer_ranges(graph, 0x1c0, 6, 64) is None,
       'a range outside the model blob is refused')

    ck(xblaworld.split_meshes(b'') == [], 'an empty blob yields no mesh')
    ck(xblaworld.split_meshes(b'\xff' * 64) == [],
       'a blob that is not geometry yields no mesh')
    ck(xblaworld.split_meshes(vtx(1.0, 2.0, 3.0) * 4) == [],
       'a run shorter than the minimum is not a mesh')

    # ---- THE LEVEL BUNDLE'S STRUCTURAL COMMAND LIST ---------------------
    #
    # A level bundle breaks both of the one-item reader's assumptions: its
    # draws carry three primitive types, and they read a SHARED vertex pool
    # with many index buffers into it. These fixtures are bytes this file lays
    # out, so they test the rules and not a bundle.
    def rec(op, args, low=0):
        ln = 4 + 4 * len(args)
        head = struct.pack('>I', (ln << 16) | (op << 8) | low)
        if not args:
            return head
        return head + struct.pack('>%dI' % len(args), *args)

    def stream_of(body, pad=b''):
        start = 36
        end = start + len(body)
        hdr = [start, end, 0, 0, 0, 0, 0, 0, 0]
        return struct.pack('>9I', *hdr) + body + pad

    body = (rec(0x15, (0x100, 0x4701))
            + rec(xblaworld.OP_DRAW, (xblaworld.PRIM_TRILIST, 6, 0x220))
            + rec(0x2d, (0x108, 0x00010000, 0x00000005, 0))
            + rec(xblaworld.OP_DRAW, (xblaworld.PRIM_QUADLIST, 8, 0x240))
            + rec(xblaworld.OP_DRAW, (xblaworld.PRIM_TRIFAN, 5, 0x260)))
    recs, end, complete = xblaworld.command_records(stream_of(body, b'\xee' * 32))
    ck(complete, 'the command list walks its record area exactly')
    ck(len(recs) == 5, 'every record in the area is read')
    draws = [r for r in recs if r[1] == xblaworld.OP_DRAW]
    ck(len(draws) == 3, 'all three draws are found, not only the triangle list')
    ck([d[3][0] for d in draws] == [4, 13, 5],
       'each draw carries its own primitive TYPE, which is not a constant')
    ck(xblaworld.draw_records(stream_of(body)) == [(36 + 12, 6, 0x220)],
       'the one-item view still sees only the triangle-list draw')

    # ---- THE PASS GATES (#68) --------------------------------------------
    # The shape of a stage skydome's list: a rest-of-list head gate that is the
    # union of its blocks, an op-0x15 block gated to the opaque pass, then a
    # LAST block gated by op 0x1a to the translucent pass. Offsets are the
    # records' own, so the block's skip target is where the second gate starts.
    head = rec(0x1a, (0x4703,))
    blk1 = rec(0x2d, (0, 0, 0, 0)) + rec(xblaworld.OP_DRAW, (4, 6, 0x220))
    b2 = 36 + len(head) + 12 + len(blk1)       # 12: the op-0x15 record itself
    gate1 = rec(0x15, (b2, 0x4701))
    tail = rec(0x1a, (0x4702,)) + rec(xblaworld.OP_DRAW, (4, 9, 0x240)) + rec(0x1d, ())
    sky = stream_of(head + gate1 + blk1 + tail)
    recs, _e, ok = xblaworld.command_records(sky)
    got = xblaworld.draw_passes(recs)
    d_at = [r[0] for r in recs if r[1] == xblaworld.OP_DRAW]
    ck(ok and len(d_at) == 2, 'the gated fixture walks and holds two draws')
    ck(got.get(d_at[0]) == 0x4701,
       'a draw inside an op-0x15 block is governed by that block, not the head')
    ck(got.get(d_at[1]) == 0x4702,
       'a draw past the block\'s skip target is governed by the op-0x1a gate '
       'after it, not by the block it has left')
    ck(bool(got[d_at[0]] & xblaworld.PASS_OPAQUE)
       and not got[d_at[1]] & xblaworld.PASS_OPAQUE,
       'the low byte says which pass admits each draw')
    nested = (rec(0x15, (36 + 12 + 12 + 16 + 16, 0x6711))
              + rec(0x15, (36 + 12 + 12 + 16, 0x4701))
              + rec(xblaworld.OP_DRAW, (4, 3, 0x200))
              + rec(xblaworld.OP_DRAW, (4, 3, 0x210)))
    recs = xblaworld.command_records(stream_of(nested))[0]
    got = xblaworld.draw_passes(recs)
    d_at = [r[0] for r in recs if r[1] == xblaworld.OP_DRAW]
    ck([got.get(o) for o in d_at] == [0x4701, 0x6711],
       'nested blocks: the innermost containing gate governs, and a draw past '
       'the inner block falls back to the outer one')
    ck(xblaworld.draw_passes(xblaworld.command_records(stream_of(
        rec(xblaworld.OP_DRAW, (4, 3, 0x200))))[0]) == {},
       'a draw no gate governs is absent, not given an invented pass')

    # ---- THE CONDITIONAL DRAW RECORD (#68) --------------------------------
    # A level list's second draw record, op 0x30 = (prim, count, index-buffer
    # handle, condition), beside op 0x17 = (condition, skip target) over a
    # block of op-0x01 draws. See OP_DRAW_GATED for the measurement.
    def walk(parts):
        body2 = b''.join(parts)
        return xblaworld.command_records(stream_of(body2)), body2
    probe = [rec(0x15, (0, 0x4701)),
             rec(0x2d, (0x108, 0x00010000, 0x00000005, 0)),
             rec(xblaworld.OP_DRAW, (xblaworld.PRIM_QUADLIST, 8, 0x240)),
             rec(xblaworld.OP_BLOCK_GATED, (0, 0)),
             rec(xblaworld.OP_DRAW, (xblaworld.PRIM_TRILIST, 6, 0x260)),
             rec(0x2d, (0x110, 0x00010000, 0x00000007, 0)),
             rec(xblaworld.OP_DRAW_GATED, (xblaworld.PRIM_QUADLIST, 4, 0x280, 0)),
             rec(xblaworld.OP_DRAW_GATED, (xblaworld.PRIM_QUADLIST, 4)),
             rec(0x1d, ())]
    (recs, _e, ok), body2 = walk(probe)
    at = [36]
    for p in probe[:-1]:
        at.append(at[-1] + len(p))
    got = xblaworld.level_draw_records(recs)
    ck(ok and len(recs) == len(probe), 'the conditional fixture walks its whole record area')
    ck([(d['prim'], d['count'], d['handle']) for d in got]
       == [(13, 8, 0x240), (4, 6, 0x260), (13, 4, 0x280)],
       'both draw records are read, in list order: op 0x01 twice and op 0x30 once')
    ck([d['at'] for d in got] == [at[2], at[4], at[6]],
       'each draw is reported at its own record, the op-0x30 one included')
    ck(len(got) == 3 and got[2]['image'] == 7 and got[2]['slots'] == {0: 7}
       and got[2]['style'] == 0x4701,
       'an op-0x30 draw takes the material and style standing where it is recorded')
    ck(len(got) > 1 and got[1]['image'] == 5,
       'a draw inside an op-0x17 block is an ordinary draw of the standing material')
    ck(all(d['at'] not in (at[3], at[7]) for d in got),
       'neither the op-0x17 block record nor an op-0x30 record too short to name '
       'its index buffer is taken as a draw')
    (recs0, _e0, ok0), _b0 = walk(probe[:6] + probe[8:])
    key = lambda d: (d['prim'], d['count'], d['handle'], d['image'], d['style'])
    ck(ok0 and [key(d) for d in xblaworld.level_draw_records(recs0)]
       == [key(d) for d in got[:2]],
       'the same list without the op-0x30 records reads the same op-0x01 draws - '
       'the conditional draw is the only addition')
    ck(xblaworld.draw_records(stream_of(body2)) == [(at[4], 6, 0x260)],
       'the one-item view is unchanged: it still sees only the triangle-list op-0x01 draw')

    ck(xblaworld.command_records(b'') == ([], 0, False),
       'an empty stream yields no record and is not complete')
    ck(xblaworld.command_records(b'\xff' * 64)[2] is False,
       'a stream whose header is not an offset is refused, not read past')
    bad = bytearray(stream_of(body))
    struct.pack_into('>I', bad, 36, (2 << 16) | (1 << 8))     # length 2: too short
    ck(xblaworld.command_records(bytes(bad))[2] is False,
       'a record shorter than its own header stops the walk')
    bad = bytearray(stream_of(body))
    struct.pack_into('>I', bad, 36, (6 << 16) | (1 << 8))     # length 6: not a multiple of 4
    ck(xblaworld.command_records(bytes(bad))[2] is False,
       'a record length that is not a multiple of four stops the walk')
    bad = bytearray(stream_of(body))
    struct.pack_into('>I', bad, 36, (0xfff0 << 16) | (1 << 8))
    got = xblaworld.command_records(bytes(bad))
    ck(got[2] is False and got[0] == [],
       'a record that claims to run past the area is refused, and nothing is read')

    # ---- primitive types ------------------------------------------------
    ck(xblaworld.primitive_triangles(4, 6) == 2, 'a triangle list of 6 is 2 triangles')
    ck(xblaworld.primitive_triangles(4, 7) is None, 'a triangle list of 7 is not')
    ck(xblaworld.primitive_triangles(13, 8) == 4, 'a quad list of 8 is 4 triangles')
    ck(xblaworld.primitive_triangles(13, 6) is None, 'a quad list of 6 is not')
    ck(xblaworld.primitive_triangles(5, 5) == 3, 'a triangle fan of 5 is 3 triangles')
    ck(xblaworld.primitive_triangles(5, 2) is None, 'a fan of 2 is not a triangle')
    ck(xblaworld.primitive_triangles(9, 6) is None, 'an unsupported type yields nothing')
    ck(xblaworld.primitive_triangles(4, 0) is None, 'a zero element count yields nothing')
    ck(xblaworld.expand_indices(4, [0, 1, 2, 3, 4, 5]) == [0, 1, 2, 3, 4, 5],
       'a triangle list expands to itself')
    ck(xblaworld.expand_indices(13, [0, 1, 2, 3]) == [0, 1, 2, 0, 2, 3],
       'a quad expands to its two corner triangles')
    # THE EXPANSION THAT FAILED, pinned at the values that separate the two
    # readings. Type 5 is a triangle FAN: element 0 is the hub and appears in
    # every triangle. It was expanded as a ribbon, and because a fan of n and a
    # strip of n both make n - 2 triangles, no count anywhere could see it -
    # only the picture could. Playtest mark 1 is where it showed: a lens of
    # sky through ground the surrounding draws bound on every side.
    ck(xblaworld.expand_indices(5, [0, 1, 2, 3, 4]) == [0, 1, 2, 0, 2, 3, 0, 3, 4],
       'a fan turns about its first element, which is in every triangle')
    ck(xblaworld.expand_indices(5, [7, 1, 2, 3]) == [7, 1, 2, 7, 2, 3],
       'the hub is the element the fan names, not element zero of the pool')
    ck(xblaworld.expand_indices(5, [0, 1, 2, 3, 4]) != [0, 1, 2, 2, 1, 3, 2, 3, 4],
       'the ribbon reading this replaces is refused by name, so it cannot return')
    ck(len(xblaworld.expand_indices(5, list(range(9)))) // 3
       == xblaworld.primitive_triangles(5, 9),
       'the fan expands to exactly the triangle count its own element count declares')
    ck(all(t[0] == 0 for t in
           [xblaworld.expand_indices(5, list(range(9)))[i:i + 3] for i in range(0, 21, 3)]),
       'every triangle of a fan carries the hub')

    # ---- the material record --------------------------------------------
    ck(xblaworld.material_slots((0x108, 0x00010000, 7, 0)) == {0: 7},
       'a one-slot material names its image')
    ck(xblaworld.material_slots((0x178, 0x00010001, 0x1b, 0x00010001, 9, 0)) == {0: 9},
       'the K block is stepped over and the slot still reads')
    ck(xblaworld.material_slots(
        (0x6ac4, 0x00040000, 0x44, 0, 0x45, 0x00010008, 0x46, 0x00020000,
         0x47, 0x00030009)) == {0: 0x44, 1: 0x45, 2: 0x46, 3: 0x47},
       'a four-slot material names an image per sampler slot')
    ck(xblaworld.material_slots((0x108, 0x00040000, 1)) is None,
       'a material whose pairs run past the record is refused')
    ck(xblaworld.material_slots((1,)) is None, 'a record too short to be one is refused')
    ck(xblaworld.material_slots((0x108, 0xffff0000, 1, 2)) is None,
       'a slot count past the limit is refused')

    # ---- the resource tables --------------------------------------------
    #   blob: vertex pool [0, 480)  then index buffers [480, 492) and [492, 512)
    graph = (struct.pack('>IIII', 24, 0, 0, 480)            # (stride, h, off, len)
             + struct.pack('>IIII', 0x220, 480, 12, xblaworld.IB_16)
             + struct.pack('>IIII', 0x240, 492, 20, xblaworld.IB_32))
    want = {0x220: set([(12, xblaworld.IB_16), (24, xblaworld.IB_32)]),
            0x240: set([(10, xblaworld.IB_16), (20, xblaworld.IB_32)]),
            0x999: set([(12, xblaworld.IB_16)])}
    tab = xblaworld.index_buffer_records(graph, 512, want)
    ck(tab.get(0x220) == set([(480, 12, xblaworld.IB_16)]),
       'a 16-bit index buffer resolves by handle and exact length')
    ck(tab.get(0x240) == set([(492, 20, xblaworld.IB_32)]),
       'a 32-bit index buffer resolves too - a pool past 65536 vertices needs one')
    ck(0x999 not in tab, 'a handle the table does not carry resolves to nothing')
    ck(xblaworld.index_buffer_records(graph, 512,
                                      {0x220: set([(99, xblaworld.IB_16)])}) == {},
       'a record whose length is not the draw\'s element count is not its buffer')
    ck(xblaworld.index_buffer_records(graph, 64, want) == {},
       'a range outside the model blob is refused')

    vb, n = xblaworld.vertex_buffer_ending_at(graph, 512, 480)
    ck(vb is not None and vb[1] == 24 and vb[2] == 0 and vb[4] == 20,
       'the SHARED pool is the buffer whose bytes end where the index run begins')
    ck(xblaworld.vertex_buffer_ending_at(graph, 512, 481)[0] is None,
       'nothing is found where no vertex buffer ends')
    ck(xblaworld.vertex_buffer_ending_at(graph, 400, 480)[0] is None,
       'a pool that would leave the blob is refused')
    ck(xblaworld.vertex_buffer_ending_at(
        struct.pack('>IIII', 24, 0, 0, 470), 512, 470)[0] is None,
       'a length that is not a whole number of vertices is not a pool')

    # ---- #80 a trailing per-vertex stream between the pool and its run ----
    sgraph = (struct.pack('>IIII', 20, 0x300, 0, 400)      # 20 vertices of 20
              + struct.pack('>IIII', 4, 0x310, 400, 80)    # 20 words after them
              + struct.pack('>IIII', 0x220, 480, 12, xblaworld.IB_16))
    vb, n, strm = xblaworld.pool_ending_at(sgraph, 512, 480)
    ck(vb is not None and vb[1] == 20 and vb[2] == 0 and vb[4] == 20 and strm == (400, 4),
       'a run beginning where a stride-4 stream ends names the position buffer '
       'ending where that stream begins, and carries the stream')
    plain = xblaworld.pool_ending_at(graph, 512, 480)
    ck(plain[2] is None and plain[0] == xblaworld.vertex_buffer_ending_at(graph, 512, 480)[0],
       'a pool whose own bytes end at the run is named as before, with no stream')
    ck(xblaworld.pool_ending_at(
        struct.pack('>IIII', 20, 0x300, 0, 400) + struct.pack('>IIII', 4, 0x310, 400, 76),
        512, 476)[0] is None,
       'a stream of a different vertex count than the buffer before it names nothing')
    ck(xblaworld.pool_ending_at(
        struct.pack('>IIII', 4, 0x310, 400, 80), 512, 480)[0] is None,
       'a stream with no position buffer ending where it begins names nothing')
    # the virtual record: the stream's word laid before the colour word
    sblob = b''.join(struct.pack('>fff', float(i), 2.0, 3.0) + struct.pack('>I', 0x11111111)
                     + struct.pack('>I', 0xff000000 | i) for i in range(20))
    sblob += b''.join(struct.pack('>hh', i * 128, 256) for i in range(20))
    sp = dict(off=0, stride=20, count=20, stream=(400, 4))
    src, soff, sstride = xblaworld.pool_source(sp, sblob)
    ck(soff == 0 and sstride == 24 and len(src) == 480
       and src[0:16] == sblob[0:16] and src[16:20] == struct.pack('>hh', 0, 256)
       and src[20:24] == sblob[16:20]
       and src[24 * 7 + 16:24 * 7 + 20] == struct.pack('>hh', 7 * 128, 256),
       'a streamed pool reads as one buffer: position, the original words, the stream '
       'word, then the colour word - stride plus stream stride')
    ck(xblaworld.pool_source(dict(off=40, stride=20, count=3, stream=None), sblob) == (sblob, 40, 20),
       'a pool without a stream reads from the blob as before')
    ck(xblaworld.pool_layout_stride(sp) is None
       and xblaworld.pool_layout_stride(dict(stride=36, stream=None)) == 36,
       'a measured layout is asked about only for a pool without a stream')

    # ---- #81 a pool with a normal where a coordinate would be ------------
    def snorm(x, y, z):
        w = 0
        for k, c in enumerate((x, y, z)):
            w |= (int(round(c * 511)) & 0x3ff) << (10 * k)
        return w
    nrm = [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0), (-1.0, 0.0, 0.0),
           (0.0, -1.0, 0.0), (0.0, 0.0, 1.0)]
    nblob = b''.join(struct.pack('>fff', 10.0 * i, 2.0, -3.0) + struct.pack('>I', snorm(*n))
                     + bytes((0xff, 0x40, 0x80, 0xc0)) for i, n in enumerate(nrm))
    lo3, hi3 = [-100.0] * 3, [100.0] * 3
    sv = xblaworld._pool_sphere(nblob, 0, 20, 6, lo3, hi3)
    ck(sv is not None and len(sv) == 6 and sv[0][:3] == (0.0, 2.0, -3.0)
       and abs(sv[0][3] - 1.0) < 1e-3 and abs(sv[0][4] - 0.5) < 1e-3
       and abs(sv[1][3] - 0.5) < 1e-3 and abs(sv[1][4] - 0.0) < 1e-3
       and abs(sv[2][3] - 0.5) < 1e-3 and abs(sv[2][4] - 0.5) < 1e-3
       and sv[0][5:] == xblaworld.vertex_colour(0xff, 0x40, 0x80, 0xc0),
       "a pool whose one word is a unit normal reads with the normal's sphere "
       'coordinate (0.5 + 0.5 n.x, 0.5 - 0.5 n.y) and its own colour')
    short = bytearray(nblob)
    short[20 + 12:20 + 16] = struct.pack('>I', snorm(0.3, 0.3, 0.0))
    ck(xblaworld._pool_sphere(bytes(short), 0, 20, 6, lo3, hi3) is None,
       'one word that is not a unit normal refuses the whole pool')
    ck(xblaworld._pool_sphere(nblob, 0, 20, 6, lo3, [5.0, 100.0, 100.0]) is None,
       "a position outside the item's bounds refuses it, as for any pool")
    ck(xblaworld._pool_sphere(nblob[:72] + bytes(8), 0, 24, 3, lo3, hi3) is None,
       'a record with more than one word between position and colour is not one')
    dome = [(-0.8, 0.6, 0.0), (-0.8, -0.6, 0.0), (-0.8, 0.0, 0.6), (-0.8, 0.0, -0.6), (-1.0, 0.0, 0.0)]
    du = xblaworld.sphere_uvs(dome)
    ck(abs(du[4][0] - 0.5) < 1e-6 and abs(du[4][1] - 0.5) < 1e-6
       and abs(du[0][1] - 0.2) < 1e-6 and abs(du[1][1] - 0.8) < 1e-6
       and abs(du[2][0] - du[3][0]) > 0.5,
       'a pool that faces one way (a dome facing -x) is taken face-on: its middle '
       'samples the image\'s middle, its top the image\'s top')

    # ---- #83 a skinned item's plain record with a normal at +20 ----------
    import xblachar
    def rec28(n):
        return (struct.pack('>3f4H', 1.0, 2.0, 3.0, 0, 0xf000, 0xf000, 0xf000)
                + struct.pack('>I', snorm(*n)) + bytes((0xff, 0x40, 0x40, 0x40)))
    pblob = b''.join(rec28(n) for n in nrm)
    pc = xblachar.plain_sphere_coords(pblob, (0, 28, 6))
    ck(pc is not None and len(pc) == 6 and pc == xblaworld.sphere_uvs(nrm),
       "a plain record whose word at +20 is a unit normal maps by the normal's sphere "
       'coordinate')
    ck(xblachar.plain_sphere_coords(b''.join(rec28((0.0, 1.0, 0.0)) for _ in nrm), (0, 28, 6))
       is None,
       'one normal on every vertex says nothing either way: the vertex colour stays')
    bad = bytearray(pblob)
    bad[28 + 20:28 + 24] = struct.pack('>I', snorm(0.3, 0.3, 0.0))
    ck(xblachar.plain_sphere_coords(bytes(bad), (0, 28, 6)) is None
       and xblachar.plain_sphere_coords(pblob[:4 * 32], (0, 32, 4)) is None,
       'a word that is not a unit normal, or another stride, is not one')

    # ---- #80 the instance matrix ------------------------------------------
    def mrec(rows, t, extra=(0, 0)):
        f = [v for r in rows for v in (r + (0.0,))] + [t[0], t[1], t[2], 1.0]
        return (0x40f0,) + tuple(extra) + struct.unpack('>16I', struct.pack('>16f', *f))
    rot = ((0.6, 0.0, 0.8), (0.0, 1.0, 0.0), (-0.8, 0.0, 0.6))
    f32 = lambda v: struct.unpack('>f', struct.pack('>f', v))[0]
    m = xblaworld.instance_matrix(mrec(rot, (-1083.0, 0.0, 118.6)))
    ck(m is not None and len(m) == 16 and m[12:15] == (-1083.0, 0.0, f32(118.6))
       and m[0] == f32(0.6) and m[2] == f32(0.8),
       'an op-0x21 record reads as sixteen floats: a rotation about y and a translation')
    ck(xblaworld.instance_apply(m, 10.0, 5.0, 0.0)
       == (10.0 * f32(0.6) - 1083.0, 5.0, 10.0 * f32(0.8) + f32(118.6)),
       'a position goes through the matrix as a row vector, the translation in the last row')
    ms = xblaworld.instance_matrix(mrec(((1.0, 0.0, 0.0), (0.0, 1.2113, 0.0), (0.0, 0.0, 1.0)), (0.0, 0.0, -978.9)))
    ck(ms is not None and abs(xblaworld.instance_apply(ms, 0.0, 10.0, 0.0)[1] - 12.113) < 1e-4,
       'a placement scaled along one axis (the taller trees) is taken')
    ck(xblaworld.instance_matrix(mrec(((1.0, 0.5, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (0.0, 0.0, 0.0))) is None,
       'a shear - rows not perpendicular - is not a placement')
    ck(xblaworld.instance_matrix(mrec(rot, (0.0, 0.0, 0.0))[:18]) is None
       and xblaworld.instance_matrix(mrec(((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (0.0, 0.0, 0.0))) is None,
       'a record of the wrong length, or with a zero row, is not a matrix')
    bad = list(mrec(rot, (0.0, 0.0, 0.0)))
    bad[3 + 3] = struct.unpack('>I', struct.pack('>f', 0.5))[0]
    ck(xblaworld.instance_matrix(tuple(bad)) is None,
       'a last column other than (0, 0, 0, 1) is not a placement')
    probe = [rec(0x15, (0, 0x4701)),
             rec(0x2d, (0x108, 0x00010000, 0x00000005, 0)),
             rec(xblaworld.OP_DRAW, (xblaworld.PRIM_QUADLIST, 8, 0x240)),
             rec(0x23, (0,)),
             rec(xblaworld.OP_INSTANCE, mrec(rot, (-1083.0, 0.0, 118.6))),
             rec(xblaworld.OP_DRAW, (xblaworld.PRIM_QUADLIST, 8, 0x240)),
             rec(xblaworld.OP_INSTANCE_END, (0,) * 19),
             rec(xblaworld.OP_DRAW, (xblaworld.PRIM_QUADLIST, 8, 0x240)),
             rec(0x1d, ())]
    (recs, _e, ok), _b = walk(probe)
    got = xblaworld.level_draw_records(recs)
    ck(ok and len(got) == 3 and got[0]['xform'] is None and got[1]['xform'] == m
       and got[2]['xform'] is None,
       'a draw after an op-0x21 record carries its matrix; the draws before it and '
       'after the op-0x25 record carry none')
    import importlib.util
    spec = importlib.util.spec_from_file_location('wa_prepare_i', os.path.join(HERE, 'prepare.py'))
    prepare_i = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare_i)
    ck(prepare_i.instance_vertices([(10.0, 5.0, 0.0, 0.5, 0.25, 1, 2, 3, 4)], m)
       == [(10.0 * f32(0.6) - 1083.0, 5.0, 10.0 * f32(0.8) + f32(118.6), 0.5, 0.25, 1, 2, 3, 4)],
       'instance_vertices carries the positions through the matrix and keeps the rest')

    # ---- the vertex layout, and what it refuses --------------------------
    #
    # A pool whose word at +12 is CONSTANT over each face (a packed normal) and
    # whose word at +16 runs linearly with position (the coordinate). The
    # measurement has to pick the second, which is the reading the first
    # attempt at a level bundle got backwards.
    def lvtx(x, z, u, v, nrm, col=0xff804020):
        return (struct.pack('>fff', x, 0.0, z) + struct.pack('>I', nrm)
                + struct.pack('>hh', u, v) + struct.pack('>I', col))

    pool = b''.join(lvtx(float(i % 2) * 64.0, float(i // 2) * 64.0,
                         (i % 2) * 128, ((i // 2) % 2) * 128, 0x0007fc00)
                    for i in range(8))
    tris = [(0, 1, 3), (0, 3, 2), (2, 3, 5), (2, 5, 4)]
    got = xblaworld._uv_format(pool, 0, 24, 8, tris)
    ck(got is not None, 'the coordinate word is identified')
    if got:
        ck(got[0] == 16, 'the word that runs with position is the coordinate, not the normal')
        ck(got[1] == 128.0, 'and its scale is read from where its values pile up')
        ck(abs(got[2] - 0.5) < 1e-6, 'and its edge ratio is the one laid out here')
    # the scale is where the values pile up MOST above chance, not the largest
    # divisor that 1% of them happen to land on: a small pool whose unit is 128
    # with a few corners at 512
    small = b''.join(lvtx(float(i % 2) * 64.0, float(i // 2) * 64.0,
                          (512 if i == 0 else (i % 2) * 128),
                          ((i // 2) % 2) * 128, 0x0007fc00)
                     for i in range(40))
    stris = [(k, k + 1, k + 3) for k in range(0, 36, 2)]
    got2 = xblaworld._uv_format(small, 0, 24, 40, stris)
    ck(got2 is not None and got2[1] == 128.0,
       'a pool whose values pile up at 128 reads at 128 though some land on 512: %r' % (got2,))
    # THE ITEM'S SCALE: a pool of a few vertices whose values pile up on no
    # divisor is refused on its own, and read at the scale the item's other
    # pools agree on - its coordinate word still chosen by the edge test
    odd = b''.join(lvtx(float(i % 2) * 64.0, float(i // 2) * 64.0,
                        (i % 2) * 93 + 37, ((i // 2) % 2) * 93 + 21, 0x0007fc00)
                   for i in range(8))
    ck(xblaworld._uv_format(odd, 0, 24, 8, tris) is None,
       'a small pool whose values pile up nowhere measures no scale of its own')
    got3 = xblaworld._uv_format(odd, 0, 24, 8, tris, scale=256.0)
    ck(got3 is not None and got3[0] == 16 and got3[1] == 256.0,
       'given the item\'s scale it reads its coordinate word at it: %r' % (got3,))
    # #81 A WORD THAT IS A UNIT NORMAL ON EVERY VERTEX is not the coordinate,
    # even where the edge test would rank it first: normals that change
    # within a face (+y and +x here, 1535 raw units apart) against a
    # coordinate stepping 4096 per 64-unit edge
    smooth = b''.join(lvtx(float(i % 2) * 64.0, float(i // 2) * 64.0,
                           (i % 2) * 4096, (i // 2) * 4096,
                           0x0007fc00 if i % 2 else 0x000001ff)
                      for i in range(8))
    got4 = xblaworld._uv_format(smooth, 0, 24, 8, tris)
    ck(got4 is not None and got4[0] == 16,
       'a word that is a unit normal on every vertex is passed over for the coordinate: %r'
       % (got4,))
    # #82 THE UNIT ITSELF: quads cornered at 0 and 512 on one axis and at 6144
    # (twelve images out) on the other. 2048 and 1024 hold the 6144s and win
    # the enrichment score, but no value lies at either; 512 has values at
    # itself and is the unit.
    far = b''.join(lvtx(float(i % 2) * 64.0, float(i // 2) * 64.0,
                        (i % 2) * 512, ((i // 2) % 2) * 6144, 0x0007fc00)
                   for i in range(8))
    got5 = xblaworld._uv_format(far, 0, 24, 8, tris)
    ck(got5 is not None and got5[0] == 16 and got5[1] == 512.0,
       'a divisor no value lies at yields to one that has values at itself: %r' % (got5,))
    # #93 THE UNIT'S EDGE: 100 values - two at 1024, thirteen at 128, twenty
    # at 127 (a chart drawn to one unit short of 128) and odd fill. 1024 wins
    # the enrichment score narrowly (2% x 1024 against 15% x 128); 128 holds
    # 33 values at its chart edge against 1024's 2, so 128 is the unit.
    def edgepool(values):
        vals = list(values) + [37 + 2 * k for k in range(100 - len(values))]
        return b''.join(lvtx(float(i % 2) * 64.0, float(i // 2) * 64.0,
                             vals[2 * i], vals[2 * i + 1], 0x0007fc00)
                        for i in range(50))
    etris = [(k, k + 1, k + 3) for k in range(0, 46, 2)]
    got6 = xblaworld._uv_format(edgepool([1024] * 2 + [128] * 13 + [127] * 20), 0, 24, 50, etris)
    ck(got6 is not None and got6[0] == 16 and got6[1] == 128.0,
       'a near-tied smaller unit with four times the chart-edge values is the unit: %r' % (got6,))
    # ... not when its 128-grid values are corners several images out (384)
    # rather than chart edges: one value at 128, none at 127
    got7 = xblaworld._uv_format(edgepool([1024] * 2 + [384] * 13 + [128]), 0, 24, 50, etris)
    ck(got7 is not None and got7[1] == 1024.0,
       'a smaller unit with no more chart-edge values does not displace the pick: %r' % (got7,))
    # ... and not when the enrichment is not near: six values at 128
    got8 = xblaworld._uv_format(edgepool([1024] * 2 + [128] * 6), 0, 24, 50, etris)
    ck(got8 is not None and got8[1] == 1024.0,
       'a smaller unit whose enrichment is not near the pick\'s does not displace it: %r'
       % (got8,))
    # THE CHART SHORT OF ITS UNIT: quads each the whole image, drawn three to
    # five raw units short of 1024 (the temple's palm fronds) - no value on any
    # divisor. Refused unless asked; asked, read at 1024, the evidence the
    # share of values near 0 or the unit.
    def shortpool(pairs):
        return b''.join(lvtx(float(i % 2) * 64.0, float(i // 2) * 64.0, pairs[i % 4][0],
                             pairs[i % 4][1], 0x0007fc00) for i in range(8))
    fronds = shortpool([(1021, -1), (1, -2), (1020, 1020), (0, 1019)])
    ck(xblaworld._uv_format(fronds, 0, 24, 8, tris) is None,
       'a pool whose charts stop short of 1024 measures no scale of its own')
    got9 = xblaworld._uv_format(fronds, 0, 24, 8, tris, short=True)
    ck(got9 is not None and got9[0] == 16 and got9[1] == 1024.0 and got9[3] == 1.0,
       'asked, a pool whose largest value stops within 2%% of 1024 reads at 1024: %r' % (got9,))
    trunks = shortpool([(1010, 13), (450, 92), (2, 10), (924, 91)])
    got10 = xblaworld._uv_format(trunks, 0, 24, 8, tris, short=True)
    ck(got10 is not None and got10[1] == 1024.0,
       'a chart reaching 1010 (1.4%% short) reads at 1024: %r' % (got10,))
    ck(xblaworld._uv_format(shortpool([(990, 13), (450, 92), (2, 10), (924, 91)]), 0, 24, 8, tris,
                            short=True) is None
       and xblaworld._uv_format(shortpool([(1030, 13), (450, 92), (2, 10), (924, 91)]), 0, 24, 8,
                                tris, short=True) is None,
       'a chart stopping 3% short of its divisor, or past the nearest one, is still refused')
    ck(xblaworld._uv_format(edgepool([1024] * 2 + [128] * 6), 0, 24, 50, etris, short=True) == got8,
       'a pool any divisor reads is not asked')
    ck(xblaworld.item_scale([dict(vertices=[1], uv_divisor=256.0), dict(vertices=None,
                                                                         uv_divisor=None),
                             dict(vertices=[1], uv_divisor=256.0)]) == 256.0
       and xblaworld.item_scale([dict(vertices=[1], uv_divisor=256.0),
                                 dict(vertices=[1], uv_divisor=128.0)]) is None
       and xblaworld.item_scale([dict(vertices=None, uv_divisor=None)]) is None,
       'the item\'s scale is the one every validated pool agrees on, else none')

    vs = xblaworld._pool_vertices(pool, 0, 24, 8,
                                  [-1e4] * 3, [1e4] * 3, 16, 128.0)
    ck(vs is not None and len(vs) == 8, 'the pool decodes at the measured layout')
    if vs:
        ck(abs(vs[1][0] - 64.0) < 1e-4, 'the position reads big-endian')
        ck(abs(vs[1][3] - 1.0) < 1e-6,
           'one texture is 128 raw units, so 128 reads as exactly 1.0')
        ck(vs[0][5:] == (0x20, 0x40, 0x80, 0xff),
           'the colour word is byte-reversed: alpha at byte 0, then B, G, R')

    # ---- the vertex colour's component order -----------------------------
    #
    # ONE rule, and the two readers share it. The word is fetched 8IN32 like
    # every other field in this record - which is why the position reads
    # big-endian - so r, g, b, a are bytes 3, 2, 1, 0. Pinned at the two words
    # the game itself adjudicated: the N64-generation model's floor decal and
    # the remastered model's tunnel road line, both of which PRESENTATION
    # ORIGINAL draws YELLOW at the 2026-09-24 playtest mark-001 camera.
    ck(xblaworld.vertex_colour(0xff, 0x00, 0xe1, 0xff) == (255, 225, 0, 255),
       'the N64-generation floor decal\'s word reads YELLOW, not cyan')
    ck(xblaworld.vertex_colour(0xff, 0x00, 0x6b, 0x6b) == (107, 107, 0, 255),
       'the tunnel road line\'s word reads YELLOW, not cyan')
    ck(xblaworld.vertex_colour(0xff, 0xff, 0xff, 0xff) == (255, 255, 255, 255),
       'a white vertex is white either way, which is why the skydome cannot '
       'tell the two readings apart')
    ck(xblaworld.vertex_colour(0x80, 0x11, 0x22, 0x33) == (0x33, 0x22, 0x11, 0x80),
       'alpha stays at byte 0 - the reading the large pools already settled')
    ck(xblaworld._vertex_at(
        struct.pack('>fff', 1.0, 2.0, 3.0) + struct.pack('>I', 0)
        + struct.pack('>HH', 0x4000, 0x4000)
        + bytes((0xff, 0x00, 0x6b, 0x6b)), 0)[5:] == (107, 107, 0, 255),
       'the one-item reader takes the same rule, from the same function')
    ck(xblaworld._pool_vertices(pool, 0, 24, 8, [0.0] * 3, [1.0] * 3, 16, 128.0) is None,
       'a pool whose positions leave the item\'s own bounds is refused')
    ck(xblaworld._pool_vertices(pool, 0, 12, 8, [-1e4] * 3, [1e4] * 3, 4, 128.0) is None,
       'a stride with no room for a coordinate and a colour is refused')
    ck(xblaworld._pool_vertices(pool, 400, 24, 8,
                                [-1e4] * 3, [1e4] * 3, 16, 128.0) is None,
       'a pool that would read past the blob is refused')

    try:
        f = os.path.join(tmp, 'notabundle.bin')
        open(f, 'wb').write(b'NOPE' + b'\0' * 64)
        xblaworld.read_item(f)
        ck(False, 'a file that is not a bundle is refused')
    except xblaworld.WorldAssetError:
        ck(True, 'a file that is not a bundle is refused')

    ck(slwa.CLASS_BACKGROUND == 2 and slwa.CLASS_SKYDOME == 1,
       'the background class joins the skydome rather than replacing it')
    p2 = os.path.join(out, 'xbla', 'background', 'dam.slwa')
    n = slwa.write(p2, slwa.CLASS_BACKGROUND, 'dam', quad(4), [0, 1, 2, 1, 3, 2],
                   [(0, slwa.BLEND_ALPHA)], [(64, 16, img)], [(0, 0, 6)])
    ck(os.path.isfile(p2) and read_back(p2)['cls'] == slwa.CLASS_BACKGROUND,
       'the writer writes a background bundle')
    ck(read_back(p2)['version'] == slwa.VERSION_STATIC,
       'at the same format version - the layout did not change')
    refuses('an asset class that is neither is still refused', bad,
            3, 'dam', quad(4), [0, 1, 2], [(0, 0)], [(64, 16, img)], [(0, 0, 3)])

    envtests(tmp)
    watertests()
    gamewatertests(tmp)
    gamecoordtests()
    zenithtests()
    panoramatests(tmp)
    overlaytests(tmp)
    chartests(tmp)
    stagecharstests(tmp)
    viewmodeltests(tmp)
    proptests(tmp)
    coveragetests()
    layertests()
    twolayertests()
    repeattests()
    bandfadetests()
    shadowtests()
    placeholdertests()
    atlastests()
    propshadetests()
    originalgentests()
    opaquedoortests()
    watchhandtests(tmp)
    sheettests(tmp)
    gameshadetests()
    gamerepeatstests()
    untexturedtests()
    campaigntests(tmp)

    print('%s: %d passed, %d failed'
          % ('WORLDASSET TOOLS SELFTEST FAILED' if _fail else 'worldasset tools selftest',
             _pass, _fail))
    return 1 if _fail else 0


if __name__ == '__main__':
    sys.exit(main())
