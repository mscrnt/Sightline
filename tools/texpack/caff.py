"""Rare CAFF bundle reader - the container a user-supplied XBLA source uses.

Layout, measured during the #47 recon:

    header | .gpu | .data | .stream        sections in header name order

The header size is filesize minus the sum of the section extents, rounded
down to 4, and is verified against the first texture record before anything
is read from it. A resource-table entry is 14 bytes big-endian:

    u32 name  u32 offset  u32 size  u8 section  u8 kind

A texture is a 0x70-byte '.data' record - the literal "texture\\0", a version
string, then (at +0x18) u32 fetch-constant flags, two u32, u16 w, u16 h,
u32 pool offset, u32 pool size, u8 mip count - plus either its own '.gpu'
entry (pool size 0xffffffff) or a slice of the bundle's shared texture pool,
which is the last kind-0x0c '.gpu' entry.

This module parses a file the USER supplies. It contains no source bytes,
no identifiers of any particular source, and no way to obtain one.
"""
import re
import struct

TEX_MAGIC = b'texture\x00'


class CaffError(Exception):
    """The file is not a CAFF bundle this reader understands."""


def parse(path):
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != b'CAFF':
        raise CaffError('%s: not a CAFF bundle' % path)

    secs = []
    for name in (b'.gpu', b'.data', b'.stream'):
        i = d.find(name + b'\x00', 0, 0x400)
        if i >= 0:
            secs.append((i, name.decode()))
    if not secs:
        raise CaffError('%s: no section names in the header' % path)
    secs = [n for _, n in sorted(secs)]

    # String table: the section names, then u32 total and u32 offsets[], then
    # the strings themselves. Resource-table name indices are 1-based into it.
    last = max(d.find(s.encode() + b'\x00', 0, 0x400) for s in secs)
    last = d.find(b'\x00', last) + 1
    m = re.search(rb'[A-Za-z]:\\', d[last:last + 0x400])
    if m is None:
        raise CaffError('%s: no string table' % path)
    S = m.start() + last
    n_off = (S - last - 4) // 4
    offs = struct.unpack('>%dI' % n_off, d[last + 4:last + 4 + n_off * 4])
    names = []
    for o in offs:
        e = d.find(b'\x00', S + o)
        names.append(d[S + o:e].decode('latin-1'))

    adb = d.find(b'.adb\x00')
    if adb >= 0:
        p = adb + 5
    else:
        m = re.search(rb'default\.rba\x00', d)
        if m is None:
            raise CaffError('%s: no resource table' % path)
        p = m.end()
    while p < len(d) and d[p] == 0:
        p += 1
    p -= 3
    ents = []
    while p + 14 <= len(d):
        nm, off, sz, sec, kind = struct.unpack('>IIIBB', d[p:p + 14])
        if nm == 0 or nm > 4000 or sec == 0 or sec > len(secs):
            break
        ents.append(dict(name=nm, off=off, size=sz, sec=secs[sec - 1], kind=kind))
        p += 14

    ext = {}
    for e in ents:
        ext[e['sec']] = max(ext.get(e['sec'], 0), e['off'] + e['size'])
    hdr = (len(d) - sum(ext.values())) & ~3

    def bases(h):
        b, o = {}, h
        for s in secs:
            b[s] = o
            o += ext.get(s, 0)
        return b

    base = bases(hdr)
    first = next((e for e in ents if e['sec'] == '.data' and e['size'] == 0x70), None)
    if first is not None:
        for delta in (0, -4, 4, -8, 8, -12, 12, -16, 16):
            q = base['.data'] + first['off'] + delta
            if d[q:q + 8] == TEX_MAGIC:
                if delta:
                    hdr += delta
                    base = bases(hdr)
                break
        else:
            raise CaffError('%s: cannot locate the .data section' % path)

    texs = []
    for e in ents:
        if e['sec'] == '.data' and e['size'] == 0x70:
            q = base['.data'] + e['off']
            r = d[q:q + 0x70]
            if r[:8] != TEX_MAGIC:
                continue
            flags, z1, z2, w, h, poff, psz, mips = struct.unpack('>IIIHHIIB', r[0x18:0x31])
            texs.append(dict(name=e['name'], flags=flags, fmt=flags & 0x3f,
                             endian=(flags >> 6) & 3, w=w, h=h,
                             poff=poff, psz=psz, mips=mips))
    texnames = {t['name'] for t in texs}
    pools = [e for e in ents if e['sec'] == '.gpu' and e['kind'] == 0x0c
             and e['name'] not in texnames]
    return dict(path=path, data=d, secs=secs, names=names, ents=ents, ext=ext,
                hdr=hdr, base=base, texs=texs, pool=pools[-1] if pools else None)


def tex_name(b, t):
    """The texture's resource name, or n<index> when the string is not one."""
    i = t['name'] - 1
    if 0 <= i < len(b['names']):
        leaf = b['names'][i].rsplit('\\', 1)[-1]
        if leaf.startswith('_0x'):
            return leaf[:-4] if leaf.endswith('.bin') else leaf
    return 'n%d' % t['name']


def base_level(b, t):
    """Bytes of the texture's base mip level (tiled, padded)."""
    d, g0 = b['data'], b['base']['.gpu']
    if t['psz'] == 0xffffffff:
        g = next((e for e in b['ents'] if e['sec'] == '.gpu' and e['name'] == t['name']), None)
        if g is None:
            raise CaffError('%s: texture %d has no .gpu entry' % (b['path'], t['name']))
        return d[g0 + g['off']: g0 + g['off'] + g['size']]
    p = b['pool']
    if p is None:
        raise CaffError('%s: texture %d wants the shared pool and there is none'
                        % (b['path'], t['name']))
    return d[g0 + p['off'] + t['poff']: g0 + p['off'] + t['psz']]
