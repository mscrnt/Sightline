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
                              ('xbla-accepted.json', 127, 'xbla')):
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


def main():
    print('texpack selftest')
    test_mappings()
    test_sltx()
    test_caff()
    test_community_conversion()
    test_powershell_converter()
    test_decoders()
    print('%d checks, %d failed, %d skipped' % (PASS + FAIL, FAIL, SKIP))
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
