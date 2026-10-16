"""Self-test for the texture-pack preparation tooling.

    python tools/texpack/selftest.py

Every fixture here is SYNTHETIC - a generated gradient, a zip built in a
temporary directory, a few bytes that are deliberately not a container. No
texture, no pack, no source and no path to one is read, so this runs on CI
and on a fresh clone exactly as it runs at a desk.

Checks that need a third-party module (numpy for the block decoders, Pillow
for PNG) are SKIPPED, loudly, when it is absent rather than failing: those
modules are a developer prerequisite of the conversion, not of Sightline.
"""
import json
import os
import struct
import sys
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import caff                                                     # noqa: E402
import sltx                                                     # noqa: E402

PASS = FAIL = SKIP = 0


def ck(cond, what):
    global PASS, FAIL
    if cond:
        PASS += 1
    else:
        FAIL += 1
        print('  FAIL  %s' % what)


def skip(what):
    global SKIP
    SKIP += 1
    print('  skip  %s' % what)


def raises(fn, exc, what):
    try:
        fn()
    except exc:
        ck(True, what)
        return
    except Exception as e:                                       # noqa: BLE001
        ck(False, '%s (raised %r instead)' % (what, e))
        return
    ck(False, '%s (raised nothing)' % what)


# --------------------------------------------------------------- mappings --

def test_mappings():
    print('mapping tables')
    for name, count, kind in (('community.json', 468, 'community'),
                              ('xbla-accepted.json', 161, 'xbla')):
        p = os.path.join(HERE, 'mapping', name)
        ck(os.path.exists(p), '%s exists' % name)
        doc = json.load(open(p))
        ck(doc.get('version') == 1, '%s is version 1' % name)
        ck(doc.get('set') == kind, '%s names its set' % name)
        e = doc['entries']
        ck(len(e) == count, '%s holds %d entries (has %d)' % (name, count, len(e)))
        ids = [r['id'] for r in e]
        ck(len(set(ids)) == len(ids), '%s: no id twice' % name)
        ck(all(len(i) == 4 and i == i.lower() and all(c in '0123456789abcdef' for c in i)
               for i in ids), '%s: every id is four lower-case hex digits' % name)
        ck(ids == sorted(ids), '%s: entries are sorted by id (a stable diff)' % name)
        ck(all(int(i, 16) < sltx.MAX_ID for i in ids), '%s: every id is in range' % name)
        ck(all(0 < r['n64'][0] <= sltx.MAX_N64 and 0 < r['n64'][1] <= sltx.MAX_N64
               for r in e), '%s: every N64 size is in range' % name)
        if kind == 'community':
            ck(all(len(r['crc']) == 8 and all(c in '0123456789ABCDEFabcdef' for c in r['crc'])
                   for r in e), 'community: every checksum is 8 hex digits')
        else:
            ck(all(r['bundle'] and r['res'] for r in e),
               'xbla: every entry names a bundle and a resource')
            ck(all(len(r['flips']) == 2 and all(f in ('none', 'h', 'v', 'hv') for f in r['flips'])
                   for r in e), 'xbla: every flip is one of none/h/v/hv')
        # The tables carry identities only: no path, no owner, no acquisition.
        raw = open(p, 'r', encoding='utf-8').read().lower()
        for forbidden in ('http://', 'https://', 'c:\\', 'd:\\', '/mnt/', '.7z', '.zip', '.iso'):
            ck(forbidden not in raw, '%s carries no %r' % (name, forbidden))


# ------------------------------------------------------------------ SLTX ---

def test_sltx():
    print('SLTX writer')
    h = sltx.header(0x05d3, 38, 38, 256, 256)
    ck(len(h) == 28, 'the header is 28 bytes')
    magic, ver, tid, nw, nh, pw, ph, flags, payload = struct.unpack('<4sIIHHHHII', h)
    ck(magic == b'SLTX' and ver == 1, 'magic and version')
    ck((tid, nw, nh, pw, ph, flags) == (0x05d3, 38, 38, 256, 256, 0), 'the header fields')
    ck(payload == 256 * 256 * 4, 'the payload size is w*h*4')

    raises(lambda: sltx.header(sltx.MAX_ID, 8, 8, 8, 8), sltx.SltxError, 'an id past the bound is refused')
    raises(lambda: sltx.header(1, 0, 8, 8, 8), sltx.SltxError, 'a zero N64 width is refused')
    raises(lambda: sltx.header(1, 8, 8, sltx.MAX_PHYS + 1, 8), sltx.SltxError,
           'an oversized replacement is refused')
    raises(lambda: sltx.header(1, sltx.MAX_N64 + 1, 8, 8, 8), sltx.SltxError,
           'an oversized N64 image is refused')

    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'deep', '0001.sltx')
        rgba = bytes(range(256)) * 4        # 4x4x4 pixels' worth of bytes... = 1024
        n = sltx.write(p, 1, 4, 4, 16, 16, rgba)
        ck(n == 28 + len(rgba), 'write returns the file size')
        first = open(p, 'rb').read()
        ck(len(first) == n, 'the file is exactly header + payload')
        sltx.write(p, 1, 4, 4, 16, 16, rgba)
        ck(open(p, 'rb').read() == first, 'a rerun writes the same bytes (idempotent)')
        ck(not any(x.endswith('.part') for x in os.listdir(os.path.dirname(p))),
           'no temporary is left behind')
        raises(lambda: sltx.write(p, 1, 4, 4, 16, 16, b'short'), sltx.SltxError,
               'a payload that disagrees with the dimensions is refused')


