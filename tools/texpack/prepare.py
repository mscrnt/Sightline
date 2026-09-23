"""Build the project-local SLTX texture packs from the user's own sources.

    prepare.py community --archive <the official pack archive> --out <root>
    prepare.py xbla      --tree <an extracted source tree>     --out <root>

Driven by tools/windows/prepare-textures.ps1, which is the command a
developer runs; this is the conversion itself.

WHAT IT READS is always something the USER supplies, and never anything in
this repository beyond the two mapping tables in mapping/, which hold ids,
dimensions and resource identifiers only. WHAT IT WRITES is <root>/<set>/
<hex4>.sltx, under a gitignored directory, and nothing else: no tracked file
is touched, and neither a source nor a converted image is ever packaged.

Both sets are per-texture: an id the mapping does not name, or whose file
the user's copy does not contain, is simply absent from the pack, and the
runtime draws the game's own artwork for it (docs/texture-packs.md).

Rerunning is safe and cheap - each file is written through a temporary and
renamed into place, so a pack is only ever complete or unchanged.
"""
import argparse
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import sltx                                             # noqa: E402

MAPPING = os.path.join(HERE, 'mapping')


def need(module, what):
    try:
        return __import__(module)
    except ImportError:
        sys.stderr.write(
            'prepare-textures: this conversion needs the Python package %r (%s).\n'
            '  It is a DEVELOPER tool prerequisite, not a Sightline dependency:\n'
            '  nothing in the engine or the build links it. Install it into the\n'
            '  project venv with:  .venv\\Scripts\\python.exe -m pip install %s\n'
            % (module, what, module))
        sys.exit(3)


def load_mapping(name):
    p = name if os.path.isabs(name) else os.path.join(MAPPING, name)
    with open(p, 'r') as f:
        doc = json.load(f)
    if doc.get('version') != 1:
        sys.exit('prepare-textures: %s is version %r, this tool reads 1' % (p, doc.get('version')))
    return doc['entries']


# ------------------------------------------------------------- community ----

def community(archive, out, mapping='community.json'):
    """The Community HD pack: match each mapped id's texture checksum against
    the user's own copy of the official release, and convert that file."""
    need('PIL', 'PNG decoding')
    from PIL import Image                                          # noqa: E402

    entries = load_mapping(mapping)
    if not zipfile.is_zipfile(archive):
        sys.exit('prepare-textures: %s is not a zip archive.\n'
                 '  Point -CommunityArchive at the official release archive as\n'
                 '  the Community HD project publishes it.' % archive)

    with zipfile.ZipFile(archive) as z:
        # index the pack by the checksum its filenames encode:
        # <prefix>#<texCRC>#<fmt>#<size>[#<palCRC>]_all.png
        by_crc = {}
        for n in z.namelist():
            base = n.rsplit('/', 1)[-1]
            if not base.lower().endswith('.png'):
                continue
            parts = base.split('#')
            if len(parts) < 3:
                continue
            by_crc.setdefault(parts[1].upper(), []).append(n)
        if not by_crc:
            sys.exit('prepare-textures: %s holds no hi-res texture files this tool\n'
                     '  recognises (expected names carrying #<checksum>#).' % archive)

        written = absent = 0
        skipped = []
        for e in sorted(entries, key=lambda r: r['id']):
            tid = int(e['id'], 16)
            cands = by_crc.get(e['crc'].upper())
            if not cands:
                absent += 1
                continue
            name = sorted(cands)[0]          # deterministic on a duplicate
            with z.open(name) as f:
                im = Image.open(f).convert('RGBA')
                w, h = im.size
                rgba = im.tobytes()
            if not (0 < w <= sltx.MAX_PHYS and 0 < h <= sltx.MAX_PHYS):
                skipped.append('%s %dx%d exceeds the runtime bound' % (e['id'], w, h))
                continue
            sltx.write(os.path.join(out, 'community', '%04x.sltx' % tid),
                       tid, e['n64'][0], e['n64'][1], w, h, rgba)
            written += 1
    return written, absent, len(entries), skipped


# ------------------------------------------------------------------ xbla ----