# ------------------------------------------------------------------ CAFF ---

def test_caff():
    print('container reader')
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'not-a-bundle.bin')
        open(p, 'wb').write(b'this is not a container' * 4)
        raises(lambda: caff.parse(p), caff.CaffError, 'a file that is not a bundle is refused')
        p2 = os.path.join(d, 'truncated.bin')
        open(p2, 'wb').write(b'CAFF')
        raises(lambda: caff.parse(p2), Exception, 'a truncated bundle does not silently succeed')


# --------------------------------------------------------------- decoders --

def png_bytes(w, h):
    """A minimal RGBA PNG, written here so the fixture needs no encoder."""
    raw = b''
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            row += bytes(((x * 7) & 0xff, (y * 11) & 0xff, (x ^ y) & 0xff, 0xff))
        raw += bytes(row)

    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw))
            + chunk(b'IEND', b''))


def test_community_conversion():
    print('community conversion (a synthetic archive)')
    try:
        import PIL                                               # noqa: F401
    except ImportError:
        skip('community conversion needs Pillow')
        return
    import prepare                                               # noqa: E402

    entries = json.load(open(os.path.join(HERE, 'mapping', 'community.json')))['entries']
    e = entries[0]
    w = h = 64
    with tempfile.TemporaryDirectory() as d:
        arc = os.path.join(d, 'synthetic.zip')
        with zipfile.ZipFile(arc, 'w') as z:
            # the pack's own naming shape, with this entry's checksum
            z.writestr('PACK/In-game/PACK#%s#2#1_all.png' % e['crc'], png_bytes(w, h))
            z.writestr('PACK/In-game/PACK#00000000#2#1_all.png', png_bytes(8, 8))
            z.writestr('PACK/readme.txt', 'not a texture')
        out = os.path.join(d, 'packs')
        written, absent, total, skipped = prepare.community(arc, out)
        ck(written == 1, 'one mapped texture is converted (got %d)' % written)
        ck(absent == total - 1, 'every other mapped id is reported absent')
        ck(skipped == [], 'nothing is skipped')
        f = os.path.join(out, 'community', '%s.sltx' % e['id'])
        ck(os.path.exists(f), 'the file is named by the N64 id')
        b = open(f, 'rb').read()
        hd = struct.unpack('<4sIIHHHHII', b[:28])
        ck(hd[0] == b'SLTX', 'it is an SLTX file')
        ck(hd[2] == int(e['id'], 16), 'it carries the N64 id')
        ck((hd[3], hd[4]) == (e['n64'][0], e['n64'][1]), 'it carries the N64 size from the mapping')
        ck((hd[5], hd[6]) == (w, h), 'it carries the replacement size from the image')
        ck(len(b) == 28 + w * h * 4, 'the payload is exactly the image')
        ck(not os.path.exists(os.path.join(out, 'xbla')),
           'converting one set never writes the other')

        empty = os.path.join(d, 'empty.zip')
        with zipfile.ZipFile(empty, 'w') as z:
            z.writestr('readme.txt', 'nothing here')
        raises(lambda: prepare.community(empty, out), SystemExit,
               'an archive with no recognisable files is refused')
        raises(lambda: prepare.community(os.path.join(d, 'readme.txt'), out), SystemExit,
               'a file that is not an archive is refused')


def test_powershell_converter():
    """The release package ships a PowerShell converter, because a player has
    no venv: it must produce the SAME BYTES as the Python one, or a pack
    prepared at a desk and a pack a player fetches are two different things.
    Synthetic fixtures throughout; skipped where there is no PowerShell."""
    print('the packaged PowerShell converter agrees with the Python one')
    import shutil
    import subprocess
    ps = shutil.which('powershell.exe') or shutil.which('pwsh')
    if ps is None:
        skip('the PowerShell converter needs PowerShell (not this platform)')
        return
    try:
        import PIL                                               # noqa: F401
    except ImportError:
        skip('the cross-check needs Pillow for the Python side')
        return
    import prepare                                               # noqa: E402

    entries = json.load(open(os.path.join(HERE, 'mapping', 'community.json')))['entries']
    sizes = ((64, 64), (37, 11), (1, 1), (256, 129))
    with tempfile.TemporaryDirectory() as d:
        arc = os.path.join(d, 'synthetic.zip')
        mapping = {'version': 1, 'set': 'community', 'entries': []}
        with zipfile.ZipFile(arc, 'w') as z:
            for (w, h), e in zip(sizes, entries):
                mapping['entries'].append({'id': e['id'], 'n64': e['n64'], 'crc': e['crc']})
                z.writestr('PACK/In-game/PACK#%s#2#1_all.png' % e['crc'], png_bytes(w, h))
            # one mapped id the archive does not carry, and one file that is
            # not a texture: both converters must agree about those too
            mapping['entries'].append({'id': entries[9]['id'], 'n64': entries[9]['n64'],
                                       'crc': 'DEADBEEF'})
            z.writestr('PACK/readme.txt', 'not a texture')
        mp = os.path.join(d, 'mapping.json')
        with open(mp, 'w') as f:
            json.dump(mapping, f)

        py = os.path.join(d, 'py')
        os.makedirs(py)
        written, absent, total, skipped = prepare.community(arc, py, mapping=mp)
        ck(written == len(sizes),
           'the Python converter writes one file per fixture (got %d)' % written)

        pspack = os.path.join(d, 'ps')
        script = os.path.join(HERE, 'get-textures.ps1')
        r = subprocess.run([ps, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script,
                            '-Archive', arc, '-AnyArchive', '-Mapping', mp,
                            '-Out', pspack, '-Quiet'],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        ck(r.returncode == 0, 'the PowerShell converter exits 0 (%r)'
           % r.stdout.decode('utf-8', 'replace')[-400:])

        pyd = os.path.join(py, 'community')
        psd = os.path.join(pspack, 'community')
        pyf = sorted(os.listdir(pyd)) if os.path.isdir(pyd) else []
        psf = sorted(os.listdir(psd)) if os.path.isdir(psd) else []
        ck(pyf == psf, 'both converters write the same file names (%r vs %r)' % (pyf, psf))
        same = True
        for n in pyf:
            if open(os.path.join(pyd, n), 'rb').read() != open(os.path.join(psd, n), 'rb').read():
                same = False
                print('  differs: %s' % n)
        ck(same, 'every converted file is byte-identical between the two converters')


def test_decoders():
    print('block decoders')
    try:
        import numpy as np
    except ImportError:
        skip('block decoders need numpy')
        return
    import xdecode                                               # noqa: E402

    # One DXT1 block, big-endian 16-bit pairs as the console stores them:
    # c0 = white (0xffff), c1 = black (0x0000), every texel selecting c0.
    blk = np.zeros((1, 1, 8), np.uint8)
    blk[0, 0, 0] = 0xff; blk[0, 0, 1] = 0xff     # c0 white
    blk[0, 0, 2] = 0x00; blk[0, 0, 3] = 0x00     # c1 black
    img = xdecode.decode_dxt1_blocks(blk)
    ck(img.shape == (4, 4, 4), 'a DXT1 block is 4x4 RGBA')
    ck((img[..., :3] == 255).all(), 'selector 0 decodes to the first colour')
    ck((img[..., 3] == 255).all(), 'a four-colour block is opaque')

    blk3 = np.zeros((1, 1, 16), np.uint8)
    blk3[0, 0, 0:8] = 0xff                        # every 4-bit alpha = 15
    blk3[0, 0, 8] = 0xff; blk3[0, 0, 9] = 0xff
    img3 = xdecode.decode_dxt3_blocks(blk3)
    ck((img3[..., 3] == 255).all(), 'DXT3 alpha 15 decodes to 255')

    blk5 = np.zeros((1, 1, 16), np.uint8)
    blk5[0, 0, 0] = 0x00; blk5[0, 0, 1] = 0x7f    # a0/a1 after the 16-bit swap
    blk5[0, 0, 8] = 0xff; blk5[0, 0, 9] = 0xff
    img5 = xdecode.decode_dxt5_blocks(blk5)
    ck(img5.shape == (4, 4, 4), 'a DXT5 block is 4x4 RGBA')

    # Untiled path: an 8x8 single-channel image is passed straight through.
    data = bytes(range(64)) + b'\0' * (32 * 32 - 64)
    out = xdecode.decode(data, 2, 8, 8, tiled=False)
    ck(out.shape == (8, 8, 4), 'a single-channel image decodes to RGBA')
    ck((out[..., :3] == 255).all(), 'a single channel becomes white with that alpha')
    ck(out[0, 1, 3] == 1, 'the channel lands in alpha')

    raises(lambda: xdecode.decode(b'', 99, 4, 4), ValueError,
           'an unknown texture format is refused')
    ck(set(xdecode.SUPPORTED) == {2, 6, 18, 19, 20}, 'the supported format list')


def test_guest_layout():
    """Where level 0 begins inside its allocation (#68).

    Synthetic surfaces only: a generated allocation whose every block carries
    its own coordinates, so what comes back names the place it was read from.
    """
    print('guest texture layout')
    try:
        import numpy as np
    except ImportError:
        skip('the guest layout checks need numpy')
        return
    import xdecode                                               # noqa: E402

    # ---- the rule, before any bytes are decoded ----
    ck(xdecode.packed_mip_level(256, 16) == 0,
       'a 256x16 texture has its mip tail at level 0')
    ck(xdecode.packed_mip_level(32, 32) == 1,
       'a 32x32 texture has its mip tail at level 1')
    ck(xdecode.packed_mip_level(64, 64) == 2,
       'a 64x64 texture has its mip tail at level 2')
    ck(xdecode.packed_mip_offset(256, 16, 18, 0) == (0, 4),
       'the packed base of a 256x16 DXT1 texture is four block rows down')
    ck(xdecode.packed_mip_offset(16, 256, 18, 0) == (4, 0),
       'a taller-than-wide texture packs across instead of down')
    ck(xdecode.packed_mip_offset(64, 64, 18, 0) is None,
       'a level whose short side is still above 16 is not packed at all')

    # Each level in the tail has a home of its own - a base that landed on
    # top of its own mip is exactly the fault this guards.
    homes = [xdecode.packed_mip_offset(256, 16, 18, L) for L in range(5)]
    ck(len(set(homes)) == len(homes),
       'every level in the tail has a distinct home (%r)' % (homes,))
    ck(homes[0] != homes[1],
       'the base level and the first mip are not at the same place')

    # ---- the gate: the base moves only when there is a tail to move into ----
    ck(xdecode.base_block_origin(256, 16, 18, 5) == (0, 4),
       'a 256x16 texture with a mip chain reads its base from the tail')
    ck(xdecode.base_block_origin(256, 16, 18, 1) == (0, 0),
       'a single-level texture has no tail, so its base is at the origin')
    for side in (32, 64, 128, 256, 512):
        ck(xdecode.base_block_origin(side, side, 18, 9) == (0, 0),
           'a %dx%d base is NOT given the packed offset' % (side, side))
    ck(xdecode.base_block_origin(256, 256, 20, 9) == (0, 0),
       'a large DXT5 square keeps the origin too')

    # ---- an allocation whose blocks name their own coordinates ----
    def surface(cols, rows, pitch_blocks, row_blocks):
        """A tiled DXT1 allocation: block (x, y) decodes to a colour whose
        red channel is x and whose green channel is y."""
        buf = bytearray(pitch_blocks * row_blocks * 8)
        for y in range(row_blocks):
            for x in range(pitch_blocks):
                i = int(xdecode.tiled_offset(x, y, pitch_blocks, 8)) * 8
                c0 = ((x & 31) << 11) | ((y & 63) << 5) | 1
                buf[i] = (c0 >> 8) & 0xff
                buf[i + 1] = c0 & 0xff
                # c1 = 0 keeps the block in four-colour mode; selectors 0
                # make every texel the first colour.
        return bytes(buf), cols, rows

    data, cols, rows = surface(64, 4, 64, 32)
    ck(len(data) == 16384, 'the synthetic 256x16 DXT1 allocation is 16 KiB')

    packed = xdecode.decode(data, 18, 256, 16, mip_levels=5)
    origin = xdecode.decode(data, 18, 256, 16, mip_levels=1)
    # green carries the block row the texel was read from
    grn_packed = sorted({int(v) for v in np.unique(packed[..., 1])})
    grn_origin = sorted({int(v) for v in np.unique(origin[..., 1])})
    want_packed = sorted({(y & 63) * 255 // 63 for y in (4, 5, 6, 7)})
    want_origin = sorted({(y & 63) * 255 // 63 for y in (0, 1, 2, 3)})
    ck(grn_packed == want_packed,
       'the packed base reads block rows 4..7 (%r)' % (grn_packed,))
    ck(grn_origin == want_origin,
       'without a mip chain the same bytes read block rows 0..3 (%r)'
       % (grn_origin,))
    ck(grn_packed != grn_origin,
       'the packed base and the origin are different pixels')
    red = sorted({int(v) for v in np.unique(packed[..., 0])})
    ck(len(red) == 32, 'all 64 block columns are read, both halves of the '
                       'five-bit column code (%d distinct)' % len(red))

    # ---- the base pitch is honoured ----
    lin = bytes(range(256)) * 32
    narrow = xdecode.untile(lin, 20, 4, 1, tiled=False)
    wide = xdecode.untile(lin, 20, 4, 1, tiled=False, pitch=40)
    ck(int(narrow[1, 0, 0]) == 32 and int(wide[1, 0, 0]) == 64,
       'a row begins at the 32-aligned PITCH, not at the width '
       '(%d vs %d)' % (int(narrow[1, 0, 0]), int(wide[1, 0, 0])))

    # ---- an impossible layout is refused, and nothing is read past the end ----
    raises(lambda: xdecode.untile(data, 64, 4, 8, origin=(0, 40)),
           ValueError, 'a level placed past the end of the surface is refused')
    raises(lambda: xdecode.untile(data, 64, 4, 8, origin=(64, 0)),
           ValueError, 'a level placed past the right edge is refused')
    raises(lambda: xdecode.untile(data, 64, 4, 8, origin=(-4, 0)),
           ValueError, 'a negative origin is refused')
    ys, xs = np.mgrid[4:8, 0:64]
    reach = int(xdecode.tiled_offset(xs, ys, 64, 8).max() + 1) * 8
    ck(reach <= len(data),
       'the packed base reads %d bytes of a %d byte allocation - no overread'
       % (reach, len(data)))


def test_nativeuv():
    print('native-domain entries')
    try:
        import numpy as np
    except ImportError:
        skip('native-domain resampling needs numpy')
        return
    import nativeuv                                              # noqa: E402

    # A synthetic source: column index in R, row index in G, opaque.
    src = np.zeros((64, 64, 4), np.uint8)
    src[:, :, 0] = np.arange(64)[None, :] * 4
    src[:, :, 1] = np.arange(64)[:, None] * 4
    src[:, :, 3] = 255
    spec = {'size': [32, 32], 'u': {'n64': [8.0, 24.0], 'src': [16.0, 48.0]},
            'v': {'n64': [0, 32], 'src': [0.0, 64.0]}}
    out = nativeuv.sample(src, spec, (32, 32))
    ck(out.shape == (32, 32, 4), 'sample: the output is the requested size')
    # Texel 16 (centre 16.5) sits half-way through the band -> source x 33.
    ck(abs(int(out[5, 16, 0]) - 4 * 32.5) <= 4,
       'sample: a native texel maps linearly into the source window (got %d)' % out[5, 16, 0])
    ck(int(out[5, 0, 0]) == int(out[5, 7, 0]),
       'sample: below the band the source is clamped to the window edge')
    spec_m = dict(spec, u=dict(spec['u'], outside='mirror'))
    om = nativeuv.sample(src, spec_m, (32, 32))
    ck(abs(int(om[5, 6, 0]) - int(om[5, 9, 0])) <= 4 and int(om[5, 6, 0]) != int(out[5, 6, 0]),
       'sample: "mirror" reflects the band instead of repeating its edge column')
    spec_w = dict(spec, u=dict(spec['u'], wrap=32, n64=[8.0, 40.0]))
    ow = nativeuv.sample(src, spec_w, (32, 32))
    ck(int(ow[5, 2, 0]) > int(ow[5, 30, 0]),
       'sample: with a wrap, texels below the band continue past its far end')
    # v repeats: with a seam, the last row blends into the window's first.
    spec_s = {'size': [16, 32], 'u': {'n64': [0, 32], 'src': [0, 64]},
              'v': {'n64': [0, 32], 'src': [10.0, 42.0]}, 'seam': 0.25}
    os_ = nativeuv.sample(src, spec_s, (32, 32))
    no = nativeuv.sample(src, dict(spec_s, seam=0.0), (32, 32))
    ck(abs(int(os_[31, 3, 1]) - int(os_[0, 3, 1])) < abs(int(no[31, 3, 1]) - int(no[0, 3, 1])),
       'sample: the seam brings the repeat\'s last row back to its first')
    ck(np.array_equal(os_[:24], no[:24]),
       'sample: rows before the seam are untouched by it')
    raises(lambda: nativeuv.sample(src, dict(spec, u={'n64': [8, 8], 'src': [0, 1]}), (32, 32)),
           ValueError, 'sample: an empty native band is refused')
    raises(lambda: nativeuv.sample(src, dict(spec, seam=1.0), (32, 32)),
           ValueError, 'sample: a seam of a whole repeat is refused')
    raises(lambda: nativeuv.sample(src, dict(spec, u=dict(spec['u'], outside='wrap')), (32, 32)),
           ValueError, 'sample: an unknown outside rule is refused')

    # level: covered mean luminance lands on the target; alpha untouched.
    g = np.zeros((4, 4, 4), np.uint8); g[:, :, :3] = 50; g[:, :, 3] = 255; g[0, 0, 3] = 0
    lv = nativeuv.level(g, 100)
    ck(int(lv[1, 1, 0]) == 100 and int(lv[0, 0, 3]) == 0, 'level: covered texels scale to the target mean')
    raises(lambda: nativeuv.level(g, 0), ValueError, 'level: a zero target is refused')

    # bleed: a transparent texel takes its covered neighbour's colour.
    b = np.zeros((4, 4, 4), np.uint8); b[1, 1] = (200, 100, 50, 255)
    bl = nativeuv.bleed(b)
    ck(tuple(bl[1, 2, :3]) == (200, 100, 50) and int(bl[1, 2, 3]) == 0,
       'bleed: a transparent neighbour takes the covered colour, keeping alpha 0')
    ck(tuple(bl[3, 3, :3]) == (200, 100, 50), 'bleed: the fill reaches every transparent texel')
    ck(np.array_equal(nativeuv.bleed(src), src), 'bleed: an opaque image is unchanged')

    # repeat_cells: a 5 x 5 grid of one tile, a 3 x 2 grid, and noise.
    rng = np.random.RandomState(7)
    tile = rng.randint(0, 256, (20, 20, 4)).astype(np.uint8); tile[..., 3] = 255
    ck(nativeuv.repeat_cells(np.tile(tile, (5, 5, 1))) == (5, 5),
       'repeat_cells: a 5 x 5 grid of one tile reads 5 x 5')
    ck(nativeuv.repeat_cells(np.tile(rng.randint(0, 256, (30, 20, 4)).astype(np.uint8), (2, 3, 1))) == (3, 2),
       'repeat_cells: 3 across and 2 down read (3, 2) - u is the column axis')
    ck(nativeuv.repeat_cells(rng.randint(0, 256, (64, 64, 4)).astype(np.uint8)) == (1, 1),
       'repeat_cells: an image with no repeat inside it reads 1 x 1')

    # retile: whole cells per native repeat, the residual signed, the window placed.
    sp, k, r = nativeuv.retile((64, 64), (512, 512), (5, 5), (0.1983, 0.1984), (412.8, 99.2))
    ck(k == (1, 1) and sp['u']['src'] == [412.8, 515.2] and sp['v']['src'] == [99.2, 201.6]
       and sp['size'] == [128, 128] and sp['u']['n64'] == [0, 64],
       'retile: 0.198 of a 5-cell image is one cell, 102.4 px, output 128')
    ck(abs(r[0] - (0.2 / 0.1983 - 1)) < 1e-9 and r[0] > 0, 'retile: the residual is k/cells/scale - 1')
    sp, k, r = nativeuv.retile((32, 32), (256, 256), (1, 1), (2.0562, 2.0548), (101, 53))
    ck(k == (2, 2) and sp['u']['src'] == [101.0, 613.0] and sp['size'] == [512, 512] and r[0] < 0,
       'retile: 2.056 repeats of a seamless image hold 2 whole repeats (tiles), drawn 2.7% larger')
    sp, k, r = nativeuv.retile((32, 32), (64, 64), (1, 1), (0.3, 0.3))
    ck(k == (1, 1), 'retile: less than half a repeat still takes one whole repeat')
    t32 = rng.randint(0, 256, (32, 32, 4)).astype(np.uint8)
    one = nativeuv.sample(np.tile(t32, (5, 5, 1)),
                          nativeuv.retile((16, 16), (160, 160), (5, 5), (0.199, 0.201), (64, 32))[0], (16, 16))
    ck(np.array_equal(one, t32), 'retile: a cell-aligned window is exactly one source cell, so it tiles')
    raises(lambda: nativeuv.retile((32, 32), (64, 64), (1, 1), (0.0, 1.0)),
           ValueError, 'retile: a zero scale is refused')

    # the mapping's own native-domain entries validate
    doc = json.load(open(os.path.join(HERE, 'mapping', 'xbla-accepted.json')))
    bad = []
    for e in doc['entries']:
        try:
            nativeuv.check_entry(e)
        except ValueError as exc:
            bad.append(str(exc))
    ck(not bad, 'xbla mapping: every native-domain field validates %s' % bad)
    raises(lambda: nativeuv.check_entry({'id': 'ffff', 'sample': {'size': [1, 1], 'u': {}}}),
           ValueError, 'check_entry: an incomplete sample block is refused')
    raises(lambda: nativeuv.check_entry({'id': 'ffff', 'level': 300}),
           ValueError, 'check_entry: a level past 255 is refused')


def _uv_bg(cols, slopes, wrap=0, tid=0x137):
    """A synthetic background file: one room at (100, 0, 200) holding a
    floor of `cols` x 2 squares 100 units on a side, two triangles each, image
    `tid`. The cartridge's s runs at slopes[c] texture units per world unit
    in column c - continuous, but bending at every change of slope; t is
    uniform."""
    pts = []
    for j in range(3):
        s = 0.0
        for i in range(cols + 1):
            if i:
                s += slopes[i - 1] * 100
            pts.append((i * 100, 0, j * 100, int(round(s)), int(round(j * 1024))))
    def deflate(b):
        c = zlib.compressobj(9, zlib.DEFLATED, -15)
        return b'\x11\x72' + c.compress(b) + c.flush()
    ptab = b''.join(struct.pack('>hhhHhhBBBB', x, y, z, 0, s, t, 255, 255, 255, 255)
                    for x, y, z, s, t in pts)
    dl = struct.pack('>II', 0xC0080002 | (wrap << 20), tid)
    n = len(pts)
    dl += struct.pack('>II', 0x04000000 | ((n - 1) << 20) | (n * 16), 0x0E000000)
    w = cols + 1
    for j in range(2):
        for i in range(cols):
            a, b, c, d = j * w + i, j * w + i + 1, (j + 1) * w + i + 1, (j + 1) * w + i
            for t in ((a, b, c), (a, c, d)):
                dl += struct.pack('>II', 0xBF000000, (t[0] * 10) << 16 | (t[1] * 10) << 8 | t[2] * 10)
    dl += struct.pack('>II', 0xB8000000, 0)
    zp, zd = deflate(ptab), deflate(dl)
    table = 0x10
    off_p = table + 3 * 0x18
    off_d = off_p + len(zp)
    end = off_d + len(zd)
    head = struct.pack('>IIII', 0, 0x0F000000 | table, 0, 0)
    rec0 = b'\x00' * 0x18
    rec1 = struct.pack('>IIIfff', 0x0F000000 | off_p, 0x0F000000 | off_d, 0, 100.0, 0.0, 200.0)
    recx = struct.pack('>IIIfff', 0x0F000000 | off_d, 0x0F000000 | end, 0, 0.0, 0.0, 0.0)
    return head + rec0 + rec1 + recx + zp + zd, pts


def test_uvcarry():
    print('carried coordinates')
    import uvcarry                                               # noqa: E402

    # 1. The background reader, on a synthetic file.
    bg, pts = _uv_bg(4, [10.24, 20.48, 10.24, 20.48])
    rooms = uvcarry.read_bg(bg)
    ck(len(rooms) == 1 and rooms[0]['room'] == 1 and rooms[0]['pos'] == (100.0, 0.0, 200.0),
       'read_bg: one room, its position read after the three offsets')
    ck(len(rooms[0]['points']) == 15 and rooms[0]['points'][6] == (100, 0, 100, 1024, 1024),
       'read_bg: the point table, entry = segment-14 offset / 16')
    ck(len(rooms[0]['tris']) == 16 and rooms[0]['tris'][0]['idx'] == (0, 1, 6)
       and rooms[0]['tris'][0]['tex'] == 0x137 and rooms[0]['tris'][0]['wrap'] == 0,
       'read_bg: 16 triangles named by their point entries, the C0 id and flags')
    raises(lambda: uvcarry.read_bg(b'\x00' * 8), uvcarry.UvCarryError, 'read_bg: a short file is refused')

    # 2. The remaster's floor over it, authored at twice the room coordinates,
    #    one uniform map: u = x / 100, v = 1 - z / 100.
    hd = []
    for R in rooms:
        for t in R['tris']:
            P = [tuple(2.0 * (R['points'][i][a] + R['pos'][a]) for a in range(3)) for i in t['idx']]
            UV = [(R['points'][i][0] / 100.0, 1.0 - R['points'][i][2] / 100.0) for i in t['idx']]
            hd.append(dict(name='_hd.tga', P=P, UV=UV))
    grid = [(100.0 * i, 10.0 * ((i * j) % 3), 100.0 * j) for i in range(5) for j in range(5)]
    ck(abs(uvcarry.fit_scale(grid, [(1.56 * x, 1.56 * y, 1.56 * z) for x, y, z in grid]) - 1.56) < 1e-6,
       'fit_scale: the one scale, from the vertices')
    raises(lambda: uvcarry.fit_scale(grid, [(1.56 * x, y, 1.2 * z) for x, y, z in grid]),
           uvcarry.UvCarryError, 'fit_scale: two sets that disagree on their scale are refused')
    rows, rep = uvcarry.derive(rooms, hd, {0x137: ('_hd.tga', (32, 32))}, 2.0)
    r = rep[0x137]
    ck(r['taken'] and r['bends_native'] == 6 and r['bends_carried'] == 0,
       'derive: 6 bent edges under the cartridge coordinates, none carried -> taken (%r)' % r)
    ck(r['carried'] == 8 and r['equal'] == 8 and len(rows) == 8,
       'derive: columns 1 and 3 carried; 0 and 2 already equal to whole repeats')
    ok = True
    for room, tid, idx, st in rows:
        for k in range(3):
            p = rooms[0]['points'][idx[k]]
            ds, dt = st[k][0] - p[0] * 10.24, st[k][1] - p[2] * 10.24
            ok = ok and abs(ds / 2048.0 - round(ds / 2048.0)) < 1e-3 and abs(dt / 2048.0 - round(dt / 2048.0)) < 1e-3
    ck(ok, 'derive: each row is the remaster\'s map, shifted by an even number of repeats')
    rows2, rep2 = uvcarry.derive(uvcarry.read_bg(_uv_bg(4, [10.24] * 4)[0]), hd, {0x137: ('_hd.tga', (32, 32))}, 2.0)
    ck(not rows2 and not rep2[0x137]['taken'], 'derive: cartridge coordinates that do not bend are kept')
    rows3, rep3 = uvcarry.derive(uvcarry.read_bg(_uv_bg(4, [10.24, 20.48, 10.24, 20.48], wrap=0x4)[0]),
                                 hd, {0x137: ('_hd.tga', (32, 32))}, 2.0)
    ck(not rows3 and rep3[0x137]['why'] == 'a clamped tile', 'derive: a clamped tile is never carried')
    # A remastered surface that changes its own map inside a native triangle.
    S = uvcarry.Surface([dict(name='m', P=[(0, 0, 0), (100, 0, 0), (0, 0, 100)], UV=[(0, 0), (1, 0), (0, 1)]),
                         dict(name='m', P=[(100, 0, 0), (100, 0, 100), (0, 0, 100)], UV=[(1.5, 0), (1.5, 1), (0.5, 1)])])
    ck(S.carry([(10, 0, 10), (90, 0, 10), (90, 0, 90)]) == (None, 'mixed'),
       'Surface.carry: two maps under one triangle -> refused')
    ck(S.carry([(10, 0, 10), (60, 0, 10), (10, 0, 60)])[1] == 'one', 'Surface.carry: one map -> taken')

    # The remaster's triangles, from a reader's level: its draws' indices are
    # already flat triangle lists (xblaworld.read_level expands every fan and
    # quad list), so they are taken three at a time and never expanded again.
    import xblaworld                                             # noqa: E402
    V = [(0.0, 0.0, 0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 1.0, 0.0), (1.0, 0.0, 1.0, 1.0, 1.0),
         (0.0, 0.0, 1.0, 0.0, 1.0), (0.5, 0.0, 1.5, 0.5, 1.5)]
    fan = xblaworld.expand_indices(5, [0, 1, 2, 3, 4])
    quad = xblaworld.expand_indices(13, [0, 1, 2, 3])
    lvl = dict(pools=[dict(vertices=V)], textures=[dict(name='_f.tga')],
               draws=[dict(pool=0, image=0, prim=5, indices=fan),
                      dict(pool=0, image=0, prim=13, indices=quad)])
    got = [tuple(sorted(T['P'])) for T in uvcarry.hd_triangles(lvl)]
    want_t = [tuple(sorted(V[i][:3] for i in fan[k:k + 3])) for k in range(0, len(fan), 3)] + \
             [tuple(sorted(V[i][:3] for i in quad[k:k + 3])) for k in range(0, len(quad), 3)]
    ck(got == want_t, 'hd_triangles: a fan and a quad list as the reader expanded them, '
                      'not expanded a second time (%d triangles)' % len(got))

    # 3. The table.
    blob = uvcarry.pack(34, rows)
    stage, back = uvcarry.unpack(blob)
    ck(stage == 34 and len(blob) == 16 + 24 * len(rows) and blob[:4] == b'SLUV',
       'pack: header and size')
    want = sorted((room, tid, tuple(sorted(idx)), tuple(st[sorted(range(3), key=lambda k: idx[k])[k]] for k in range(3)))
                  for room, tid, idx, st in rows)
    ck(sorted(back) == want, 'pack/unpack: corners sorted with their coordinates, rows sorted')
    raises(lambda: uvcarry.pack(34, rows + rows[:1]), uvcarry.UvCarryError, 'pack: two rows for one triangle are refused')
    raises(lambda: uvcarry.unpack(blob[:-1]), uvcarry.UvCarryError, 'unpack: a short table is refused')

    # 4. The stage, from the repository's own tables.
    ck(uvcarry.stage_of(uvcarry.ROOT, 'facility') == (34, 'assets/obseg/bg/bg_ark_all_p.bin'),
       'stage_of: facility is LEVELID 34, bg_ark')
    raises(lambda: uvcarry.stage_of(uvcarry.ROOT, 'nowhere'), uvcarry.UvCarryError, 'stage_of: an unknown stage is refused')


def main():
    print('texpack selftest')
    test_mappings()
    test_nativeuv()
    test_uvcarry()
    test_sltx()
    test_caff()
    test_community_conversion()
    test_powershell_converter()
    test_decoders()
    test_guest_layout()
    print('%d checks, %d failed, %d skipped' % (PASS + FAIL, FAIL, SKIP))
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