def xbla(tree, out):
    """The user-supplied XBLA set: read each accepted mapping's resource out
    of the extracted source tree, decode it and orient it to the N64 image."""
    np = need('numpy', 'texture decoding')
    import caff                                                    # noqa: E402
    import xdecode                                                 # noqa: E402

    entries = load_mapping('xbla-accepted.json')

    def flip(a, how):
        if how in ('v', 'hv'):
            a = a[::-1, :, :]
        if how in ('h', 'hv'):
            a = a[:, ::-1, :]
        return a

    # one parse per bundle, not one per texture
    by_bundle = {}
    for e in entries:
        by_bundle.setdefault(e['bundle'], []).append(e)

    # Index the extracted tree by directory path SUFFIX, so the archive's own
    # top-level prefix - whatever the user's copy happens to use - never has
    # to be known here or spelled anywhere.
    exact, suffix = {}, {}
    for root, dirs, _files in os.walk(tree):
        dirs.sort()                      # deterministic on any filesystem
        rel = os.path.relpath(root, tree).replace('\\', '/')
        if rel == '.':
            continue
        exact.setdefault(rel, root)
        parts = rel.split('/')
        for i in range(1, len(parts)):
            suffix.setdefault('/'.join(parts[i:]), root)
    index = dict(suffix)
    index.update(exact)                  # an exact path always wins

    written = absent = 0
    skipped = []
    for bundle in sorted(by_bundle):
        d = index.get(bundle)
        files = []
        if d is not None and os.path.isdir(d):
            files = sorted(n for n in os.listdir(d)
                           if n.lower().endswith(('.bin', '.rba')))
        if not files:
            absent += len(by_bundle[bundle])
            continue
        texs = {}
        for n in files:
            try:
                b = caff.parse(os.path.join(d, n))
            except Exception as exc:                               # noqa: BLE001
                skipped.append('%s/%s: %s' % (bundle, n, exc))
                continue
            for t in b['texs']:
                nm = caff.tex_name(b, t)
                texs.setdefault(nm, (b, t))
                # A standalone bundle carrying exactly one unnamed texture is
                # identified by the bundle itself, which is how the mapping
                # names those entries.
                if len(b['texs']) == 1 and nm.startswith('n'):
                    texs.setdefault(bundle.rsplit('/', 1)[-1], (b, t))
        for e in sorted(by_bundle[bundle], key=lambda r: r['id']):
            tid = int(e['id'], 16)
            hit = texs.get(e['res'])
            if hit is None:
                absent += 1
                continue
            b, t = hit
            if t['fmt'] not in xdecode.SUPPORTED:
                skipped.append('%s: texture format %d is not one this tool decodes'
                               % (e['id'], t['fmt']))
                continue
            try:
                img = xdecode.decode(caff.base_level(b, t), t['fmt'], t['w'], t['h'])
            except Exception as exc:                               # noqa: BLE001
                skipped.append('%s: %s' % (e['id'], exc))
                continue
            img = flip(flip(img, e['flips'][0]), e['flips'][1])
            h, w = img.shape[0], img.shape[1]
            if not (0 < w <= sltx.MAX_PHYS and 0 < h <= sltx.MAX_PHYS):
                skipped.append('%s %dx%d exceeds the runtime bound' % (e['id'], w, h))
                continue
            sltx.write(os.path.join(out, 'xbla', '%04x.sltx' % tid),
                       tid, e['n64'][0], e['n64'][1], w, h,
                       np.ascontiguousarray(img).tobytes())
            written += 1
    return written, absent, len(entries), skipped


def prune(out, name, mapping):
    """Remove pack files the mapping no longer names.

    A pack is the mapping's output, not an accumulation of every mapping the
    directory has ever seen: without this, an id DROPPED from the table keeps
    shipping from an earlier run, and the set still replaces a texture the
    project has decided it should not. Only `<hex4>.sltx` is considered, and
    only in the set's own directory.
    """
    d = os.path.join(out, name)
    if not os.path.isdir(d):
        return 0
    keep = {'%s.sltx' % e['id'] for e in mapping}
    gone = 0
    for fn in sorted(os.listdir(d)):
        if len(fn) != 9 or not fn.endswith('.sltx') or fn in keep:
            continue
        try:
            int(fn[:4], 16)
        except ValueError:
            continue
        os.remove(os.path.join(d, fn))
        print('  pruned %s (no longer mapped)' % fn)
        gone += 1
    return gone


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('set', choices=('community', 'xbla'))
    ap.add_argument('--archive', help='the user\'s own copy of the official pack archive')
    ap.add_argument('--tree', help='an extracted source tree')
    ap.add_argument('--out', required=True, help='the pack root to write under')
    a = ap.parse_args()

    if a.set == 'community':
        if not a.archive:
            ap.error('community needs --archive')
        written, absent, total, skipped = community(a.archive, a.out)
        table = load_mapping('community.json')
    else:
        if not a.tree:
            ap.error('xbla needs --tree')
        written, absent, total, skipped = xbla(a.tree, a.out)
        table = load_mapping('xbla-accepted.json')

    prune(a.out, a.set, table)

    for s in skipped[:20]:
        print('  skipped %s' % s)
    if len(skipped) > 20:
        print('  ... and %d more' % (len(skipped) - 20))
    print('%s: %d of %d mapped textures written to %s (%d not in this source)'
          % (a.set, written, total, os.path.join(a.out, a.set), absent))
    return 0 if written else 1


if __name__ == '__main__':
    sys.exit(main())
